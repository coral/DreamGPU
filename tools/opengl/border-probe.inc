/* SPDX-License-Identifier: GPL-2.0-or-later
 * Bounded installed GL1.1 supplied-border oracle. Constant coordinates avoid
 * rasterization ambiguity; one byte tolerance permits UNORM rounding only.
 */
static bool border_probe(HMODULE module, Log &log) {
#define GL_ENTRY(name, signature) auto name = entry<signature>(module, #name)
    GL_ENTRY(glGenTextures, void(WINAPI *)(GLsizei, GLuint *));
    GL_ENTRY(glDeleteTextures, void(WINAPI *)(GLsizei, const GLuint *));
    GL_ENTRY(glBindTexture, void(WINAPI *)(GLenum, GLuint));
    GL_ENTRY(glTexImage1D,
             void(WINAPI *)(GLenum, GLint, GLint, GLsizei, GLint, GLenum, GLenum, const void *));
    GL_ENTRY(glTexImage2D, void(WINAPI *)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum,
                                          GLenum, const void *));
    GL_ENTRY(glTexSubImage1D,
             void(WINAPI *)(GLenum, GLint, GLint, GLsizei, GLenum, GLenum, const void *));
    GL_ENTRY(glTexSubImage2D, void(WINAPI *)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum,
                                             GLenum, const void *));
    GL_ENTRY(glGetTexImage, void(WINAPI *)(GLenum, GLint, GLenum, GLenum, void *));
    GL_ENTRY(glTexParameteri, void(WINAPI *)(GLenum, GLenum, GLint));
    GL_ENTRY(glTexEnvi, void(WINAPI *)(GLenum, GLenum, GLint));
    GL_ENTRY(glEnable, void(WINAPI *)(GLenum));
    GL_ENTRY(glDisable, void(WINAPI *)(GLenum));
    GL_ENTRY(glViewport, void(WINAPI *)(GLint, GLint, GLsizei, GLsizei));
    GL_ENTRY(glBegin, void(WINAPI *)(GLenum));
    GL_ENTRY(glEnd, void(WINAPI *)());
    GL_ENTRY(glTexCoord4f, void(WINAPI *)(GLfloat, GLfloat, GLfloat, GLfloat));
    GL_ENTRY(glVertex2f, void(WINAPI *)(GLfloat, GLfloat));
    GL_ENTRY(glReadPixels, void(WINAPI *)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *));
    GL_ENTRY(glGetError, GLenum(WINAPI *)());
