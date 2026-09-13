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
void APIENTRY glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format,
                           GLenum type, void *pixels) {
    JGL_UNPACK *pack;
    ULONG components, row, column, columns, rows, i, j, args[3], bytes, drawable_width,
        drawable_height, tile_width;
    ULONG *rgba, capacity;
    ULONG_PTR stride, skip, first, span, end;
    BYTE *output;
    BOOL reverse, swap = type == 0x8367;
    if (!JglReady())
        return;
    /* x86 8_8_8_8_REV words have the same component bytes as UBYTE. */
    if (type == 0x8367) {
        if (format != GL_RGBA && format != 0x80e1) {
            JglSetError(GL_INVALID_OPERATION);
            return;
        }
        type = GL_UNSIGNED_BYTE;
    }
    reverse = format == 0x80e0 || format == 0x80e1; /* BGR/BGRA */
    components = format == GL_RGB || format == 0x80e0    ? 3
                 : format == GL_RGBA || format == 0x80e1 ? 4
                                                         : 0;
    if (!components || type != GL_UNSIGNED_BYTE) {
        JglSetError(GL_INVALID_ENUM);
        return;
    }
    if (width < 0 || height < 0 || width > DG_GL_MAX_DIMENSION || height > DG_GL_MAX_DIMENSION ||
        (int64_t)x + width > INT32_MAX || (int64_t)y + height > INT32_MAX)
        goto invalid;
    if (!width || !height)
        return;
    if (!pixels)
        goto invalid;
    pack = JglPack();
    swap = swap && pack->SwapBytes;
    if (!Multiply(pack->RowLength ? (ULONG_PTR)pack->RowLength : (ULONG_PTR)width, components,
                  &stride) ||
        !Add(stride, pack->Alignment - 1, &stride))
        goto invalid;
    stride &= ~((ULONG_PTR)pack->Alignment - 1);
    if (!Multiply(pack->SkipRows, stride, &first) ||
        !Multiply(pack->SkipPixels, components, &skip) || !Add(first, skip, &first) ||
        !Multiply(height - 1, stride, &span) || !Add(span, (ULONG_PTR)width * components, &span) ||
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
                    BYTE *dest = output + (row + j) * stride + (column + i) * components;
                    if (clipped_width && clipped_height && left + i >= clipped_left &&
                        left + i < clipped_right && bottom + j >= clipped_bottom &&
                        bottom + j < clipped_top)
                        pixel = rgba[(ULONG)(bottom + j - clipped_bottom) * clipped_width +
                                     (ULONG)(left + i - clipped_left)];
                    dest[0] = (BYTE)(pixel >> (reverse ? 16 : 0));
                    dest[1] = (BYTE)(pixel >> 8);
                    dest[2] = (BYTE)(pixel >> (reverse ? 0 : 16));
                    if (components == 4)
                        dest[3] = (BYTE)(pixel >> 24);
                    if (swap) {
                        BYTE temp = dest[0];
                        dest[0] = dest[3];
                        dest[3] = temp;
                        temp = dest[1];
                        dest[1] = dest[2];
                        dest[2] = temp;
                    }
                }
        }
    }
    return;
invalid:
    JglSetError(GL_INVALID_VALUE);
}

void APIENTRY glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, void *pixels) {
    ULONG args[3], bytes, components, row, column, i, at = 0, total;
    ULONG *rgba, capacity;
    GLint width = 0, height = 1;
    ULONG_PTR stride, skip, first, span, end;
    JGL_UNPACK *pack;
    BOOL reverse, swap = type == 0x8367;
    if (!JglReady())
        return;
    /* x86 8_8_8_8_REV words have the same component bytes as UBYTE. */
    if (type == 0x8367) {
        if (format != GL_RGBA && format != 0x80e1) {
            JglSetError(GL_INVALID_OPERATION);
            return;
        }
        type = GL_UNSIGNED_BYTE;
    }
    reverse = format == 0x80e0 || format == 0x80e1;
    components = format == GL_ALPHA || format == GL_LUMINANCE ? 1
                 : format == GL_LUMINANCE_ALPHA               ? 2
                 : format == GL_RGB || format == 0x80e0       ? 3
                 : format == GL_RGBA || format == 0x80e1      ? 4
                                                              : 0;
    if ((target != GL_TEXTURE_1D && target != GL_TEXTURE_2D) || !components ||
        type != GL_UNSIGNED_BYTE) {
        JglSetError(GL_INVALID_ENUM);
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
    swap = swap && pack->SwapBytes;
    if (!Multiply(pack->RowLength ? (ULONG_PTR)pack->RowLength : (ULONG_PTR)width, components,
                  &stride) ||
        !Add(stride, pack->Alignment - 1, &stride))
        goto invalid;
    stride &= ~((ULONG_PTR)pack->Alignment - 1);
    if (!Multiply(target == GL_TEXTURE_1D ? 0 : pack->SkipRows, stride, &first) ||
        !Multiply(pack->SkipPixels, components, &skip) || !Add(first, skip, &first) ||
        !Multiply(height - 1, stride, &span) || !Add(span, (ULONG_PTR)width * components, &span) ||
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
            dest = (BYTE *)first + row * stride + column * components;
            if (format == GL_ALPHA)
                dest[0] = (BYTE)(rgba[i] >> 24);
            else {
                dest[0] = (BYTE)(rgba[i] >> (reverse ? 16 : 0));
                if (format == GL_LUMINANCE_ALPHA)
                    dest[1] = (BYTE)(rgba[i] >> 24);
                else if (components >= 3) {
                    dest[1] = (BYTE)(rgba[i] >> 8);
                    dest[2] = (BYTE)(rgba[i] >> (reverse ? 0 : 16));
                    if (components == 4)
                        dest[3] = (BYTE)(rgba[i] >> 24);
                    if (swap) {
                        BYTE temp = dest[0];
                        dest[0] = dest[3];
                        dest[3] = temp;
                        temp = dest[1];
                        dest[1] = dest[2];
                        dest[2] = temp;
                    }
                }
            }
        }
        at += count;
    }
    return;
invalid:
    JglSetError(GL_INVALID_VALUE);
}

} /* extern C */
