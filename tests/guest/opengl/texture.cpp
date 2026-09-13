/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    ULONG Function, Args[8], Bytes;
    unsigned char *Pixels;
} TILE;
static TILE Tiles[1024];
static ULONG Count, Maximum, FailAfter;
static GLenum Error;
static BOOL Ready;
static JGL_UNPACK Unpack;
static JGL_UNPACK Pack;
static GLint NativeWidth, NativeBorder;
static ULONG Queries, QueryBytes;
static BOOL QueryFail, Compiling;

BOOL JglCompiling(void) {
    return Compiling;
}
BOOL JglQuery(ULONG function, const ULONG *args, ULONG kind, void *out, ULONG capacity,
              ULONG *bytes) {
    assert(function == FEnum_glGetTexLevelParameteriv && args[0] == GL_TEXTURE_1D &&
           kind == DG_GL_RESULT_INT && capacity == 4);
    ++Queries;
    if (QueryFail) {
        JglSetError(GL_INVALID_OPERATION);
        return FALSE;
    }
    GLint value = args[2] == 0x1000 ? NativeWidth : NativeBorder;
    memcpy(out, &value, 4);
    *bytes = QueryBytes;
    return TRUE;
}

BOOL JglReady(void) {
    return Ready;
}
void JglSetError(GLenum error) {
    if (!Error)
        Error = error;
}
ULONG JglMaxDataBytes(ULONG args) {
    assert(args == 8);
    return Maximum;
}
JGL_UNPACK *JglUnpack(void) {
    return &Unpack;
}
JGL_UNPACK *JglPack(void) {
    return &Pack;
}
BOOL JglData(ULONG function, const ULONG *args, ULONG words, const void *data, ULONG bytes) {
    TILE *tile;
    assert(words == 8 && Count < 1024 && bytes <= Maximum);
    assert((bytes + 3u) / 4u * 4u <= Maximum);
    if (Count == FailAfter) {
        JglSetError(GL_INVALID_OPERATION);
        return FALSE;
    }
    tile = &Tiles[Count++];
    tile->Function = function;
    tile->Bytes = bytes;
    memcpy(tile->Args, args, sizeof(tile->Args));
    tile->Pixels = (unsigned char *)malloc(bytes ? bytes : 1);
    assert(tile->Pixels);
    if (bytes)
        memcpy(tile->Pixels, data, bytes);
    return TRUE;
}
static void Reset(ULONG maximum) {
    ULONG i;
    for (i = 0; i < Count; ++i)
        free(Tiles[i].Pixels);
    Count = 0;
    Maximum = maximum;
    FailAfter = (ULONG)-1;
    Error = 0;
    Ready = TRUE;
    NativeWidth = DG_GL_MAX_TEXTURE_DIMENSION;
    NativeBorder = 0;
    Queries = 0;
    QueryBytes = 4;
    QueryFail = Compiling = FALSE;
    Unpack = (JGL_UNPACK){4, 0, 0, 0, 0, 0};
    Pack = (JGL_UNPACK){4, 0, 0, 0, 0, 0};
}
static void Image(GLsizei w, GLsizei h, GLenum format, const void *pixels) {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, format, GL_UNSIGNED_BYTE, pixels);
}
static void AssertError(GLenum error) {
    assert(Error == error && Count == 0);
}

