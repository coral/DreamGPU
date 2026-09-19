/* SPDX-License-Identifier: GPL-2.0-or-later
 * Loads the real WGL DLL and calls public entrypoints. No private escape or
 * QEMU ABI is visible to this executable; host pixels/counters are the oracle.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#define FN(result, name, args) static result(WINAPI *p##name) args
FN(HGLRC, wglCreateContext, (HDC));
FN(BOOL, wglDeleteContext, (HGLRC));
FN(BOOL, wglMakeCurrent, (HDC, HGLRC));
FN(HGLRC, wglGetCurrentContext, (void));
FN(HDC, wglGetCurrentDC, (void));
FN(BOOL, wglSwapBuffers, (HDC));
FN(int, wglChoosePixelFormat, (HDC, const PIXELFORMATDESCRIPTOR *));
FN(int, wglDescribePixelFormat, (HDC, int, UINT, PIXELFORMATDESCRIPTOR *));
FN(BOOL, wglSetPixelFormat, (HDC, int, const PIXELFORMATDESCRIPTOR *));
FN(void, glClearColor, (GLclampf, GLclampf, GLclampf, GLclampf));
FN(void, glClear, (GLbitfield));
FN(void, glClearDepth, (GLdouble));
FN(void, glClearStencil, (GLint));
FN(void, glViewport, (GLint, GLint, GLsizei, GLsizei));
FN(void, glMatrixMode, (GLenum));
FN(void, glLoadIdentity, (void));
FN(void, glBegin, (GLenum));
FN(void, glEnd, (void));
FN(void, glColor4f, (GLfloat, GLfloat, GLfloat, GLfloat));
FN(void, glVertex2f, (GLfloat, GLfloat));
FN(GLenum, glGetError, (void));
FN(void, glBindTexture, (GLenum, GLuint));
FN(void, glTexParameteri, (GLenum, GLenum, GLint));
FN(void, glTexImage2D,
   (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *));
FN(void, glTexSubImage2D,
   (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *));
FN(void, glPixelStorei, (GLenum, GLint));
FN(void, glTexCoord2f, (GLfloat, GLfloat));
FN(void, glEnable, (GLenum));
FN(void, glDisable, (GLenum));
FN(void, glGetIntegerv, (GLenum, GLint *));
FN(void, glGetFloatv, (GLenum, GLfloat *));
FN(void, glGetDoublev, (GLenum, GLdouble *));
FN(void, glGetBooleanv, (GLenum, GLboolean *));
FN(void, glGetTexLevelParameteriv, (GLenum, GLint, GLenum, GLint *));
FN(GLboolean, glIsTexture, (GLuint));
FN(GLboolean, glIsEnabled, (GLenum));
FN(const GLubyte *, glGetString, (GLenum));
FN(PROC, wglGetProcAddress, (LPCSTR));
FN(void, glDrawBuffer, (GLenum));
FN(void, glReadBuffer, (GLenum));
FN(void, glFlush, (void));
FN(void, glReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *));
FN(void, glColor4ub, (GLubyte, GLubyte, GLubyte, GLubyte));
FN(void, glVertex3fv, (const GLfloat *));
FN(void, glHint, (GLenum, GLenum));
FN(BOOL, wglShareLists, (HGLRC, HGLRC));
FN(void, glVertexPointer, (GLint, GLenum, GLsizei, const void *));
FN(void, glColorPointer, (GLint, GLenum, GLsizei, const void *));
FN(void, glEnableClientState, (GLenum));
FN(void, glDisableClientState, (GLenum));
FN(void, glGetPointerv, (GLenum, void **));
FN(void, glDrawArrays, (GLenum, GLint, GLsizei));
FN(void, glDrawElements, (GLenum, GLsizei, GLenum, const void *));
FN(void, glGenTextures, (GLsizei, GLuint *));
FN(void, glPushAttrib, (GLbitfield));
FN(void, glPopAttrib, (void));
FN(void, glTexImage1D, (GLenum, GLint, GLint, GLsizei, GLint, GLenum, GLenum, const void *));
FN(void, glGetTexImage, (GLenum, GLint, GLenum, GLenum, void *));
FN(void, glSecondaryColor3fEXT, (GLfloat, GLfloat, GLfloat));
FN(void, glSecondaryColor3ubEXT, (GLubyte, GLubyte, GLubyte));
FN(void, glSecondaryColorPointerEXT, (GLint, GLenum, GLsizei, const void *));
static BOOL AutomatedArrays;
static BOOL FrontStage;
static BOOL Textured;
static HWND Windows[2];
static HDC DCs[2];
static HGLRC Contexts[2];
static HMODULE Library;
static HINSTANCE Instance;
static HANDLE LogFile;
static BOOL Failed, Closing;
static ULONG Frames;
static void Log(const char *line) {
    DWORD written;
    if (LogFile == INVALID_HANDLE_VALUE)
        return;
    WriteFile(LogFile, line, lstrlenA(line), &written, NULL);
    WriteFile(LogFile, "\r\n", 2, &written, NULL);
    FlushFileBuffers(LogFile);
}
static BOOL Check(BOOL result, const char *message) {
    if (!result) {
        Failed = TRUE;
        Log(message);
    }
    return result;
}
static void TexturedQuad(void) {
    pglBegin(GL_QUADS);
    pglTexCoord2f(0, 0);
    pglVertex2f(-1, -1);
    pglTexCoord2f(1, 0);
    pglVertex2f(1, -1);
    pglTexCoord2f(1, 1);
    pglVertex2f(1, 1);
    pglTexCoord2f(0, 1);
    pglVertex2f(-1, 1);
    pglEnd();
}
static BOOL Draw(ULONG pane) {
    ULONG i;
    if (!Windows[pane])
        return TRUE;
    if (!Check(pwglMakeCurrent(DCs[pane], Contexts[pane]), "FAIL MakeCurrent"))
        return FALSE;
    if (!Check(pwglGetCurrentContext() == Contexts[pane] && pwglGetCurrentDC() == DCs[pane],
               "FAIL current context/DC"))
        return FALSE;
    pglDisable(GL_TEXTURE_2D);
    pglViewport(0, 0, 320, 240);
    pglMatrixMode(GL_PROJECTION);
    pglLoadIdentity();
    pglMatrixMode(GL_MODELVIEW);
    pglLoadIdentity();
    pglClearColor(0, pane ? 1 : 0, 1, 1);
    pglClear(GL_COLOR_BUFFER_BIT);
    pglBegin(GL_QUADS);
    pglClear(GL_COLOR_BUFFER_BIT);
    /* Cross the immutable command-record bound repeatedly inside one Begin/
     * End pair. Driver completion must preserve primitive/context state. */
    for (i = 0; i < 300; ++i) {
        pglColor4f(1, pane ? 1 : 0, 0, 1);
        pglVertex2f(-1, -1);
        pglVertex2f(0, -1);
        pglVertex2f(0, 1);
        pglVertex2f(-1, 1);
    }
    pglEnd();
    if (!Check(pglGetError() == GL_INVALID_OPERATION,
               "FAIL illegal Begin call did not remain local"))
        return FALSE;
    if (!Check(pwglSwapBuffers(DCs[pane]), "FAIL SwapBuffers"))
        return FALSE;
    ++Frames;
    return Check(pglGetError() == GL_NO_ERROR, "FAIL GL error after frame");
}
static BOOL DrawTexture(BOOL upload) {
    static BYTE image[1040 * 258], patch[16 * 16 * 4];
    ULONG x, y, at;
    GLint value;
    GLfloat color[4];
    GLdouble range[2];
    GLboolean mask[4];
    if (!Check(pwglMakeCurrent(DCs[0], Contexts[0]), "FAIL texture MakeCurrent"))
        return FALSE;
    pglBindTexture(GL_TEXTURE_2D, 42);
    pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    if (upload) {
        /* Padded source with skipped row/pixels forces bounded row gathers.
         * The image exceeds the entire command buffer by more than4x. */
        ZeroMemory(image, sizeof(image));
        for (y = 0; y < 256; ++y)
            for (x = 0; x < 256; ++x) {
                at = (y + 1) * 1040 + (x + 2) * 4;
                image[at] = y < 128 ? (x < 128 ? 255 : 0) : (x < 128 ? 0 : 255);
                image[at + 1] = x < 128 ? 0 : 255;
                image[at + 2] = y < 128 ? 0 : (x < 128 ? 255 : 0);
                image[at + 3] = 255;
            }
        pglPixelStorei(GL_UNPACK_ALIGNMENT, 8);
        pglPixelStorei(GL_UNPACK_ROW_LENGTH, 259);
        pglPixelStorei(GL_UNPACK_SKIP_ROWS, 1);
        pglPixelStorei(GL_UNPACK_SKIP_PIXELS, 2);
        pglGetIntegerv(GL_UNPACK_ALIGNMENT, &value);
        if (!Check(value == 8, "FAIL frontend unpack query"))
            return FALSE;
        pglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
        ZeroMemory(image, sizeof(image)); /* no caller memory may be retained */
        pglPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        pglPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        pglPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
        pglPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
        for (at = 0; at < sizeof(patch); ++at)
            patch[at] = 255;
        pglTexSubImage2D(GL_TEXTURE_2D, 0, 120, 120, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, patch);
        ZeroMemory(patch, sizeof(patch));
    }
    pglGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &value);
    if (!Check(value == 256 && pglIsTexture(42), "FAIL texture query"))
        return FALSE;
    pglClearColor(.25f, .5f, .75f, 1);
    pglGetFloatv(GL_COLOR_CLEAR_VALUE, color);
    pglGetDoublev(GL_DEPTH_RANGE, range);
    pglGetBooleanv(GL_COLOR_WRITEMASK, mask);
    if (!Check(color[0] == .25f && color[1] == .5f && color[2] == .75f && color[3] == 1 &&
                   range[0] == 0 && range[1] == 1 && mask[0] && mask[1] && mask[2] && mask[3],
               "FAIL typed public state query"))
        return FALSE;
    if (!Check(lstrcmpA((LPCSTR)pglGetString(GL_VENDOR), "DreamGPU") == 0, "FAIL GL vendor"))
        return FALSE;
    pglEnable(GL_TEXTURE_2D);
    if (!Check(pglIsEnabled(GL_TEXTURE_2D), "FAIL enable query ordering"))
        return FALSE;
    pglViewport(0, 0, 320, 240);
    pglMatrixMode(GL_PROJECTION);
    pglLoadIdentity();
    pglMatrixMode(GL_MODELVIEW);
    pglLoadIdentity();
    pglColor4f(1, 1, 1, 1);
    TexturedQuad();
    if (!Check(pwglSwapBuffers(DCs[0]) && pglGetError() == GL_NO_ERROR, "FAIL textured present"))
        return FALSE;
    ++Frames;
    Textured = TRUE;
    Log(upload ? "STAGE textured:256x256 padded upload, immutable white subimage, typed queries"
               : "STAGE textured redraw without upload");
    return TRUE;
}
static void TextureThroughput(void) {
    LARGE_INTEGER frequency, start, end;
    ULONG swaps = 0, elapsed;
    CHAR line[192];
    /* Initialization, uploads and state queries are outside the measured loop.
     * This is an explicit, bounded workload; production rendering still sleeps
     * for work and presentation completion normally. */
    if (!DrawTexture(!Textured))
        return;
    if (!Check(QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0 &&
                   QueryPerformanceCounter(&start),
               "FAIL throughput performance counter"))
        return;
    end = start;
    do {
        TexturedQuad();
        if (!Check(pwglSwapBuffers(DCs[0]), "FAIL throughput SwapBuffers"))
            return;
        ++swaps;
        ++Frames;
        if (!Check(QueryPerformanceCounter(&end), "FAIL throughput performance counter"))
            return;
    } while (end.QuadPart - start.QuadPart < frequency.QuadPart * 5);
    elapsed = (ULONG)((end.QuadPart - start.QuadPart) * 1000000 / frequency.QuadPart);
    if (!Check(pglGetError() == GL_NO_ERROR, "FAIL GL error after throughput"))
        return;
    wsprintfA(line,
              "BENCH textured swaps=%lu elapsed_us=%lu; 320x240 quad, no uploads or state queries "
              "in timed loop",
              swaps, elapsed);
    Log(line);
}
static BOOL PixelIs(GLenum buffer, BYTE red, BYTE green) {
    BYTE pixel[4] = {17, 18, 19, 20};
    pglReadBuffer(buffer);
    pglReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    return pixel[0] == red && pixel[1] == green && pixel[2] == 0 && pixel[3] == 255;
}
static void FrontBuffers(BOOL swap) {
    static BYTE packed[2000];
    static const GLfloat vertices[4][3] = {{-1, -1, 0}, {1, -1, 0}, {1, 1, 0}, {-1, 1, 0}};
    ULONG i, offset;
    BYTE expected;
    if (!Check(pwglMakeCurrent(DCs[0], Contexts[0]), "FAIL front MakeCurrent"))
        return;
    pglPixelStorei(GL_PACK_ALIGNMENT, 4);
    pglPixelStorei(GL_PACK_ROW_LENGTH, 0);
    pglPixelStorei(GL_PACK_SKIP_ROWS, 0);
    pglPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    if (swap) {
        if (!FrontStage)
            return;
        if (!Check(pwglSwapBuffers(DCs[0]) && PixelIs(GL_FRONT, 0, 255) && PixelIs(GL_BACK, 255, 0),
                   "FAIL front/back exchange or logical read selection"))
            return;
        ++Frames;
        Log("STAGE front swap: visible green; retained back red");
    } else {
        if (!Check((ULONG_PTR)pwglGetProcAddress("glVertex3fv") == (ULONG_PTR)pglVertex3fv &&
                       !pwglGetProcAddress("glDreamGPUUnsupportedExtension"),
                   "FAIL procedure lookup"))
            return;
        pglDisable(GL_TEXTURE_2D);
        pglHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
        pglDrawBuffer(GL_BACK);
        pglClearColor(0, 1, 0, 1);
        pglClear(GL_COLOR_BUFFER_BIT);
        if (!Check(PixelIs(GL_BACK, 0, 255), "FAIL initial back read"))
            return;
        pglDrawBuffer(GL_FRONT);
        pglColor4ub(255, 0, 0, 255);
        pglBegin(GL_QUADS);
        for (i = 0; i < 4; ++i)
            pglVertex3fv(vertices[i]);
        pglEnd();
        pglFlush();
        if (!Check(PixelIs(GL_FRONT, 255, 0) && PixelIs(GL_BACK, 0, 255),
                   "FAIL front flush corrupted back"))
            return;
        pglReadBuffer(GL_FRONT);
        pglPixelStorei(GL_PACK_ALIGNMENT, 8);
        pglPixelStorei(GL_PACK_ROW_LENGTH, 137);
        pglPixelStorei(GL_PACK_SKIP_ROWS, 1);
        pglPixelStorei(GL_PACK_SKIP_PIXELS, 2);
        for (i = 0; i < sizeof(packed); ++i)
            packed[i] = 0xcc;
        pglReadPixels(0, 0, 130, 2, GL_RGBA, GL_UNSIGNED_BYTE, packed);
        for (i = 0; i < sizeof(packed); ++i) {
            offset = i >= 1112 && i < 1632  ? i - 1112
                     : i >= 560 && i < 1080 ? i - 560
                                            : 0xffffffffUL;
            expected = offset == 0xffffffffUL ? 0xcc : offset % 4 == 0 || offset % 4 == 3 ? 255 : 0;
            if (!Check(packed[i] == expected, "FAIL bounded packed RGBA read or padding"))
                return;
        }
        pglPixelStorei(GL_PACK_ALIGNMENT, 4);
        pglPixelStorei(GL_PACK_ROW_LENGTH, 0);
        pglPixelStorei(GL_PACK_SKIP_ROWS, 0);
        pglPixelStorei(GL_PACK_SKIP_PIXELS, 0);
        pglDrawBuffer(GL_BACK);
        FrontStage = TRUE;
        Log("STAGE front flush: visible red; back green; bounded packed reads verified");
    }
    Check(pglGetError() == GL_NO_ERROR, "FAIL GL error after front stage");
}

