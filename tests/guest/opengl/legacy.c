/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "compatibility.cpp"
#include "readback.cpp"
static BOOL Ready = TRUE;
static GLenum Error;
static GLfloat Values[4];
static ULONG Calls, Deleted[20], DeletedCount, Capacity = 12, QueryCalls, ShortReply;
static ULONG Readback[128];
ULONG JglReadbackCapacity(void) {
    return sizeof(Readback);
}
ULONG *JglReadbackBuffer(void) {
    return Readback;
}
static JGL_UNPACK Pack = {4, 0, 0, 0, 0, 0};
static void Marker(void) {}
BOOL JglReady(void) {
    return Ready;
}
void JglSetError(GLenum e) {
    Error = e;
}
HINSTANCE JglModule(void) {
    return (HINSTANCE)1;
}
HGLRC wglGetCurrentContext(void) {
    return Ready ? (HGLRC)1 : NULL;
}
PROC GetProcAddress(HINSTANCE module, LPCSTR name) {
    assert(module == (HINSTANCE)1);
    return !strcmp(name, "known") ? Marker : NULL;
}
JGL_UNPACK *JglPack(void) {
    return &Pack;
}
void JglDrawableSize(ULONG *width, ULONG *height) {
    *width = 640;
    *height = 480;
}
ULONG JglMaxDataBytes(ULONG words) {
    assert(words == 1);
    return Capacity;
}
void glColor3f(GLfloat a, GLfloat b, GLfloat c) {
    Values[0] = a;
    Values[1] = b;
    Values[2] = c;
    Values[3] = 1;
}
void glColor4f(GLfloat a, GLfloat b, GLfloat c, GLfloat d) {
    glColor3f(a, b, c);
    Values[3] = d;
}
void glVertex3f(GLfloat a, GLfloat b, GLfloat c) {
    glColor3f(a, b, c);
}
void glTexCoord2f(GLfloat a, GLfloat b) {
    glColor3f(a, b, 0);
}
void glNormal3f(GLfloat a, GLfloat b, GLfloat c) {
    glColor3f(a, b, c);
}
void JglScalarVector(ULONG function, ULONG words, const void *arguments) {
    assert((function == FEnum_glVertex3f || function == FEnum_glColor3f ||
            function == FEnum_glNormal3f)
               ? words == 3
           : function == FEnum_glTexCoord2f ? words == 2
                                            : function == FEnum_glColor4f && words == 4);
    memcpy(Values, arguments, words * sizeof(ULONG));
}
void glFogf(GLenum name, GLfloat value) {
    assert(name == GL_FOG_MODE);
    Values[0] = value;
}
BOOL JglData(ULONG fn, const ULONG *args, ULONG words, const void *data, ULONG bytes) {
    assert(words == 1);
    ++Calls;
    if (fn == FEnum_glDeleteTextures) {
        assert(bytes == args[0] * 4 && bytes <= Capacity && DeletedCount + args[0] <= 20);
        memcpy(Deleted + DeletedCount, data, bytes);
        DeletedCount += args[0];
    } else {
        assert(fn == FEnum_glFogfv && args[0] == GL_FOG_COLOR && bytes == 16);
        memcpy(Values, data, bytes);
    }
    return TRUE;
}
BOOL JglQuery(ULONG fn, const ULONG *args, ULONG type, void *out, ULONG capacity, ULONG *bytes) {
    ULONG i, count = args[2] & 65535, *rgba = (ULONG *)out;
    if (fn == FEnum_glGetTexLevelParameteriv) {
        assert(capacity == 4);
        *(GLint *)out = args[2] == GL_TEXTURE_WIDTH ? 129 : 2;
        *bytes = 4;
        ++QueryCalls;
        return TRUE;
    }
    if (fn == FEnum_glGetTexImage) {
        assert(capacity == 512);
        for (i = 0; i < 128; ++i)
            rgba[i] = ((args[2] + i) & 255) | 0xff332200;
        *bytes = ShortReply ? 508 : 512;
        ++QueryCalls;
        return TRUE;
    }
    assert(fn == FEnum_glReadPixels && type == DG_GL_RESULT_INT && args[2] >> 16 == 1);
    assert(count && count <= 128 && capacity == count * 4);
    ++QueryCalls;
    for (i = 0; i < count; ++i)
        rgba[i] = ((args[0] + i) & 255) | ((args[1] & 255) << 8) | 0xaa550000UL;
    *bytes = ShortReply ? capacity - 4 : capacity;
    return TRUE;
}
int main(void) {
    GLuint names[8] = {3, 5, 7, 11, 13, 17, 19, 23};
    GLubyte color[4] = {0, 128, 255, 255};
    GLfloat vector[4] = {.25f, .5f, .75f, 1};
    BYTE image[3 * 416 + 32], before[sizeof(image)];
    ULONG i, row, column, at;
    assert(wglGetProcAddress("known") == Marker && !wglGetProcAddress("unknown"));
    Ready = FALSE;
    assert(!wglGetProcAddress("known"));
    Ready = TRUE;
    glColor4ubv(color);
    assert(Values[0] == 0 && Values[1] == 128 / 255.0f && Values[2] == 1 && Values[3] == 1);
    glColor3ubv(color);
    assert(Values[2] == 1 && Values[3] == 1);
    glVertex3fv(vector);
    memset(vector, 0, sizeof(vector));
    assert(Values[0] == .25f && Values[2] == .75f);
    vector[0] = .25f;
    vector[1] = .5f;
    vector[2] = .75f;
    vector[3] = 1;
    glFogfv(GL_FOG_COLOR, vector);
    memset(vector, 0, sizeof(vector));
    assert(Values[3] == 1 && Values[0] == .25f);
    glFogi(GL_FOG_MODE, 0x800);
    assert(Values[0] == 2048);
    glFogfv(0, vector);
    assert(Error == GL_INVALID_ENUM);
    Error = 0;
    Calls = 0;
    glDeleteTextures(8, names);
    memset(names, 0, sizeof(names));
    assert(Calls == 3 && DeletedCount == 8 && Deleted[0] == 3 && Deleted[7] == 23);
    glDeleteTextures(-1, names);
    assert(Error == GL_INVALID_VALUE && Calls == 3);
    Error = 0;
    Pack = (JGL_UNPACK){8, 137, 1, 2, 0, 0};
    memset(image, 0xcc, sizeof(image));
    glReadPixels(10, 20, 130, 2, GL_RGB, GL_UNSIGNED_BYTE, image);
    assert(QueryCalls == 4 && !Error); /*128+2 pixels per row, no hidden full read. */
    for (row = 0; row < 2; ++row)
        for (column = 0; column < 130; ++column) {
            at = (row + 1) * 416 + 6 + column * 3;
            assert(image[at] == (BYTE)(column + 10) && image[at + 1] == row + 20 &&
                   image[at + 2] == 0x55);
        }
    for (i = 0; i < 422; ++i)
        assert(image[i] == 0xcc);
    assert(image[812] == 0xcc && image[837] == 0xcc && image[1228] == 0xcc);
    Pack = (JGL_UNPACK){4, 0, 0, 0, 0, 0};
    glReadPixels(7, 9, 1, 1, 0x80e1, GL_UNSIGNED_BYTE, image);
    assert(image[0] == 0x55 && image[1] == 9 && image[2] == 7 && image[3] == 0xaa);
    memset(image, 0xcc, sizeof(image));
    glReadPixels(7, 9, 1, 1, 0x80e1, 0x8367, image + 1);
    assert(!Error && image[0] == 0xcc && image[1] == 0x55 && image[2] == 9 && image[3] == 7 &&
           image[4] == 0xaa && image[5] == 0xcc);
    Pack.SwapBytes = 1;
    glReadPixels(7, 9, 1, 1, 0x80e1, 0x8367, image + 1);
    assert(image[0] == 0xcc && image[1] == 0xaa && image[2] == 7 && image[3] == 9 &&
           image[4] == 0x55 && image[5] == 0xcc);
    glReadPixels(7, 9, 1, 1, GL_RGBA, 0x8367, image + 1);
    assert(image[1] == 0xaa && image[2] == 0x55 && image[3] == 9 && image[4] == 7);
    glReadPixels(7, 9, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, image + 1);
    assert(image[1] == 7 && image[2] == 9 && image[3] == 0x55 && image[4] == 0xaa);
    Pack.SwapBytes = 0;
    Calls = QueryCalls;
    memset(image, 0xcc, sizeof(image));
    glReadPixels(-1, -1, 2, 2, GL_RGBA, GL_UNSIGNED_BYTE, image);
    assert(QueryCalls == Calls + 1);
    for (i = 0; i < 12; ++i)
        assert(image[i] == 0);
    assert(image[12] == 0 && image[13] == 0 && image[14] == 0x55 && image[15] == 0xaa);
    memcpy(before, image, sizeof(image));
    ShortReply = 1;
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, image);
    assert(Error == GL_INVALID_OPERATION && !memcmp(image, before, sizeof(image)));
    ShortReply = 0;
    Error = 0;
    Calls = QueryCalls;
    glReadPixels(INT32_MAX, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, image);
    assert(Error == GL_INVALID_VALUE && QueryCalls == Calls);
    {
        BYTE texture[1060];
        ULONG j;
        memset(texture, 0xcc, sizeof(texture));
        Pack = (JGL_UNPACK){8, 0, 999, 2, 0, 0};
        Error = 0;
        ShortReply = 0;
        glGetTexImage(GL_TEXTURE_1D, 0, GL_RGBA, GL_UNSIGNED_BYTE, texture);
        assert(!Error);
        for (j = 0; j < sizeof(texture); ++j) {
            BYTE expected = 0xcc;
            if (j >= 8 && j < 524) {
                ULONG pixel = (j - 8) / 4, component = (j - 8) % 4;
                expected = component == 0   ? (BYTE)pixel
                           : component == 1 ? 0x22
                           : component == 2 ? 0x33
                                            : 0xff;
            }
            assert(texture[j] == expected);
        }
        memset(texture, 0xcc, sizeof(texture));
        Pack = (JGL_UNPACK){8, 0, 999, 2, 0, 0};
        glGetTexImage(GL_TEXTURE_1D, 0, 0x80e1, 0x8367, texture + 1);
        assert(!Error && texture[8] == 0xcc && texture[9] == 0x33 && texture[10] == 0x22 &&
               texture[11] == 0 && texture[12] == 0xff && texture[525] == 0xcc);
        Pack.SwapBytes = 1;
        glGetTexImage(GL_TEXTURE_1D, 0, 0x80e1, 0x8367, texture + 1);
        assert(!Error && texture[8] == 0xcc && texture[9] == 0xff && texture[10] == 0 &&
               texture[11] == 0x22 && texture[12] == 0x33 && texture[525] == 0xcc);
        glGetTexImage(GL_TEXTURE_1D, 0, GL_RGBA, GL_UNSIGNED_BYTE, texture + 1);
        assert(texture[9] == 0 && texture[10] == 0x22 && texture[11] == 0x33 &&
               texture[12] == 0xff);
        Pack = (JGL_UNPACK){4, 0, 0, 0, 0, 0};
        ShortReply = 1;
        Error = 0;
        glGetTexImage(GL_TEXTURE_1D, 0, GL_RGBA, GL_UNSIGNED_BYTE, texture);
        assert(Error == GL_INVALID_OPERATION);
        ShortReply = 0;
        Error = 0;
    }
    puts("PASS legacy frontend: scalar normalization, immutable vectors/deletes, bounded reads, "
         "pack stride/skips and malformed results");
    return 0;
}

void JglForgetTextures(ULONG count, const GLuint *textures) {
    (void)count;
    (void)textures;
}

void glVertex2f(GLfloat a, GLfloat b) {
    glColor3f(a, b, 0);
}
void glVertex4f(GLfloat a, GLfloat b, GLfloat c, GLfloat d) {
    glColor4f(a, b, c, d);
}
void glTexCoord4f(GLfloat a, GLfloat b, GLfloat c, GLfloat d) {
    glColor4f(a, b, c, d);
}
void glLightModelf(GLenum pname, GLfloat value) {
    (void)pname;
    Values[0] = value;
}
