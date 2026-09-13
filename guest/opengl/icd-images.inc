/* SPDX-License-Identifier: GPL-2.0-or-later
 * GL 1.1 sections 3.6.3 and 3.7: one immutable image, one native draw.
 * Source semantics: https://registry.khronos.org/OpenGL/specs/gl/glspec11.pdf
 * Transport chunks never draw independently or advance the raster position.
 */
namespace dg_icd_images {
constexpr ULONG ImageLimit = 64UL * 1024 * 1024;
constexpr ULONG First = 1, Last = 2;
struct Layout {
    const BYTE *Source;
    ULONG_PTR Stride, Skip;
    ULONG Row, Total, Unit;
    bool Bits, Swap, Lsb;
};
static bool Add(ULONG_PTR a, ULONG_PTR b, ULONG_PTR *out) {
    if (a > ~(ULONG_PTR)0 - b)
        return false;
    *out = a + b;
    return true;
}
static bool Multiply(ULONG_PTR a, ULONG_PTR b, ULONG_PTR *out) {
    if (b && a > ~(ULONG_PTR)0 / b)
        return false;
    *out = a * b;
    return true;
}
static bool Describe(GLsizei width, GLsizei height, GLenum format, GLenum type, const void *pixels,
                     Layout *out) {
    ULONG components;
    switch (format) {
        case GL_COLOR_INDEX:
        case GL_STENCIL_INDEX:
        case GL_DEPTH_COMPONENT:
        case GL_RED:
        case GL_GREEN:
        case GL_BLUE:
        case GL_ALPHA:
        case GL_LUMINANCE:
            components = 1;
            break;
        case GL_LUMINANCE_ALPHA:
            components = 2;
            break;
        case GL_RGB:
            components = 3;
            break;
        case GL_RGBA:
            components = 4;
            break;
        default:
            JglSetError(GL_INVALID_ENUM);
            return false;
    }
    ULONG unit;
    switch (type) {
        case GL_BITMAP:
            unit = 0;
            break;
        case GL_BYTE:
        case GL_UNSIGNED_BYTE:
            unit = 1;
            break;
        case GL_SHORT:
        case GL_UNSIGNED_SHORT:
            unit = 2;
            break;
        case GL_INT:
        case GL_UNSIGNED_INT:
        case GL_FLOAT:
            unit = 4;
            break;
        default:
            JglSetError(GL_INVALID_ENUM);
            return false;
    }
    if (!unit && format != GL_COLOR_INDEX && format != GL_STENCIL_INDEX) {
        JglSetError(GL_INVALID_ENUM);
        return false;
    }
    if (width < 0 || height < 0) {
        JglSetError(GL_INVALID_VALUE);
        return false;
    }
    *out = {};
    out->Bits = !unit;
    out->Unit = unit;
    if (!width || !height)
        return true;
    if (!pixels) {
        JglSetError(GL_INVALID_VALUE);
        return false;
    }
    const JGL_UNPACK *u = JglUnpack();
    if (!u)
        return false;
    ULONG_PTR row, stride, skip, first, end, span;
    const ULONG_PTR length = u->RowLength ? (ULONG_PTR)u->RowLength : (ULONG_PTR)width;
    const ULONG_PTR per = unit * components;
    if (u->RowLength < 0 || u->SkipRows < 0 || u->SkipPixels < 0 ||
        (u->Alignment != 1 && u->Alignment != 2 && u->Alignment != 4 && u->Alignment != 8))
        goto invalid;
    if (unit) {
        if (!Multiply((ULONG_PTR)width, per, &row) || !Multiply(length, per, &stride) ||
            !Multiply((ULONG_PTR)u->SkipPixels, per, &skip))
            goto invalid;
    } else {
        if (!Add((ULONG_PTR)width, 7, &row) || !Add(length, 7, &stride))
            goto invalid;
        row /= 8;
        stride /= 8;
        skip = (ULONG_PTR)u->SkipPixels / 8;
    }
    if (row > ImageLimit || (ULONG_PTR)height > ImageLimit / row) {
        JglSetError(GL_OUT_OF_MEMORY);
        return false;
    }
    if (!Add(stride, (ULONG_PTR)u->Alignment - 1, &stride))
        goto invalid;
    stride &= ~((ULONG_PTR)u->Alignment - 1);
    if (!Multiply((ULONG_PTR)u->SkipRows, stride, &first) || !Add(first, skip, &first) ||
        !Multiply((ULONG_PTR)height - 1, stride, &span))
        goto invalid;
    if (unit) {
        if (!Add(span, row, &span))
            goto invalid;
    } else {
        ULONG_PTR touched;
        if (!Add((ULONG_PTR)width, ((ULONG_PTR)u->SkipPixels & 7) + 7, &touched) ||
            !Add(span, touched / 8, &span))
            goto invalid;
    }
    if (!Add(first, span, &end) || !Add((ULONG_PTR)pixels, end - 1, &end))
        goto invalid;
    out->Source = (const BYTE *)pixels + first;
    out->Stride = stride;
    out->Skip = (ULONG_PTR)u->SkipPixels & 7;
    out->Row = (ULONG)row;
    out->Total = (ULONG)row * (ULONG)height;
    out->Swap = u->SwapBytes != 0;
    out->Lsb = u->LsbFirst != 0;
    return true;
invalid:
    JglSetError(GL_INVALID_VALUE);
    return false;
}
static void Pack(const Layout &l, ULONG width, ULONG offset, BYTE *output, ULONG bytes) {
    while (bytes) {
        ULONG row = offset / l.Row, column = offset % l.Row;
        ULONG count = l.Row - column;
        if (count > bytes)
            count = bytes;
        const BYTE *source = l.Source + (ULONG_PTR)row * l.Stride;
        if (!l.Bits && (!l.Swap || l.Unit == 1))
            CopyMemory(output, source + column, count);
        else
            for (ULONG n = 0; n < count; ++n) {
                const ULONG current = column + n;
                if (l.Bits) {
                    BYTE value = 0;
                    for (ULONG bit = 0; bit < 8 && current * 8 + bit < width; ++bit) {
                        ULONG_PTR index = l.Skip + current * 8 + bit;
                        ULONG shift = l.Lsb ? (ULONG)(index & 7) : 7 - (ULONG)(index & 7);
                        value |= ((source[index / 8] >> shift) & 1) << (7 - bit);
                    }
                    output[n] = value;
                } else {
                    ULONG index = current - current % l.Unit + l.Unit - 1 - current % l.Unit;
                    output[n] = source[index];
                }
            }
        offset += count;
        output += count;
        bytes -= count;
    }
}
static void Draw(ULONG function, GLsizei width, GLsizei height, GLenum format, GLenum type,
                 const void *pixels, const ULONG *bitmap) {
    if (!JglReady())
        return;
    Layout layout;
    if (!Describe(width, height, format, type, pixels, &layout))
        return;
    /* Native DrawPixels expands bitmap indices to exact 0/1 bytes before its
     * one draw: Mesa's direct stencil BITMAP path can hang. Charge that actual
     * staging allocation, while retaining packed bits in the wire descriptor.
     * glBitmap stays packed and does not need this additional budget. */
    if (function == FEnum_glDrawPixels && type == GL_BITMAP && width > 0 && height > 0 &&
        (ULONG)width > ImageLimit / (ULONG)height) {
        JglSetError(GL_OUT_OF_MEMORY);
        return;
    }
    JGL_ARRAY_STATE *arrays = JglArrays();
    if (!arrays)
        return;
    ULONG capacity = JglMaxDataBytes(8);
    if (capacity > sizeof(arrays->Scratch))
        capacity = sizeof(arrays->Scratch);
    if (capacity <= 16) {
        JglSetError(GL_OUT_OF_MEMORY);
        return;
    }
    ULONG id = JglNextImageId();
    if (!id)
        return;
    ULONG offset = 0;
    do {
        ULONG prefix = bitmap && !offset ? 16 : 0;
        ULONG bytes = layout.Total - offset;
        if (bytes > capacity - prefix)
            bytes = capacity - prefix;
        ULONG flags = !offset ? First : 0;
        if (offset + bytes == layout.Total)
            flags |= Last;
        if (prefix)
            CopyMemory(arrays->Scratch, bitmap, prefix);
        if (bytes)
            Pack(layout, (ULONG)width, offset, arrays->Scratch + prefix, bytes);
        ULONG args[] = {(ULONG)width, (ULONG)height, format, type, layout.Total, offset, flags, id};
        if (!JglData(function, args, 8, arrays->Scratch, prefix + bytes))
            return;
        offset += bytes;
    } while (offset < layout.Total);
}
} // namespace dg_icd_images
static void APIENTRY AliasBitmap(GLsizei width, GLsizei height, GLfloat xorig, GLfloat yorig,
                                 GLfloat xmove, GLfloat ymove, const GLubyte *bitmap) {
    ULONG args[4];
    CopyMemory(args, &xorig, 4);
    CopyMemory(args + 1, &yorig, 4);
    CopyMemory(args + 2, &xmove, 4);
    CopyMemory(args + 3, &ymove, 4);
    dg_icd_images::Draw(FEnum_glBitmap, width, height, GL_COLOR_INDEX, GL_BITMAP, bitmap, args);
}
static void APIENTRY AliasDrawPixels(GLsizei width, GLsizei height, GLenum format, GLenum type,
                                     const GLvoid *pixels) {
    dg_icd_images::Draw(FEnum_glDrawPixels, width, height, format, type, pixels, nullptr);
}
