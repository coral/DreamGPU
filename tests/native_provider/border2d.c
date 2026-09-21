/* SPDX-License-Identifier: GPL-2.0-or-later
 * Isolated private-provider 2D border contract. Prints the actual renderer;
 * a software-renderer pass is source/runtime verification, not GPU acceptance.
 */
#include "border-test.h"

static void point2(float s, float t) {
    glClear(GL_COLOR_BUFFER_BIT);
    glTexCoord2f(s, t);
    glBegin(GL_QUADS);
    glVertex2f(-1, -1); glVertex2f(1, -1); glVertex2f(1, 1); glVertex2f(-1, 1);
    glEnd();
}
static void footprint2(float lambda, float s, float t, int vertical) {
    float slope = exp2f(lambda) / (vertical ? 2.0f : 4.0f);
    float left = (vertical ? t : s) - slope * .5f;
    float right = left + slope * (vertical ? 4 : 256);
    glClear(GL_COLOR_BUFFER_BIT);
    glBegin(GL_QUADS);
    glTexCoord2f(vertical ? s : left, vertical ? left : t); glVertex2f(-1, -1);
    glTexCoord2f(vertical ? s : right, vertical ? left : t); glVertex2f(1, -1);
    glTexCoord2f(vertical ? s : right, vertical ? right : t); glVertex2f(1, 1);
    glTexCoord2f(vertical ? s : left, vertical ? right : t); glVertex2f(-1, 1);
    glEnd();
}
static void fill(unsigned char *data, unsigned width, unsigned height,
                 const unsigned char *edge, const unsigned char *inside) {
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x)
            memcpy(data + 4 * (y * width + x),
                   !x || !y || x == width - 1 || y == height - 1 ? edge : inside, 4);
}
int main(void) {
    EGLDisplay display = eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA,
                                                   EGL_DEFAULT_DISPLAY, NULL);
    EGLint a, b, n;
    if (!eglInitialize(display, &a, &b) || !eglBindAPI(EGL_OPENGL_API)) return 1;
    EGLConfig config;
    EGLint attributes[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE,
                           EGL_OPENGL_BIT, EGL_NONE};
    if (!eglChooseConfig(display, attributes, &config, 1, &n) || n != 1) return 2;
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, NULL);
    if (context == EGL_NO_CONTEXT ||
        !eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context)) return 3;
    printf("vendor=%s renderer=%s version=%s\n", glGetString(GL_VENDOR),
           glGetString(GL_RENDERER), glGetString(GL_VERSION));
    GLuint fbo, output, texture;
    glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenTextures(1, &output); glBindTexture(GL_TEXTURE_2D, output);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, output, 0);
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "FBO");
    glDrawBuffer(GL_COLOR_ATTACHMENT0); glReadBuffer(GL_COLOR_ATTACHMENT0);
    glViewport(0, 0, 256, 4); glDisable(GL_DITHER); glClearColor(0, 0, 0, 0);
    glGenTextures(1, &texture); glBindTexture(GL_TEXTURE_2D, texture);
    unsigned char base[96], mip1[48], mip2[36], out[104];
    const unsigned char red[] = {255,0,0,255}, green[] = {0,255,0,255},
        blue[] = {0,0,255,255}, yellow[] = {255,255,0,255}, cyan[] = {0,255,255,255},
        white[] = {255,255,255,255}, magenta[] = {255,0,255,255};
    fill(base, 6, 4, red, green);
    for (unsigned y = 1; y < 3; ++y) memcpy(base + 4*(y*6+5), blue, 4);
    for (unsigned x = 1; x < 5; ++x) {
        memcpy(base + 4*x, yellow, 4); memcpy(base + 4*(18+x), cyan, 4);
    }
    memcpy(base, white, 4);
    fill(mip1, 4, 3, blue, magenta); fill(mip2, 3, 3, red, blue);
    glTexImage2D(GL_PROXY_TEXTURE_2D, 0, GL_RGBA8, 6, 4, 1, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    GLint w, h, border;
    glGetTexLevelParameteriv(GL_PROXY_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
    glGetTexLevelParameteriv(GL_PROXY_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
    check(w == 6 && h == 4, "2D proxy dimensions include border");
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 6, 4, 1, GL_RGBA, GL_UNSIGNED_BYTE, base);
    glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 4, 3, 1, GL_RGBA, GL_UNSIGNED_BYTE, mip1);
    glTexImage2D(GL_TEXTURE_2D, 2, GL_RGBA8, 3, 3, 1, GL_RGBA, GL_UNSIGNED_BYTE, mip2);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_BORDER, &border);
    check(w == 6 && h == 4 && border == 1, "2D stored dimensions and border");
    memset(out, 0xcc, sizeof out); glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, out+4);
    check(!memcmp(out+4, base, sizeof base) && out[0] == 0xcc && out[103] == 0xcc,
          "2D complete image readback and guards");
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glEnable(GL_TEXTURE_2D); glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    point2(0, .5f); pixel("2D left border", 128, 2, 128,128,0,255,1);
    point2(1, .5f); pixel("2D right border", 128, 2, 0,128,128,255,1);
    point2(.5f, 0); pixel("2D bottom border", 128, 2, 128,255,0,255,1);
    point2(.5f, 1); pixel("2D top border", 128, 2, 0,255,128,255,1);
    point2(0, 0); pixel("2D independent corner", 128, 2, 191,191,64,255,1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    point2(-1, 2); pixel("2D repeat excludes all borders", 128,2,0,255,0,255,0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    point2(0, 0); pixel("2D edge clamp excludes corner",128,2,0,255,0,255,0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    point2(-2, 2); pixel("2D nearest clamp selects interior",128,2,0,255,0,255,0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    footprint2(1.5f, 0, .5f, 0); pixel("2D trilinear bordered mips",0,0,128,0,191,255,2);
    footprint2(1.5f, .5f, .5f, 1); pixel("2D vertical derivatives choose mip",0,0,128,0,255,255,2);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    footprint2(1, .5f, .5f, 0); pixel("2D nearest mip1",0,0,255,0,255,255,0);
    const GLenum filters[] = {GL_NEAREST,GL_LINEAR,GL_NEAREST_MIPMAP_NEAREST,
        GL_LINEAR_MIPMAP_NEAREST,GL_NEAREST_MIPMAP_LINEAR,GL_LINEAR_MIPMAP_LINEAR};
    const int filter_colors[][3] = {{0,255,0},{0,255,0},{255,0,255},
        {255,0,255},{128,0,255},{128,0,255}};
    for(unsigned filter=0;filter<6;++filter) {
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,filters[filter]);
        footprint2(filter<4?1.0f:1.5f,.5f,.5f,0);
        pixel("2D all six minification filters",0,0,filter_colors[filter][0],
              filter_colors[filter][1],filter_colors[filter][2],255,2);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    point2(0, .5f); pixel("2D base-level bordered image",128,2,128,0,255,255,1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1000);
    glTexSubImage2D(GL_TEXTURE_2D, 0, -1, -1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, blue);
    point2(0,0); pixel("2D corner mutation",128,2,128,128,64,255,1);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, out+4);
    memcpy(base,blue,4); check(!memcmp(out+4,base,96), "2D subimage preserves every other texel");
    glDisable(GL_TEXTURE_2D); glClearColor(1,0,1,1); glClear(GL_COLOR_BUFFER_BIT);
    glCopyTexSubImage2D(GL_TEXTURE_2D,0,4,2,0,0,1,1);
    glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,out+4);
    memcpy(base+92,magenta,4); check(!memcmp(out+4,base,96),"2D copy to opposite corner preserves image");
    glCopyTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16,0,0,6,4,1);
    unsigned short precise[96]; glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_SHORT,precise);
    for(unsigned i=0;i<24;++i) check(precise[i*4]==65535 && precise[i*4+1]==0 &&
        precise[i*4+2]==65535 && precise[i*4+3]==65535,"2D native RGBA16 copy including edges");
    for(unsigned i=0;i<96;++i) precise[i]=(unsigned short)(i*631);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA16,6,4,1,GL_RGBA,GL_UNSIGNED_SHORT,precise);
    unsigned short precise_out[96]; glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_SHORT,precise_out);
    check(!memcmp(precise,precise_out,sizeof precise),"2D RGBA16 exact native storage");
    error("2D storage and sampling");
    // GL1.1 fixed-function texturing is disabled for an incomplete image.
    GLuint incomplete;
    glGenTextures(1,&incomplete);glBindTexture(GL_TEXTURE_2D,incomplete);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,6,4,1,GL_RGBA,GL_UNSIGNED_BYTE,base);
    glEnable(GL_TEXTURE_2D);glColor4ub(64,128,192,255);point2(.5f,.5f);
    pixel("2D incomplete mip chain preserves primary color",128,2,64,128,192,255,0);
    glColor4ub(255,255,255,255);
    glBindTexture(GL_TEXTURE_2D,texture);glDeleteTextures(1,&incomplete);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,6,4,1,GL_RGBA,GL_UNSIGNED_BYTE,base);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    // The second context keeps its original sampler view while the first
    // modifies a corner, then deletes the public texture name.
    EGLContext other=eglCreateContext(display,config,context,NULL);
    check(other!=EGL_NO_CONTEXT,"2D shared context create");
    check(eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,other),"2D reader current");
    GLuint fbo2;glGenFramebuffers(1,&fbo2);glBindFramebuffer(GL_FRAMEBUFFER,fbo2);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,output,0);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);glReadBuffer(GL_COLOR_ATTACHMENT0);
    glViewport(0,0,256,4);glDisable(GL_DITHER);glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D,texture);glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_REPLACE);
    point2(0,0);pixel("2D shared original corner",128,2,128,128,64,255,1);
    check(eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,context),"2D writer current");
    glTexSubImage2D(GL_TEXTURE_2D,0,-1,-1,1,1,GL_RGBA,GL_UNSIGNED_BYTE,white);glFinish();
    check(eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,other),"2D reader after mutation");
    point2(0,0);pixel("2D shared mutation without rebind",128,2,191,191,64,255,1);
    check(eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,context),"2D deleting context");
    glDeleteTextures(1,&texture);
    check(eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,other),"2D deleted binding current");
    point2(0,0);pixel("2D deleted binding preserves storage",128,2,191,191,64,255,1);
    const char *vs="#version 120\nvoid main(){gl_Position=gl_Vertex;gl_TexCoord[0]=gl_MultiTexCoord0;}";
    const char *fs="#version 120\nuniform sampler2D image;void main(){gl_FragColor=texture2D(image,gl_TexCoord[0].st);}";
    GLuint vertex=glCreateShader(GL_VERTEX_SHADER),fragment=glCreateShader(GL_FRAGMENT_SHADER),program=glCreateProgram();
    glShaderSource(vertex,1,&vs,NULL);glCompileShader(vertex);glShaderSource(fragment,1,&fs,NULL);glCompileShader(fragment);
    glAttachShader(program,vertex);glAttachShader(program,fragment);glLinkProgram(program);glUseProgram(program);
    point2(0,0);check(glGetError()==GL_INVALID_OPERATION,"2D programmable border draw rejected");
    glUseProgram(0);glDeleteProgram(program);glDeleteShader(vertex);glDeleteShader(fragment);
    point2(0,0);pixel("2D fixed function after rejected program",128,2,191,191,64,255,1);
    glBindTexture(GL_TEXTURE_2D,0);glDeleteFramebuffers(1,&fbo2);
    error("2D sharing and programmable guard");
    check(eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,context),"2D restore original context");
    eglDestroyContext(display,other);

    glDeleteTextures(1,&texture);glDeleteTextures(1,&output);glDeleteFramebuffers(1,&fbo);
    eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    eglDestroyContext(display,context);eglTerminate(display);
    printf("%s isolated2D border contract (%d failures)\n",failures?"FAIL":"PASS",failures);
    return failures?4:0;
}