/* Deterministic command-driven acceptance: no menu, key, screenshot or wait
 * cycle. Pixel reads are intentional correctness oracles, not presentation. */
static BOOL ArrayPixels(BYTE r, BYTE g, BYTE b) {
    BYTE pixels[16 * 16 * 4];
    ULONG i;
    pglReadBuffer(GL_BACK);
    pglReadPixels(152, 112, 16, 16, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    for (i = 0; i < sizeof(pixels); i += 4)
        if (!Check(pixels[i] == r && pixels[i + 1] == g && pixels[i + 2] == b &&
                       pixels[i + 3] == 255,
                   "FAIL array pixel oracle"))
            return FALSE;
    return Check(pglGetError() == GL_NO_ERROR, "FAIL array GL error");
}
static BOOL PackedTextureAcceptance(void) {
    const struct {
        GLenum Format, Type;
        ULONG Bytes;
        GLushort Words[4];
        BYTE Pixels[16];
    } cases[] = {
        {GL_RGB,
         0x8363,
         2,
         {0xf800, 0x07e0, 0x001f, 0x8410},
         {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 132, 130, 132, 255}},
        {GL_RGBA,
         0x8033,
         2,
         {0xf00f, 0x0f0f, 0x00ff, 0x1234},
         {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 17, 34, 51, 68}},
        {GL_RGBA,
         0x8034,
         2,
         {0xf801, 0x07c1, 0x003f, 0x8420},
         {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 132, 132, 132, 0}},
        {0x80e1,
         0x8365,
         2,
         {0xff00, 0xf0f0, 0xf00f, 0x4321},
         {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 51, 34, 17, 68}},
        {0x80e1,
         0x8366,
         2,
         {0xfc00, 0x83e0, 0x801f, 0x4210},
         {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 132, 132, 132, 0}},
        {GL_RGB,
         0x8032,
         1,
         {0xe0, 0x1c, 0x03, 0x92},
         {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 146, 146, 170, 255}},
    };
    BYTE source[32], actual[16];
    ULONG f, i, offset;
    pglBindTexture(GL_TEXTURE_2D, 79);
    pglPixelStorei(GL_UNPACK_ALIGNMENT, 8);
    pglPixelStorei(GL_UNPACK_ROW_LENGTH, 3);
    pglPixelStorei(GL_UNPACK_SKIP_ROWS, 1);
    pglPixelStorei(GL_UNPACK_SKIP_PIXELS, 1);
    for (f = 0; f < sizeof(cases) / sizeof(cases[0]); ++f) {
        for (i = 0; i < sizeof(source); ++i)
            source[i] = 0xa5;
        for (i = 0; i < 4; ++i) {
            offset = 1 + 8 * (1 + i / 2) + (1 + i % 2) * cases[f].Bytes;
            source[offset] = (BYTE)cases[f].Words[i];
            if (cases[f].Bytes == 2)
                source[offset + 1] = (BYTE)(cases[f].Words[i] >> 8);
        }
        pglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, cases[f].Format, cases[f].Type,
                      source + 1);
        ZeroMemory(source, sizeof(source));
        ZeroMemory(actual, sizeof(actual));
        pglGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, actual);
        for (i = 0; i < sizeof(actual); ++i)
            if (!Check(actual[i] == cases[f].Pixels[i], "FAIL packed texture native pixel oracle"))
                return FALSE;
        /* The reverse transfer is essential: DirectDraw saves menu backgrounds
         * using the original packed format, not RGBA bytes. */
        for (offset = 0; offset < 2; ++offset) {
            BYTE packed[18];
            ULONG byte;
            for (byte = 0; byte < sizeof(packed); ++byte)
                packed[byte] = 0xa5;
            pglPixelStorei(GL_PACK_ALIGNMENT, 1);
            pglPixelStorei(GL_PACK_SWAP_BYTES, offset);
            pglGetTexImage(GL_TEXTURE_2D, 0, cases[f].Format, cases[f].Type, packed + 1);
            for (i = 0; i < 4; ++i)
                for (byte = 0; byte < cases[f].Bytes; ++byte) {
                    ULONG shift = 8 * (offset ? cases[f].Bytes - 1 - byte : byte);
                    if (!Check(packed[1 + i * cases[f].Bytes + byte] ==
                                   (BYTE)(cases[f].Words[i] >> shift),
                               "FAIL packed texture readback roundtrip"))
                        return FALSE;
                }
            if (!Check(packed[0] == 0xa5 && packed[1 + 4 * cases[f].Bytes] == 0xa5,
                       "FAIL packed texture readback guards"))
                return FALSE;
        }
        pglPixelStorei(GL_PACK_SWAP_BYTES, 0);
        pglPixelStorei(GL_PACK_ALIGNMENT, 4);
        if (!Check(pglGetError() == GL_NO_ERROR, "FAIL packed texture GL error"))
            return FALSE;
    }
    pglPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    pglPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    pglPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    pglPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    pglBindTexture(GL_TEXTURE_2D, 73);
    Log("PASS automated packed textures: RGB332/565, RGBA4444/5551, BGRA4444REV/1555REV;24 exact "
        "GPU texture pixels and padded unaligned inputs");
    return TRUE;
}

