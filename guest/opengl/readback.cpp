/* SPDX-License-Identifier: GPL-2.0-or-later
 * Explicit application pixel reads. Normal drawing never enters this path.
 * The driver owns the bounded result DMA; caller packing stays in user mode.
 */
#include "internal.h"
#include <stdint.h>

extern "C" {

static BOOL Add(ULONG_PTR a, ULONG_PTR b, ULONG_PTR *out) {
    if (a > (ULONG_PTR)-1 - b)
        return FALSE;
    *out = a + b;
    return TRUE;
}
static BOOL Multiply(ULONG_PTR a, ULONG_PTR b, ULONG_PTR *out) {
    if (b && a > (ULONG_PTR)-1 / b)
        return FALSE;
    *out = a * b;
    return TRUE;
}
/* Transport color pixels are RGBA8; depth is float32 and stencil is uint32.
 * Caller packing is independent of DMA tile size and permits unaligned output. */
static ULONG ReadComponents(GLenum format, BOOL texture) {
    switch (format) {
        case 0x1903: /* RED */
        case 0x1904: /* GREEN */
        case 0x1905: /* BLUE */
        case GL_ALPHA:
        case GL_LUMINANCE:
            return 1;
        case GL_LUMINANCE_ALPHA:
            return 2;
        case GL_RGB:
        case 0x80e0: /* BGR */
            return 3;
        case GL_RGBA:
        case 0x80e1: /* BGRA */
            return 4;
        case 0x1901: /* STENCIL_INDEX */
        case 0x1902: /* DEPTH_COMPONENT */
            return texture ? 0 : 1;
        case JGL_DEPTH_STENCIL: /* EXT_packed_depth_stencil, framebuffers only */
            return texture || !JglFramebufferObjectAvailable() ? 0 : 1;
        default:
            return 0;
    }
}
static ULONG ReadPixelBytes(GLenum format, GLenum type, BOOL texture) {
    ULONG components = ReadComponents(format, texture);
    if (!texture && format == 0x1900) { /* COLOR_INDEX on an RGBA drawable. */
        JglSetError(GL_INVALID_OPERATION);
        return 0;
    }
    if (!components) {
        JglSetError(GL_INVALID_ENUM);
        return 0;
    }
    /* DEPTH_STENCIL is read only as UNSIGNED_INT_24_8. */
    if (format == JGL_DEPTH_STENCIL) {
        if (type == JGL_UNSIGNED_INT_24_8)
            return 4;
        JglSetError(GL_INVALID_ENUM);
        return 0;
    }
    switch (type) {
        case JGL_UNSIGNED_INT_24_8: /* DEPTH_STENCIL only */
            if (!texture && JglFramebufferObjectAvailable())
                break;
            JglSetError(GL_INVALID_ENUM);
            return 0;
        case 0x1a00: /* BITMAP: only integer stencil indices are exposed. */
            if (!texture && format == 0x1901)
                return 1;
            break;
        case 0x1400: /* BYTE */
        case GL_UNSIGNED_BYTE:
            return components;
        case 0x1402: /* SHORT */
        case 0x1403: /* UNSIGNED_SHORT */
            return components * 2;
        case 0x1404: /* INT */
        case 0x1405: /* UNSIGNED_INT */
        case 0x1406: /* FLOAT */
            return components * 4;
        case 0x8032: /* UNSIGNED_BYTE_3_3_2 */
        case 0x8362: /* UNSIGNED_BYTE_2_3_3_REV */
            if (format == GL_RGB)
                return 1;
            break;
        case 0x8363: /* UNSIGNED_SHORT_5_6_5 */
        case 0x8364: /* UNSIGNED_SHORT_5_6_5_REV */
            if (format == GL_RGB)
                return 2;
            break;
        case 0x8033: /* UNSIGNED_SHORT_4_4_4_4 */
        case 0x8034: /* UNSIGNED_SHORT_5_5_5_1 */
        case 0x8365: /* UNSIGNED_SHORT_4_4_4_4_REV */
        case 0x8366: /* UNSIGNED_SHORT_1_5_5_5_REV */
            if (components == 4)
                return 2;
            break;
        case 0x8035: /* UNSIGNED_INT_8_8_8_8 */
        case 0x8367: /* UNSIGNED_INT_8_8_8_8_REV */
        case 0x8036: /* UNSIGNED_INT_10_10_10_2 */
        case 0x8368: /* UNSIGNED_INT_2_10_10_10_REV */
            if (components == 4)
                return 4;
            break;
        default:
            JglSetError(GL_INVALID_ENUM);
            return 0;
    }
    JglSetError(GL_INVALID_OPERATION);
    return 0;
}
static void StoreReadWord(BYTE *dest, ULONG value, ULONG bytes, BOOL swap) {
    ULONG i;
    for (i = 0; i < bytes; ++i)
        dest[swap ? bytes - 1 - i : i] = (BYTE)(value >> (i * 8));
}
static ULONG ReadScalarBytes(GLenum type) {
    return type == 0x1400 || type == GL_UNSIGNED_BYTE ? 1
           : type == 0x1402 || type == 0x1403         ? 2
                                                      : 4;
}
static void StoreReadScalar(BYTE *dest, double value, GLenum type, BOOL swap, BOOL index) {
    ULONG word, bytes = ReadScalarBytes(type);
    BOOL signed_type = type == 0x1400 || type == 0x1402 || type == 0x1404;
    double maximum = type == 0x1400             ? 127.0
                     : type == GL_UNSIGNED_BYTE ? 255.0
                     : type == 0x1402           ? 32767.0
                     : type == 0x1403           ? 65535.0
                     : type == 0x1404           ? 2147483647.0
                                                : 4294967295.0;
    if (type == 0x1406) {
        union {
            float f;
            ULONG u;
        } bits;
        bits.f = (float)value;
        word = bits.u;
    } else if (index) {
        /* GL1.1 index masks exclude the sign bit for signed output types. */
        word = (ULONG)value & (ULONG)maximum;
    } else {
        if (!(value > 0.0))
            value = 0.0;
        if (value >= 1.0)
            word = (ULONG)maximum;
        else {
            /* GL1.1 Table4.6 signed conversion is ((2^n-1)c-1)/2,
             * rounded to nearest. Adding1/2 cancels its negative offset. */
            word = (ULONG)(signed_type ? value * (maximum + 0.5) : value * maximum + 0.5);
        }
    }
    StoreReadWord(dest, word, bytes, swap && bytes > 1);
}
static BOOL WideReadColor(GLenum type) {
    return (type >= 0x1400 && type <= 0x1406 && type != GL_UNSIGNED_BYTE) || type == 0x8036 ||
           type == 0x8368;
}
static ULONG PackedReadComponent(double value, ULONG bits) {
    ULONG limit = 1u << bits;
    if (!(value > 0.0))
        return 0;
    return value >= 1.0 ? limit - 1 : (ULONG)(value * limit);
}
static void StoreReadByteColor(BYTE *dest, ULONG rgba, GLenum format, GLenum type, BOOL swap,
                               BOOL texture) {
    ULONG r = rgba & 255, g = (rgba >> 8) & 255, b = (rgba >> 16) & 255, a = rgba >> 24;
    ULONG value, bytes = 2, i;
    if (format == 0x80e0 || format == 0x80e1) {
        ULONG tmp = r;
        r = b;
        b = tmp;
    }
    if (type == GL_UNSIGNED_BYTE) {
        if (format == 0x1903 || format == 0x1904 || format == 0x1905) {
            dest[0] = (BYTE)(format == 0x1903 ? r : format == 0x1904 ? g : b);
            return;
        }
        if (!texture && (format == GL_LUMINANCE || format == GL_LUMINANCE_ALPHA))
            r = r + g + b > 255 ? 255 : r + g + b;
        if (format == GL_ALPHA)
            dest[0] = (BYTE)a;
        else {
            dest[0] = (BYTE)r;
            if (format == GL_LUMINANCE_ALPHA)
                dest[1] = (BYTE)a;
            else if (format != GL_LUMINANCE) {
                dest[1] = (BYTE)g;
                dest[2] = (BYTE)b;
                if (format == GL_RGBA || format == 0x80e1)
                    dest[3] = (BYTE)a;
            }
        }
        return; /* PACK_SWAP_BYTES has no effect on individual byte components. */
    }
    /* Native packed readback takes the high bits of each normalized component. */
    switch (type) {
        case 0x8032:
            value = (r & 0xe0) | ((g >> 3) & 0x1c) | (b >> 6);
            bytes = 1;
            break;
        case 0x8362:
            value = (r >> 5) | ((g >> 2) & 0x38) | (b & 0xc0);
            bytes = 1;
            break;
        case 0x8364:
            value = (r >> 3) | ((g >> 2) << 5) | ((b >> 3) << 11);
            break;
        case 0x8035:
            value = (r << 24) | (g << 16) | (b << 8) | a;
            bytes = 4;
            break;
        case 0x8363:
            value = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
            break;
        case 0x8033:
            value = ((r >> 4) << 12) | ((g >> 4) << 8) | ((b >> 4) << 4) | (a >> 4);
            break;
        case 0x8034:
            value = ((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | (a >> 7);
            break;
        case 0x8365:
            value = (r >> 4) | ((g >> 4) << 4) | ((b >> 4) << 8) | ((a >> 4) << 12);
            break;
        case 0x8366:
            value = (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | ((a >> 7) << 15);
            break;
        default: /* UNSIGNED_INT_8_8_8_8_REV */
            value = r | (g << 8) | (b << 16) | (a << 24);
            bytes = 4;
            break;
    }
    for (i = 0; i < bytes; ++i)
        dest[swap ? bytes - 1 - i : i] = (BYTE)(value >> (i * 8));
}
static void StoreReadPixel(BYTE *dest, const ULONG *source, GLenum format, GLenum type, BOOL swap,
                           BOOL texture, BOOL wide) {
    ULONG components, bytes, i, word = 0, shift = 0, bits[4];
    BOOL reverse = FALSE;
    double values[4];
    if (format == 0x1902) {
        union {
            float f;
            ULONG u;
        } depth;
        depth.u = source[0];
        StoreReadScalar(dest, depth.f, type, swap, FALSE);
        return;
    }
    if (format == 0x1901) {
        StoreReadScalar(dest, source[0], type, swap, TRUE);
        return;
    }
    if (format == JGL_DEPTH_STENCIL) {
        /* UNSIGNED_INT_24_8: depth (float32 word) in the high 24 bits, the
         * stencil index (uint32 word) in the low 8, one 32-bit unit. */
        union {
            float f;
            ULONG u;
        } depth;
        depth.u = source[0];
        const double value = depth.f;
        const ULONG z = !(value > 0.0) ? 0
                        : value >= 1.0 ? 0xffffff
                                       : (ULONG)(value * 0xffffff + 0.5);
        StoreReadWord(dest, z << 8 | (source[1] & 0xff), 4, swap);
        return;
    }
    if (!wide) {
        StoreReadByteColor(dest, source[0], format, type, swap, texture);
        return;
    }
    for (i = 0; i < 4; ++i) {
        if (wide) {
            union {
                float f;
                ULONG u;
            } component;
            component.u = source[i];
            values[i] = component.f;
        } else
            values[i] = ((source[0] >> (i * 8)) & 255) / 255.0;
    }
    if (format == 0x80e0 || format == 0x80e1) {
        double tmp = values[0];
        values[0] = values[2];
        values[2] = tmp;
    }
    if (type >= 0x1400 && type <= 0x1406) {
        if (format == GL_ALPHA)
            values[0] = values[3];
        else if (format == 0x1904)
            values[0] = values[1];
        else if (format == 0x1905)
            values[0] = values[2];
        else if (format == GL_LUMINANCE || format == GL_LUMINANCE_ALPHA) {
            if (!texture) {
                values[0] += values[1] + values[2];
                if (values[0] > 1.0)
                    values[0] = 1.0;
            }
            values[1] = values[3];
        }
        components = ReadComponents(format, texture);
        bytes = ReadScalarBytes(type);
        for (i = 0; i < components; ++i)
            StoreReadScalar(dest + i * bytes, values[i], type, swap, FALSE);
        return;
    }
    components = 4;
    switch (type) {
        case 0x8362:
            reverse = TRUE;
            /* fall through */
        case 0x8032:
            bits[0] = bits[1] = 3;
            bits[2] = 2;
            components = 3;
            bytes = 1;
            break;
        case 0x8364:
            reverse = TRUE;
            /* fall through */
        case 0x8363:
            bits[0] = bits[2] = 5;
            bits[1] = 6;
            components = 3;
            bytes = 2;
            break;
        case 0x8365:
            reverse = TRUE;
            /* fall through */
        case 0x8033:
            bits[0] = bits[1] = bits[2] = bits[3] = 4;
            bytes = 2;
            break;
        case 0x8366:
            reverse = TRUE;
            /* fall through */
        case 0x8034:
            bits[0] = bits[1] = bits[2] = 5;
            bits[3] = 1;
            bytes = 2;
            break;
        case 0x8367:
            reverse = TRUE;
            /* fall through */
        case 0x8035:
            bits[0] = bits[1] = bits[2] = bits[3] = 8;
            bytes = 4;
            break;
        case 0x8368:
            reverse = TRUE;
            /* fall through */
        default: /* 10_10_10_2 */
            bits[0] = bits[1] = bits[2] = 10;
            bits[3] = 2;
            bytes = 4;
            break;
    }
    for (i = 0; i < components; ++i) {
        ULONG component = reverse ? i : components - 1 - i;
        word |= PackedReadComponent(values[component], bits[component]) << shift;
        shift += bits[component];
    }
    StoreReadWord(dest, word, bytes, swap && bytes > 1);
}
/* Row conversions of RGBA8 wire pixels that need no per-pixel dispatch,
 * byte-identical to StoreReadPixel: WineD3D downloads whole X8R8G8B8 and
 * A8R8G8B8 surfaces as BGRA/UNSIGNED_INT_8_8_8_8_REV, R5G6B5 as
 * RGB/UNSIGNED_SHORT_5_6_5 and X1R5G5B5/A1R5G5B5 as BGRA/1_5_5_5_REV. */
#define FAST_NONE 0UL
#define FAST_RGBA 1UL
#define FAST_BGRA 2UL
#define FAST_565 3UL
#define FAST_1555 4UL
static ULONG FastRead(GLenum format, GLenum type, BOOL swap, BOOL wide) {
    if (wide)
        return FAST_NONE;
    if (type == GL_UNSIGNED_BYTE || (type == 0x8367 && !swap)) /* 8_8_8_8_REV */
        return format == GL_RGBA ? FAST_RGBA : format == 0x80e1 ? FAST_BGRA : FAST_NONE;
    if (swap)
        return FAST_NONE;
    if (type == 0x8363 && format == GL_RGB) /* UNSIGNED_SHORT_5_6_5 */
        return FAST_565;
    if (type == 0x8366 && format == 0x80e1) /* UNSIGNED_SHORT_1_5_5_5_REV */
        return FAST_1555;
    return FAST_NONE;
}
/* `count` pixels of one row: a fast conversion, else StoreReadPixel each. */
static void StoreReadRow(BYTE *dest, const ULONG *source, ULONG count, ULONG pixel_bytes,
                         ULONG wire_words, ULONG fast, GLenum format, GLenum type, BOOL swap,
                         BOOL texture, BOOL wide) {
    ULONG i;
    switch (fast) {
        case FAST_RGBA:
            CopyMemory(dest, source, count * 4);
            return;
        case FAST_BGRA:
            for (i = 0; i < count; ++i) {
                const ULONG w = source[i];
                const ULONG v = (w & 0xff00ff00UL) | ((w >> 16) & 0xff) | ((w & 0xff) << 16);
                dest[i * 4] = (BYTE)v;
                dest[i * 4 + 1] = (BYTE)(v >> 8);
                dest[i * 4 + 2] = (BYTE)(v >> 16);
                dest[i * 4 + 3] = (BYTE)(v >> 24);
            }
            return;
        case FAST_565:
            for (i = 0; i < count; ++i) {
                const ULONG w = source[i];
                const ULONG v =
                    ((w & 0xf8) << 8) | ((w >> 5) & 0x7e0) | ((w >> 19) & 0x1f); /* r g b */
                dest[i * 2] = (BYTE)v;
                dest[i * 2 + 1] = (BYTE)(v >> 8);
            }
            return;
        case FAST_1555:
            for (i = 0; i < count; ++i) {
                const ULONG w = source[i];
                /* BGRA: blue in the low five bits, then green, red, alpha. */
                const ULONG v = ((w >> 19) & 0x1f) | ((w >> 6) & 0x3e0) | ((w & 0xf8) << 7) |
                                ((w >> 16) & 0x8000);
                dest[i * 2] = (BYTE)v;
                dest[i * 2 + 1] = (BYTE)(v >> 8);
            }
            return;
        default:
            for (i = 0; i < count; ++i)
                StoreReadPixel(dest + i * pixel_bytes, source + i * wire_words, format, type, swap,
                               texture, wide);
    }
}
void APIENTRY glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format,
                           GLenum type, void *pixels) {
    JGL_UNPACK *pack;
    ULONG pixel_bytes, row, column, columns, rows, i, j, args[3], bytes, drawable_width,
        drawable_height, tile_width, fast;
    ULONG *rgba = NULL, capacity, bulk;
    ULONG *depth_copy = NULL;
    ULONG_PTR stride, skip, first, span, end;
    BYTE *output;
    ULONG mode = 0, kind = DG_GL_RESULT_INT, wire_bytes = 4;
    BOOL wide = FALSE, bitmap = type == 0x1a00;
    /* DEPTH_STENCIL composes a DEPTH and a STENCIL read of each tile: eight
     * wire bytes per pixel, the depth words then the stencil words. */
    const BOOL packed = format == JGL_DEPTH_STENCIL;
    if (!JglReady())
        return;
    pixel_bytes = ReadPixelBytes(format, type, FALSE);
    if (!pixel_bytes)
        return;
    if (format == 0x1902 || format == 0x1901 || packed) {
        if (!JglDepthStencilReadbackAvailable()) {
            JglSetError(GL_INVALID_OPERATION);
            return;
        }
        mode = format == 0x1901 ? DG_GL_READ_STENCIL : DG_GL_READ_DEPTH;
        kind = format == 0x1901 ? DG_GL_RESULT_INT : DG_GL_RESULT_FLOAT;
        if (packed)
            wire_bytes = 8;
    } else if (WideReadColor(type)) {
        if (!JglDepthStencilReadbackAvailable()) {
            JglSetError(GL_INVALID_OPERATION);
            return;
        }
        mode = DG_GL_READ_RGBA_FLOAT;
        kind = DG_GL_RESULT_FLOAT;
        wire_bytes = 16;
        wide = TRUE;
    }
    if (width < 0 || height < 0 || width > DG_GL_MAX_DIMENSION || height > DG_GL_MAX_DIMENSION ||
        (int64_t)x + width > INT32_MAX || (int64_t)y + height > INT32_MAX)
        goto invalid;
    if (!width || !height)
        return;
    if (!pixels)
        goto invalid;
    pack = JglPack();
    if (bitmap) {
        if (!Add(pack->RowLength ? (ULONG_PTR)pack->RowLength : (ULONG_PTR)width, 7, &stride))
            goto invalid;
        stride /= 8;
        skip = (ULONG_PTR)pack->SkipPixels / 8;
    } else {
        if (!Multiply(pack->RowLength ? (ULONG_PTR)pack->RowLength : (ULONG_PTR)width, pixel_bytes,
                      &stride) ||
            !Multiply(pack->SkipPixels, pixel_bytes, &skip))
            goto invalid;
    }
    if (!Add(stride, pack->Alignment - 1, &stride))
        goto invalid;
    stride &= ~((ULONG_PTR)pack->Alignment - 1);
    if (!Multiply(pack->SkipRows, stride, &first) || !Add(first, skip, &first) ||
        !Multiply(height - 1, stride, &span) ||
        !Add(span,
             bitmap ? ((ULONG_PTR)(pack->SkipPixels & 7) + width + 7) / 8
                    : (ULONG_PTR)width * pixel_bytes,
             &span) ||
        !Add(first, span, &end) || !Add((ULONG_PTR)pixels, end, &end))
        goto invalid;
    output = (BYTE *)pixels + first;
    /* A framebuffer object bound for reading need not fit the drawable: the
     * host reads what lies inside its attachments and zero-fills the rest. */
    if (JglReadFramebufferBound())
        drawable_width = drawable_height = DG_GL_MAX_DIMENSION;
    else
        JglDrawableSize(&drawable_width, &drawable_height);
    /* Bulk reads (DG_CAP2_GL_BULK_READ): tiles of the negotiated result size,
     * converted straight from the reply; otherwise contiguous-limit tiles
     * copied into the readback buffer. */
    bulk = JglBulkCapacity();
    capacity = bulk ? bulk : JglReadbackCapacity();
    if (!bulk) {
        rgba = JglReadbackBuffer();
        if (!rgba)
            return;
    }
    fast = bitmap || packed ? FAST_NONE : FastRead(format, type, pack->SwapBytes, wide);
    tile_width = (ULONG)width < capacity / wire_bytes ? (ULONG)width : capacity / wire_bytes;
    for (row = 0; row < (ULONG)height; row += rows) {
        rows = capacity / wire_bytes / tile_width;
        if (rows > (ULONG)height - row)
            rows = (ULONG)height - row;
        for (column = 0; column < (ULONG)width; column += columns) {
            int64_t left = (int64_t)x + column, bottom = (int64_t)y + row;
            int64_t clipped_left = left < 0 ? 0 : left, clipped_bottom = bottom < 0 ? 0 : bottom;
            int64_t clipped_right, clipped_top;
            ULONG clipped_width = 0, clipped_height = 0;
            const ULONG *tile = rgba, *stencil = NULL;
            columns = (ULONG)width - column;
            if (columns > tile_width)
                columns = tile_width;
            clipped_right = left + columns;
            clipped_top = bottom + rows;
            if (clipped_right > drawable_width)
                clipped_right = drawable_width;
            if (clipped_top > drawable_height)
                clipped_top = drawable_height;
            if (clipped_right > clipped_left && clipped_top > clipped_bottom) {
                clipped_width = (ULONG)(clipped_right - clipped_left);
                clipped_height = (ULONG)(clipped_top - clipped_bottom);
                const ULONG count = clipped_width * clipped_height, reply = packed ? 4 : wire_bytes;
                args[0] = (ULONG)clipped_left | mode;
                args[1] = (ULONG)clipped_bottom;
                args[2] = clipped_width | (clipped_height << 16);
                if (bulk) {
                    tile = (const ULONG *)JglQueryBulk(FEnum_glReadPixels, args, kind,
                                                       count * reply, &bytes);
                    if (!tile)
                        goto done;
                } else if (!JglQuery(FEnum_glReadPixels, args, kind, rgba, count * reply, &bytes))
                    return;
                if (bytes != count * reply) {
                    JglSetError(GL_INVALID_OPERATION);
                    goto done;
                }
                if (packed) {
                    if (bulk) {
                        /* The stencil query replaces the reply: keep the depth. */
                        if (!depth_copy)
                            depth_copy = (ULONG *)JglAllocate(capacity / 2);
                        if (!depth_copy) {
                            JglSetError(GL_OUT_OF_MEMORY);
                            return;
                        }
                        CopyMemory(depth_copy, tile, count * 4);
                        tile = depth_copy;
                    }
                    args[0] = (ULONG)clipped_left | DG_GL_READ_STENCIL;
                    if (bulk) {
                        stencil = (const ULONG *)JglQueryBulk(FEnum_glReadPixels, args,
                                                              DG_GL_RESULT_INT, count * 4, &bytes);
                        if (!stencil)
                            goto done;
                    } else {
                        stencil = rgba + count;
                        if (!JglQuery(FEnum_glReadPixels, args, DG_GL_RESULT_INT, rgba + count,
                                      count * 4, &bytes))
                            return;
                    }
                    if (bytes != count * 4) {
                        JglSetError(GL_INVALID_OPERATION);
                        goto done;
                    }
                }
            }
            for (j = 0; j < rows; ++j) {
                BYTE *dest = output + (row + j) * stride;
                const BOOL inside = clipped_width && clipped_height &&
                                    bottom + j >= clipped_bottom && bottom + j < clipped_top;
                /* Columns [lo, hi) of this row lie inside the read rectangle. */
                const ULONG lo = inside ? (ULONG)(clipped_left - left) : columns;
                const ULONG hi = inside ? (ULONG)(clipped_right - left) : columns;
                for (i = 0; i < columns; ++i) {
                    ULONG value[4] = {0, 0, 0, 0};
                    const ULONG *pixel = value;
                    if (i >= lo && i < hi) {
                        const ULONG at = (ULONG)(bottom + j - clipped_bottom) * clipped_width +
                                         (ULONG)(left + i - clipped_left);
                        if (fast != FAST_NONE && i == lo) {
                            StoreReadRow(dest + (column + i) * pixel_bytes, tile + at, hi - lo,
                                         pixel_bytes, 1, fast, format, type, pack->SwapBytes, FALSE,
                                         FALSE);
                            i = hi - 1;
                            continue;
                        }
                        if (packed) {
                            value[0] = tile[at];
                            value[1] = stencil[at];
                        } else
                            pixel = tile + at * (wire_bytes / 4);
                    }
                    if (bitmap) {
                        ULONG bit = (pack->SkipPixels & 7) + column + i;
                        BYTE mask = (BYTE)(1u << (pack->LsbFirst ? bit % 8 : 7 - bit % 8));
                        dest[bit / 8] =
                            (BYTE)((dest[bit / 8] & ~mask) | ((pixel[0] & 1) ? mask : 0));
                    } else
                        StoreReadPixel(dest + (column + i) * pixel_bytes, pixel, format, type,
                                       pack->SwapBytes, FALSE, wide);
                }
            }
        }
    }
done:
    JglFree(depth_copy);
    return;
invalid:
    JglSetError(GL_INVALID_VALUE);
}

/* ARB_depth_texture: GetTexImage of a depth level as DEPTH_COMPONENT (any
 * scalar type) or, of a packed level (EXT_packed_depth_stencil), as
 * DEPTH_STENCIL/UNSIGNED_INT_24_8. Zero when the format is no depth format or
 * depth textures are not negotiated (then ReadPixelBytes reports it). */
static ULONG DepthTexturePixelBytes(GLenum format, GLenum type) {
    if (!JglDepthTextureAvailable() || (format != 0x1902 && format != JGL_DEPTH_STENCIL))
        return 0;
    if (format == JGL_DEPTH_STENCIL || type == JGL_UNSIGNED_INT_24_8) {
        if (format == JGL_DEPTH_STENCIL && type == JGL_UNSIGNED_INT_24_8)
            return 4;
        JglSetError(format == JGL_DEPTH_STENCIL ? GL_INVALID_ENUM : GL_INVALID_OPERATION);
        return (ULONG)-1;
    }
    if (type >= 0x1400 && type <= 0x1406) /* BYTE..FLOAT */
        return type <= 0x1401 ? 1 : type <= 0x1403 ? 2 : 4;
    /* Packed colour types and BITMAP are no depth types. */
    JglSetError((type >= 0x8032 && type <= 0x8036) || (type >= 0x8362 && type <= 0x8368)
                    ? GL_INVALID_OPERATION
                    : GL_INVALID_ENUM);
    return (ULONG)-1;
}
void APIENTRY glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, void *pixels) {
    ULONG args[3], bytes, pixel_bytes, at = 0, total, fast, per_query = 0, shape[5];
    ULONG *rgba = NULL, capacity, bulk;
    GLint width = 0, height = 1, depth = 1;
    ULONG_PTR stride, skip, first, span, end, image_stride, images;
    /* A 3D level is all of its images (EXT_texture3D). */
    const BOOL volume = target == JGL_TEXTURE_3D && JglTexture3DAvailable();
    JGL_UNPACK *pack;
    /* Depth reads (ARB_depth_texture): one float32 depth or packed word per
     * pixel, of the targets that have depth images. */
    const BOOL depth_read =
        JglDepthTextureAvailable() && (format == 0x1902 || format == JGL_DEPTH_STENCIL);
    const BOOL depth_target =
        target == GL_TEXTURE_1D || target == GL_TEXTURE_2D || JglRectangleTarget(target);
    BOOL wide = !depth_read && (WideReadColor(type) ||
                                (target == GL_TEXTURE_1D && JglDepthStencilReadbackAvailable()));
    ULONG wire_bytes = wide ? 16 : 4,
          kind = wide || (depth_read && format == 0x1902) ? DG_GL_RESULT_FLOAT : DG_GL_RESULT_INT;
    if (!JglReady())
        return;
    /* TEXTURE_2D or, once negotiated, one cube map face, TEXTURE_3D or
     * TEXTURE_RECTANGLE (level zero only). */
    if (target != GL_TEXTURE_1D && !JglImage2DTarget(target) && !volume &&
        !JglRectangleTarget(target)) {
        JglSetError(GL_INVALID_ENUM);
        return;
    }
    pixel_bytes =
        depth_read ? DepthTexturePixelBytes(format, type) : ReadPixelBytes(format, type, TRUE);
    if (!pixel_bytes || pixel_bytes == (ULONG)-1)
        return;
    /* Cube faces and 3D levels have no depth images. */
    if (depth_read && !depth_target) {
        JglSetError(GL_INVALID_OPERATION);
        return;
    }
    if (wide && !JglDepthStencilReadbackAvailable()) {
        JglSetError(GL_INVALID_OPERATION);
        return;
    }
    if (level < 0 || level > DG_GL_MAX_TEXTURE_LEVEL || !pixels ||
        (level && JglRectangleTarget(target)))
        goto invalid;
    args[0] = target;
    args[1] = level;
    /* Bulk reads (DG_CAP2_GL_BULK_READ): one SHAPE query answers the
     * GetTexLevelParameteriv preamble below from the same native queries. */
    bulk = JglBulkCapacity();
    if (bulk) {
        args[1] = (ULONG)level | DG_GL_TEXTURE_READ_SHAPE;
        args[2] = 0;
        if (!JglQuery(FEnum_glGetTexImage, args, DG_GL_RESULT_INT, shape, sizeof(shape), &bytes))
            return;
        if (bytes != sizeof(shape)) {
            JglSetError(GL_INVALID_OPERATION);
            return;
        }
        width = (GLint)shape[0];
        if (target != GL_TEXTURE_1D)
            height = (GLint)shape[1];
        if (volume)
            depth = (GLint)shape[2];
        args[1] = level;
    }
    args[2] = GL_TEXTURE_WIDTH;
    if (!bulk && !JglQuery(FEnum_glGetTexLevelParameteriv, args, DG_GL_RESULT_INT, &width,
                           sizeof(width), &bytes))
        return;
    if (!bulk && target != GL_TEXTURE_1D) {
        args[2] = GL_TEXTURE_HEIGHT;
        if (!JglQuery(FEnum_glGetTexLevelParameteriv, args, DG_GL_RESULT_INT, &height,
                      sizeof(height), &bytes))
            return;
    }
    if (!bulk && volume) {
        args[2] = 0x8071; /* TEXTURE_DEPTH */
        if (!JglQuery(FEnum_glGetTexLevelParameteriv, args, DG_GL_RESULT_INT, &depth, sizeof(depth),
                      &bytes))
            return;
    }
    if (!width || !height || !depth)
        return;
    /* A depth read needs a depth level (and DEPTH_STENCIL a packed one), a
     * colour read a colour level: the host fails the batch on a mismatch,
     * GL reports INVALID_OPERATION (ARB_depth_texture). */
    if (JglDepthTextureAvailable() && depth_target) {
        GLint depth_size = bulk ? (GLint)shape[3] : 0, stencil_size = 0;
        args[2] = 0x884a; /* TEXTURE_DEPTH_SIZE */
        if (!bulk && !JglQuery(FEnum_glGetTexLevelParameteriv, args, DG_GL_RESULT_INT, &depth_size,
                               sizeof(depth_size), &bytes))
            return;
        if (bulk && depth_size > 0 && format == JGL_DEPTH_STENCIL)
            stencil_size = (GLint)shape[4];
        else if (depth_size > 0 && format == JGL_DEPTH_STENCIL) {
            args[2] = 0x88f1; /* TEXTURE_STENCIL_SIZE */
            if (!JglQuery(FEnum_glGetTexLevelParameteriv, args, DG_GL_RESULT_INT, &stencil_size,
                          sizeof(stencil_size), &bytes))
                return;
        }
        if ((depth_size > 0) != depth_read || (format == JGL_DEPTH_STENCIL && stencil_size <= 0)) {
            JglSetError(GL_INVALID_OPERATION);
            return;
        }
    }
    if (width < 0 || height < 0 || depth < 0 || width > DG_GL_MAX_TEXTURE_DIMENSION + 2 ||
        height > DG_GL_MAX_TEXTURE_DIMENSION + 2 ||
        (volume && (width > DG_GL_MAX_3D_TEXTURE_SIZE || height > DG_GL_MAX_3D_TEXTURE_SIZE ||
                    depth > DG_GL_MAX_3D_TEXTURE_SIZE)))
        goto invalid;
    pack = JglPack();
    if (!Multiply(pack->RowLength ? (ULONG_PTR)pack->RowLength : (ULONG_PTR)width, pixel_bytes,
                  &stride) ||
        !Add(stride, pack->Alignment - 1, &stride))
        goto invalid;
    stride &= ~((ULONG_PTR)pack->Alignment - 1);
    /* Images are PACK_IMAGE_HEIGHT (or height) rows apart, after
     * PACK_SKIP_IMAGES of them; other targets have one image. */
    images = volume && pack->ImageHeight ? (ULONG_PTR)pack->ImageHeight : (ULONG_PTR)height;
    if (!Multiply(images, stride, &image_stride) ||
        !Multiply(volume ? (ULONG_PTR)pack->SkipImages : 0, image_stride, &first) ||
        !Multiply(target == GL_TEXTURE_1D ? 0 : pack->SkipRows, stride, &skip) ||
        !Add(first, skip, &first) || !Multiply(pack->SkipPixels, pixel_bytes, &skip) ||
        !Add(first, skip, &first) || !Multiply((ULONG_PTR)depth - 1, image_stride, &span) ||
        !Add(first, span, &end) || !Multiply(height - 1, stride, &span) ||
        !Add(span, (ULONG_PTR)width * pixel_bytes, &span) || !Add(end, span, &end) ||
        !Add((ULONG_PTR)pixels, end, &end) || !Add((ULONG_PTR)pixels, first, &first))
        goto invalid;
    /* Unit-counted reads of the negotiated result size when it holds a unit;
     * otherwise contiguous-limit chunks copied into the readback buffer. */
    if (bulk)
        per_query = bulk / (wire_bytes * DG_GL_TEXTURE_READ_UNIT) * DG_GL_TEXTURE_READ_UNIT;
    capacity = JglReadbackCapacity();
    if (!per_query) {
        rgba = JglReadbackBuffer();
        if (!rgba)
            return;
    }
    fast = depth_read ? FAST_NONE : FastRead(format, type, pack->SwapBytes, wide);
    total = (ULONG)width * height * depth;
    args[2] = 0;
    while (at < total) {
        ULONG count = total - at, requested;
        const ULONG *source = rgba;
        args[1] = (ULONG)level | (wide ? DG_GL_TEXTURE_READ_FLOAT : 0) |
                  (depth_read ? DG_GL_TEXTURE_READ_DEPTH : 0) |
                  (depth_read && format == JGL_DEPTH_STENCIL ? DG_GL_TEXTURE_READ_FLOAT : 0);
        args[2] = at;
        if (per_query) {
            /* Whole units; the host zero-fills past the level end. */
            if (count > per_query)
                count = per_query;
            const ULONG unit_count =
                (count + DG_GL_TEXTURE_READ_UNIT - 1) / DG_GL_TEXTURE_READ_UNIT;
            args[1] |= DG_GL_TEXTURE_READ_UNITS | unit_count << DG_GL_TEXTURE_READ_COUNT_SHIFT;
            requested = unit_count * DG_GL_TEXTURE_READ_UNIT * wire_bytes;
            source =
                (const ULONG *)JglQueryBulk(FEnum_glGetTexImage, args, kind, requested, &bytes);
            if (!source)
                return;
        } else {
            if (count > capacity / wire_bytes)
                count = capacity / wire_bytes;
            requested = DG_GL_MAX_RESULT_BYTES;
            if (wide || capacity > DG_GL_MAX_RESULT_BYTES) {
                args[1] |= count << DG_GL_TEXTURE_READ_COUNT_SHIFT;
                requested = count * wire_bytes;
            }
            if (!JglQuery(FEnum_glGetTexImage, args, kind, rgba, requested, &bytes))
                return;
        }
        if (bytes != requested) {
            JglSetError(GL_INVALID_OPERATION);
            return;
        }
        /* Row runs of the level image (rows of every image for 3D). */
        for (ULONG done = 0; done < count;) {
            const ULONG row = (at + done) / (ULONG)width, column = (at + done) % (ULONG)width;
            ULONG run = (ULONG)width - column, i;
            BYTE *dest = (BYTE *)first + row / (ULONG)height * image_stride +
                         row % (ULONG)height * stride + column * pixel_bytes;
            if (run > count - done)
                run = count - done;
            if (depth_read && format == JGL_DEPTH_STENCIL) {
                for (i = 0; i < run; ++i) /* the packed word */
                    StoreReadWord(dest + i * 4, source[done + i], 4, pack->SwapBytes);
            } else
                StoreReadRow(dest, source + done * (wire_bytes / 4), run, pixel_bytes,
                             wire_bytes / 4, fast, format, type, pack->SwapBytes, TRUE, wide);
            done += run;
        }
        at += count;
    }
    return;
invalid:
    JglSetError(GL_INVALID_VALUE);
}

} /* extern C */
