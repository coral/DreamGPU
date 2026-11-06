/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "internal.h"
#include <assert.h>
#include <initializer_list>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    ULONG Function, Args[8], Bytes;
    unsigned char *Pixels;
    BOOL Gathered; /* captured row by row (JglCaptureData), not one span */
} TILE;
static TILE Tiles[1024];
static ULONG Count, Maximum, FailAfter;
static GLenum Error;
static BOOL Ready, ImageSupport, BorderSupport, CompressedSupport;
static JGL_UNPACK Unpack;
static JGL_UNPACK Pack;
static GLint NativeWidth, NativeBorder;
static ULONG Queries, QueryBytes;
static BOOL QueryFail, Compiling;

BOOL JglTextureImagesAvailable(void) {
    return ImageSupport;
}
BOOL JglTextureBordersAvailable(void) {
    return BorderSupport;
}
BOOL JglCompressedTexturesAvailable(void) {
    return CompressedSupport;
}
BOOL JglTextureCubeMapAvailable(void) {
    return FALSE; /* Cube faces: tests/guest/opengl/cube-map.cpp. */
}
BOOL JglTexture3DAvailable(void) {
    return FALSE; /* 3D images: tests/guest/opengl/texture-3d.cpp. */
}
BOOL JglTextureRectangleAvailable(void) {
    return FALSE; /* Rectangles: tests/guest/opengl/texture-rectangle.cpp. */
}
BOOL JglFramebufferObjectAvailable(void) {
    return FALSE; /* Depth textures: tests/guest/opengl/framebuffer-object.cpp. */
}
BOOL JglCompiling(void) {
    return Compiling;
}
BOOL JglQuery(ULONG function, const ULONG *args, ULONG kind, void *out, ULONG capacity,
              ULONG *bytes) {
    assert(function == FEnum_glGetTexLevelParameteriv &&
           (args[0] == GL_TEXTURE_1D || args[0] == GL_TEXTURE_2D) && kind == DG_GL_RESULT_INT &&
           capacity == 4);
    ++Queries;
    if (QueryFail) {
        JglSetError(GL_INVALID_OPERATION);
        return FALSE;
    }
    GLint value = (args[2] == 0x1000 || args[2] == 0x1001) ? NativeWidth : NativeBorder;
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
static TILE *NewTile(ULONG function, const ULONG *args, ULONG words, ULONG bytes) {
    TILE *tile;
    assert(words == 8 && Count < 1024 && bytes <= Maximum);
    assert((bytes + 3u) / 4u * 4u <= Maximum);
    if (Count == FailAfter) {
        JglSetError(GL_INVALID_OPERATION);
        return NULL;
    }
    tile = &Tiles[Count++];
    tile->Function = function;
    tile->Bytes = bytes;
    tile->Gathered = FALSE;
    memcpy(tile->Args, args, sizeof(tile->Args));
    tile->Pixels = (unsigned char *)malloc(bytes ? bytes : 1);
    assert(tile->Pixels);
    return tile;
}
BOOL JglData(ULONG function, const ULONG *args, ULONG words, const void *data, ULONG bytes) {
    TILE *tile = NewTile(function, args, words, bytes);
    if (tile && bytes)
        memcpy(tile->Pixels, data, bytes);
    return tile != NULL;
}
BOOL JglCaptureData(ULONG function, const ULONG *args, ULONG words, ULONG bytes,
                    JGL_CAPTURE capture, void *opaque) {
    assert(capture && bytes);
    TILE *tile = NewTile(function, args, words, bytes);
    if (!tile)
        return FALSE;
    capture(opaque, tile->Pixels); /* ASan bounds the capture to exactly bytes */
    tile->Gathered = TRUE;
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
    Ready = ImageSupport = BorderSupport = TRUE;
    CompressedSupport = FALSE;
    NativeWidth = DG_GL_MAX_TEXTURE_DIMENSION;
    NativeBorder = 0;
    Queries = 0;
    QueryBytes = 4;
    QueryFail = Compiling = FALSE;
    Unpack = (JGL_UNPACK){4, 0, 0, 0, 0, 0, 0, 0};
    Pack = (JGL_UNPACK){4, 0, 0, 0, 0, 0, 0, 0};
}
static void Image(GLsizei w, GLsizei h, GLenum format, const void *pixels) {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, format, GL_UNSIGNED_BYTE, pixels);
}
static void AssertError(GLenum error) {
    assert(Error == error && Count == 0);
}
/* Places sub-image tiles [first, Count) of a rectangle at (x0, y0) with the
 * given row pitch into image: each tile is its own row-major width x height
 * block of pixel_bytes pixels. Every pixel must be written exactly once. */