/* Actual driver/transport/native roundtrip, in addition to packer unit tests. */
static BOOL ReadbackAcceptance(void) {
    const struct { GLenum Format, Type; ULONG Bytes, Value; } cases[] = {
        {GL_RGB, 0x8032, 1, 0xe0}, /* 332 */
        {GL_RGB, 0x8362, 1, 0x07}, /* 233_REV */
        {GL_RGB, 0x8363, 2, 0xf800},
        {GL_RGB, 0x8364, 2, 0x001f},
        {GL_RGBA, 0x8033, 2, 0xf00f},
        {GL_RGBA, 0x8365, 2, 0xf00f},
        {GL_RGBA, 0x8034, 2, 0xf801},
        {GL_RGBA, 0x8366, 2, 0x801f},
        {GL_RGBA, 0x8035, 4, 0xff0000ff},
        {GL_RGBA, 0x8367, 4, 0xff0000ff},
        {GL_RGBA, 0x8036, 4, 0xffc00003},
        {GL_RGBA, 0x8368, 4, 0xc00003ff},
        {GL_RED, GL_BYTE, 1, 0x7f},
        {GL_RED, GL_UNSIGNED_BYTE, 1, 0xff},
        {GL_RED, GL_SHORT, 2, 0x7fff},
        {GL_RED, GL_UNSIGNED_SHORT, 2, 0xffff},
        {GL_RED, GL_INT, 4, 0x7fffffff},
        {GL_RED, GL_UNSIGNED_INT, 4, 0xffffffff},
        {GL_RED, GL_FLOAT, 4, 0x3f800000},
        {GL_BLUE, GL_UNSIGNED_BYTE, 1, 0},
        {GL_ALPHA, GL_UNSIGNED_BYTE, 1, 255},
    };
    BYTE out[24];
    ULONG f, b, swap;
    GLfloat depth;
    GLuint stencil;
    pglDisable(GL_SCISSOR_TEST);
    pglDrawBuffer(GL_BACK);
    pglReadBuffer(GL_BACK);
    pglClearColor(1, 0, 0, 1);
    pglClear(GL_COLOR_BUFFER_BIT);
    pglPixelStorei(GL_PACK_ALIGNMENT, 1);
    pglPixelStorei(GL_PACK_ROW_LENGTH, 0);
    pglPixelStorei(GL_PACK_SKIP_ROWS, 0);
    pglPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    for (f = 0; f < sizeof(cases) / sizeof(cases[0]); ++f)
        for (swap = 0; swap < 2; ++swap) {
            for (b = 0; b < sizeof(out); ++b)
                out[b] = 0xa5;
            pglPixelStorei(GL_PACK_SWAP_BYTES, swap);
            pglReadPixels(0, 0, 1, 1, cases[f].Format, cases[f].Type, out + 1);
            for (b = 0; b < cases[f].Bytes; ++b) {
                ULONG shift = 8 * (swap ? cases[f].Bytes - 1 - b : b);
                if (!Check(out[b + 1] == (BYTE)(cases[f].Value >> shift),
                           "FAIL framebuffer readback format"))
                    return FALSE;
            }
            if (!Check(out[0] == 0xa5 && out[1 + cases[f].Bytes] == 0xa5 &&
                           pglGetError() == GL_NO_ERROR, "FAIL framebuffer readback guards/error"))
                return FALSE;
        }
    pglPixelStorei(GL_PACK_SWAP_BYTES, 0);
    pglPixelStorei(GL_PACK_ALIGNMENT, 4);
    pglClearDepth(0.25);
    pglClearStencil(0x5a);
    pglClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    depth = -1;
    stencil = 0;
    pglReadPixels(0, 0, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
    pglReadPixels(0, 0, 1, 1, GL_STENCIL_INDEX, GL_UNSIGNED_INT, &stencil);
    if (!Check(depth > 0.24999f && depth < 0.25001f && stencil == 0x5a &&
                   pglGetError() == GL_NO_ERROR, "FAIL depth/stencil readback"))
        return FALSE;
    Log("PASS automated readback: packed8/16/32, signed/unsigned scalars, float, byte swap, "
        "unaligned guards, depth and stencil");
    return TRUE;
}

static BOOL Extension(const char *name) {
    const char *s = (const char *)pglGetString(GL_EXTENSIONS);
    int n = lstrlenA(name);
    if (!s)
        return FALSE;
    while (*s) {
        int i = 0;
        while (i < n && s[i] == name[i])
            i++;
        if (i == n && (!s[i] || s[i] == ' '))
            return TRUE;
        while (*s && *s != ' ')
            s++;
        while (*s == ' ')
            s++;
    }
    return FALSE;
}
static BOOL SecondaryPixels(BYTE r, BYTE g, BYTE b, BYTE a) {
    BYTE pixels[8 * 8 * 4];
    ULONG i;
    pglReadBuffer(GL_BACK);
    pglReadPixels(0, 0, 8, 8, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    for (i = 0; i < sizeof(pixels); i += 4)
        if (!Check(pixels[i] == r && pixels[i + 1] == g && pixels[i + 2] == b && pixels[i + 3] == a,
                   "FAIL secondary color pixel oracle"))
            return FALSE;
    return Check(pglGetError() == GL_NO_ERROR, "FAIL secondary color GL error");
}
static BOOL SecondaryAcceptance(void) {
    const GLenum sum = 0x8458, current = 0x8459, array = 0x845e;
    GLfloat vertices[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    GLfloat colors[4][4] = {{1, 0, 0, .5f}, {1, 0, 0, .5f}, {1, 0, 0, .5f}, {1, 0, 0, .5f}};
    GLubyte secondary[4][3] = {{0, 0, 255}, {0, 0, 255}, {0, 0, 255}, {0, 0, 255}};
    BYTE green[4] = {0, 255, 0, 128};
    void *pointer = NULL;
    struct {
        DWORD pre;
        GLfloat v[4];
        DWORD post;
    } f;
    struct {
        DWORD pre;
        GLint v[4];
        DWORD post;
    } iv;
    struct {
        DWORD pre;
        GLdouble v[4];
        DWORD post;
    } d;
    struct {
        DWORD pre;
        GLboolean v[4];
        DWORD post;
    } b;
    if (!Check(Extension("GL_EXT_secondary_color") && Extension("GL_EXT_separate_specular_color"),
               "FAIL negotiated secondary extensions"))
        return FALSE;
    pglSecondaryColor3fEXT = (void *)pwglGetProcAddress("glSecondaryColor3fEXT");
    pglSecondaryColor3ubEXT = (void *)pwglGetProcAddress("glSecondaryColor3ubEXT");
    pglSecondaryColorPointerEXT = (void *)pwglGetProcAddress("glSecondaryColorPointerEXT");
    if (!Check(pglSecondaryColor3fEXT && pglSecondaryColor3ubEXT && pglSecondaryColorPointerEXT,
               "FAIL public secondary extension entrypoints"))
        return FALSE;
    pglSecondaryColor3fEXT(0, 1, 0);
    f.pre = f.post = iv.pre = iv.post = d.pre = d.post = b.pre = b.post = 0x12345678;
    pglGetFloatv(current, f.v);
    pglGetIntegerv(current, iv.v);
    pglGetDoublev(current, d.v);
    pglGetBooleanv(current, b.v);
    if (!Check(f.pre == 0x12345678 && f.post == 0x12345678 && iv.pre == 0x12345678 &&
                   iv.post == 0x12345678 && d.pre == 0x12345678 && d.post == 0x12345678 &&
                   b.pre == 0x12345678 && b.post == 0x12345678,
               "FAIL secondary query guards"))
        return FALSE;
    if (!Check(f.v[0] == 0 && f.v[1] == 1 && f.v[2] == 0 && f.v[3] == 0 && iv.v[0] == 0 &&
                   iv.v[1] == 0x7fffffff && iv.v[2] == 0 && iv.v[3] == 0 && d.v[0] == 0 &&
                   d.v[1] == 1 && d.v[2] == 0 && d.v[3] == 0 && b.v[0] == 0 && b.v[1] == 1 &&
                   b.v[2] == 0 && b.v[3] == 0,
               "FAIL secondary typed RGBA values"))
        return FALSE;
    pglDisable(GL_TEXTURE_1D);
    pglBindTexture(GL_TEXTURE_2D, 91);
    pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    pglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, green);
    pglEnable(GL_TEXTURE_2D);
    pglEnable(sum);
    pglColor4f(1, 1, 1, 1);
    pglBegin(GL_QUADS);
    pglSecondaryColor3ubEXT(255, 0, 0);
    pglVertex2f(-1, -1);
    pglVertex2f(1, -1);
    pglVertex2f(1, 1);
    pglVertex2f(-1, 1);
    pglEnd();
    if (!SecondaryPixels(255, 255, 0, 128))
        return FALSE;
    pglDisable(sum);
    TexturedQuad();
    if (!SecondaryPixels(0, 255, 0, 128))
        return FALSE;
    pglDisable(GL_TEXTURE_2D);
    pglEnable(sum);
    pglVertexPointer(2, GL_FLOAT, 0, vertices);
    pglColorPointer(4, GL_FLOAT, 0, colors);
    pglSecondaryColorPointerEXT(3, GL_UNSIGNED_BYTE, 0, secondary);
    pglEnableClientState(GL_VERTEX_ARRAY);
    pglEnableClientState(GL_COLOR_ARRAY);
    pglEnableClientState(array);
    pglGetPointerv(0x845d, &pointer);
    if (!Check(pointer == secondary, "FAIL secondary array pointer"))
        return FALSE;
    pglDrawArrays(GL_QUADS, 0, 4);
    if (!SecondaryPixels(255, 0, 255, 128))
        return FALSE;
    pglGetFloatv(current, f.v);
    if (!Check(f.v[0] == 1 && f.v[1] == 0 && f.v[2] == 0 && f.v[3] == 0,
               "FAIL secondary current after array"))
        return FALSE;
    pglDisableClientState(array);
    pglDisableClientState(GL_COLOR_ARRAY);
    pglDisableClientState(GL_VERTEX_ARRAY);
    pglDisable(sum);
    Log("PASS automated secondary: negotiated public EXT scalar/array entrypoints,192 exact GPU "
        "pixels,texture modulation then color addition, unchanged alpha and four typed RGBA query "
        "guards");
    return TRUE;
}

static void ArrayAcceptance(void) {
    GLfloat quad[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    GLubyte colors[4][4] = {{0, 255, 0, 255}, {0, 255, 0, 255}, {0, 255, 0, 255}, {0, 255, 0, 255}};
    GLushort indices[6] = {0, 1, 2, 0, 2, 3};
    BYTE red[4] = {255, 0, 0, 255};
    static GLfloat vertices[1050][2];
    BYTE stripe[129 * 4], readback[540];
    GLuint names[3] = {0};
    ULONG i;
    GLint enabled;
    void *pointer = (void *)1;
    if (!Check(pwglShareLists(Contexts[0], Contexts[1]), "FAIL pristine context sharing"))
        return;
    if (!Check(pwglMakeCurrent(DCs[0], Contexts[0]), "FAIL array source current"))
        return;
    pglGenTextures(2, names);
    if (!Check(names[0] && names[1] && names[0] != names[1], "FAIL texture name reservation"))
        return;
    pglBindTexture(GL_TEXTURE_2D, 73);
    pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    pglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, red);
    pglViewport(0, 0, 320, 240);
    pglClearColor(0, 0, 0, 1);
    pglClear(GL_COLOR_BUFFER_BIT);
    pglVertexPointer(2, GL_FLOAT, 0, quad);
    pglColorPointer(4, GL_UNSIGNED_BYTE, 0, colors);
    pglEnableClientState(GL_VERTEX_ARRAY);
    pglEnableClientState(GL_COLOR_ARRAY);
    pglDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);
    ZeroMemory(quad, sizeof(quad));
    ZeroMemory(colors, sizeof(colors));
    ZeroMemory(indices, sizeof(indices));
    if (!ArrayPixels(0, 255, 0))
        return;
    if (!Check(pwglMakeCurrent(DCs[1], Contexts[1]), "FAIL array destination current"))
        return;
    pglGetIntegerv(GL_VERTEX_ARRAY, &enabled);
    pglGetPointerv(GL_VERTEX_ARRAY_POINTER, &pointer);
    if (!Check(!enabled && !pointer && pglIsTexture(73), "FAIL shared texture/local array state"))
        return;
    pglGenTextures(1, names + 2);
    if (!Check(names[2] && names[2] != names[0] && names[2] != names[1],
               "FAIL shared name reservation"))
        return;
    if (!Check(pwglDeleteContext(Contexts[0]), "FAIL shared source deletion"))
        return;
    Contexts[0] = NULL;
    pglBindTexture(GL_TEXTURE_2D, 73);
    pglEnable(GL_TEXTURE_2D);
    pglColor4f(1, 1, 1, 1);
    pglViewport(0, 0, 320, 240);
    pglClearColor(0, 0, 0, 1);
    pglClear(GL_COLOR_BUFFER_BIT);
    for (i = 0; i < 1050; ++i) {
        vertices[i][0] = 2;
        vertices[i][1] = 2;
    }
    vertices[1047][0] = -1;
    vertices[1047][1] = -1;
    vertices[1048][0] = 3;
    vertices[1048][1] = -1;
    vertices[1049][0] = -1;
    vertices[1049][1] = 3;
    pglVertexPointer(2, GL_FLOAT, 0, vertices);
    pglEnableClientState(GL_VERTEX_ARRAY);
    pglDrawArrays(GL_TRIANGLES, 0, 1050);
    ZeroMemory(vertices, sizeof(vertices));
    if (!ArrayPixels(255, 0, 0))
        return;
    if (!Check(pwglSwapBuffers(DCs[1]), "FAIL array swap"))
        return;
    if (!PackedTextureAcceptance())
        return;
    pglPushAttrib(GL_ENABLE_BIT | GL_TEXTURE_BIT | GL_COLOR_BUFFER_BIT | GL_VIEWPORT_BIT);
    pglBindTexture(GL_TEXTURE_1D, 77);
    pglTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    pglTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    for (i = 0; i < 129; ++i) {
        stripe[i * 4] = i == 128 ? 255 : 0;
        stripe[i * 4 + 1] = i == 128 ? 0 : 255;
        stripe[i * 4 + 2] = 0;
        stripe[i * 4 + 3] = 255;
    }
    pglTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA, 129, 0, GL_RGBA, GL_UNSIGNED_BYTE, stripe);
    for (i = 0; i < sizeof(readback); ++i)
        readback[i] = 0xa5;
    pglPixelStorei(GL_PACK_ALIGNMENT, 8);
    pglPixelStorei(GL_PACK_SKIP_PIXELS, 2);
    pglPixelStorei(GL_PACK_SKIP_ROWS, 99);
    pglGetTexImage(GL_TEXTURE_1D, 0, GL_RGBA, GL_UNSIGNED_BYTE, readback);
    for (i = 0; i < sizeof(readback); ++i)
        if (!Check(readback[i] == (i >= 8 && i < 524 ? stripe[i - 8] : 0xa5),
                   "FAIL1D texture readback/pack canary"))
            return;
    pglPixelStorei(GL_PACK_ALIGNMENT, 4);
    pglPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    pglPixelStorei(GL_PACK_SKIP_ROWS, 0);
    pglViewport(0, 0, 8, 8);
    pglDrawBuffer(GL_FRONT);
    pglPopAttrib();
    pglGetIntegerv(GL_TEXTURE_BINDING_1D, &enabled);
    if (!Check(!enabled, "FAIL attrib restore1D binding"))
        return;
    pglGetIntegerv(GL_DRAW_BUFFER, &enabled);
    if (!Check(enabled == GL_BACK && pglGetError() == GL_NO_ERROR,
               "FAIL attrib restore draw buffer/error"))
        return;
    {
        static BYTE upload[65536], bulk[65538];
        GLuint texture;
        pglGenTextures(1, &texture);
        pglBindTexture(GL_TEXTURE_2D, texture);
        for (i = 0; i < 16384; ++i) {
            upload[i * 4] = (BYTE)i;
            upload[i * 4 + 1] = (BYTE)(i >> 8);
            upload[i * 4 + 2] = 0x55;
            upload[i * 4 + 3] = 255;
        }
        for (i = 0; i < sizeof(bulk); ++i)
            bulk[i] = 0xa5;
        pglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 128, 128, 0, GL_RGBA, GL_UNSIGNED_BYTE, upload);
        pglGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, bulk + 1);
        if (!Check(pglGetError() == GL_NO_ERROR && bulk[0] == 0xa5 && bulk[65537] == 0xa5,
                   "FAIL64KiB bulk readback error/guard"))
            return;
        for (i = 0; i < 65536; ++i)
            if (!Check(bulk[i + 1] == upload[i], "FAIL64KiB bulk readback exact pixels"))
                return;
        Log("PASS automated bulkreadback: 16384 exact RGBA pixels,64KiB unaligned destination and "
            "guards");
    }
    Log("PASS automated resources: shared generated names,1D texture129pixels, bounded texture "
        "readback and pack guards, attrib restoration");
    if (!ReadbackAcceptance())
        return;
    if (!SecondaryAcceptance())
        return;
    Log("PASS automated arrays: indexed immutable colors, context-local pointers, shared texture "
        "survives source deletion, 1050-vertex bounded draw; 512 exact GPU pixels");
}

