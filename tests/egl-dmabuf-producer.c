/* Real EGL producer for the explicitly requested Linux GPU integration test.
 * No CPU mapping: GBM allocation -> GL FBO draw -> native completion fence.
 * Built into a temporary shared library by the ignored Rust hardware test.
 */
#define EGL_EGLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <gbm.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct exported_image {
    unsigned width, height, stride, fourcc;
    unsigned long long modifier;
    int fd, ready;
    void *owner;
};
struct image_owner {
    int drm;
    struct gbm_device *gbm;
    struct gbm_bo *bo;
    EGLDisplay display;
    EGLContext context;
    EGLImageKHR image;
    EGLSyncKHR sync;
    GLuint texture, framebuffer;
    PFNEGLCREATEIMAGEKHRPROC create_image;
    PFNEGLDESTROYIMAGEKHRPROC destroy_image;
    PFNEGLCREATESYNCKHRPROC create_sync;
    PFNEGLDESTROYSYNCKHRPROC destroy_sync;
    PFNEGLDUPNATIVEFENCEFDANDROIDPROC export_fence;
};

void dreamgpu_test_image_destroy(struct exported_image *exported) {
    struct image_owner *owner = exported->owner;
    if (!owner)
        return;
    if (owner->context) {
        glDeleteFramebuffers(1, &owner->framebuffer);
        glDeleteTextures(1, &owner->texture);
        eglMakeCurrent(owner->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }
    if (owner->sync)
        owner->destroy_sync(owner->display, owner->sync);
    if (owner->image)
        owner->destroy_image(owner->display, owner->image);
    if (owner->context)
        eglDestroyContext(owner->display, owner->context);
    if (owner->display)
        eglTerminate(owner->display);
    if (owner->bo)
        gbm_bo_destroy(owner->bo);
    if (owner->gbm)
        gbm_device_destroy(owner->gbm);
    if (owner->drm >= 0)
        close(owner->drm);
    if (exported->fd >= 0)
        close(exported->fd);
    if (exported->ready >= 0)
        close(exported->ready);
    free(owner);
    exported->owner = NULL;
}

int dreamgpu_test_image_create(struct exported_image *out, const char *render_node, char *error,
                               unsigned capacity) {
    struct image_owner *o = calloc(1, sizeof(*o));
    EGLint count, major, minor;
    EGLConfig config;
    const EGLint config_attributes[] = {EGL_RENDERABLE_TYPE,
                                        EGL_OPENGL_BIT,
                                        EGL_SURFACE_TYPE,
                                        EGL_PBUFFER_BIT,
                                        EGL_RED_SIZE,
                                        8,
                                        EGL_GREEN_SIZE,
                                        8,
                                        EGL_BLUE_SIZE,
                                        8,
                                        EGL_NONE};
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC target_image;
    PFNEGLQUERYDEVICESEXTPROC query_devices;
    PFNEGLQUERYDEVICESTRINGEXTPROC query_device_string;
    EGLDeviceEXT devices[32], selected = NULL;
    out->fd = out->ready = -1;
    if (!o)
        return 0;
    out->owner = o;
    o->drm = -1;
#define REQUIRE(condition, message)                                                                \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            snprintf(error, capacity, "%s (EGL 0x%x, GL 0x%x)", message, eglGetError(),            \
                     glGetError());                                                                \
            dreamgpu_test_image_destroy(out);                                                      \
            return 0;                                                                              \
        }                                                                                          \
    } while (0)
    o->drm = open(render_node, O_RDWR | O_CLOEXEC);
    REQUIRE(o->drm >= 0, "open DRM render node");
    o->gbm = gbm_create_device(o->drm);
    REQUIRE(o->gbm, "GBM device");
    query_devices = (PFNEGLQUERYDEVICESEXTPROC)eglGetProcAddress("eglQueryDevicesEXT");
    query_device_string =
        (PFNEGLQUERYDEVICESTRINGEXTPROC)eglGetProcAddress("eglQueryDeviceStringEXT");
    REQUIRE(query_devices && query_device_string && query_devices(32, devices, &count),
            "EGL device enumeration");
    for (EGLint i = 0; i < count; i++) {
        const char *node = query_device_string(devices[i], EGL_DRM_RENDER_NODE_FILE_EXT);
        if (node && !strcmp(node, render_node))
            selected = devices[i];
    }
    REQUIRE(selected, "EGL device matching Vulkan render node");
    o->display = eglGetPlatformDisplay(EGL_PLATFORM_DEVICE_EXT, selected, NULL);
    REQUIRE(o->display && eglInitialize(o->display, &major, &minor), "EGL initialize");
    o->create_image = (PFNEGLCREATEIMAGEKHRPROC)eglGetProcAddress("eglCreateImageKHR");
    o->destroy_image = (PFNEGLDESTROYIMAGEKHRPROC)eglGetProcAddress("eglDestroyImageKHR");
    o->create_sync = (PFNEGLCREATESYNCKHRPROC)eglGetProcAddress("eglCreateSyncKHR");
    o->destroy_sync = (PFNEGLDESTROYSYNCKHRPROC)eglGetProcAddress("eglDestroySyncKHR");
    o->export_fence =
        (PFNEGLDUPNATIVEFENCEFDANDROIDPROC)eglGetProcAddress("eglDupNativeFenceFDANDROID");
    REQUIRE(o->create_image && o->destroy_image && o->create_sync && o->destroy_sync &&
                o->export_fence,
            "EGL image/fence extension entry points");
    REQUIRE(eglBindAPI(EGL_OPENGL_API), "desktop OpenGL API");
    REQUIRE(eglChooseConfig(o->display, config_attributes, &config, 1, &count) && count,
            "EGL config");
    o->context = eglCreateContext(o->display, config, EGL_NO_CONTEXT, NULL);
    REQUIRE(o->context, "GL context");
    REQUIRE(eglMakeCurrent(o->display, EGL_NO_SURFACE, EGL_NO_SURFACE, o->context),
            "surfaceless context");
    const uint64_t modifiers[] = {0};
    o->bo = gbm_bo_create_with_modifiers2(o->gbm, 32, 16, GBM_FORMAT_ARGB8888, modifiers, 1,
                                          GBM_BO_USE_RENDERING);
    REQUIRE(o->bo && gbm_bo_get_plane_count(o->bo) == 1, "single-plane GBM allocation");
    out->width = 32;
    out->height = 16;
    out->stride = gbm_bo_get_stride(o->bo);
    out->fourcc = GBM_FORMAT_ARGB8888;
    out->modifier = gbm_bo_get_modifier(o->bo);
    REQUIRE(out->modifier == 0, "linear GBM modifier");
    out->fd = gbm_bo_get_fd(o->bo);
    REQUIRE(out->fd >= 0, "DMA-BUF export");
    const EGLint image_attributes[] = {EGL_WIDTH,
                                       32,
                                       EGL_HEIGHT,
                                       16,
                                       EGL_LINUX_DRM_FOURCC_EXT,
                                       GBM_FORMAT_ARGB8888,
                                       EGL_DMA_BUF_PLANE0_FD_EXT,
                                       out->fd,
                                       EGL_DMA_BUF_PLANE0_OFFSET_EXT,
                                       0,
                                       EGL_DMA_BUF_PLANE0_PITCH_EXT,
                                       out->stride,
                                       EGL_NONE};
    o->image =
        o->create_image(o->display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, NULL, image_attributes);
    REQUIRE(o->image, "EGL DMA-BUF image");
    target_image =
        (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)eglGetProcAddress("glEGLImageTargetTexture2DOES");
    REQUIRE(target_image, "GL EGLImage binding");
    glGenTextures(1, &o->texture);
    glBindTexture(GL_TEXTURE_2D, o->texture);
    target_image(GL_TEXTURE_2D, o->image);
    glGenFramebuffers(1, &o->framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, o->framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, o->texture, 0);
    REQUIRE(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "GL framebuffer");
    glViewport(0, 0, 32, 16);
    glClearColor(1, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 8, 32, 8);
    glClearColor(0, 0, 1, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    REQUIRE(glGetError() == GL_NO_ERROR, "GL draw");
    o->sync = o->create_sync(o->display, EGL_SYNC_NATIVE_FENCE_ANDROID, NULL);
    REQUIRE(o->sync, "native producer fence");
    glFlush();
    out->ready = o->export_fence(o->display, o->sync);
    REQUIRE(out->ready >= 0, "native fence export");
    return 1;
}
