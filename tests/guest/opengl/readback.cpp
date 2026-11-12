#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "internal.h"
static JGL_UNPACK pack = {4, 0, 0, 0, 0, 0, 0, 0};
static ULONG capacity = 65536, calls, error, scratch[16384];
static GLint texture_width = 1024, texture_height = 1024;
/* DG_CAP2_GL_BULK_READ: the negotiated result size, zero without it. */
static ULONG bulk_capacity;
static BOOL wide_reads, framebuffer_objects, depth_textures, hashed;
/* The bound level's DEPTH_SIZE and STENCIL_SIZE. */
static GLint depth_size, stencil_size;
static ULONG drawable_width = 640, drawable_height = 480;
static BYTE *reply;
BOOL JglReady(void) {
    return TRUE;
}
BOOL JglTextureCubeMapAvailable(void) {
    return FALSE;
}
BOOL JglTexture3DAvailable(void) {
    return FALSE; /* 3D levels: tests/guest/opengl/texture-3d.cpp. */
}
BOOL JglTextureRectangleAvailable(void) {
    return FALSE; /* Rectangles: tests/guest/opengl/texture-rectangle.cpp. */
}
BOOL JglFramebufferObjectAvailable(void) {
    return framebuffer_objects;
}
BOOL JglDepthTextureAvailable(void) {
    return depth_textures;
}
BOOL JglDepthStencilReadbackAvailable(void) {
    return wide_reads;
}
BOOL JglReadFramebufferBound(void) {
    return FALSE;
}
void JglSetError(GLenum e) {
    error = e;
}
JGL_UNPACK *JglPack(void) {
    return &pack;
}
ULONG JglReadbackCapacity(void) {
    return capacity;
}
ULONG *JglReadbackBuffer(void) {
    return scratch;
}
ULONG JglBulkCapacity(void) {
    return bulk_capacity;
}
void *JglAllocate(ULONG bytes) {
    return malloc(bytes);
}
void JglFree(void *memory) {
    free(memory);
}
void JglDrawableSize(ULONG *w, ULONG *h) {
    *w = drawable_width;
    *h = drawable_height;
}
/* Every byte varies with the pixel when hashed, so any swizzle, offset or
 * row mistake shows. */
static ULONG pixel(ULONG at) {
    if (hashed) {
        ULONG v = at * 2654435761u + 0x9e3779b9u;
        v ^= v >> 15;
        v *= 2246822519u;
        return v ^ (v >> 13);
    }
    return (at & 255) | (((at >> 8) & 255) << 8) | 0xff550000;
}
static ULONG float_bits(float f) {
    ULONG u;
    memcpy(&u, &f, 4);
    return u;
}
/* The wire words of texel `at` of the bound level for GetTexImage flags:
 * zero past the level end, as the host pads. */