static void Place(unsigned char *image, ULONG first, GLint x0, GLint y0, ULONG width, ULONG height,
                  ULONG pixel_bytes) {
    static unsigned char covered[2050 * 2050];
    memset(covered, 0, width * height);
    for (ULONG i = first; i < Count; ++i) {
        const TILE *t = &Tiles[i];
        const ULONG x = (ULONG)((GLint)t->Args[2] - x0), y = (ULONG)((GLint)t->Args[3] - y0);
        assert(t->Function == FEnum_glTexSubImage2D && x < width && y < height && t->Args[4] &&
               t->Args[5] && t->Args[4] <= width - x && t->Args[5] <= height - y &&
               t->Bytes == t->Args[4] * t->Args[5] * pixel_bytes);
        for (ULONG row = 0; row < t->Args[5]; ++row) {
            memcpy(image + ((y + row) * width + x) * pixel_bytes,
                   t->Pixels + row * t->Args[4] * pixel_bytes, t->Args[4] * pixel_bytes);
            for (ULONG column = 0; column < t->Args[4]; ++column)
                assert(!covered[(y + row) * width + x + column]++);
        }
    }
    for (ULONG i = 0; i < width * height; ++i)
        assert(covered[i] == 1);
}
/* Tiles [first, Count) start on four-pixel boundaries of their rectangle and
 * are four-pixel multiples except where they reach its edge. */