static void PackedTextures(void) {
    const struct {
        GLenum Format, Type;
        ULONG Bytes;
        unsigned short Word[4];
        unsigned char RGBA[16];
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
    ULONG f, i, x, y;
    for (f = 0; f < sizeof(cases) / sizeof(cases[0]); ++f) {
        unsigned char source[128], saved[128], actual[40], expected[40];
        ULONG bytes = cases[f].Bytes, stride = (7 * bytes + 7) & ~7u;
        ULONG wireBytes = bytes == 2 ? 2 : 4;
        memset(source, 0xa5, sizeof(source));
        for (y = 0; y < 2; ++y)
            for (x = 0; x < 5; ++x) {
                ULONG offset = 1 + (y + 1) * stride + (x + 1) * bytes, pixel = (y * 5 + x) % 4;
                source[offset] = cases[f].Word[pixel] & 255;
                if (bytes == 2)
                    source[offset + 1] = cases[f].Word[pixel] >> 8;
                if (bytes == 2) {
                    expected[(y * 5 + x) * 2] = cases[f].Word[pixel] & 255;
                    expected[(y * 5 + x) * 2 + 1] = cases[f].Word[pixel] >> 8;
                } else {
                    memcpy(expected + (y * 5 + x) * 4, cases[f].RGBA + pixel * 4, 4);
                }
            }
        memcpy(saved, source, sizeof(source));
        Reset(8);
        Unpack = (JGL_UNPACK){8, 7, 1, 1, 0, 0};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 5, 2, 0, cases[f].Format, cases[f].Type,
                     source + 1);
        assert(!Error && Count == (bytes == 2 ? 5 : 7) && !Tiles[0].Bytes);
        assert(Tiles[0].Args[6] == (bytes == 2 ? cases[f].Format : GL_RGBA) &&
               Tiles[0].Args[7] == (bytes == 2 ? cases[f].Type : GL_UNSIGNED_BYTE));
        memset(actual, 0, sizeof(actual));
        for (i = 1; i < Count; ++i) {
            TILE *t = &Tiles[i];
            assert(t->Args[5] == 1 && t->Args[6] == (bytes == 2 ? cases[f].Format : GL_RGBA) &&
                   t->Args[7] == (bytes == 2 ? cases[f].Type : GL_UNSIGNED_BYTE));
            memcpy(actual + (t->Args[3] * 5 + t->Args[2]) * wireBytes, t->Pixels, t->Bytes);
        }
        assert(!memcmp(actual, expected, 10 * wireBytes));
        assert(!memcmp(source, saved, sizeof(source))); /* Input and padding are read-only. */
        memset(source, 0, sizeof(source));
        assert(!memcmp(Tiles[1].Pixels, expected, 8)); /* Immutable converted bytes. */

        Reset(64);
        Unpack = (JGL_UNPACK){8, 7, 1, 1, 0, 0};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 5, 2, 0, cases[f].Format, cases[f].Type, saved + 1);
        if (bytes == 2) {
            assert(!Error && Count == 3 && Tiles[1].Bytes == 10 && Tiles[2].Bytes == 10);
            assert(!memcmp(Tiles[1].Pixels, expected, 10));
            assert(!memcmp(Tiles[2].Pixels, expected + 10, 10));
        } else {
            assert(!Error && Count == 2 && Tiles[1].Args[5] == 2 && Tiles[1].Bytes == 40);
            assert(!memcmp(Tiles[1].Pixels, expected, 40));
        }

        Reset(8);
        Unpack = (JGL_UNPACK){8, 7, 123, 1, 0, 0};
        glTexSubImage1D(GL_TEXTURE_1D, 0, 4, 5, cases[f].Format, cases[f].Type, saved + 1 + stride);
        assert(!Error && Count == (bytes == 2 ? 2 : 3) && Tiles[0].Args[2] == 4 &&
               Tiles[Count - 1].Args[2] == 8);
        assert(!memcmp(Tiles[0].Pixels, expected, 8));
        assert(!memcmp(Tiles[Count - 1].Pixels, expected + 4 * wireBytes, wireBytes));
    }
    {
        const unsigned char source[] = {99, 1, 2, 3, 4, 5, 6, 7, 8};
        const GLint formats[] = {0x2a10, 0x8043, 0x804f, 0x8050, 0x8056, 0x8057};
        for (i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i) {
            Reset(64);
            glTexImage2D(GL_TEXTURE_2D, 0, formats[i], 2, 1, 0, 0x80e1, 0x8367, source + 1);
            assert(!Error && Count == 1 && Tiles[0].Bytes == 8 &&
                   Tiles[0].Args[2] == (ULONG)formats[i]);
            assert(Tiles[0].Args[6] == 0x80e1 && Tiles[0].Args[7] == GL_UNSIGNED_BYTE);
            assert(
                !memcmp(Tiles[0].Pixels, source + 1, 8)); /* BGRA packed32 keeps the direct path. */
        }
        Reset(64);
        glTexSubImage1D(GL_TEXTURE_1D, 0, 0, 2, GL_RGBA, 0x8367, source + 1);
        assert(!Error && Count == 1 && !memcmp(Tiles[0].Pixels, source + 1, 8));
        Reset(64);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 2, 1, 0, GL_RGB, 0x8367, source + 1);
        AssertError(GL_INVALID_OPERATION);
    }
    {
        // The measured Glide bottleneck: one immutable packed128x128 packet,
        // instead of eight expanded RGBA packets through the conversion scratch.
        unsigned char source[128 * 128 * 2];
        for (i = 0; i < sizeof(source); ++i)
            source[i] = (unsigned char)i;
        Reset(65504);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 128, 128, GL_RGB, 0x8363, source);
        assert(!Error && Count == 1 && Tiles[0].Bytes == sizeof(source));
        assert(Tiles[0].Args[6] == GL_RGB && Tiles[0].Args[7] == 0x8363);
        assert(!memcmp(Tiles[0].Pixels, source, sizeof(source)));
        Reset(64);
        FailAfter = 1;
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 128, 128, GL_RGB, 0x8363, source);
        assert(Error == GL_INVALID_OPERATION && Count == 1);
    }

    Reset(64);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, 0x8363, NULL);
    AssertError(GL_INVALID_OPERATION);
    Reset(64);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGB, 0x8033, NULL);
    AssertError(GL_INVALID_OPERATION);
    Reset(3);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 1, 1, 0, GL_RGB, 0x8363, "xx");
    AssertError(GL_OUT_OF_MEMORY);
    Reset(0);
    Unpack.SkipRows = INT32_MAX;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 2048, 2048, 0, GL_RGB, 0x8363, NULL);
    assert(!Error && Count == 1 && !Tiles[0].Bytes && Tiles[0].Args[7] == 0x8363);
}