static ULONG texel(ULONG at, ULONG flags, ULONG *out) {
    const ULONG total = (ULONG)texture_width * (ULONG)texture_height, p = pixel(at);
    const ULONG words = flags & DG_GL_TEXTURE_READ_DEPTH   ? 1
                        : flags & DG_GL_TEXTURE_READ_FLOAT ? 4
                                                           : 1;
    ULONG i;
    for (i = 0; i < words; ++i)
        out[i] = 0;
    if (at >= total)
        return words;
    if (flags & DG_GL_TEXTURE_READ_DEPTH)
        out[0] =
            flags & DG_GL_TEXTURE_READ_FLOAT ? p : float_bits((float)(p & 0xffffff) / 16777215.0f);
    else if (flags & DG_GL_TEXTURE_READ_FLOAT)
        for (i = 0; i < 4; ++i)
            out[i] = float_bits(((p >> (8 * i)) & 255) / 255.0f);
    else
        out[0] = p;
    return words;
}
/* The window's wire words at x, y for a ReadPixels mode. */
static ULONG framebuffer(ULONG x, ULONG y, ULONG mode, ULONG *out) {
    const ULONG p = pixel(y * drawable_width + x);
    ULONG i;
    switch (mode) {
        case DG_GL_READ_DEPTH:
            out[0] = float_bits((float)(p & 0xffffff) / 16777215.0f);
            return 1;
        case DG_GL_READ_STENCIL:
            out[0] = p >> 24;
            return 1;
        case DG_GL_READ_RGBA_FLOAT:
            for (i = 0; i < 4; ++i)
                out[i] = float_bits(((p >> (8 * i)) & 255) / 255.0f);
            return 4;
        default:
            out[0] = p;
            return 1;
    }
}
/* One ReadPixels or GetTexImage answer of `bytes` into `out`. */
static void answer(ULONG fn, const ULONG args[3], ULONG count, ULONG *out, ULONG bytes) {
    ULONG i, n = 0;
    if (fn == FEnum_glGetTexImage) {
        for (i = 0; i < count; ++i)
            n += texel(args[2] + i, args[1], out + n);
    } else {
        const ULONG x = args[0] & 0xffff, w = args[2] & 65535, h = args[2] >> 16;
        assert(x + w <= drawable_width && args[1] + h <= drawable_height);
        for (i = 0; i < w * h; ++i)
            n += framebuffer(x + i % w, args[1] + i / w, args[0] & ~0xffffu, out + n);
    }
    assert(n * 4 == bytes);
}
static ULONG wire_words(ULONG flags) {
    return flags & DG_GL_TEXTURE_READ_DEPTH ? 1 : flags & DG_GL_TEXTURE_READ_FLOAT ? 4 : 1;
}
BOOL JglQuery(ULONG fn, const ULONG args[3], ULONG kind, void *out, ULONG bytes, ULONG *written) {
    assert(bytes <= capacity || (fn == FEnum_glGetTexImage && bytes == 20));
    (void)kind;
    ++calls;
    *written = bytes;
    if (fn == FEnum_glGetTexLevelParameteriv) {
        assert(bytes == 4 && !bulk_capacity);
        assert(args[2] == GL_TEXTURE_WIDTH || args[2] == GL_TEXTURE_HEIGHT || args[2] == 0x884a ||
               (args[2] == 0x88f1 && depth_size > 0));
        *(GLint *)out = args[2] == GL_TEXTURE_WIDTH    ? texture_width
                        : args[2] == GL_TEXTURE_HEIGHT ? texture_height
                        : args[2] == 0x884a            ? depth_size
                                                       : stencil_size;
        return TRUE;
    }
    if (fn == FEnum_glGetTexImage && (args[1] & DG_GL_TEXTURE_READ_SHAPE)) {
        /* The host's shape: the same native answers. */
        GLint shape[5] = {texture_width, texture_height, 1, depth_size,
                          depth_size ? stencil_size : 0};
        assert(bulk_capacity && bytes == 20 && args[1] == DG_GL_TEXTURE_READ_SHAPE && !args[2]);
        memcpy(out, shape, 20);
        return TRUE;
    }
    if (fn == FEnum_glGetTexImage) {
        ULONG count = args[1] >> 16;
        assert((args[1] & 0x3fff) == 0 && !bulk_capacity);
        if (!count)
            count = 128;
        answer(fn, args, count, (ULONG *)out, bytes);
        return TRUE;
    }
    assert(fn == FEnum_glReadPixels && !bulk_capacity);
    {
        ULONG w = args[2] & 65535, h = args[2] >> 16, mode = args[0] & ~0xffffu;
        assert(w * h * (mode == DG_GL_READ_RGBA_FLOAT ? 16 : 4) == bytes);
        answer(fn, args, 0, (ULONG *)out, bytes);
    }
    return TRUE;
}
const void *JglQueryBulk(ULONG fn, const ULONG args[3], ULONG kind, ULONG bytes, ULONG *written) {
    (void)kind;
    assert(bulk_capacity && bytes && bytes <= bulk_capacity);
    ++calls;
    /* Stale bytes of an earlier reply must never be used. */
    memset(reply, 0xdb, DG_GL_MAX_BULK_RESULT_BYTES);
    *written = bytes;
    if (fn == FEnum_glGetTexImage) {
        const ULONG units = args[1] >> 16, flags = args[1] & 0xffff;
        assert((flags & DG_GL_TEXTURE_READ_UNITS) && units &&
               !(flags & ~(DG_GL_TEXTURE_READ_FLOAT | DG_GL_TEXTURE_READ_DEPTH |
                           DG_GL_TEXTURE_READ_UNITS)));
        assert(units * DG_GL_TEXTURE_READ_UNIT * wire_words(flags) * 4 == bytes);
        answer(fn, args, units * DG_GL_TEXTURE_READ_UNIT, (ULONG *)reply, bytes);
    } else {
        ULONG w = args[2] & 65535, h = args[2] >> 16, mode = args[0] & ~0xffffu;
        assert(fn == FEnum_glReadPixels);
        assert(w * h * (mode == DG_GL_READ_RGBA_FLOAT ? 16 : 4) == bytes);
        answer(fn, args, 0, (ULONG *)reply, bytes);
    }
    return reply;
}
#include "readback.cpp"