static void Aligned(ULONG first, GLint x0, GLint y0, ULONG width, ULONG height) {
    for (ULONG i = first; i < Count; ++i) {
        const TILE *t = &Tiles[i];
        const ULONG x = (ULONG)((GLint)t->Args[2] - x0), y = (ULONG)((GLint)t->Args[3] - y0);
        assert(!(x % 4) && !(y % 4));
        assert(!(t->Args[4] % 4) || x + t->Args[4] == width);
        assert(!(t->Args[5] % 4) || y + t->Args[5] == height);
    }
}
static void ExpectTile(ULONG index, GLint x, GLint y, ULONG width, ULONG height, BOOL gathered) {
    const TILE *t = &Tiles[index];
    assert(t->Function == FEnum_glTexSubImage2D && (GLint)t->Args[2] == x &&
           (GLint)t->Args[3] == y && t->Args[4] == width && t->Args[5] == height &&
           t->Gathered == gathered);
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
        Unpack = (JGL_UNPACK){8, 7, 1, 1, 0, 0, 0, 0};
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
        Unpack = (JGL_UNPACK){8, 7, 1, 1, 0, 0, 0, 0};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 5, 2, 0, cases[f].Format, cases[f].Type, saved + 1);
        if (bytes == 2) {
            // Padded rows: one record gathering both rows, not two row records.
            assert(!Error && Count == 2 && Tiles[1].Bytes == 20 && Tiles[1].Args[5] == 2);
            assert(Tiles[1].Gathered && !memcmp(Tiles[1].Pixels, expected, 20));
        } else {
            assert(!Error && Count == 2 && Tiles[1].Args[5] == 2 && Tiles[1].Bytes == 40);
            assert(!memcmp(Tiles[1].Pixels, expected, 40));
        }

        Reset(8);
        Unpack = (JGL_UNPACK){8, 7, 123, 1, 0, 0, 0, 0};
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
        // GL 1.2 packed inputs outside the 16-bit direct path expand to RGBA8.
        // Wine creates its per-unit dummy textures with RGBA/8_8_8_8; a rejected
        // upload leaves those units incomplete and their stages are skipped.
        const struct {
            GLenum Format, Type;
            ULONG Bytes;
            ULONG Word[4];
            unsigned char RGBA[16];
        } wide[] = {
            {GL_RGB,
             0x8362,
             1,
             {0x07, 0x38, 0xc0, 0x92},
             {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 73, 73, 170, 255}},
            {GL_RGB,
             0x8364,
             2,
             {0x001f, 0x07e0, 0xf800, 0x8410},
             {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 132, 130, 132, 255}},
            {GL_RGBA,
             0x8035,
             4,
             {0xff0000ff, 0x00ff00ff, 0x0000ffff, 0x11223344},
             {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 17, 34, 51, 68}},
            {0x80e1,
             0x8035,
             4,
             {0x0000ffff, 0x00ff00ff, 0xff0000ff, 0x11223344},
             {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 51, 34, 17, 68}},
            {GL_RGBA,
             0x8036,
             4,
             {0xffc00003, 0x003ff003, 0x00000fff, 0x80000001},
             {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 128, 0, 0, 85}},
            {GL_RGBA,
             0x8368,
             4,
             {0xc00003ff, 0xc00ffc00, 0xfff00000, 0x40000200},
             {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 128, 0, 0, 85}},
            {0x80e1,
             0x8368,
             4,
             {0xfff00000, 0xc00ffc00, 0xc00003ff, 0x40000200},
             {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 0, 0, 128, 85}},
        };
        for (f = 0; f < sizeof(wide) / sizeof(wide[0]); ++f) {
            unsigned char source[16], swapped[16];
            for (x = 0; x < 4; ++x)
                for (i = 0; i < wide[f].Bytes; ++i) {
                    source[x * wide[f].Bytes + i] = (unsigned char)(wide[f].Word[x] >> (8 * i));
                    swapped[x * wide[f].Bytes + wide[f].Bytes - 1 - i] =
                        (unsigned char)(wide[f].Word[x] >> (8 * i));
                }
            Reset(64);
            Unpack = (JGL_UNPACK){1, 0, 0, 0, 0, 0, 0, 0};
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 1, 0, wide[f].Format, wide[f].Type, source);
            assert(!Error && Count == 2 && Tiles[0].Args[6] == GL_RGBA &&
                   Tiles[0].Args[7] == GL_UNSIGNED_BYTE);
            assert(Tiles[1].Args[6] == GL_RGBA && Tiles[1].Args[7] == GL_UNSIGNED_BYTE &&
                   Tiles[1].Bytes == 16 && !memcmp(Tiles[1].Pixels, wide[f].RGBA, 16));
            if (wide[f].Bytes == 1)
                continue;
            Reset(64);
            Unpack = (JGL_UNPACK){1, 0, 0, 0, 0, 0, 0, 0};
            Unpack.SwapBytes = 1;
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 4, 1, wide[f].Format, wide[f].Type, swapped);
            assert(!Error && Count == 1 && !memcmp(Tiles[0].Pixels, wide[f].RGBA, 16));
        }
        Reset(64);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGB, 0x8035, NULL);
        AssertError(GL_INVALID_OPERATION);
        Reset(64);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, 0x8364, NULL);
        AssertError(GL_INVALID_OPERATION);
        Reset(64);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, 0x8362, NULL);
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

static void NegotiatedTextures(void) {
    Reset(64);
    BorderSupport = FALSE;
    glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA, 4, 1, GL_RGBA, GL_UNSIGNED_BYTE, (const void *)1);
    AssertError(GL_INVALID_OPERATION);
    Reset(64);
    BorderSupport = FALSE;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 1, GL_RGBA, GL_UNSIGNED_BYTE, (const void *)1);
    AssertError(GL_INVALID_OPERATION);
    Reset(64);
    ImageSupport = FALSE;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, 0x1406, (const void *)1);
    AssertError(GL_INVALID_OPERATION);
    Reset(64);
    ImageSupport = FALSE;
    glTexImage2D(0x8064, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void *)1);
    AssertError(GL_INVALID_OPERATION);
    Reset(64);
    ImageSupport = FALSE;
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    assert(!Error && Count == 1); // ordinary legacy definitions retain compatibility
}

/* S3TC and generic COMPRESSED_* internal formats: 2D images and their proxy
 * only, and only with negotiated compression; the upload itself is unchanged. */