int main(void) {
    unsigned char pixels[1024], framebuffer[128];
    ULONG i, x, y;
    for (i = 0; i < sizeof(pixels); ++i)
        pixels[i] = (unsigned char)i;

    /* Byte order belongs to the client context; UBYTE never swaps. */
    Reset(64);
    glPixelStorei(GL_UNPACK_SWAP_BYTES, -7);
    glPixelStorei(GL_PACK_LSB_FIRST, 9);
    assert(!Error && Unpack.SwapBytes == 1 && !Pack.SwapBytes && Pack.LsbFirst == 1 &&
           !Unpack.LsbFirst && !Count);
    {
        const unsigned char rgb565[] = {0xf8, 0x00, 0x07, 0xe0};
        const unsigned char words[] = {0x00, 0xf8, 0xe0, 0x07};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 2, 1, 0, GL_RGB, 0x8363, rgb565);
        assert(!Error && Count == 2 && Tiles[1].Bytes == 4 &&
               Tiles[1].Args[6] == GL_RGB && Tiles[1].Args[7] == 0x8363 &&
               !memcmp(Tiles[1].Pixels, words, 4));
    }
    {
        const unsigned char source[] = {4, 3, 2, 1, 8, 7, 6, 5};
        const unsigned char rgba[] = {1, 2, 3, 4, 5, 6, 7, 8};
        const unsigned char bgra[] = {3, 2, 1, 4, 7, 6, 5, 8};
        Reset(64);
        glPixelStorei(GL_UNPACK_SWAP_BYTES, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 2, 1, GL_RGBA, 0x8367, source);
        assert(!Error && Count == 1 && !memcmp(Tiles[0].Pixels, rgba, 8));
        Reset(64);
        glPixelStorei(GL_UNPACK_SWAP_BYTES, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 2, 1, 0x80e1, 0x8367, source);
        assert(!Error && Count == 1 && !memcmp(Tiles[0].Pixels, bgra, 8));
        Reset(64);
        glPixelStorei(GL_UNPACK_SWAP_BYTES, 1);
        Image(2, 1, GL_RGBA, source);
        assert(!Error && Count == 1 && !memcmp(Tiles[0].Pixels, source, 8));
    }
    Reset(64);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 3);
    AssertError(GL_INVALID_VALUE);
    assert(Unpack.Alignment == 4);
    Reset(64);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, -1);
    AssertError(GL_INVALID_VALUE);
    Reset(64);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    assert(!Error && !Count && Pack.Alignment == 1 && Unpack.Alignment == 4);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 2);
    assert(Pack.SkipPixels == 2 && !Unpack.SkipPixels);
    Reset(64);
    glPixelStorei(0xdead, 1);
    AssertError(GL_INVALID_ENUM);
    Reset(64);
    Ready = FALSE;
    Image(1, 1, GL_RGBA, pixels);
    assert(!Count && !Error);

    Reset(64);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 8);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 5);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 1);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 1);
    Image(3, 2, GL_RGB, pixels);
    assert(!Error && Count == 3 && Tiles[0].Function == FEnum_glTexImage2D && !Tiles[0].Bytes);
    assert(Tiles[1].Bytes == 9 && Tiles[2].Bytes == 9);
    assert(!memcmp(Tiles[1].Pixels, pixels + 19, 9));
    assert(!memcmp(Tiles[2].Pixels, pixels + 35, 9));
    assert(Tiles[1].Args[3] == 0 && Tiles[2].Args[3] == 1);

    Reset(48);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 2);
    Image(4, 5, 0x80e1, pixels);
    assert(!Error && Count == 3 && Tiles[1].Bytes == 48 && Tiles[2].Bytes == 32);
    assert(Tiles[1].Args[5] == 3 && Tiles[2].Args[3] == 3 && Tiles[2].Args[5] == 2);
    assert(!memcmp(Tiles[1].Pixels, pixels + 32, 48));
    assert(!memcmp(Tiles[2].Pixels, pixels + 80, 32));

    Reset(8);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 7, 11, 5, 2, GL_RGB, GL_UNSIGNED_BYTE, pixels);
    assert(!Error && Count == 6);
    memset(framebuffer, 0xff, sizeof(framebuffer));
    for (i = 0; i < Count; ++i) {
        TILE *t = &Tiles[i];
        assert(t->Function == FEnum_glTexSubImage2D && t->Args[5] == 1);
        x = t->Args[2] - 7;
        y = t->Args[3] - 11;
        memcpy(framebuffer + y * 15 + x * 3, t->Pixels, t->Bytes);
    }
    assert(!memcmp(framebuffer, pixels, 30));

    {
        const struct {
            GLenum Format;
            ULONG Components;
        } formats[] = {
            {GL_ALPHA, 1}, {GL_LUMINANCE, 1}, {GL_LUMINANCE_ALPHA, 2}, {GL_RGB, 3}, {0x80e0, 3},
            {GL_RGBA, 4},  {0x80e1, 4},
        };
        ULONG f;
        for (f = 0; f < sizeof(formats) / sizeof(formats[0]); ++f) {
            ULONG components = formats[f].Components;
            Reset(20);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glPixelStorei(GL_UNPACK_SKIP_PIXELS, 1);
            Image(7, 3, formats[f].Format, pixels);
            assert(!Error && Count > 1 && !Tiles[0].Bytes);
            memset(framebuffer, 0xff, sizeof(framebuffer));
            for (i = 1; i < Count; ++i) {
                TILE *t = &Tiles[i];
                assert(t->Args[6] == formats[f].Format);
                x = t->Args[2];
                y = t->Args[3];
                memcpy(framebuffer + (y * 7 + x) * components, t->Pixels, t->Bytes);
            }
            assert(!memcmp(framebuffer, pixels + components, 21 * components));
        }
    }

    Reset(64);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 1); /* Short row length may overlap input rows. */
    Image(3, 2, GL_RGB, pixels);
    assert(!Error && Count == 3);
    assert(!memcmp(Tiles[1].Pixels, pixels, 9));
    assert(!memcmp(Tiles[2].Pixels, pixels + 4, 9));

    Reset(64);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    Image(3, 2, GL_RGB, pixels); /* Packed small image needs only one copied record. */
    assert(!Error && Count == 1 && Tiles[0].Bytes == 18 && Tiles[0].Function == FEnum_glTexImage2D);
    assert(!memcmp(Tiles[0].Pixels, pixels, 18));
    pixels[0] ^= 255;
    assert(Tiles[0].Pixels[0] != pixels[0]); /* No retained application pointer. */
    pixels[0] ^= 255;

    Reset(0);
    Unpack.SkipRows = Unpack.SkipPixels = Unpack.RowLength = INT32_MAX;
    Image(2048, 2048, GL_RGB, NULL); /* NULL allocation ignores source unpack arithmetic. */
    assert(!Error && Count == 1 && !Tiles[0].Bytes);
    Reset(64);
    Image(0, 1, GL_RGBA, NULL);
    AssertError(GL_INVALID_VALUE);
    Reset(64);
    Image(2049, 1, GL_RGBA, NULL);
    AssertError(GL_INVALID_VALUE);
    Reset(64);
    Image(1, 1, 0x1902, pixels);
    AssertError(GL_INVALID_ENUM);
    Reset(64);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    AssertError(GL_INVALID_VALUE);
    Reset(64);
    glTexImage2D(GL_TEXTURE_2D, 12, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    AssertError(GL_INVALID_VALUE);
    Reset(64);
    glTexImage2D(GL_TEXTURE_2D, 1, GL_RGBA, 2048, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    AssertError(GL_INVALID_VALUE);
    Reset(64);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, 0x1403, pixels);
    AssertError(GL_INVALID_ENUM);
    Reset(64);
    glTexImage2D(GL_TEXTURE_2D, 0, 0x1234, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    AssertError(GL_INVALID_VALUE);

    Reset(64);
    Image(4, 1, GL_RGBA, (const void *)(UINTPTR_MAX - 2));
    AssertError(GL_INVALID_VALUE);
    Reset(64);
    Unpack.Alignment = 8;
    Unpack.RowLength = Unpack.SkipRows = Unpack.SkipPixels = INT32_MAX;
    Image(4, 1, GL_RGBA, pixels);
    AssertError(GL_INVALID_VALUE);
    Reset(3);
    Image(1, 1, GL_RGBA, pixels);
    AssertError(GL_OUT_OF_MEMORY);
    Reset(8);
    FailAfter = 2;
    Image(4, 2, GL_RGBA, pixels);
    assert(Count == 2 && Error == GL_INVALID_OPERATION); /* Never replay a partial batch. */
    Reset(64);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 1, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    assert(!Count && !Error);
    Reset(64);
    glTexSubImage2D(GL_TEXTURE_2D, 0, -1, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    AssertError(GL_INVALID_VALUE);
    Reset(64);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 2047, 0, 2, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    AssertError(GL_INVALID_VALUE);
    Reset(64);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    AssertError(GL_INVALID_VALUE);
    Reset(64);
    Unpack.SkipRows = 999;
    Unpack.SkipPixels = 2;
    Unpack.RowLength = 999;
    glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA, 20, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    assert(!Error && Count == 3 && Tiles[0].Function == FEnum_glTexImage1D && !Tiles[0].Bytes);
    assert(Tiles[1].Function == FEnum_glTexSubImage1D && Tiles[1].Args[3] == 0 &&
           Tiles[1].Args[5] == 1);
    assert(Tiles[1].Bytes == 64 && Tiles[2].Bytes == 16);
    assert(!memcmp(Tiles[1].Pixels, pixels + 8, 64) && !memcmp(Tiles[2].Pixels, pixels + 72, 16));
    Reset(64);
    glTexImage1D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    AssertError(GL_INVALID_ENUM);
    Reset(64);
    glTexSubImage1D(GL_TEXTURE_1D, 0, 2047, 2, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    AssertError(GL_INVALID_VALUE);
    Reset(8);
    glTexImage1D(GL_TEXTURE_1D, 0, 0x805b /* RGBA16 */, 6, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    assert(!Error && Count == 4 && Tiles[0].Args[5] == 1 && !Tiles[0].Bytes);
    for (ULONG i = 1; i < 4; ++i) {
        assert(Tiles[i].Function == FEnum_glTexSubImage1D && Tiles[i].Bytes == 8 &&
               Tiles[i].Args[2] == (ULONG)(-1 + (GLint)(i - 1) * 2));
        assert(!memcmp(Tiles[i].Pixels, pixels + (i - 1) * 8, 8));
    }
    Reset(64);
    glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA, 6, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    assert(!Error && Count == 1 && Tiles[0].Bytes == 24 && Tiles[0].Args[5] == 1);
    Reset(64);
    glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA, 6, 1, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    assert(!Error && Count == 1 && !Tiles[0].Bytes && Tiles[0].Args[5] == 1);
    Reset(8);
    FailAfter = 2;
    glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA, 6, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    assert(Error == GL_INVALID_OPERATION && Count == 2); // no replay after partial upload
    Reset(64);
    NativeWidth = 6;
    NativeBorder = 1;
    glTexSubImage1D(GL_TEXTURE_1D, 0, -1, 6, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    assert(!Error && Queries == 2 && Count == 1 && Tiles[0].Args[2] == (ULONG)-1);
    Reset(64);
    glTexSubImage1D(GL_TEXTURE_1D, 0, -1, 1, GL_RGBA, GL_UNSIGNED_BYTE, (const void *)1);
    AssertError(GL_INVALID_VALUE); // queried border zero, no caller read
    Reset(4);
    NativeWidth = 6;
    NativeBorder = 1;
    glTexSubImage1D(GL_TEXTURE_1D, 0, 4, 2, GL_RGBA, GL_UNSIGNED_BYTE, (const void *)1);
    AssertError(GL_INVALID_VALUE);
    Reset(64);
    QueryBytes = 2;
    glTexSubImage1D(GL_TEXTURE_1D, 0, -1, 1, GL_RGBA, GL_UNSIGNED_BYTE, (const void *)1);
    AssertError(GL_INVALID_OPERATION);
    assert(Queries == 1);
    Reset(64);
    QueryFail = TRUE;
    glTexSubImage1D(GL_TEXTURE_1D, 0, -1, 1, GL_RGBA, GL_UNSIGNED_BYTE, (const void *)1);
    AssertError(GL_INVALID_OPERATION);
    Reset(64);
    glTexSubImage1D(GL_TEXTURE_1D, 0, 0, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    assert(!Error && !Queries && Count == 1); // ordinary single packet stays asynchronous
    Reset(64);
    Compiling = TRUE;
    glTexSubImage1D(GL_TEXTURE_1D, 0, -1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    assert(!Error && !Queries && Count == 1 && Tiles[0].Args[2] == (ULONG)-1);
    Reset(4);
    Compiling = TRUE;
    glTexSubImage1D(GL_TEXTURE_1D, 0, -1, 6, GL_RGBA, GL_UNSIGNED_BYTE, (const void *)1);
    AssertError(GL_OUT_OF_MEMORY);
    assert(!Queries); // no caller read, query or partial list record
    Reset(8200);
    Compiling = TRUE;
    unsigned char full_border_packed[(DG_GL_MAX_TEXTURE_DIMENSION + 2) * 2] = {};
    glTexSubImage1D(GL_TEXTURE_1D, 0, -1, DG_GL_MAX_TEXTURE_DIMENSION + 2, GL_RGBA,
                    0x8033 /* RGBA4444 */, full_border_packed);
    assert(!Error && !Queries && Count == 1 && Tiles[0].Bytes == 4100);
    for (ULONG i = 0; i < Tiles[0].Bytes; ++i)
        assert(Tiles[0].Pixels[i] == 0);
    Reset(64);
    PackedTextures();
    Reset(64);
    puts("Packed RGB332/565, RGBA4444/5551, BGRA4444REV/1555REV pixel/padding oracles passed");
    puts("Texture unpack/tiling tests passed, including1D skip-pixels-only semantics and bounded "
         "chunks");
    return 0;
}