#undef GL_ENTRY
    if (!glGenTextures || !glDeleteTextures || !glBindTexture || !glTexImage1D || !glTexImage2D ||
        !glTexSubImage1D || !glTexSubImage2D || !glGetTexImage || !glTexParameteri || !glTexEnvi ||
        !glEnable || !glDisable || !glViewport || !glBegin || !glEnd || !glTexCoord4f ||
        !glVertex2f || !glReadPixels || !glGetError) {
        log.line("FAIL border exports");
        return false;
    }
    glDisable(GL_DITHER);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glViewport(0, 0, 64, 64);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    bool passed = true;
    for (unsigned dimension = 1; dimension <= 2 && passed; ++dimension) {
        const GLenum target = dimension == 1 ? GL_TEXTURE_1D : GL_TEXTURE_2D;
        GLuint name = 0;
        glGenTextures(1, &name);
        glBindTexture(target, name);
        glEnable(target);
        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP);
        glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP);
        BYTE pixels[64], expected[64], download[64];
        const unsigned count = dimension == 1 ? 4 : 16;
        for (unsigned i = 0; i < count; ++i) {
            const bool border =
                (i % 4 == 0 || i % 4 == 3 || (dimension == 2 && (i / 4 == 0 || i / 4 == 3)));
            expected[i * 4] = pixels[i * 4] = border ? 0 : 255;
            expected[i * 4 + 1] = pixels[i * 4 + 1] = border ? 255 : 0;
            expected[i * 4 + 2] = pixels[i * 4 + 2] = 0;
            expected[i * 4 + 3] = pixels[i * 4 + 3] = 255;
        }
        if (dimension == 1)
            glTexImage1D(target, 0, GL_RGBA, 4, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        else
            glTexImage2D(target, 0, GL_RGBA, 4, 4, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        // Texture ownership cannot depend on retaining the caller's buffer.
        for (unsigned i = 0; i < sizeof(pixels); ++i)
            pixels[i] = 0xcd;
        for (unsigned phase = 0; phase < 2 && passed; ++phase) {
            if (phase) {
                const BYTE blue[4] = {0, 0, 255, 255};
                if (dimension == 1) {
                    glTexSubImage1D(target, 0, -1, 1, GL_RGBA, GL_UNSIGNED_BYTE, blue);
                    CopyMemory(expected, blue, 4);
                } else {
                    glTexSubImage2D(target, 0, -1, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, blue);
                    CopyMemory(expected + 16, blue, 4);
                }
            }
            for (unsigned i = 0; i < sizeof(download); ++i)
                download[i] = 0xa5;
            glGetTexImage(target, 0, GL_RGBA, GL_UNSIGNED_BYTE, download);
            for (unsigned i = 0; i < count * 4; ++i)
                if (download[i] != expected[i]) {
                    log.value("FAIL border texture storage byte", i);
                    passed = false;
                    break;
                }
            for (unsigned i = count * 4; i < sizeof(download); ++i)
                if (download[i] != 0xa5) {
                    log.line("FAIL border texture storage guard");
                    passed = false;
                }
            const unsigned cases = phase ? 1 : 4;
            for (unsigned sample = 0; sample < cases && passed; ++sample) {
                // s=0 edge, interior, repeat seam, then projective s/q=1/8.
                const GLfloat s = phase || sample == 0 || sample == 2 ? 0.0f
                                  : sample == 1                       ? 0.5f
                                                                      : 0.25f;
                const GLfloat t = !phase && sample == 0 ? 0.0f : 0.25f;
                const GLfloat q = !phase && sample == 3 ? 2.0f : 1.0f;
                glTexParameteri(target, GL_TEXTURE_WRAP_S, sample == 2 ? GL_REPEAT : GL_CLAMP);
                glTexParameteri(target, GL_TEXTURE_WRAP_T, sample == 2 ? GL_REPEAT : GL_CLAMP);
                glTexCoord4f(s, t * q, 0.0f, q);
                glBegin(GL_QUADS);
                glVertex2f(-1, -1);
                glVertex2f(1, -1);
                glVertex2f(1, 1);
                glVertex2f(-1, 1);
                glEnd();
                BYTE actual[4] = {};
                glReadPixels(32, 32, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, actual);
                const unsigned red = phase         ? 128
                                     : sample == 0 ? (dimension == 1 ? 128 : 64)
                                     : sample == 3 ? 191
                                                   : 255;
                const unsigned green = phase         ? 0
                                       : sample == 0 ? (dimension == 1 ? 128 : 191)
                                       : sample == 3 ? 64
                                                     : 0;
                const unsigned wanted[3] = {red, green, phase ? 128u : 0u};
                for (unsigned channel = 0; channel < 3; ++channel) {
                    const int difference =
                        static_cast<int>(actual[channel]) - static_cast<int>(wanted[channel]);
                    if (difference < -1 || difference > 1) {
                        log.value("FAIL border dimension", dimension);
                        log.value("phase", phase);
                        log.value("sample", sample);
                        log.value("channel", channel);
                        log.value("expected", wanted[channel]);
                        log.value("actual", actual[channel]);
                        passed = false;
                    }
                }
            }
            const GLenum error = glGetError();
            if (error != GL_NO_ERROR) {
                log.value("FAIL border GL error", error);
                passed = false;
            }
        }
        glDisable(target);
        glDeleteTextures(1, &name);
        if (glGetError() != GL_NO_ERROR) {
            log.line("FAIL border cleanup");
            passed = false;
        }
    }
    return passed;
}