/* Pack-state byte layout shared by the oracles. */
static ULONG_PTR row_stride(ULONG width, ULONG pixel_bytes) {
    ULONG_PTR stride = (ULONG_PTR)(pack.RowLength ? pack.RowLength : width) * pixel_bytes;
    return (stride + pack.Alignment - 1) & ~((ULONG_PTR)pack.Alignment - 1);
}
static ULONG_PTR image_bytes(ULONG width, ULONG height, ULONG pixel_bytes) {
    return (pack.SkipRows + height) * row_stride(width, pixel_bytes) +
           (pack.SkipPixels + width) * pixel_bytes;
}
/* GetTexImage of the whole level, StoreReadPixel per pixel. */
static void texture_oracle(BYTE *out, GLenum format, GLenum type) {
    const BOOL wide = WideReadColor(type);
    const ULONG pixel_bytes = ReadPixelBytes(format, type, TRUE);
    const ULONG_PTR stride = row_stride(texture_width, pixel_bytes);
    ULONG at, words[4];
    for (at = 0; at < (ULONG)texture_width * (ULONG)texture_height; ++at) {
        texel(at, wide ? DG_GL_TEXTURE_READ_FLOAT : 0, words);
        StoreReadPixel(out + (pack.SkipRows + at / texture_width) * stride +
                           (pack.SkipPixels + at % texture_width) * pixel_bytes,
                       words, format, type, pack.SwapBytes, TRUE, wide);
    }
}
static void framebuffer_oracle(BYTE *out, ULONG x, ULONG y, ULONG w, ULONG h, GLenum format,
                               GLenum type) {
    const BOOL wide = WideReadColor(type);
    const ULONG pixel_bytes = ReadPixelBytes(format, type, FALSE);
    const ULONG_PTR stride = row_stride(w, pixel_bytes);
    ULONG i, words[4];
    for (i = 0; i < w * h; ++i) {
        framebuffer(x + i % w, y + i / w, wide ? DG_GL_READ_RGBA_FLOAT : 0, words);
        StoreReadPixel(out + (pack.SkipRows + i / w) * stride +
                           (pack.SkipPixels + i % w) * pixel_bytes,
                       words, format, type, pack.SwapBytes, FALSE, wide);
    }
}
#define GUARD 64
/* A whole-level GetTexImage against the per-pixel oracle, with guard bytes
 * on both sides of exactly the bytes GL may write. Returns its queries. */