static void CompressedFormats(void) {
    static const GLint formats[] = {0x83f0, 0x83f1, 0x83f2, 0x83f3, 0x84e9,
                                    0x84ea, 0x84eb, 0x84ec, 0x84ed, 0x84ee};
    const unsigned char pixels[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    for (GLint format : formats) {
        Reset(64);
        glTexImage2D(GL_TEXTURE_2D, 0, format, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        AssertError(GL_INVALID_VALUE);
        Reset(64);
        glTexImage2D(0x8064, 0, format, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        AssertError(GL_INVALID_VALUE);
        // Negotiated, even without the extended image contract.
        Reset(64);
        CompressedSupport = TRUE;
        ImageSupport = FALSE;
        glTexImage2D(GL_TEXTURE_2D, 1, format, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        assert(!Error && Count == 1 && Tiles[0].Function == FEnum_glTexImage2D &&
               Tiles[0].Args[1] == 1 && Tiles[0].Args[2] == (ULONG)format &&
               Tiles[0].Args[6] == GL_RGBA && Tiles[0].Args[7] == GL_UNSIGNED_BYTE &&
               Tiles[0].Bytes == 16 && !memcmp(Tiles[0].Pixels, pixels, 16));
        Reset(64);
        CompressedSupport = TRUE;
        glTexImage2D(0x8064, 0, format, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        assert(!Error && Count == 1 && Tiles[0].Args[0] == 0x8064 && !Tiles[0].Bytes);
        // Only S3TC forbids a border; generic formats keep the bordered path.
        const BOOL s3tc = format <= 0x83f3;
        for (GLenum target : {(GLenum)GL_TEXTURE_2D, (GLenum)0x8064}) {
            Reset(64);
            CompressedSupport = TRUE;
            glTexImage2D(target, 0, format, 4, 4, 1, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
            if (s3tc)
                AssertError(GL_INVALID_OPERATION);
            else
                assert(!Error && Count == 1 && Tiles[0].Args[5] == 1);
        }
        // TexImage1D and the 1D proxy never accept a compressed internal format.
        Reset(64);
        CompressedSupport = TRUE;
        glTexImage1D(GL_TEXTURE_1D, 0, format, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        AssertError(GL_INVALID_VALUE);
        Reset(64);
        CompressedSupport = TRUE;
        glTexImage1D(0x8063, 0, format, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        AssertError(GL_INVALID_VALUE);
    }
    // Neighbouring enums are not compressed formats.
    for (GLint format : {0x83ef, 0x83f4, 0x84e8, 0x84ef}) {
        Reset(64);
        CompressedSupport = TRUE;
        glTexImage2D(GL_TEXTURE_2D, 0, format, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        AssertError(GL_INVALID_VALUE);
    }
}

/* Split 2D rectangles: tiles start on four-pixel boundaries of the rectangle,
 * rows and column chunks are multiples of four, and only the last tile in
 * each direction takes the remainder (block-aligned for a compressed level). */
static void AlignedTiles(void) {
    static unsigned char big[256 * 256 * 4], actual[256 * 256 * 4];
    for (ULONG i = 0; i < sizeof(big); ++i)
        big[i] = (unsigned char)(i * 7 + (i >> 9));

    // 256x256 RGBA at the real 64 KB packet: 60-row bands, not 63-row bands.
    Reset(65464);
    Image(256, 256, GL_RGBA, big);
    assert(!Error && Count == 6 && Tiles[0].Function == FEnum_glTexImage2D && !Tiles[0].Bytes);
    for (ULONG band = 0; band < 4; ++band) {
        ExpectTile(1 + band, 0, band * 60, 256, 60, FALSE);
        assert(Tiles[1 + band].Bytes == 61440 &&
               !memcmp(Tiles[1 + band].Pixels, big + band * 61440, 61440));
    }
    ExpectTile(5, 0, 240, 256, 16, FALSE);
    assert(Tiles[5].Bytes == 16384 && !memcmp(Tiles[5].Pixels, big + 240 * 1024, 16384));
    Aligned(1, 0, 0, 256, 256);

    // Padded RGB rows (30 of 32 bytes): whole bands gathered into one record.
    Reset(256);
    Image(10, 10, GL_RGB, big);
    assert(!Error && Count == 3);
    ExpectTile(1, 0, 0, 10, 8, TRUE);
    ExpectTile(2, 0, 8, 10, 2, TRUE);
    Place(actual, 1, 0, 0, 10, 10, 3);
    for (ULONG row = 0; row < 10; ++row)
        assert(!memcmp(actual + row * 30, big + row * 32, 30));
    // Smaller records: 4x4 gathered blocks, the last column and row partial.
    Reset(64);
    Image(10, 10, GL_RGB, big);
    assert(!Error && Count == 10);
    for (ULONG band = 0; band < 3; ++band)
        for (ULONG chunk = 0; chunk < 3; ++chunk)
            ExpectTile(1 + band * 3 + chunk, chunk * 4, band * 4, chunk < 2 ? 4 : 2,
                       band < 2 ? 4 : 2, TRUE);
    Place(actual, 1, 0, 0, 10, 10, 3);
    for (ULONG row = 0; row < 10; ++row)
        assert(!memcmp(actual + row * 30, big + row * 32, 30));
    Aligned(1, 0, 0, 10, 10);

    // Horizontal split: four full rows do not fit, so 60-wide 4-row chunks.
    Reset(1000);
    Image(100, 10, GL_RGBA, big);
    assert(!Error && Count == 7);
    for (ULONG band = 0; band < 3; ++band) {
        ExpectTile(1 + band * 2, 0, band * 4, 60, band < 2 ? 4 : 2, TRUE);
        ExpectTile(2 + band * 2, 60, band * 4, 40, band < 2 ? 4 : 2, TRUE);
    }
    Place(actual, 1, 0, 0, 100, 10, 4);
    assert(!memcmp(actual, big, 100 * 10 * 4));

    // A bordered image tiles from its origin (-1, -1).
    Reset(64);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 6, 6, 1, GL_RGBA, GL_UNSIGNED_BYTE, big);
    assert(!Error && Count == 5);
    ExpectTile(1, -1, -1, 4, 4, TRUE);
    ExpectTile(2, 3, -1, 2, 4, TRUE);
    ExpectTile(3, -1, 3, 4, 2, TRUE);
    ExpectTile(4, 3, 3, 2, 2, TRUE);
    Place(actual, 1, -1, -1, 6, 6, 4);
    assert(!memcmp(actual, big, 6 * 6 * 4));

    // TexSubImage2D at an offset: tiles start at the rectangle's origin plus
    // multiples of four. Splitting still preflights the level extent.
    Reset(256);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 12, 21, 30, 9, GL_RGBA, GL_UNSIGNED_BYTE, big);
    assert(!Error && Count == 6 && Queries == 3);
    for (ULONG band = 0; band < 3; ++band) {
        // A one-row chunk is contiguous in the caller and copied as one span.
        ExpectTile(band * 2, 12, 21 + band * 4, 16, band < 2 ? 4 : 1, band < 2);
        ExpectTile(band * 2 + 1, 28, 21 + band * 4, 14, band < 2 ? 4 : 1, band < 2);
    }
    Place(actual, 0, 12, 21, 30, 9, 4);
    assert(!memcmp(actual, big, 30 * 9 * 4));
    Aligned(0, 12, 21, 30, 9);
    Reset(256);
    Compiling = TRUE;
    glTexSubImage2D(GL_TEXTURE_2D, 0, 12, 21, 30, 9, GL_RGBA, GL_UNSIGNED_BYTE, big);
    AssertError(GL_OUT_OF_MEMORY); // a split list rectangle still is refused

    // The conversion path: swapped RGB565 at (8, 4), 4x4 blocks of 32 bytes.
    Reset(32);
    Unpack.SwapBytes = 1;
    glTexSubImage2D(GL_TEXTURE_2D, 0, 8, 4, 20, 6, GL_RGB, 0x8363, big);
    assert(!Error && Count == 10);
    for (ULONG band = 0; band < 2; ++band)
        for (ULONG chunk = 0; chunk < 5; ++chunk)
            ExpectTile(band * 5 + chunk, 8 + chunk * 4, 4 + band * 4, 4, band ? 2 : 4, FALSE);
    Place(actual, 0, 8, 4, 20, 6, 2);
    for (ULONG i = 0; i < 20 * 6; ++i)
        assert(actual[i * 2] == big[i * 2 + 1] && actual[i * 2 + 1] == big[i * 2]);

    // Padded rows that fit one record are no longer split: no preflight and
    // they compile into a list like any single record.
    for (ULONG compiling = 0; compiling < 2; ++compiling) {
        Reset(64);
        Compiling = compiling;
        glTexSubImage2D(GL_TEXTURE_2D, 0, 5, 7, 3, 2, GL_RGB, GL_UNSIGNED_BYTE, big);
        assert(!Error && !Queries && Count == 1);
        ExpectTile(0, 5, 7, 3, 2, TRUE);
        assert(!memcmp(Tiles[0].Pixels, big, 9) && !memcmp(Tiles[0].Pixels + 9, big + 12, 9));
    }

    // 1D keeps its unaligned chunks: 15 pixels per 60-byte record.
    Reset(60);
    glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA, 20, 0, GL_RGBA, GL_UNSIGNED_BYTE, big);
    assert(!Error && Count == 3 && Tiles[1].Args[2] == 0 && Tiles[1].Args[4] == 15 &&
           Tiles[2].Args[2] == 15 && Tiles[2].Args[4] == 5);
    Reset(60);
    glTexSubImage1D(GL_TEXTURE_1D, 0, 0, 20, GL_RGBA, 0x8035, big);
    assert(!Error && Count == 2 && Tiles[0].Args[4] == 15 && Tiles[1].Args[2] == 15);

    // Below sixteen pixels per record no aligned tile exists: unaligned rows.
    Reset(60);
    Image(8, 8, GL_RGBA, big);
    assert(!Error && Count == 9);
    for (ULONG row = 0; row < 8; ++row)
        ExpectTile(1 + row, 0, row, 8, 1, FALSE);
    Reset(64);
    Image(8, 8, GL_RGBA, big);
    assert(!Error && Count == 5);
    Aligned(1, 0, 0, 8, 8);
    Place(actual, 1, 0, 0, 8, 8, 4);
    assert(!memcmp(actual, big, 8 * 8 * 4));
}

static void BorderedTextures(void) {
    unsigned char pixels[144], actual[144] = {};
    for (ULONG i = 0; i < sizeof(pixels); ++i)
        pixels[i] = (unsigned char)(i + 13);
    Reset(16);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 6, 6, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    assert(!Error && Count == 13 && Tiles[0].Args[5] == 1 && !Tiles[0].Bytes);
    for (ULONG i = 1; i < Count; ++i) {
        const TILE *t = &Tiles[i];
        assert(t->Args[5] == 1);
        memcpy(actual + (((GLint)t->Args[3] + 1) * 6 + (GLint)t->Args[2] + 1) * 4, t->Pixels,
               t->Bytes);
    }
    assert(!memcmp(actual, pixels, sizeof(pixels)));
    Reset(16);
    NativeWidth = 6;
    NativeBorder = 1;
    glTexSubImage2D(GL_TEXTURE_2D, 0, -1, -1, 6, 6, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    assert(!Error && Count == 12 && Queries == 3);
    Reset(16);
    NativeWidth = 6;
    NativeBorder = 1;
    glTexSubImage2D(GL_TEXTURE_2D, 0, 4, -1, 2, 6, GL_RGBA, GL_UNSIGNED_BYTE, (const void *)1);
    AssertError(GL_INVALID_VALUE);
    Reset(0);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2050, 2050, 1, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    assert(!Error && Count == 1 && !Tiles[0].Bytes && Tiles[0].Args[3] == 2050 &&
           Tiles[0].Args[4] == 2050);
}

static void ScalarTextures(void) {
    const struct {
        GLenum Type;
        ULONG Unit;
    } types[] = {{0x1400, 1}, {0x1401, 1}, {0x1402, 2}, {0x1403, 2},
                 {0x1404, 4}, {0x1405, 4}, {0x1406, 4}};
    const struct {
        GLenum Format;
        ULONG Channels;
    } formats[] = {{0x1900, 1},
                   {0x1903, 1},
                   {0x1904, 1},
                   {0x1905, 1},
                   {GL_ALPHA, 1},
                   {GL_LUMINANCE, 1},
                   {GL_LUMINANCE_ALPHA, 2},
                   {GL_RGB, 3},
                   {GL_RGBA, 4},
                   {0x80e0, 3},
                   {0x80e1, 4}};
    for (const auto &type : types)
        for (const auto &format : formats)
            for (ULONG swap = 0; swap < 2; ++swap) {
                unsigned char source[1024], expected[256], actual[256] = {};
                ULONG bpp = type.Unit * format.Channels, stride = (7 * bpp + 7) & ~7u;
                for (ULONG i = 0; i < sizeof(source); ++i)
                    source[i] = (unsigned char)(i * 13 + 17);
                for (ULONG y = 0; y < 2; ++y)
                    for (ULONG x = 0; x < 5; ++x)
                        for (ULONG c = 0; c < format.Channels; ++c)
                            for (ULONG j = 0; j < type.Unit; ++j)
                                expected[(y * 5 + x) * bpp + c * type.Unit + j] =
                                    source[1 + (y + 1) * stride + (x + 1) * bpp + c * type.Unit +
                                           (swap ? type.Unit - 1 - j : j)];
                Reset(32);
                Unpack = (JGL_UNPACK){8, 7, 1, 1, (GLint)swap, 0, 0, 0};
                glTexImage2D(GL_TEXTURE_2D, 0, 0x805b /* RGBA16 */, 5, 2, 0, format.Format,
                             type.Type, source + 1);
                assert(!Error && Count > 1 && !Tiles[0].Bytes);
                for (ULONG i = 1; i < Count; ++i) {
                    TILE *tile = &Tiles[i];
                    assert(tile->Args[6] == format.Format && tile->Args[7] == type.Type);
                }
                Place(actual, 1, 0, 0, 5, 2, bpp);
                assert(!memcmp(actual, expected, 10 * bpp));
                // Whole rows fit for bpp <= 3; RGBA8 takes block-aligned 4x2 and 1x2
                // chunks; wider pixels cannot form an aligned chunk in 32 bytes.
                assert(Count == (bpp <= 3 ? 2 : bpp == 4 ? 3 : bpp == 6 ? 3 : bpp == 8 ? 5 : 7));
                if (bpp == 4) {
                    ExpectTile(1, 0, 0, 4, 2, !swap || type.Unit == 1);
                    ExpectTile(2, 4, 0, 1, 2, !swap || type.Unit == 1);
                }
                memset(source, 0, sizeof(source));
                assert(!memcmp(Tiles[1].Pixels, expected, Tiles[1].Args[4] * bpp));
            }
    for (ULONG lsb = 0; lsb < 2; ++lsb) {
        unsigned char bits[16] = {0, 0, 0, 0, 0x96, 0x69, 0, 0, 0x5a, 0xa5};
        unsigned char actual[18] = {};
        Reset(4);
        Unpack = (JGL_UNPACK){4, 13, 1, 3, 1, (GLint)lsb, 0, 0};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 9, 2, 0, 0x1900, 0x1a00, bits);
        assert(!Error && Count == 7);
        for (ULONG i = 1; i < Count; ++i) {
            TILE *tile = &Tiles[i];
            assert(tile->Args[6] == 0x1900 && tile->Args[7] == GL_UNSIGNED_BYTE);
            memcpy(actual + tile->Args[3] * 9 + tile->Args[2], tile->Pixels, tile->Bytes);
        }
        for (ULONG y = 0; y < 2; ++y)
            for (ULONG x = 0; x < 9; ++x) {
                ULONG bit = x + 3;
                assert(actual[y * 9 + x] ==
                       ((bits[(y + 1) * 4 + bit / 8] >> (lsb ? bit % 8 : 7 - bit % 8)) & 1));
            }
        Reset(64);
        Unpack = (JGL_UNPACK){8, 1000, INT32_MAX, 3, 0, (GLint)lsb, 0, 0};
        glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA, 9, 0, 0x1900, 0x1a00, bits + 4);
        assert(!Error && Count == 2 && !memcmp(Tiles[1].Pixels, actual, 9));
    }
    for (GLenum target : {0x8063u, 0x8064u}) {
        Reset(0);
        Unpack = (JGL_UNPACK){8, INT32_MAX, INT32_MAX, INT32_MAX, 1, 1, 0, 0};
        if (target == 0x8063)
            glTexImage1D(target, 0, 0x805b, INT32_MAX, 0, GL_RGBA, 0x1406, (const void *)1);
        else
            glTexImage2D(target, 0, 0x805b, INT32_MAX, INT32_MAX, 0, GL_RGBA, 0x1406,
                         (const void *)1);
        assert(!Error && Count == 1 && !Tiles[0].Bytes && Tiles[0].Args[0] == target);
    }
    Reset(0);
    glTexImage1D(GL_TEXTURE_1D, 0, 0x8049, 0, 0, GL_RGBA, 0x1406, (const void *)1);
    assert(!Error && Count == 1 && !Tiles[0].Bytes && Tiles[0].Args[3] == 0);
    Reset(0);
    glTexImage2D(GL_TEXTURE_2D, 0, 0x8049, 2, 0, 0, GL_RGBA, 0x1406, (const void *)1);
    assert(!Error && Count == 1 && !Tiles[0].Bytes && Tiles[0].Args[4] == 0);
    Reset(8);
    NativeWidth = 4;
    glTexSubImage2D(GL_TEXTURE_2D, 0, 1, 0, 4, 2, GL_RGBA, 0x1406, (const void *)1);
    AssertError(GL_OUT_OF_MEMORY); // one scalar pixel cannot fit, no caller read
    Reset(16);
    NativeWidth = 4;
    glTexSubImage2D(GL_TEXTURE_2D, 0, 1, 0, 4, 2, GL_RGBA, 0x1406, (const void *)1);
    AssertError(GL_INVALID_VALUE); // invalid split rectangle cannot partially write
    assert(Queries == 3);
    Reset(16);
    Compiling = TRUE;
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 4, 2, GL_RGBA, 0x1406, (const void *)1);
    AssertError(GL_OUT_OF_MEMORY);
    assert(!Queries);
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
        assert(!Error && Count == 2 && Tiles[1].Bytes == 4 && Tiles[1].Args[6] == GL_RGB &&
               Tiles[1].Args[7] == 0x8363 && !memcmp(Tiles[1].Pixels, words, 4));
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
    assert(!Error && Count == 2 && Tiles[0].Function == FEnum_glTexImage2D && !Tiles[0].Bytes);
    ExpectTile(1, 0, 0, 3, 2, TRUE);
    assert(Tiles[1].Bytes == 18);
    assert(!memcmp(Tiles[1].Pixels, pixels + 19, 9));
    assert(!memcmp(Tiles[1].Pixels + 9, pixels + 35, 9));

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
            for (i = 1; i < Count; ++i)
                assert(Tiles[i].Args[6] == formats[f].Format);
            Place(framebuffer, 1, 0, 0, 7, 3, components);
            assert(!memcmp(framebuffer, pixels + components, 21 * components));
            if (components == 1) { /* 4x3 and 3x3 gathered block chunks */
                assert(Count == 3);
                ExpectTile(1, 0, 0, 4, 3, TRUE);
                ExpectTile(2, 4, 0, 3, 3, TRUE);
            }
        }
    }

    Reset(64);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 1); /* Short row length may overlap input rows. */
    Image(3, 2, GL_RGB, pixels);
    assert(!Error && Count == 2);
    ExpectTile(1, 0, 0, 3, 2, TRUE);
    assert(!memcmp(Tiles[1].Pixels, pixels, 9));
    assert(!memcmp(Tiles[1].Pixels + 9, pixels + 4, 9));

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
    Image(0, 1, GL_RGBA, (const void *)1);
    assert(!Error && Count == 1 && Tiles[0].Args[3] == 0 && !Tiles[0].Bytes);
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
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, 0x140a, pixels);
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
    ScalarTextures();
    BorderedTextures();
    NegotiatedTextures();
    CompressedFormats();
    AlignedTiles();
    Reset(64);
    puts("Packed RGB332/565, RGBA4444/5551, BGRA4444REV/1555REV pixel/padding oracles passed");
    puts("Texture unpack/tiling tests passed, including1D skip-pixels-only semantics and bounded "
         "chunks");
    return 0;
}
