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
/* Readback transport is canonical RGBA8. Convert to the same packed formats
 * accepted by texture uploads, without unaligned word stores in x86 callers. */
static ULONG ReadPixelBytes(GLenum format, GLenum type, BOOL texture) {
    ULONG components = format == GL_RGB || format == 0x80e0                        ? 3
                       : format == GL_RGBA || format == 0x80e1                     ? 4
                       : texture && (format == GL_ALPHA || format == GL_LUMINANCE) ? 1
                       : texture && format == GL_LUMINANCE_ALPHA                   ? 2
                                                                                   : 0;
    if (!components) {
        JglSetError(GL_INVALID_ENUM);
        return 0;
    }
    if (type == GL_UNSIGNED_BYTE)
        return components;
    switch (type) {
        case 0x8363: /* UNSIGNED_SHORT_5_6_5 */
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
        case 0x8367: /* UNSIGNED_INT_8_8_8_8_REV */
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
static void StoreReadPixel(BYTE *dest, ULONG rgba, GLenum format, GLenum type, BOOL swap) {
    ULONG r = rgba & 255, g = (rgba >> 8) & 255, b = (rgba >> 16) & 255, a = rgba >> 24;
    ULONG value, bytes = 2, i;
    if (format == 0x80e0 || format == 0x80e1) {
        ULONG tmp = r;
        r = b;
        b = tmp;
    }
    if (type == GL_UNSIGNED_BYTE) {
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
void APIENTRY glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format,
                           GLenum type, void *pixels) {
    JGL_UNPACK *pack;
    ULONG pixel_bytes, row, column, columns, rows, i, j, args[3], bytes, drawable_width,
        drawable_height, tile_width;
    ULONG *rgba, capacity;
    ULONG_PTR stride, skip, first, span, end;
    BYTE *output;
    if (!JglReady())
        return;
    pixel_bytes = ReadPixelBytes(format, type, FALSE);
    if (!pixel_bytes)
        return;
    if (width < 0 || height < 0 || width > DG_GL_MAX_DIMENSION || height > DG_GL_MAX_DIMENSION ||
        (int64_t)x + width > INT32_MAX || (int64_t)y + height > INT32_MAX)
        goto invalid;
    if (!width || !height)
        return;
    if (!pixels)
        goto invalid;
    pack = JglPack();
    if (!Multiply(pack->RowLength ? (ULONG_PTR)pack->RowLength : (ULONG_PTR)width, pixel_bytes,
                  &stride) ||
        !Add(stride, pack->Alignment - 1, &stride))
        goto invalid;
    stride &= ~((ULONG_PTR)pack->Alignment - 1);
    if (!Multiply(pack->SkipRows, stride, &first) ||
        !Multiply(pack->SkipPixels, pixel_bytes, &skip) || !Add(first, skip, &first) ||
        !Multiply(height - 1, stride, &span) || !Add(span, (ULONG_PTR)width * pixel_bytes, &span) ||
        !Add(first, span, &end) || !Add((ULONG_PTR)pixels, end, &end))
        goto invalid;
    output = (BYTE *)pixels + first;
    JglDrawableSize(&drawable_width, &drawable_height);
    capacity = JglReadbackCapacity();
    rgba = JglReadbackBuffer();
    if (!rgba)
        return;
    tile_width = (ULONG)width < capacity / 4 ? (ULONG)width : capacity / 4;
    for (row = 0; row < (ULONG)height; row += rows) {
        rows = capacity / 4 / tile_width;
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
                args[0] = (ULONG)clipped_left;
                args[1] = (ULONG)clipped_bottom;
                args[2] = clipped_width | (clipped_height << 16);
                if (!JglQuery(FEnum_glReadPixels, args, DG_GL_RESULT_INT, rgba,
                              clipped_width * clipped_height * 4, &bytes))
                    return;
                if (bytes != clipped_width * clipped_height * 4) {
                    JglSetError(GL_INVALID_OPERATION);
                    return;
                }
            }
            for (j = 0; j < rows; ++j)
                for (i = 0; i < columns; ++i) {
                    ULONG pixel = 0;
                    BYTE *dest = output + (row + j) * stride + (column + i) * pixel_bytes;
                    if (clipped_width && clipped_height && left + i >= clipped_left &&
                        left + i < clipped_right && bottom + j >= clipped_bottom &&
                        bottom + j < clipped_top)
                        pixel = rgba[(ULONG)(bottom + j - clipped_bottom) * clipped_width +
                                     (ULONG)(left + i - clipped_left)];
                    StoreReadPixel(dest, pixel, format, type, pack->SwapBytes);
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
    if (!JglReady())
        return;
    if (target != GL_TEXTURE_1D && target != GL_TEXTURE_2D) {
        JglSetError(GL_INVALID_ENUM);
        return;
    }
    pixel_bytes = ReadPixelBytes(format, type, TRUE);
    if (!pixel_bytes)
        return;
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
        if (count > capacity / 4)
            count = capacity / 4;
        args[1] = (ULONG)level;
        requested = DG_GL_MAX_RESULT_BYTES;
        if (capacity > DG_GL_MAX_RESULT_BYTES) {
            args[1] |= count << DG_GL_TEXTURE_READ_COUNT_SHIFT;
            requested = count * 4;
        }
        args[2] = at;
        if (!JglQuery(FEnum_glGetTexImage, args, DG_GL_RESULT_INT, rgba, requested, &bytes))
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
            StoreReadPixel(dest, rgba[i], format, type, pack->SwapBytes);
        }
        at += count;
    }
    return;
invalid:
    JglSetError(GL_INVALID_VALUE);
}

} /* extern C */