static ULONG texture_case(GLenum format, GLenum type, ULONG bulk) {
    const ULONG pixel_bytes = ReadPixelBytes(format, type, TRUE);
    const ULONG_PTR bytes = image_bytes(texture_width, texture_height, pixel_bytes);
    BYTE *got = (BYTE *)malloc(bytes + 2 * GUARD), *want = (BYTE *)malloc(bytes + 2 * GUARD);
    ULONG queries;
    assert(got && want && !error);
    memset(got, 0xa5, bytes + 2 * GUARD);
    memset(want, 0xa5, bytes + 2 * GUARD);
    texture_oracle(want + GUARD, format, type);
    bulk_capacity = bulk;
    calls = 0;
    glGetTexImage(GL_TEXTURE_2D, 0, format, type, got + GUARD);
    queries = calls;
    bulk_capacity = 0;
    assert(!error);
    assert(!memcmp(got, want, bytes + 2 * GUARD));
    free(got);
    free(want);
    return queries;
}
static ULONG framebuffer_case(ULONG x, ULONG y, ULONG w, ULONG h, GLenum format, GLenum type,
                              ULONG bulk) {
    const ULONG pixel_bytes = ReadPixelBytes(format, type, FALSE);
    const ULONG_PTR bytes = image_bytes(w, h, pixel_bytes);
    BYTE *got = (BYTE *)malloc(bytes + 2 * GUARD), *want = (BYTE *)malloc(bytes + 2 * GUARD);
    ULONG queries;
    assert(got && want && !error);
    memset(got, 0xa5, bytes + 2 * GUARD);
    memset(want, 0xa5, bytes + 2 * GUARD);
    framebuffer_oracle(want + GUARD, x, y, w, h, format, type);
    bulk_capacity = bulk;
    calls = 0;
    glReadPixels((GLint)x, (GLint)y, (GLsizei)w, (GLsizei)h, format, type, got + GUARD);
    queries = calls;
    bulk_capacity = 0;
    assert(!error);
    assert(!memcmp(got, want, bytes + 2 * GUARD));
    free(got);
    free(want);
    return queries;
}
/* The bulk and chunked paths of a call write the same bytes. */
static void same_paths(void (*read)(void *), ULONG_PTR bytes, ULONG bulk) {
    BYTE *a = (BYTE *)malloc(bytes + 2 * GUARD), *b = (BYTE *)malloc(bytes + 2 * GUARD);
    assert(a && b);
    memset(a, 0xa5, bytes + 2 * GUARD);
    memset(b, 0xa5, bytes + 2 * GUARD);
    read(a + GUARD);
    assert(!error);
    bulk_capacity = bulk;
    read(b + GUARD);
    bulk_capacity = 0;
    assert(!error);
    assert(!memcmp(a, b, bytes + 2 * GUARD));
    free(a);
    free(b);
}
static void read_depth_stencil(void *out) {
    glReadPixels(3, 2, 601, 333, JGL_DEPTH_STENCIL, JGL_UNSIGNED_INT_24_8, out);
}
static void read_clipped(void *out) {
    glReadPixels(-5, -3, 700, 500, 0x80e1, 0x8367, out);
}
static void read_depth_texels(void *out) {
    glGetTexImage(GL_TEXTURE_2D, 0, 0x1902, 0x1403, out);
}
static void read_packed_texels(void *out) {
    glGetTexImage(GL_TEXTURE_2D, 0, JGL_DEPTH_STENCIL, JGL_UNSIGNED_INT_24_8, out);
}
static void bulk_reads(void) {
    static const GLenum formats[][2] = {
        {GL_RGBA, GL_UNSIGNED_BYTE},
        {0x80e1, GL_UNSIGNED_BYTE},
        {GL_RGBA, 0x8367},
        {0x80e1, 0x8367},
        {GL_RGB, 0x8363},
        {0x80e1, 0x8366},
        {GL_RGB, GL_UNSIGNED_BYTE},
        {0x80e0, GL_UNSIGNED_BYTE},
        {GL_RGBA, 0x8034},
        {GL_LUMINANCE, GL_UNSIGNED_BYTE},
    };
    static const GLint sizes[][2] = {{1, 1},     {63, 1},    {64, 1},    {65, 1},
                                     {333, 257}, {129, 127}, {128, 128}, {145, 113},
                                     {217, 151}, {331, 99},  {1023, 767}};
    ULONG i, j, legacy, bulk;
    reply = (BYTE *)malloc(DG_GL_MAX_BULK_RESULT_BYTES);
    assert(reply);
    hashed = TRUE;
    pack = (JGL_UNPACK){4, 0, 0, 0, 0, 0, 0, 0};
    capacity = 65536;
    /* Round trips of a whole 1024x768 surface (WineD3D's full-surface lock):
     * 2 level queries and 48 64 KiB reads chunked (51 with depth textures,
     * which add TEXTURE_DEPTH_SIZE), one SHAPE and one read in bulk. */
    texture_width = 1024;
    texture_height = 768;
    legacy = texture_case(0x80e1, 0x8367, 0);
    bulk = texture_case(0x80e1, 0x8367, DG_GL_MAX_BULK_RESULT_BYTES);
    assert(legacy == 50 && bulk == 2);
    depth_textures = TRUE;
    legacy = texture_case(0x80e1, 0x8367, 0);
    assert(legacy == 51);
    assert(texture_case(0x80e1, 0x8367, DG_GL_MAX_BULK_RESULT_BYTES) == 2);
    depth_textures = FALSE;
    printf("1024x768 BGRA GetTexImage with depth textures: %lu round trips chunked, %lu bulk\n",
           (unsigned long)legacy, (unsigned long)bulk);
    drawable_width = 1024;
    drawable_height = 768;
    legacy = framebuffer_case(0, 0, 1024, 768, 0x80e1, 0x8367, 0);
    bulk = framebuffer_case(0, 0, 1024, 768, 0x80e1, 0x8367, DG_GL_MAX_BULK_RESULT_BYTES);
    assert(legacy == 48 && bulk == 1);
    printf("1024x768 BGRA ReadPixels: %lu round trips chunked, %lu bulk\n", (unsigned long)legacy,
           (unsigned long)bulk);
    /* Every format, fast or not, through both paths and odd sizes. */
    drawable_width = 1100;
    drawable_height = 800;
    for (i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i)
        for (j = 0; j < sizeof(sizes) / sizeof(sizes[0]); ++j) {
            texture_width = sizes[j][0];
            texture_height = sizes[j][1];
            assert(texture_case(formats[i][0], formats[i][1], 0));
            assert(texture_case(formats[i][0], formats[i][1], DG_GL_MAX_BULK_RESULT_BYTES) == 2);
            assert(framebuffer_case(7, 3, (ULONG)sizes[j][0], (ULONG)sizes[j][1], formats[i][0],
                                    formats[i][1], DG_GL_MAX_BULK_RESULT_BYTES) == 1);
        }
    /* Pack alignment, row length, skips and swapped bytes (the slow path). */
    texture_width = 333;
    texture_height = 257;
    for (i = 0; i < 4; ++i) {
        static const GLint alignment[] = {1, 2, 4, 8};
        pack = (JGL_UNPACK){alignment[i],
                            (GLint)(i ? 341 + i : 0),
                            (GLint)i,
                            (GLint)(3 * i),
                            (GLint)(i & 1),
                            0,
                            0,
                            0};
        for (j = 0; j < sizeof(formats) / sizeof(formats[0]); ++j) {
            texture_case(formats[j][0], formats[j][1], DG_GL_MAX_BULK_RESULT_BYTES);
            framebuffer_case(1, 2, 333, 257, formats[j][0], formats[j][1],
                             DG_GL_MAX_BULK_RESULT_BYTES);
        }
    }
    pack = (JGL_UNPACK){4, 0, 0, 0, 0, 0, 0, 0};
    /* Results smaller than the level: whole units per query, the last one
     * padded by the host and never stored past the caller's image. */
    texture_width = 1023;
    texture_height = 767;
    for (i = 0; i < 4; ++i) {
        static const ULONG limits[] = {4096, 5000, 65536, 3 * 65536 + 12};
        const ULONG per = limits[i] / 256 * 64;
        const ULONG expected = 1 + (1023 * 767 + per - 1) / per;
        assert(texture_case(GL_RGBA, GL_UNSIGNED_BYTE, limits[i]) == expected);
        assert(texture_case(GL_RGB, 0x8363, limits[i]) == expected);
    }
    /* Exactly 16384 pixels per query across several, and counts one off. */
    for (i = 0; i < 3; ++i) {
        texture_width = 2048;
        texture_height = (GLint)(15 + i); /* 30720, 32768, 34816 pixels */
        assert(texture_case(GL_RGBA, GL_UNSIGNED_BYTE, 65536) ==
               1 + ((ULONG)texture_height * 2048 + 16383) / 16384);
    }
    texture_width = 129; /* 16383 */
    texture_height = 127;
    assert(texture_case(GL_RGBA, GL_UNSIGNED_BYTE, 65536) == 2);
    texture_width = 145; /* 16385 */
    texture_height = 113;
    assert(texture_case(GL_RGBA, GL_UNSIGNED_BYTE, 65536) == 3);
    /* Float reads are 16 wire bytes per pixel: 1024x768 takes three 4 MiB
     * bulk queries, the chunked path 192. */
    wide_reads = TRUE;
    texture_width = 1024;
    texture_height = 768;
    assert(texture_case(GL_RGBA, 0x1406, DG_GL_MAX_BULK_RESULT_BYTES) == 4);
    assert(texture_case(GL_RGBA, 0x1406, 0) == 2 + 192);
    assert(texture_case(0x80e1, 0x1403, 65536 + 512) == 1 + 192);
    assert(framebuffer_case(0, 0, 1024, 768, GL_RGBA, 0x1406, DG_GL_MAX_BULK_RESULT_BYTES) == 3);
    /* Packed depth-stencil ReadPixels composes two queries per tile. */
    framebuffer_objects = TRUE;
    same_paths(read_depth_stencil, image_bytes(601, 333, 4), DG_GL_MAX_BULK_RESULT_BYTES);
    {
        ULONG *out = (ULONG *)malloc(601 * 333 * 4);
        bulk_capacity = DG_GL_MAX_BULK_RESULT_BYTES;
        calls = 0;
        read_depth_stencil(out);
        bulk_capacity = 0;
        assert(!error && calls == 2);
        free(out);
    }
    same_paths(read_depth_stencil, image_bytes(601, 333, 4), 65536);
    /* Clipped rectangles: zero outside the drawable, fast spans inside. */
    same_paths(read_clipped, image_bytes(700, 500, 4), DG_GL_MAX_BULK_RESULT_BYTES);
    same_paths(read_clipped, image_bytes(700, 500, 4), 8192);
    /* Depth textures: SHAPE answers decide the depth checks as the level
     * queries did. */
    depth_textures = TRUE;
    texture_width = 333;
    texture_height = 257;
    depth_size = 24;
    same_paths(read_depth_texels, image_bytes(333, 257, 2), DG_GL_MAX_BULK_RESULT_BYTES);
    stencil_size = 8;
    same_paths(read_packed_texels, image_bytes(333, 257, 4), DG_GL_MAX_BULK_RESULT_BYTES);
    {
        ULONG *out = (ULONG *)malloc(333 * 257 * 4);
        /* A colour read of a depth level, a packed read of a depth-only one
         * and a depth read of a colour level: INVALID_OPERATION, no read. */
        for (i = 0; i < 2; ++i) {
            bulk_capacity = i ? DG_GL_MAX_BULK_RESULT_BYTES : 0;
            calls = 0;
            glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, out);
            assert(error == GL_INVALID_OPERATION && calls == (i ? 1 : 3));
            error = 0;
            stencil_size = 0;
            calls = 0;
            glGetTexImage(GL_TEXTURE_2D, 0, JGL_DEPTH_STENCIL, JGL_UNSIGNED_INT_24_8, out);
            assert(error == GL_INVALID_OPERATION && calls == (i ? 1 : 4));
            error = 0;
            stencil_size = 8;
            depth_size = 0;
            calls = 0;
            glGetTexImage(GL_TEXTURE_2D, 0, 0x1902, 0x1406, out);
            assert(error == GL_INVALID_OPERATION && calls == (i ? 1 : 3));
            error = 0;
            depth_size = 24;
            /* An empty level reads nothing. */
            texture_width = 0;
            calls = 0;
            glGetTexImage(GL_TEXTURE_2D, 0, 0x1902, 0x1406, out);
            assert(!error && calls == (i ? 1 : 2));
            texture_width = 333;
        }
        bulk_capacity = 0;
        free(out);
    }
    depth_textures = framebuffer_objects = wide_reads = FALSE;
    depth_size = stencil_size = 0;
    drawable_width = 640;
    drawable_height = 480;
    texture_width = texture_height = 1024;
    hashed = FALSE;
    free(reply);
}
int main(void) {
    ULONG *out = (ULONG *)malloc(1024 * 1024 * 4 + 8);
    ULONG i, bulk_calls;
    assert(out);
    out[0] = out[1024 * 1024 + 1] = 0x12345678;
    calls = 0;
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, out + 1);
    assert(!error && calls == 66);
    bulk_calls = calls;
    for (i = 0; i < 1024 * 1024; ++i)
        assert(out[i + 1] == pixel(i));
    assert(out[0] == 0x12345678 && out[1024 * 1024 + 1] == 0x12345678);
    capacity = 512;
    calls = 0;
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, out + 1);
    assert(!error && calls == 8194 && calls > bulk_calls * 100);
    capacity = 65536;
    calls = 0;
    glReadPixels(0, 0, 640, 480, GL_RGBA, GL_UNSIGNED_BYTE, out + 1);
    assert(!error && calls == 20);
    for (i = 0; i < 640 * 480; ++i)
        assert(out[i + 1] == pixel(i));
    capacity = 512;
    calls = 0;
    glReadPixels(0, 0, 640, 480, GL_RGBA, GL_UNSIGNED_BYTE, out + 1);
    assert(!error && calls == 2400);
    for (i = 0; i < 640 * 480; ++i)
        assert(out[i + 1] == pixel(i));
    capacity = 65536;
    pack.RowLength = 8;
    pack.SkipRows = 1;
    pack.SkipPixels = 1;
    memset(out, 0xcc, 8 * 8 * 4);
    glReadPixels(-1, -1, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, out);
    for (i = 0; i < 16; ++i) {
        ULONG row = i / 4, col = i % 4;
        assert(out[(row + 1) * 8 + col + 1] == (row && col ? pixel((row - 1) * 640 + col - 1) : 0));
    }
    assert(out[0] == 0xcccccccc && out[8] == 0xcccccccc);
    free(out);
    bulk_reads();
    puts("PASS bulk readback: 128x fewer texture RPCs,120x fewer framebuffer RPCs,legacy512 "
         "compatibility,clipping,packing and guards; DG_CAP2_GL_BULK_READ: whole surfaces in "
         "one read, every format byte-identical to per-pixel conversion");
    return 0;
}