static DWORD WINAPI OtherThread(LPVOID transfer) {
    if (!transfer) {
        if (!pwglMakeCurrent(DCs[1], Contexts[1]))
            return 10;
        if (pwglMakeCurrent(DCs[0], Contexts[0]))
            return 11;
        if (pwglGetCurrentContext() || pwglGetCurrentDC())
            return 12;
        return 1;
    }
    if (!pwglMakeCurrent(DCs[0], Contexts[0]))
        return 13;
    if (pwglGetCurrentContext() != Contexts[0])
        return 14;
    return pwglMakeCurrent(NULL, NULL) ? 1 : 15;
}
static BOOL ThreadCheck(BOOL transfer) {
    HANDLE thread;
    DWORD result = 0, wait, error, thread_id = 0;
    char line[160];
    /* Windows 9x requires an actual thread-ID output pointer. */
    thread = CreateThread(NULL, 0, OtherThread, (LPVOID)(ULONG_PTR)transfer, 0, &thread_id);
    if (!thread) {
        error = GetLastError();
        wsprintfA(line, "THREAD create failed error=%lu transfer=%lu", error, (DWORD)transfer);
        Log(line);
        return FALSE;
    }
    wait = WaitForSingleObject(thread, 5000);
    error = GetLastError();
    if (wait == WAIT_OBJECT_0) {
        if (!GetExitCodeThread(thread, &result))
            error = GetLastError();
        else
            error = 0;
    }
    if (result != 1) {
        wsprintfA(line, "THREAD failed stage=%lu wait=%lu error=%lu transfer=%lu", result, wait,
                  error, (DWORD)transfer);
        Log(line);
    }
    CloseHandle(thread);
    return result == 1;
}
static void Cleanup(void) {
    ULONG i;
    Closing = TRUE;
    pwglMakeCurrent(NULL, NULL);
    for (i = 0; i < 2; ++i) {
        if (Contexts[i])
            Check(pwglDeleteContext(Contexts[i]), "FAIL delete context");
        Contexts[i] = NULL;
        if (Windows[i]) {
            ReleaseDC(Windows[i], DCs[i]);
            DestroyWindow(Windows[i]);
            Windows[i] = NULL;
        }
    }
}
static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM key, LPARAM extra) {
    if (message == WM_PAINT) {
        PAINTSTRUCT p;
        BeginPaint(window, &p);
        EndPaint(window, &p);
        return 0;
    }
    if (message == WM_KEYDOWN) {
        if (key == VK_ESCAPE) {
            PostQuitMessage(0);
            return 0;
        }
        if (key == VK_F3) {
            DrawTexture(TRUE);
            return 0;
        }
        if (key == VK_F4 && Textured) {
            DrawTexture(FALSE);
            return 0;
        }
        if (key == VK_F5) {
            TextureThroughput();
            return 0;
        }
        if (key == VK_F6) {
            FrontBuffers(FALSE);
            return 0;
        }
        if (key == VK_F7) {
            FrontBuffers(TRUE);
            return 0;
        }
        if (key == VK_F1) {
            Draw(0);
            Draw(1);
            Log("STAGE redraw");
            return 0;
        }
        if (key == VK_F2 && Windows[1]) {
            HWND closed = Windows[1];
            Windows[1] = NULL;
            DestroyWindow(closed);
            pwglMakeCurrent(NULL, NULL);
            Check(pwglDeleteContext(Contexts[1]), "FAIL HWND-before-context destruction");
            Contexts[1] = NULL;
            Draw(0);
            Log("STAGE second window closed");
            return 0;
        }
    }
    if (message == WM_CLOSE && !Closing) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(window, message, key, extra);
}
void WINAPI WinMainCRTStartup(void) {
    WNDCLASSA cls;
    PIXELFORMATDESCRIPTOR requested, described;
    MSG message;
    ULONG i, frame;
    CHAR line[160];
    {
        const char *arg = GetCommandLineA();
        for (; *arg; ++arg)
            if ((*arg == ' ' || *arg == '\t') && !lstrcmpA(arg + 1, "-arrays"))
                AutomatedArrays = TRUE;
    }
    LogFile = CreateFileA("C:\\DGWGL.LOG", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                          FILE_ATTRIBUTE_NORMAL, NULL);
    Library = LoadLibraryA("dgpugl.dll");
    if (!Check(Library != NULL, "FAIL LoadLibrary dgpugl.dll"))
        goto done;
#define LOAD(name)                                                                                 \
    p##name = (void *)GetProcAddress(Library, #name);                                              \
    if (!Check(p##name != NULL, "FAIL missing export " #name))                                     \
    goto done
    LOAD(wglCreateContext);
    LOAD(wglDeleteContext);
    LOAD(wglMakeCurrent);
    LOAD(wglGetCurrentContext);
    LOAD(wglGetCurrentDC);
    LOAD(wglSwapBuffers);
    LOAD(wglChoosePixelFormat);
    LOAD(wglDescribePixelFormat);
    LOAD(wglSetPixelFormat);
    LOAD(glClearColor);
    LOAD(glClear);
    LOAD(glClearDepth);
    LOAD(glClearStencil);
    LOAD(glViewport);
    LOAD(glMatrixMode);
    LOAD(glLoadIdentity);
    LOAD(glBegin);
    LOAD(glEnd);
    LOAD(glColor4f);
    LOAD(glVertex2f);
    LOAD(glGetError);
    LOAD(glBindTexture);
    LOAD(glTexParameteri);
    LOAD(glTexImage2D);
    LOAD(glTexSubImage2D);
    LOAD(glPixelStorei);
    LOAD(glTexCoord2f);
    LOAD(glEnable);
    LOAD(glDisable);
    LOAD(glGetIntegerv);
    LOAD(glGetFloatv);
    LOAD(glGetDoublev);
    LOAD(glGetBooleanv);
    LOAD(glGetTexLevelParameteriv);
    LOAD(glIsTexture);
    LOAD(glIsEnabled);
    LOAD(glGetString);
    LOAD(wglGetProcAddress);
    LOAD(glDrawBuffer);
    LOAD(glReadBuffer);
    LOAD(glFlush);
    LOAD(glReadPixels);
    LOAD(glColor4ub);
    LOAD(glVertex3fv);
    LOAD(glHint);
    LOAD(wglShareLists);
    LOAD(glVertexPointer);
    LOAD(glColorPointer);
    LOAD(glEnableClientState);
    LOAD(glDisableClientState);
    LOAD(glGetPointerv);
    LOAD(glDrawArrays);
    LOAD(glDrawElements);
    LOAD(glGenTextures);
    LOAD(glPushAttrib);
    LOAD(glPopAttrib);
    LOAD(glTexImage1D);
    LOAD(glGetTexImage);
#undef LOAD
    Instance = GetModuleHandleA(NULL);
    ZeroMemory(&cls, sizeof(cls));
    cls.style = CS_OWNDC;
    cls.lpfnWndProc = WindowProc;
    cls.hInstance = Instance;
    cls.lpszClassName = "DreamGPUWGLProbe";
    RegisterClassA(&cls);
    ZeroMemory(&requested, sizeof(requested));
    requested.nSize = sizeof(requested);
    requested.nVersion = 1;
    requested.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    requested.iPixelType = PFD_TYPE_RGBA;
    requested.cColorBits = 32;
    requested.cDepthBits = 32;
    requested.cStencilBits = 8;
    for (i = 0; i < 2; ++i) {
        Windows[i] =
            CreateWindowExA(0, cls.lpszClassName, "DreamGPU WGL public API", WS_POPUP | WS_VISIBLE,
                            64 + i * 352, 64, 320, 240, NULL, NULL, Instance, NULL);
        if (!Check(Windows[i] != NULL, "FAIL window"))
            goto cleanup;
        DCs[i] = GetDC(Windows[i]);
        if (!Check(pwglChoosePixelFormat(DCs[i], &requested) == 1 &&
                       pwglDescribePixelFormat(DCs[i], 1, sizeof(described), &described) == 1 &&
                       described.cDepthBits == 24 && described.cStencilBits == 8 &&
                       pwglSetPixelFormat(DCs[i], 1, &requested),
                   "FAIL closest pixel format"))
            goto cleanup;
        Contexts[i] = pwglCreateContext(DCs[i]);
        if (!Check(Contexts[i] != NULL, "FAIL create context"))
            goto cleanup;
    }
    if (AutomatedArrays) {
        ArrayAcceptance();
        goto cleanup;
    }
    if (!Check(pwglMakeCurrent(DCs[0], Contexts[0]), "FAIL initial context"))
        goto cleanup;
    if (!Check(!pwglMakeCurrent(DCs[0], (HGLRC)(ULONG_PTR)0xffffffffUL) &&
                   !pwglGetCurrentContext() && !pwglGetCurrentDC(),
               "FAIL failed MakeCurrent must unbind"))
        goto cleanup;
    if (!Check(pwglMakeCurrent(DCs[0], Contexts[0]), "FAIL rebind after failed MakeCurrent"))
        goto cleanup;
    if (!Check(ThreadCheck(FALSE), "FAIL concurrent ownership rejection"))
        goto cleanup;
    if (!Check(pwglMakeCurrent(NULL, NULL) && ThreadCheck(TRUE), "FAIL context thread transfer"))
        goto cleanup;
    for (frame = 0; frame < 9; ++frame)
        for (i = 0; i < 2; ++i)
            if (!Draw(i))
                goto cleanup;
    SetForegroundWindow(Windows[0]);
    SetFocus(Windows[0]);
    wsprintfA(line,
              "READY frames=%lu; left pane red/blue, right pane yellow/cyan; vertical split. F1 "
              "redraw F2 destroy second Esc close",
              Frames);
    Log(line);
    while (GetMessageA(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
cleanup:
    Cleanup();
    if (!Failed && !AutomatedArrays)
        Log("PASS public WGL lifecycle, cross-thread ownership, batched GL, window presentation "
            "and clean close; HOST PIXELS REQUIRED");
done:
    if (Library)
        FreeLibrary(Library);
    if (LogFile != INVALID_HANDLE_VALUE)
        CloseHandle(LogFile);
    ExitProcess(Failed ? 1 : 0);
}
