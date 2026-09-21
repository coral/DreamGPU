/* SPDX-License-Identifier: GPL-2.0-or-later
 * Exact native GPU oracle for the private provider GL1.1 border image profile.
 */
/* Isolated provider proof: actual GPU draws and native-format image round trips. */
#include "border-test.h"

static void point(float s) {
    glClear(GL_COLOR_BUFFER_BIT);
    glTexCoord1f(s);
    glBegin(GL_QUADS);
    glVertex2f(-1, -1);
    glVertex2f(1, -1);
    glVertex2f(1, 1);
    glVertex2f(-1, 1);
    glEnd();
}
static void footprint(float lambda, float s) {
    /* First pixel center samples s. The polygon's exact ds/dx sets lambda. */
    float slope = exp2f(lambda) / 4.0f, left = s - slope * .5f, right = left + slope * 256;
    glClear(GL_COLOR_BUFFER_BIT);
    glBegin(GL_QUADS);
    glTexCoord1f(left);
    glVertex2f(-1, -1);
    glTexCoord1f(right);
    glVertex2f(1, -1);
    glTexCoord1f(right);
    glVertex2f(1, 1);
    glTexCoord1f(left);
    glVertex2f(-1, 1);
    glEnd();
}
int main(void) {
    EGLDisplay d =
        eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
    EGLint a, b, n;
    if (!eglInitialize(d, &a, &b) || !eglBindAPI(EGL_OPENGL_API))
        return 1;
    EGLConfig cfg;
    EGLint ca[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
                   EGL_NONE};
    if (!eglChooseConfig(d, ca, &cfg, 1, &n) || n != 1)
        return 2;
    EGLContext c = eglCreateContext(d, cfg, EGL_NO_CONTEXT, NULL);
    if (c == EGL_NO_CONTEXT || !eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c))
        return 3;
    printf("vendor=%s renderer=%s version=%s\n", glGetString(GL_VENDOR), glGetString(GL_RENDERER),
           glGetString(GL_VERSION));
    GLuint f, t;
    glGenFramebuffers(1, &f);
    glBindFramebuffer(GL_FRAMEBUFFER, f);
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t, 0);
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "FBO");
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glViewport(0, 0, 256, 4);
    glDisable(GL_DITHER);
    glClearColor(0, 0, 0, 0);
    GLuint one;
    glGenTextures(1, &one);
    glBindTexture(GL_TEXTURE_1D, one);
    const unsigned char base[] = {255, 0,   0, 255, 0, 255, 0, 255, 0, 255, 0,   255,
                                  0,   255, 0, 255, 0, 255, 0, 255, 0, 0,   255, 255};
    const unsigned char mip1[] = {255, 255, 0,   255, 255, 0,   255, 255,
                                  255, 0,   255, 255, 0,   255, 255, 255};
    const unsigned char mip2[] = {255, 255, 255, 255, 0, 0, 0, 255, 0, 0, 255, 255};
    glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA8, 6, 1, GL_RGBA, GL_UNSIGNED_BYTE, base);
    glTexImage1D(GL_TEXTURE_1D, 1, GL_RGBA8, 4, 1, GL_RGBA, GL_UNSIGNED_BYTE, mip1);
    glTexImage1D(GL_TEXTURE_1D, 2, GL_RGBA8, 3, 1, GL_RGBA, GL_UNSIGNED_BYTE, mip2);
    error("image upload");
    unsigned char out[24];
    memset(out, 0xcc, sizeof out);
    GLint w = -1, border = -1;
    glGetTexLevelParameteriv(GL_TEXTURE_1D, 0, GL_TEXTURE_WIDTH, &w);
    glGetTexLevelParameteriv(GL_TEXTURE_1D, 0, GL_TEXTURE_BORDER, &border);
    check(w == 6 && border == 1, "actual full dimensions");
    glGetTexImage(GL_TEXTURE_1D, 0, GL_RGBA, GL_UNSIGNED_BYTE, out);
    check(!memcmp(base, out, 24), "all six stored texels");
    error("readback");
    glEnable(GL_TEXTURE_1D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    point(0);
    pixel("clamp left half actual red border", 128, 2, 128, 128, 0, 255, 1);
    point(1);
    pixel("clamp right half actual blue border", 128, 2, 0, 128, 128, 255, 1);
    point(-7);
    pixel("clamp outside still half border", 128, 2, 128, 128, 0, 255, 1);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    point(0);
    pixel("repeat excludes borders", 128, 2, 0, 255, 0, 255, 0);
    point(-.25f);
    pixel("negative repeat", 128, 2, 0, 255, 0, 255, 0);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    point(0);
    pixel("nearest clamp left interior", 128, 2, 0, 255, 0, 255, 0);
    point(1);
    pixel("nearest clamp right interior", 128, 2, 0, 255, 0, 255, 0);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_NEAREST);
    footprint(1, 0);
    pixel("level1 own left border", 0, 2, 255, 128, 128, 255, 1);
    footprint(1, 1);
    pixel("level1 own right border", 0, 2, 128, 128, 255, 255, 1);
    footprint(2, 0);
    pixel("level2 own left border", 0, 2, 128, 128, 128, 255, 1);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    footprint(.5f, 0);
    pixel("trilinear independently filters two borders", 0, 2, 191, 128, 64, 255, 2);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    footprint(1, 0);
    pixel("nearest mip ignores border", 0, 2, 255, 0, 255, 255, 0);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR);
    footprint(1.5f, 0);
    pixel("nearest trilinear interiors", 0, 2, 128, 0, 128, 255, 2);
    error("render filters");
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    point(-2);
    pixel("extension edge excludes actual border", 128, 2, 0, 255, 0, 255, 0);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    point(-2);
    pixel("extension border uses actual left texel", 128, 2, 255, 0, 0, 255, 0);
    point(2);
    pixel("extension border uses actual right texel", 128, 2, 0, 0, 255, 255, 0);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
    point(-1);
    pixel("mirror excludes border", 128, 2, 0, 255, 0, 255, 0);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_BASE_LEVEL, 1);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAX_LEVEL, 1);
    point(0);
    pixel("base and max select stored mip1", 128, 2, 255, 128, 128, 255, 1);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAX_LEVEL, 1000);
    point(0);
    pixel("base restore selects original mip", 128, 2, 128, 128, 0, 255, 1);
    /* Native GLSL is outside the exported guest contract. It must fail safely,
       never bind the atlas as an incompatible sampler1D. */
    const char *vs =
        "#version 120\nvoid main(){gl_Position=ftransform();gl_TexCoord[0]=gl_MultiTexCoord0;}";
    const char *fs = "#version 120\nuniform sampler1D image;void "
                     "main(){gl_FragColor=texture1D(image,gl_TexCoord[0].s);}";
    GLuint v = glCreateShader(GL_VERTEX_SHADER), frag = glCreateShader(GL_FRAGMENT_SHADER),
           prog = glCreateProgram();
    glShaderSource(v, 1, &vs, NULL);
    glCompileShader(v);
    glShaderSource(frag, 1, &fs, NULL);
    glCompileShader(frag);
    glAttachShader(prog, v);
    glAttachShader(prog, frag);
    glLinkProgram(prog);
    GLint linked = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &linked);
    check(linked, "native sampler1D program links");
    glUseProgram(prog);
    point(.5f);
    check(glGetError() == GL_INVALID_OPERATION, "programmable border draw rejected");
    pixel("rejected shader leaves clear framebuffer", 128, 2, 0, 0, 0, 0, 0);
    glUseProgram(0);
    glDeleteProgram(prog);
    glDeleteShader(v);
    glDeleteShader(frag);
    point(0);
    pixel("fixed function recovers after rejected shader", 128, 2, 128, 128, 0, 255, 1);
    error("state and programmable guard");
    /* Mutating one real border must invalidate the sampled image. */
    unsigned char white[] = {255, 255, 255, 255};
    glTexSubImage1D(GL_TEXTURE_1D, 0, -1, 1, GL_RGBA, GL_UNSIGNED_BYTE, white);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    point(0);
    pixel("subimage border mutation visible", 128, 2, 128, 255, 128, 255, 1);
    glDisable(GL_TEXTURE_1D);
    glClearColor(.25f, .5f, .75f, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glCopyTexSubImage1D(GL_TEXTURE_1D, 0, 4, 0, 0, 1); /* right border index */
    glGetTexImage(GL_TEXTURE_1D, 0, GL_RGBA, GL_UNSIGNED_BYTE, out);
    check(abs(out[20] - 64) <= 1 && abs(out[21] - 128) <= 1 && abs(out[22] - 191) <= 1,
          "copy subimage true right border");
    glCopyTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA16, 0, 0, 6, 1);
    glGetTexLevelParameteriv(GL_TEXTURE_1D, 0, GL_TEXTURE_WIDTH, &w);
    glGetTexLevelParameteriv(GL_TEXTURE_1D, 0, GL_TEXTURE_BORDER, &border);
    glGetTexImage(GL_TEXTURE_1D, 0, GL_RGBA, GL_UNSIGNED_BYTE, out);
    check(w == 6 && border == 1, "copy image full dimensions");
    for (int i = 0; i < 6; i++)
        check(abs(out[i * 4] - 64) <= 1 && abs(out[i * 4 + 1] - 128) <= 1 &&
                  abs(out[i * 4 + 2] - 191) <= 1,
              "copy all border/interior texels");
    error("copy and subcopy");
    /* Shared object, distinct context sampler. B must observe A's image
       writes without a texture/sampler rebind and retain deleted storage. */
    glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA8, 6, 1, GL_RGBA, GL_UNSIGNED_BYTE, base);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    EGLContext other = eglCreateContext(d, cfg, c, NULL);
    check(other != EGL_NO_CONTEXT, "shared context create");
    check(eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, other), "make shared current");
    GLuint f2, sampler;
    glGenFramebuffers(1, &f2);
    glBindFramebuffer(GL_FRAMEBUFFER, f2);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t, 0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glViewport(0, 0, 256, 4);
    glDisable(GL_DITHER);
    glBindTexture(GL_TEXTURE_1D, one);
    glEnable(GL_TEXTURE_1D);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    glGenSamplers(1, &sampler);
    glSamplerParameteri(sampler, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glSamplerParameteri(sampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glSamplerParameteri(sampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindSampler(0, sampler);
    point(0);
    pixel("shared B separate sampler original border", 128, 2, 128, 128, 0, 255, 1);
    check(eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c), "make writer current");
    glTexSubImage1D(GL_TEXTURE_1D, 0, -1, 1, GL_RGBA, GL_UNSIGNED_BYTE, white);
    check(eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, other), "make reader current");
    point(0);
    pixel("shared B sees A mutation without rebind", 128, 2, 128, 255, 128, 255, 1);
    check(eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c), "make deleting context current");
    glDeleteTextures(1, &one);
    check(eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, other), "make deleted binding current");
    point(0);
    pixel("deleted shared binding retains image", 128, 2, 128, 255, 128, 255, 1);
    glBindTexture(GL_TEXTURE_1D, 0);
    glBindSampler(0, 0);
    glDeleteSamplers(1, &sampler);
    glDeleteFramebuffers(1, &f2);
    error("shared lifetime");
    check(eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, c), "restore original context");
    eglDestroyContext(d, other);
    glDeleteTextures(1, &t);
    glDeleteFramebuffers(1, &f);
    eglMakeCurrent(d, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(d, c);
    eglTerminate(d);
    printf("%s isolated border image/filter oracle (%d failures)\n", failures ? "FAIL" : "PASS",
           failures);
    return failures ? 4 : 0;
}
