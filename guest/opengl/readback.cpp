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
    switch (type) {
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
void APIENTRY glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format,
                           GLenum type, void *pixels) {
    JGL_UNPACK *pack;
    ULONG pixel_bytes, row, column, columns, rows, i, j, args[3], bytes, drawable_width,
        drawable_height, tile_width;
    ULONG *rgba, capacity;
    ULONG_PTR stride, skip, first, span, end;
    BYTE *output;
    ULONG mode = 0, kind = DG_GL_RESULT_INT, wire_bytes = 4;
    BOOL wide = FALSE, bitmap = type == 0x1a00;
    if (!JglReady())
        return;
    pixel_bytes = ReadPixelBytes(format, type, FALSE);
    if (!pixel_bytes)
        return;
    if (format == 0x1902 || format == 0x1901) {
        if (!JglDepthStencilReadbackAvailable()) {
            JglSetError(GL_INVALID_OPERATION);
            return;
        }
        mode = format == 0x1902 ? DG_GL_READ_DEPTH : DG_GL_READ_STENCIL;
        kind = format == 0x1902 ? DG_GL_RESULT_FLOAT : DG_GL_RESULT_INT;
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
    JglDrawableSize(&drawable_width, &drawable_height);
    capacity = JglReadbackCapacity();
    rgba = JglReadbackBuffer();
    if (!rgba)
        return;
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
                args[0] = (ULONG)clipped_left | mode;
                args[1] = (ULONG)clipped_bottom;
                args[2] = clipped_width | (clipped_height << 16);
                if (!JglQuery(FEnum_glReadPixels, args, kind, rgba,
                              clipped_width * clipped_height * wire_bytes, &bytes))
                    return;
                if (bytes != clipped_width * clipped_height * wire_bytes) {
                    JglSetError(GL_INVALID_OPERATION);
                    return;
                }
            }
            for (j = 0; j < rows; ++j)
                for (i = 0; i < columns; ++i) {
                    ULONG zero[4] = {0, 0, 0, 0};
                    const ULONG *pixel = zero;
                    BYTE *dest = output + (row + j) * stride;
                    if (clipped_width && clipped_height && left + i >= clipped_left &&
                        left + i < clipped_right && bottom + j >= clipped_bottom &&
                        bottom + j < clipped_top)
                        pixel = rgba + ((ULONG)(bottom + j - clipped_bottom) * clipped_width +
                                        (ULONG)(left + i - clipped_left)) *
                                           (wire_bytes / 4);
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
    return;
invalid:
    JglSetError(GL_INVALID_VALUE);
}

void APIENTRY glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, void *pixels) {
    ULONG args[3], bytes, pixel_bytes, row, column, i, at = 0, total;
    ULONG *rgba, capacity;
    GLint width = 0, height = 1;
    ULONG_PTR stride, skip, first, span, end;
    JGL_UNPACK *pack;
    BOOL wide =
        WideReadColor(type) || (target == GL_TEXTURE_1D && JglDepthStencilReadbackAvailable());
    ULONG wire_bytes = wide ? 16 : 4, kind = wide ? DG_GL_RESULT_FLOAT : DG_GL_RESULT_INT;
    if (!JglReady())
        return;
    if (target != GL_TEXTURE_1D && target != GL_TEXTURE_2D) {
        JglSetError(GL_INVALID_ENUM);
        return;
    }
    pixel_bytes = ReadPixelBytes(format, type, TRUE);
    if (!pixel_bytes)
        return;
    if (wide && !JglDepthStencilReadbackAvailable()) {
        JglSetError(GL_INVALID_OPERATION);
        return;
    }
    if (level < 0 || level > DG_GL_MAX_TEXTURE_LEVEL || !pixels)
        goto invalid;
    args[0] = target;
    args[1] = level;
    args[2] = GL_TEXTURE_WIDTH;
    if (!JglQuery(FEnum_glGetTexLevelParameteriv, args, DG_GL_RESULT_INT, &width, sizeof(width),
                  &bytes))
        return;
    if (target == GL_TEXTURE_2D) {
        args[2] = GL_TEXTURE_HEIGHT;
        if (!JglQuery(FEnum_glGetTexLevelParameteriv, args, DG_GL_RESULT_INT, &height,
                      sizeof(height), &bytes))
            return;
    }
    if (!width || !height)
        return;
    if (width < 0 || height < 0 || width > DG_GL_MAX_TEXTURE_DIMENSION ||
        height > DG_GL_MAX_TEXTURE_DIMENSION)
        goto invalid;
    pack = JglPack();
    if (!Multiply(pack->RowLength ? (ULONG_PTR)pack->RowLength : (ULONG_PTR)width, pixel_bytes,
                  &stride) ||
        !Add(stride, pack->Alignment - 1, &stride))
        goto invalid;
    stride &= ~((ULONG_PTR)pack->Alignment - 1);
    if (!Multiply(target == GL_TEXTURE_1D ? 0 : pack->SkipRows, stride, &first) ||
        !Multiply(pack->SkipPixels, pixel_bytes, &skip) || !Add(first, skip, &first) ||
        !Multiply(height - 1, stride, &span) || !Add(span, (ULONG_PTR)width * pixel_bytes, &span) ||
        !Add(first, span, &end) || !Add((ULONG_PTR)pixels, end, &end) ||
        !Add((ULONG_PTR)pixels, first, &first))
        goto invalid;
    capacity = JglReadbackCapacity();
    rgba = JglReadbackBuffer();
    if (!rgba)
        return;
    total = (ULONG)width * height;
    args[2] = 0;
    while (at < total) {
        ULONG count = total - at, requested;
        if (count > capacity / wire_bytes)
            count = capacity / wire_bytes;
        args[1] = (ULONG)level | (wide ? DG_GL_TEXTURE_READ_FLOAT : 0);
        requested = DG_GL_MAX_RESULT_BYTES;
        if (wide || capacity > DG_GL_MAX_RESULT_BYTES) {
            args[1] |= count << DG_GL_TEXTURE_READ_COUNT_SHIFT;
            requested = count * wire_bytes;
        }
        args[2] = at;
        if (!JglQuery(FEnum_glGetTexImage, args, kind, rgba, requested, &bytes))
            return;
        if (bytes != requested) {
            JglSetError(GL_INVALID_OPERATION);
            return;
        }
        for (i = 0; i < count; ++i) {
            BYTE *dest;
            row = (at + i) / (ULONG)width;
            column = (at + i) % (ULONG)width;
            dest = (BYTE *)first + row * stride + column * pixel_bytes;
            StoreReadPixel(dest, rgba + i * (wire_bytes / 4), format, type, pack->SwapBytes, TRUE,
                           wide);
        }
        at += count;
    }
    return;
invalid:
    JglSetError(GL_INVALID_VALUE);
}

} /* extern C */
