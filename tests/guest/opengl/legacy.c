/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "compatibility.cpp"
extern "C" BOOL JglDepthStencilReadbackAvailable(void);
#include "readback.cpp"
static BOOL Ready = TRUE;
static GLenum Error;
static GLfloat Values[4];
static ULONG Calls, Deleted[20], DeletedCount, Capacity = 12, QueryCalls, ShortReply;
static ULONG Readback[128];
static GLint TextureWidth = 129, TextureHeight = 2;
static BOOL PackedOracle, FixedPixel, ExtendedReadback;
static float FloatPixel[4] = {.25f, .5f, .75f, 1.0f};
BOOL JglDepthStencilReadbackAvailable(void) {
    return ExtendedReadback;
}
static ULONG ReadPixel(ULONG at) {
    if (FixedPixel)
        return 0x78563412;
    if (PackedOracle) {
        /* Expand every possible original RGB565 texel to transport RGBA8. */
        ULONG r = (at >> 11) & 31, g = (at >> 5) & 63, b = at & 31;
        return ((r * 255 + 15) / 31) | (((g * 255 + 31) / 63) << 8) |
               (((b * 255 + 15) / 31) << 16) | 0xff000000;
    }
    return (at & 255) | 0xff332200;
}
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
        *(GLint *)out = args[2] == GL_TEXTURE_WIDTH ? TextureWidth : TextureHeight;
        *bytes = 4;
        ++QueryCalls;
        return TRUE;
    }
    if (fn == FEnum_glGetTexImage && (args[1] & DG_GL_TEXTURE_READ_FLOAT)) {
        ULONG pixels = args[1] >> 16;
        assert(ExtendedReadback && type == DG_GL_RESULT_FLOAT && pixels && capacity == pixels * 16);
        for (i = 0; i < pixels; ++i) {
            ULONG c, pixel = ReadPixel(args[2] + i);
            for (c = 0; c < 4; ++c)
                ((float *)out)[i * 4 + c] =
                    FixedPixel ? FloatPixel[c] : ((pixel >> (c * 8)) & 255) / 255.0f;
        }
        *bytes = ShortReply ? capacity - 4 : capacity;
        ++QueryCalls;
        return TRUE;
    }
    if (fn == FEnum_glReadPixels && (args[0] & ~DG_GL_READ_X_MASK)) {
        ULONG mode = args[0] & ~DG_GL_READ_X_MASK, x = args[0] & DG_GL_READ_X_MASK;
        ULONG pixels = (args[2] & 65535) * (args[2] >> 16), wide = mode == DG_GL_READ_RGBA_FLOAT;
        assert(ExtendedReadback && capacity == pixels * (wide ? 16 : 4));
        assert(type == (mode == DG_GL_READ_STENCIL ? DG_GL_RESULT_INT : DG_GL_RESULT_FLOAT));
        for (i = 0; i < pixels; ++i) {
            if (mode == DG_GL_READ_STENCIL)
                rgba[i] = FixedPixel ? 0xed : ((x + i % (args[2] & 65535)) & 1);
            else if (mode == DG_GL_READ_DEPTH)
                ((float *)out)[i] = .5f;
            else {
                assert(wide);
                memcpy((float *)out + i * 4, FloatPixel, sizeof(FloatPixel));
            }
        }
        *bytes = ShortReply ? capacity - 4 : capacity;
        ++QueryCalls;
        return TRUE;
    }
    if (fn == FEnum_glGetTexImage) {
        assert(capacity == 512);
        for (i = 0; i < 128; ++i)
            rgba[i] = ReadPixel(args[2] + i);
        *bytes = ShortReply ? 508 : 512;
        ++QueryCalls;
        return TRUE;
    }
    assert(fn == FEnum_glReadPixels && type == DG_GL_RESULT_INT && args[2] >> 16 == 1);
    assert(count && count <= 128 && capacity == count * 4);
    ++QueryCalls;
    for (i = 0; i < count; ++i)
        rgba[i] = FixedPixel ? ReadPixel(0)
                             : ((args[0] + i) & 255) | ((args[1] & 255) << 8) | 0xaa550000UL;
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
    /* Wine's RGB565 texture download must survive every representable pixel,
     * chunk boundaries, row padding/skips, and byte swapping. */
    {
        static BYTE packed[257 * 520 + 16];
        ULONG swapped, j;
        TextureWidth = TextureHeight = 256;
        PackedOracle = TRUE;
        for (swapped = 0; swapped < 2; ++swapped) {
            memset(packed, 0xcc, sizeof(packed));
            Pack = (JGL_UNPACK){8, 257, 1, 1, (GLint)swapped, 0};
            Error = 0;
            Calls = QueryCalls;
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, 0x8363, packed + 1);
            assert(!Error && QueryCalls == Calls + 514);
            for (j = 0; j < sizeof(packed); ++j) {
                BYTE expected = 0xcc;
                if (j >= 523) {
                    ULONG relative = j - 523, y = relative / 520, x = relative % 520;
                    if (y < 256 && x < 512) {
                        ULONG word = y * 256 + x / 2;
                        expected = (BYTE)(word >> (((x % 2) ^ swapped) * 8));
                    }
                }
                assert(packed[j] == expected);
            }
        }
        PackedOracle = FALSE;
    }
    /* Independent known words cover every accepted 16-bit type and component
     * order through both public read APIs, including unaligned destinations. */
    {
        static const struct {
            GLenum format, type;
            ULONG word;
        } cases[] = {
            {GL_RGB, 0x8363, 0x11aa},  {GL_RGBA, 0x8033, 0x1357}, {0x80e1, 0x8033, 0x5317},
            {GL_RGBA, 0x8034, 0x1194}, {0x80e1, 0x8034, 0x5184},  {GL_RGBA, 0x8365, 0x7531},
            {0x80e1, 0x8365, 0x7135},  {GL_RGBA, 0x8366, 0x28c2}, {0x80e1, 0x8366, 0x08ca},
        };
        ULONG c, swapped, api;
        TextureWidth = TextureHeight = 1;
        FixedPixel = TRUE;
        for (c = 0; c < sizeof(cases) / sizeof(cases[0]); ++c)
            for (swapped = 0; swapped < 2; ++swapped)
                for (api = 0; api < 2; ++api) {
                    Pack = (JGL_UNPACK){4, 0, 0, 0, (GLint)swapped, 0};
                    memset(image, 0xcc, sizeof(image));
                    Error = 0;
                    if (api)
                        glGetTexImage(GL_TEXTURE_2D, 0, cases[c].format, cases[c].type, image + 1);
                    else
                        glReadPixels(0, 0, 1, 1, cases[c].format, cases[c].type, image + 1);
                    assert(!Error && image[0] == 0xcc && image[3] == 0xcc);
                    assert(image[1] == (BYTE)(cases[c].word >> (swapped * 8)));
                    assert(image[2] == (BYTE)(cases[c].word >> ((1 - swapped) * 8)));
                }
        FixedPixel = FALSE;
        /* RGB565 framebuffer tiling uses two-byte destination strides. */
        Pack = (JGL_UNPACK){8, 137, 1, 2, 0, 0};
        memset(image, 0xcc, sizeof(image));
        Calls = QueryCalls;
        glReadPixels(10, 20, 130, 2, GL_RGB, 0x8363, image + 1);
        assert(!Error && QueryCalls == Calls + 4);
        for (i = 0; i < sizeof(image); ++i) {
            BYTE expected = 0xcc;
            if (i >= 285) {
                ULONG relative = i - 285, y = relative / 280, x = relative % 280;
                if (y < 2 && x < 260) {
                    ULONG word = (((x / 2 + 10) / 8) * 2048) + (((y + 20) / 4) * 32) + 10;
                    expected = (BYTE)(word >> ((x % 2) * 8));
                }
            }
            assert(image[i] == expected);
        }
        Pack = (JGL_UNPACK){4, 0, 0, 0, 0, 0};
        for (api = 0; api < 2; ++api) {
            memset(image, 0xcc, sizeof(image));
            memcpy(before, image, sizeof(image));
            ShortReply = 1;
            Error = 0;
            if (api)
                glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, 0x8363, image + 1);
            else
                glReadPixels(0, 0, 1, 1, GL_RGB, 0x8363, image + 1);
            assert(Error == GL_INVALID_OPERATION && !memcmp(image, before, sizeof(image)));
            ShortReply = 0;
            Error = 0;
            Calls = QueryCalls;
            if (api)
                glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, 0x8363, image);
            else
                glReadPixels(0, 0, 1, 1, GL_RGBA, 0x8363, image);
            assert(Error == GL_INVALID_OPERATION && Calls == QueryCalls);
            assert(!memcmp(image, before, sizeof(image)));
        }
    }
    /* New formats must never send unnegotiated wire modes to old drivers. */
    {
        static const GLenum types[] = {0x1400, 0x1402, 0x1403, 0x1404,
                                       0x1405, 0x1406, 0x8036, 0x8368};
        ULONG k;
        Pack = (JGL_UNPACK){4, 0, 0, 0, 0, 0};
        for (k = 0; k < sizeof(types) / sizeof(types[0]); ++k) {
            Calls = QueryCalls;
            Error = 0;
            glReadPixels(0, 0, 1, 1, GL_RGBA, types[k], image);
            assert(Error == GL_INVALID_OPERATION && QueryCalls == Calls);
            Error = 0;
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, types[k], image);
            assert(Error == GL_INVALID_OPERATION && QueryCalls == Calls);
        }
        for (k = 0x1901; k <= 0x1902; ++k) {
            Error = 0;
            Calls = QueryCalls;
            glReadPixels(0, 0, 1, 1, k, GL_UNSIGNED_BYTE, image);
            assert(Error == GL_INVALID_OPERATION && Calls == QueryCalls);
        }
    }
    ExtendedReadback = TRUE;
    FixedPixel = TRUE;
    Error = 0;
    TextureWidth = TextureHeight = 1;
    /* Exact non-byte-representable values prove wide transport, while fixed
     * signed results follow GL1.1's conversion table rather than unsigned masks. */
    {
        static const struct {
            GLenum type;
            ULONG bytes, red, green, blue, alpha;
        } cases[] = {
            {0x1400, 1, 31, 63, 95, 127},
            {0x1402, 2, 8191, 16383, 24575, 32767},
            {0x1403, 2, 16384, 32768, 49151, 65535},
            {0x1404, 4, 536870911, 1073741823, 1610612735, 2147483647},
            {0x1405, 4, 1073741824, 2147483648u, 3221225471u, 4294967295u},
            {0x1406, 4, 0x3e800000, 0x3f000000, 0x3f400000, 0x3f800000},
            {0x8036, 4, 0x40200c03, 0, 0, 0},
            {0x8368, 4, 0xf0080100, 0, 0, 0},
        };
        ULONG k, swapped, api, c;
        for (k = 0; k < sizeof(cases) / sizeof(cases[0]); ++k)
            for (swapped = 0; swapped < 2; ++swapped)
                for (api = 0; api < 2; ++api) {
                    ULONG components = cases[k].type >= 0x8000 ? 1 : 4;
                    ULONG expected[] = {cases[k].red, cases[k].green, cases[k].blue,
                                        cases[k].alpha};
                    Pack = (JGL_UNPACK){4, 0, 0, 0, (GLint)swapped, 0};
                    memset(image, 0xcc, sizeof(image));
                    if (api)
                        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, cases[k].type, image + 1);
                    else
                        glReadPixels(0, 0, 1, 1, GL_RGBA, cases[k].type, image + 1);
                    assert(!Error && image[0] == 0xcc &&
                           image[1 + components * cases[k].bytes] == 0xcc);
                    for (c = 0; c < components * cases[k].bytes; ++c) {
                        ULONG offset = c % cases[k].bytes;
                        if (swapped)
                            offset = cases[k].bytes - 1 - offset;
                        assert(image[c + 1] ==
                               (BYTE)(expected[c / cases[k].bytes] >> (offset * 8)));
                    }
                }
        /* Float transport has four words per pixel, including chunk starts
         * beyond the first tile and destination row padding. */
        {
            static BYTE tiled[3 * 536 + 8];
            ULONG j, api;
            TextureWidth = 33;
            TextureHeight = 2;
            for (api = 0; api < 2; ++api) {
                Pack = (JGL_UNPACK){8, 33, 1, 0, 0, 0};
                memset(tiled, 0xcc, sizeof(tiled));
                if (api)
                    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, 0x1406, tiled + 1);
                else
                    glReadPixels(0, 0, 33, 2, GL_RGBA, 0x1406, tiled + 1);
                assert(!Error);
                for (j = 0; j < sizeof(tiled); ++j) {
                    BYTE expected = 0xcc;
                    if (j >= 529 && j < 1585) {
                        ULONG word = ((j - 529) % 16) / 4;
                        static const ULONG bits[] = {0x3e800000, 0x3f000000, 0x3f400000,
                                                     0x3f800000};
                        expected = (BYTE)(bits[word] >> (((j - 529) % 4) * 8));
                    }
                    assert(tiled[j] == expected);
                }
            }
            TextureWidth = TextureHeight = 1;
        }
        /* Non-8bit 1D texture data remains exact even with 16bit scalar output. */
        Pack = (JGL_UNPACK){4, 0, 0, 0, 0, 0};
        FloatPixel[0] = 12345 / 65535.0f;
        FloatPixel[1] = 23456 / 65535.0f;
        FloatPixel[2] = 34567 / 65535.0f;
        FloatPixel[3] = 45678 / 65535.0f;
        Pack.SwapBytes = 0;
        glGetTexImage(GL_TEXTURE_1D, 0, GL_RGBA, 0x1403, image + 1);
        assert(!Error);
        assert((image[1] | (image[2] << 8)) == 12345 && (image[3] | (image[4] << 8)) == 23456);
        assert((image[5] | (image[6] << 8)) == 34567 && (image[7] | (image[8] << 8)) == 45678);
        FloatPixel[0] = .25f;
        FloatPixel[1] = .5f;
        FloatPixel[2] = .75f;
        FloatPixel[3] = 1;
        /* Component extraction and the distinct framebuffer/texture luminance rules. */
        for (api = 0; api < 2; ++api) {
            static const GLenum formats[] = {
                0x1903, 0x1904, 0x1905, GL_ALPHA, GL_LUMINANCE, GL_LUMINANCE_ALPHA, 0x80e0, 0x80e1};
            for (k = 0; k < sizeof(formats) / sizeof(formats[0]); ++k) {
                float out[4] = {-9, -9, -9, -9};
                if (api)
                    glGetTexImage(GL_TEXTURE_2D, 0, formats[k], 0x1406, out);
                else
                    glReadPixels(0, 0, 1, 1, formats[k], 0x1406, out);
                assert(!Error);
                assert(out[0] == (k == 0   ? .25f
                                  : k == 1 ? .5f
                                  : k == 2 ? .75f
                                  : k == 3 ? 1.0f
                                  : k < 6  ? (api ? .25f : 1.0f)
                                           : .75f));
                if (k == 5)
                    assert(out[1] == 1.0f && out[2] == -9);
                if (k >= 6)
                    assert(out[1] == .5f && out[2] == .25f && out[3] == (k == 7 ? 1.0f : -9.0f));
            }
        }
        /* Additional packed types preserve exact native field order. */
        {
            static const struct {
                GLenum type;
                ULONG word, bytes;
            } packed[] = {{0x8032, 0x05, 1},
                          {0x8362, 0x48, 1},
                          {0x8364, 0x51a2, 2},
                          {0x8035, 0x12345678, 4},
                          {0x8367, 0x78563412, 4}};
            for (k = 0; k < sizeof(packed) / sizeof(packed[0]); ++k) {
                glReadPixels(0, 0, 1, 1, k < 3 ? GL_RGB : GL_RGBA, packed[k].type, image + 1);
                assert(!Error);
                for (c = 0; c < packed[k].bytes; ++c)
                    assert(image[c + 1] == (BYTE)(packed[k].word >> (c * 8)));
            }
        }
        /* Depth converts normalized values; stencil uses integer masks. */
        {
            static const struct {
                GLenum type;
                ULONG depth, stencil, bytes;
            } scalar[] = {{0x1400, 63, 0x6d, 1},
                          {GL_UNSIGNED_BYTE, 128, 0xed, 1},
                          {0x1402, 16383, 0xed, 2},
                          {0x1403, 32768, 0xed, 2},
                          {0x1404, 1073741823, 0xed, 4},
                          {0x1405, 2147483648u, 0xed, 4},
                          {0x1406, 0x3f000000, 0x436d0000, 4}};
            for (k = 0; k < sizeof(scalar) / sizeof(scalar[0]); ++k)
                for (api = 0; api < 2; ++api) {
                    ULONG expected = api ? scalar[k].depth : scalar[k].stencil;
                    glReadPixels(0, 0, 1, 1, api ? 0x1902 : 0x1901, scalar[k].type, image + 1);
                    assert(!Error);
                    for (c = 0; c < scalar[k].bytes; ++c)
                        assert(image[c + 1] == (BYTE)(expected >> (c * 8)));
                }
        }
        /* Bit-packed stencil retains neighboring bits, row padding and skip
         * pixels across a transport boundary, in either bit order. */
        FixedPixel = FALSE;
        for (swapped = 0; swapped < 2; ++swapped) {
            Pack = (JGL_UNPACK){4, 141, 1, 3, 1, (GLint)swapped};
            memset(image, 0xa5, sizeof(image));
            glReadPixels(1, 0, 130, 2, 0x1901, 0x1a00, image);
            assert(!Error);
            for (c = 0; c < sizeof(image) * 8; ++c) {
                ULONG byte = c / 8, bit = c % 8, expected = (0xa5 >> bit) & 1;
                if (byte >= 20 && byte < 60) {
                    ULONG y = (byte - 20) / 20,
                          x = ((byte - 20) % 20) * 8 + (swapped ? bit : 7 - bit);
                    if (y < 2 && x >= 3 && x < 133)
                        expected = (x - 3 + 1) & 1;
                }
                assert(((image[byte] >> bit) & 1) == expected);
            }
        }
        Error = 0;
        Calls = QueryCalls;
        glReadPixels(0, 0, 1, 1, 0x1900, GL_UNSIGNED_BYTE, image);
        assert(Error == GL_INVALID_OPERATION && Calls == QueryCalls);
        Error = 0;
        /* Malformed wide replies do not mutate output. */
        Pack = (JGL_UNPACK){4, 0, 0, 0, 0, 0};
        ShortReply = 1;
        memcpy(before, image, sizeof(image));
        Error = 0;
        glReadPixels(0, 0, 1, 1, GL_RGBA, 0x1406, image);
        assert(Error == GL_INVALID_OPERATION && !memcmp(image, before, sizeof(image)));
        Error = 0;
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, 0x1406, image);
        assert(Error == GL_INVALID_OPERATION && !memcmp(image, before, sizeof(image)));
        ShortReply = 0;
        Error = 0;
    }
    puts("PASS legacy frontend: scalar normalization, immutable vectors/deletes, bounded reads, "
         "pack stride/skips, all RGB565 texels, wide color, depth/stencil, bitmap and malformed "
         "results");
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
