/* SPDX-License-Identifier: GPL-2.0-or-later
 * Capture unpacked pixel rows into bounded immutable transport records.
 * JglData copies each tile synchronously; no caller pointer survives a call.
 */
#include "internal.h"

extern "C" {

typedef struct {
    const unsigned char *First;
    ULONG_PTR Stride;
    ULONG RowBytes;
    ULONG BitOffset;
} JGL_PIXEL_LAYOUT;

static BOOL Add(ULONG_PTR a, ULONG_PTR b, ULONG_PTR *result) {
    if (a > (ULONG_PTR)-1 - b)
        return FALSE;
    *result = a + b;
    return TRUE;
}

static BOOL Multiply(ULONG_PTR a, ULONG_PTR b, ULONG_PTR *result) {
    if (b && a > (ULONG_PTR)-1 / b)
        return FALSE;
    *result = a * b;
    return TRUE;
}

static ULONG Components(GLenum format) {
    switch (format) {
        case 0x1900: /* COLOR_INDEX */
        case 0x1903: /* RED */
        case 0x1904: /* GREEN */
        case 0x1905: /* BLUE */
        case GL_ALPHA:
        case GL_LUMINANCE:
            return 1;
        case GL_LUMINANCE_ALPHA:
            return 2;
        case GL_RGB:
        case 0x80e0:
            return 3; /* BGR */
        case GL_RGBA:
        case 0x80e1:
            return 4; /* BGRA */
        default:
            return 0;
    }
}

/* Preserve scalar components until native pixel transfer: normalizing to
 * RGBA8 here loses precision and changes index maps, scale and bias. */
static ULONG ScalarBytes(GLenum type) {
    switch (type) {
        case 0x1400: /* BYTE */
        case GL_UNSIGNED_BYTE:
            return 1;
        case 0x1402: /* SHORT */
        case 0x1403: /* UNSIGNED_SHORT */
            return 2;
        case 0x1404: /* INT */
        case 0x1405: /* UNSIGNED_INT */
        case 0x1406: /* FLOAT */
            return 4;
        default:
            return 0;
    }
}

/* Only these packed inputs are exposed; no packed-pixel extension claim. */
static ULONG PixelBytes(GLenum format, GLenum type, BOOL immediate = FALSE) {
    void (*error)(GLenum) = immediate ? JglSetError : JglCommandError;
    if (((ScalarBytes(type) && type != GL_UNSIGNED_BYTE) || type == 0x1a00 || format == 0x1900 ||
         (format >= 0x1903 && format <= 0x1905)) &&
        !JglTextureImagesAvailable()) {
        error(GL_INVALID_OPERATION);
        return 0;
    }
    if (ScalarBytes(type) || type == 0x1a00 /* BITMAP */) {
        ULONG bytes = Components(format);
        if (type == 0x1a00 && format != 0x1900) {
            error(bytes ? GL_INVALID_OPERATION : GL_INVALID_ENUM);
            return 0;
        }
        if (!bytes)
            error(GL_INVALID_ENUM);
        return bytes * (type == 0x1a00 ? 1 : ScalarBytes(type));
    }
    switch (type) {
        case 0x8367: /* UNSIGNED_INT_8_8_8_8_REV with byte swapping */
            if (format == GL_RGBA || format == 0x80e1)
                return 4;
            break;
        case 0x8032: /* UNSIGNED_BYTE_3_3_2 */
            if (format == GL_RGB)
                return 1;
            break;
        case 0x8363: /* UNSIGNED_SHORT_5_6_5 */
            if (format == GL_RGB)
                return 2;
            break;
        case 0x8033: /* UNSIGNED_SHORT_4_4_4_4 */
        case 0x8034: /* UNSIGNED_SHORT_5_5_5_1 */
        case 0x8365: /* UNSIGNED_SHORT_4_4_4_4_REV */
        case 0x8366: /* UNSIGNED_SHORT_1_5_5_5_REV */
            if (format == GL_RGBA || format == 0x80e1)
                return 2;
            break;
        default:
            error(GL_INVALID_ENUM);
            return 0;
    }
    error(Components(format) ? GL_INVALID_OPERATION : GL_INVALID_ENUM);
    return 0;
}

static BOOL Packed16(GLenum type) {
    return type == 0x8363 || type == 0x8033 || type == 0x8034 || type == 0x8365 || type == 0x8366;
}

static BOOL DirectPixels(GLenum type) {
    return ScalarBytes(type) == 1 ||
           ((ScalarBytes(type) || Packed16(type)) && !JglUnpack()->SwapBytes);
}

static ULONG WirePixelBytes(GLenum type, ULONG components) {
    return ScalarBytes(type) || Packed16(type) || type == 0x1a00 ? components : 4;
}

static GLenum WireFormat(GLenum format, GLenum type) {
    return ScalarBytes(type) || Packed16(type) || type == 0x1a00 ? format : GL_RGBA;
}
static GLenum WireType(GLenum type) {
    return ScalarBytes(type) || Packed16(type) ? type : GL_UNSIGNED_BYTE;
}

static unsigned char Expand(ULONG value, ULONG maximum) {
    return (unsigned char)((value * 255 + maximum / 2) / maximum);
}

static void ConvertPacked(unsigned char *out, const unsigned char *in, ULONG count, GLenum format,
                          GLenum type) {
    ULONG i, value, a, b, c, d, j, bytes = type == 0x8032 ? 1 : type == 0x8367 ? 4 : 2;
    BOOL swap = JglUnpack()->SwapBytes;
    for (i = 0; i < count; ++i) {
        /* x86 guest words may be unaligned. Never read through a ushort pointer. */
        value = 0;
        for (j = 0; j < bytes; ++j)
            value |= (ULONG)in[swap ? bytes - 1 - j : j] << (j * 8);
        switch (type) {
            case 0x8367:
                a = value & 255;
                b = (value >> 8) & 255;
                c = (value >> 16) & 255;
                d = value >> 24;
                break;
            case 0x8032:
                a = Expand(value >> 5, 7);
                b = Expand((value >> 2) & 7, 7);
                c = Expand(value & 3, 3);
                d = 255;
                break;
            case 0x8363:
                a = Expand(value >> 11, 31);
                b = Expand((value >> 5) & 63, 63);
                c = Expand(value & 31, 31);
                d = 255;
                break;
            case 0x8033:
                a = (value >> 12) * 17;
                b = ((value >> 8) & 15) * 17;
                c = ((value >> 4) & 15) * 17;
                d = (value & 15) * 17;
                break;
            case 0x8034:
                a = Expand(value >> 11, 31);
                b = Expand((value >> 6) & 31, 31);
                c = Expand((value >> 1) & 31, 31);
                d = (value & 1) * 255;
                break;
            case 0x8365:
                a = (value & 15) * 17;
                b = ((value >> 4) & 15) * 17;
                c = ((value >> 8) & 15) * 17;
                d = (value >> 12) * 17;
                break;
            default: /* 1_5_5_5_REV */
                a = Expand(value & 31, 31);
                b = Expand((value >> 5) & 31, 31);
                c = Expand((value >> 10) & 31, 31);
                d = (value >> 15) * 255;
                break;
        }
        out[0] = (unsigned char)(format == 0x80e1 ? c : a);
        out[1] = (unsigned char)b;
        out[2] = (unsigned char)(format == 0x80e1 ? a : c);
        out[3] = (unsigned char)d;
        in += bytes;
        out += 4;
    }
}

/* On the x86 guest 8_8_8_8_REV has exactly unsigned-byte component order. */
static BOOL NormalizeType(GLenum format, GLenum *type) {
    if (*type != 0x8367)
        return TRUE;
    if (format != GL_RGBA && format != 0x80e1) {
        JglCommandError(Components(format) ? GL_INVALID_OPERATION : GL_INVALID_ENUM);
        return FALSE;
    }
    if (!JglUnpack()->SwapBytes)
        *type = GL_UNSIGNED_BYTE;
    return TRUE;
}

static BOOL LegacyInternalFormat2D(GLint format) {
    return (format >= 1 && format <= 4) || format == GL_ALPHA || format == GL_LUMINANCE ||
           format == GL_LUMINANCE_ALPHA || format == GL_RGB || format == GL_RGBA ||
           format == 0x2a10 || format == 0x8043 || format == 0x804f || format == 0x8050 ||
           format == 0x8056 || format == 0x8057 || format == 0x803c || format == 0x8040 ||
           format == 0x8045 || format == 0x8051 || format == 0x8058;
}

static BOOL InternalFormat(GLint format) {
    return (format >= 1 && format <= 4) || format == GL_ALPHA || format == GL_LUMINANCE ||
           format == GL_LUMINANCE_ALPHA || format == 0x8049 /* INTENSITY */ || format == GL_RGB ||
           format == GL_RGBA || format == 0x2a10 || (format >= 0x803b && format <= 0x8048) ||
           (format >= 0x804a && format <= 0x804d) || (format >= 0x804f && format <= 0x805b);
}

static BOOL Validate(GLenum target, GLint level, GLsizei width, GLsizei height, GLenum format,
                     GLenum type, ULONG *components, GLint border = 0) {
    if (target != GL_TEXTURE_2D) {
        JglCommandError(GL_INVALID_ENUM);
        return FALSE;
    }
    if (!(*components = PixelBytes(format, type)))
        return FALSE;
    if (level < 0 || level > DG_GL_MAX_TEXTURE_LEVEL || width < 0 || height < 0 || border < 0 ||
        border > 1 || (ULONG)width > ((ULONG)DG_GL_MAX_TEXTURE_DIMENSION >> level) + 2 * border ||
        (ULONG)height > ((ULONG)DG_GL_MAX_TEXTURE_DIMENSION >> level) + 2 * border) {
        JglCommandError(GL_INVALID_VALUE);
        return FALSE;
    }
    return TRUE;
}

static BOOL Layout(GLsizei width, GLsizei height, ULONG components, const void *pixels,
                   JGL_PIXEL_LAYOUT *layout) {
    JGL_UNPACK *unpack = JglUnpack();
    ULONG_PTR stride, first, skip, span, end;
    ULONG_PTR row_length = unpack->RowLength ? (ULONG_PTR)unpack->RowLength : (ULONG_PTR)width;

    layout->BitOffset = 0;
    layout->RowBytes = width * components;
    if (!Multiply(row_length, components, &stride) || !Add(stride, unpack->Alignment - 1, &stride))
        goto overflow;
    stride &= ~((ULONG_PTR)unpack->Alignment - 1);
    if (!Multiply((ULONG_PTR)unpack->SkipRows, stride, &first) ||
        !Multiply((ULONG_PTR)unpack->SkipPixels, components, &skip) || !Add(first, skip, &first) ||
        !Multiply((ULONG_PTR)height - 1, stride, &span) || !Add(span, layout->RowBytes, &span) ||
        !Add(first, span, &end) || !Add((ULONG_PTR)pixels, end, &end))
        goto overflow;
    layout->First = (const unsigned char *)pixels + first;
    layout->Stride = stride;
    return TRUE;
overflow:
    JglCommandError(GL_INVALID_VALUE);
    return FALSE;
}

static BOOL LayoutBitmap(GLsizei width, GLsizei height, BOOL one, const void *pixels,
                         JGL_PIXEL_LAYOUT *layout) {
    JGL_UNPACK *unpack = JglUnpack();
    ULONG_PTR stride = 0, first, span, end;
    ULONG_PTR row = unpack->RowLength && !one ? (ULONG_PTR)unpack->RowLength : (ULONG_PTR)width;
    if (!Add(row, 7, &stride) || !Add(stride / 8, unpack->Alignment - 1, &stride))
        goto overflow;
    stride &= ~((ULONG_PTR)unpack->Alignment - 1);
    layout->BitOffset = unpack->SkipPixels & 7;
    layout->RowBytes = width; /* Expanded immutable indices use one byte each. */
    if (!Multiply(one ? 0 : unpack->SkipRows, stride, &first) ||
        !Add(first, (ULONG_PTR)unpack->SkipPixels / 8, &first) ||
        !Multiply((ULONG_PTR)height - 1, stride, &span) ||
        !Add(span, ((ULONG_PTR)width + layout->BitOffset + 7) / 8, &span) ||
        !Add(first, span, &end) || !Add((ULONG_PTR)pixels, end, &end))
        goto overflow;
    layout->First = (const unsigned char *)pixels + first;
    layout->Stride = stride;
    return TRUE;
overflow:
    JglCommandError(GL_INVALID_VALUE);
    return FALSE;
}

static BOOL UploadPacked(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width,
                         GLsizei height, GLenum format, GLenum type, ULONG components,
                         const JGL_PIXEL_LAYOUT *layout, ULONG capacity) {
    /* Swap complete scalar components, or unpack bitmap/legacy packed inputs.
     * Ordinary component and packed16 rows retain their direct transport path. */
    unsigned char converted[(DG_GL_MAX_TEXTURE_DIMENSION + 2) * 4];
    ULONG y = 0, x, row, rows, columns, args[8];
    BOOL words = Packed16(type);
    ULONG unit = ScalarBytes(type);
    BOOL bitmap = type == 0x1a00;
    ULONG pixelBytes = WirePixelBytes(type, components);
    if (capacity > sizeof(converted))
        capacity = sizeof(converted);
    while (y < (ULONG)height) {
        rows = 1;
        if ((ULONG)width <= capacity / pixelBytes) {
            rows = capacity / (width * pixelBytes);
            if (rows > (ULONG)height - y)
                rows = height - y;
        }
        for (x = 0; x < (ULONG)width; x += columns) {
            columns = capacity / pixelBytes;
            if (columns > (ULONG)width - x)
                columns = width - x;
            for (row = 0; row < rows; ++row) {
                unsigned char *out = converted + row * columns * pixelBytes;
                const unsigned char *in =
                    layout->First + (y + row) * layout->Stride + (bitmap ? 0 : x * components);
                if (bitmap) {
                    for (ULONG i = 0; i < columns; ++i) {
                        ULONG bit = layout->BitOffset + x + i;
                        out[i] =
                            (in[bit / 8] >> (JglUnpack()->LsbFirst ? bit % 8 : 7 - bit % 8)) & 1;
                    }
                } else if (unit) {
                    for (ULONG i = 0; i < columns * components; i += unit)
                        for (ULONG j = 0; j < unit; ++j)
                            out[i + j] = in[i + unit - 1 - j];
                } else if (words) {
                    for (ULONG i = 0; i < columns; ++i) {
                        out[i * 2] = in[i * 2 + 1];
                        out[i * 2 + 1] = in[i * 2];
                    }
                } else {
                    ConvertPacked(out, in, columns, format, type);
                }
            }
            args[0] = target;
            args[1] = level;
            args[2] = xoffset + x;
            args[3] = yoffset + y;
            args[4] = columns;
            args[5] = rows;
            args[6] = WireFormat(format, type);
            args[7] = WireType(type);
            if (!JglData(target == GL_TEXTURE_1D ? FEnum_glTexSubImage1D : FEnum_glTexSubImage2D,
                         args, 8, converted, columns * rows * pixelBytes))
                return FALSE;
        }
        y += rows;
    }
    return TRUE;
}

static BOOL Upload(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width,
                   GLsizei height, GLenum format, GLenum type, ULONG components,
                   const JGL_PIXEL_LAYOUT *layout, ULONG capacity) {
    ULONG y = 0, x, rows, columns, args[8];
    if (!DirectPixels(type))
        return UploadPacked(target, level, xoffset, yoffset, width, height, format, type,
                            components, layout, capacity);

    while (y < (ULONG)height) {
        rows = 1;
        if (layout->Stride == layout->RowBytes && capacity >= layout->RowBytes) {
            rows = capacity / layout->RowBytes;
            if (rows > (ULONG)height - y)
                rows = height - y;
        }
        x = 0;
        while (x < (ULONG)width) {
            columns = capacity / components;
            if (columns > (ULONG)width - x)
                columns = width - x;
            args[0] = target;
            args[1] = level;
            args[2] = xoffset + x;
            args[3] = yoffset + y;
            args[4] = columns;
            args[5] = rows;
            args[6] = format;
            args[7] = type;
            if (!JglData(target == GL_TEXTURE_1D ? FEnum_glTexSubImage1D : FEnum_glTexSubImage2D,
                         args, 8, layout->First + y * layout->Stride + x * components,
                         columns * rows * components))
                return FALSE;
            x += columns;
        }
        y += rows;
    }
    return TRUE;
}

static ULONG Capacity(ULONG components) {
    /* The record also pads its payload to a four-byte boundary. */
    ULONG capacity = JglMaxDataBytes(8) & ~3UL;
    if (capacity < components) {
        JglCommandError(GL_OUT_OF_MEMORY);
        return 0;
    }
    return capacity;
}

void APIENTRY glPixelStorei(GLenum pname, GLint param) {
    JGL_UNPACK *unpack;
    if (!JglReady())
        return;
    unpack = JglUnpack();
    if (pname == GL_PACK_SWAP_BYTES || pname == GL_PACK_LSB_FIRST || pname == GL_PACK_ALIGNMENT ||
        pname == GL_PACK_ROW_LENGTH || pname == GL_PACK_SKIP_ROWS || pname == GL_PACK_SKIP_PIXELS) {
        unpack = JglPack();
        /* PACK and UNPACK selectors have matching low offsets. */
        pname += GL_UNPACK_ALIGNMENT - GL_PACK_ALIGNMENT;
    }
    switch (pname) {
        case GL_UNPACK_SWAP_BYTES:
            unpack->SwapBytes = param != 0;
            return;
        case GL_UNPACK_LSB_FIRST:
            unpack->LsbFirst = param != 0;
            return;
        case GL_UNPACK_ALIGNMENT:
            if (param != 1 && param != 2 && param != 4 && param != 8)
                goto invalid;
            unpack->Alignment = param;
            return;
        case GL_UNPACK_ROW_LENGTH:
            if (param < 0)
                goto invalid;
            unpack->RowLength = param;
            return;
        case GL_UNPACK_SKIP_ROWS:
            if (param < 0)
                goto invalid;
            unpack->SkipRows = param;
            return;
        case GL_UNPACK_SKIP_PIXELS:
            if (param < 0)
                goto invalid;
            unpack->SkipPixels = param;
            return;
        default:
            JglSetError(GL_INVALID_ENUM);
            return;
    }
invalid:
    JglSetError(GL_INVALID_VALUE);
}

/* Proxies query native allocation feasibility, never allocate or read pixels.
 * They execute immediately even while compiling a display list. */
static void ProxyImage(GLenum target, GLint level, GLint internal_format, GLsizei width,
                       GLsizei height, GLint border, GLenum format, GLenum type) {
    if (!JglReady() || !PixelBytes(format, type, TRUE))
        return;
    if (!JglTextureImagesAvailable()) {
        JglSetError(GL_INVALID_OPERATION);
        return;
    }
    if (level < 0 || level > DG_GL_MAX_TEXTURE_LEVEL || width < 0 || height < 0 || border < 0 ||
        border > 1 || !InternalFormat(internal_format)) {
        JglSetError(GL_INVALID_VALUE);
        return;
    }
    ULONG args[] = {target,        (ULONG)level,  (ULONG)internal_format,   (ULONG)width,
                    (ULONG)height, (ULONG)border, WireFormat(format, type), WireType(type)};
    JglData(target == 0x8063 ? FEnum_glTexImage1D : FEnum_glTexImage2D, args, 8, NULL, 0);
}

void APIENTRY glTexImage2D(GLenum target, GLint level, GLint internal_format, GLsizei width,
                           GLsizei height, GLint border, GLenum format, GLenum type,
                           const void *pixels) {
    if (target == 0x8064 /* PROXY_TEXTURE_2D */) {
        ProxyImage(target, level, internal_format, width, height, border, format, type);
        return;
    }
    ULONG components, capacity, args[8];
    JGL_PIXEL_LAYOUT layout;

    if (!JglCommandReady() || !NormalizeType(format, &type) ||
        !Validate(target, level, width, height, format, type, &components, border))
        return;
    if (width < 2 * border || height < 2 * border || !InternalFormat(internal_format)) {
        JglCommandError(GL_INVALID_VALUE);
        return;
    }
    if ((border && !JglTextureBordersAvailable()) ||
        ((!width || !height || !LegacyInternalFormat2D(internal_format)) &&
         !JglTextureImagesAvailable())) {
        JglCommandError(GL_INVALID_OPERATION);
        return;
    }
    if (!width || !height)
        pixels = NULL; /* Empty definitions replace storage without reading caller memory. */
    capacity = pixels ? Capacity(WirePixelBytes(type, components)) : 0;
    if (pixels &&
        (!capacity || !(type == 0x1a00 ? LayoutBitmap(width, height, FALSE, pixels, &layout)
                                       : Layout(width, height, components, pixels, &layout))))
        return;
    args[0] = target;
    args[1] = level;
    args[2] = internal_format;
    args[3] = width;
    args[4] = height;
    args[5] = border;
    args[6] = WireFormat(format, type);
    args[7] = WireType(type);
    if (DirectPixels(type) && pixels && layout.RowBytes &&
        (height == 1 || layout.Stride == layout.RowBytes) &&
        (ULONG)height <= capacity / layout.RowBytes) {
        JglData(FEnum_glTexImage2D, args, 8, layout.First, layout.RowBytes * height);
        return;
    }
    if (!JglData(FEnum_glTexImage2D, args, 8, NULL, 0))
        return;
    if (pixels)
        Upload(target, level, -border, -border, width, height, format, type, components, &layout,
               capacity);
}

void APIENTRY glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                              GLsizei width, GLsizei height, GLenum format, GLenum type,
                              const void *pixels) {
    ULONG components, capacity, limit;
    JGL_PIXEL_LAYOUT layout;

    if (!JglCommandReady() || !NormalizeType(format, &type) ||
        !Validate(target, level, width, height, format, type, &components, 1))
        return;
    limit = DG_GL_MAX_TEXTURE_DIMENSION >> level;
    if (xoffset < -1 || yoffset < -1 || xoffset > (GLint)limit + 1 || yoffset > (GLint)limit + 1 ||
        width > (GLint)limit + 1 - xoffset || height > (GLint)limit + 1 - yoffset) {
        JglCommandError(GL_INVALID_VALUE);
        return;
    }
    if (!width || !height)
        return;
    if (!pixels) {
        JglCommandError(GL_INVALID_VALUE);
        return;
    }
    capacity = Capacity(WirePixelBytes(type, components));
    if (!capacity)
        return;
    if (!(type == 0x1a00 ? LayoutBitmap(width, height, FALSE, pixels, &layout)
                         : Layout(width, height, components, pixels, &layout)))
        return;
    // Splitting must not turn an invalid rectangle into a valid partial write.
    // The image extent is execution-dependent while compiling a display list.
    ULONG tile_capacity = capacity;
    if (!DirectPixels(type) && tile_capacity > (DG_GL_MAX_TEXTURE_DIMENSION + 2) * 4)
        tile_capacity = (DG_GL_MAX_TEXTURE_DIMENSION + 2) * 4;
    BOOL split = (DirectPixels(type) && height > 1 && layout.Stride != layout.RowBytes) ||
                 (ULONG)width > tile_capacity / WirePixelBytes(type, components) ||
                 (ULONG)height > tile_capacity / WirePixelBytes(type, components) / (ULONG)width;
    if (split && JglCompiling()) {
        JglCommandError(GL_OUT_OF_MEMORY);
        return;
    }
    if (!JglCompiling() && (split || xoffset < 0 || yoffset < 0 || width > (GLint)limit - xoffset ||
                            height > (GLint)limit - yoffset)) {
        GLint extent[2] = {0, 0}, border = 0;
        ULONG query[] = {target, (ULONG)level, 0x1000 /* TEXTURE_WIDTH */}, bytes = 0;
        for (ULONG axis = 0; axis < 2; ++axis) {
            query[2] = 0x1000 + axis; /* TEXTURE_WIDTH/HEIGHT */
            if (!JglQuery(FEnum_glGetTexLevelParameteriv, query, DG_GL_RESULT_INT, &extent[axis],
                          sizeof(GLint), &bytes))
                return;
            if (bytes != sizeof(GLint) || extent[axis] < 0 || (ULONG)extent[axis] > limit + 2) {
                JglCommandError(GL_INVALID_OPERATION);
                return;
            }
        }
        if (!extent[0] || !extent[1]) {
            JglCommandError(GL_INVALID_OPERATION);
            return;
        }
        query[2] = 0x1005; /* TEXTURE_BORDER */
        if (!JglQuery(FEnum_glGetTexLevelParameteriv, query, DG_GL_RESULT_INT, &border,
                      sizeof(border), &bytes))
            return;
        if (bytes != sizeof(border) || border < 0 || border > 1 || extent[0] < 2 * border ||
            extent[1] < 2 * border) {
            JglCommandError(GL_INVALID_OPERATION);
            return;
        }
        if (xoffset < -border || yoffset < -border || xoffset > extent[0] - border ||
            yoffset > extent[1] - border || width > extent[0] - border - xoffset ||
            height > extent[1] - border - yoffset) {
            JglCommandError(GL_INVALID_VALUE);
            return;
        }
    }
    Upload(target, level, xoffset, yoffset, width, height, format, type, components, &layout,
           capacity);
}

static BOOL Layout1D(GLsizei width, ULONG components, const void *pixels,
                     JGL_PIXEL_LAYOUT *layout) {
    ULONG_PTR skip, first, end;
    if (!Multiply(JglUnpack()->SkipPixels, components, &skip) ||
        !Add((ULONG_PTR)pixels, skip, &first) || !Add(first, (ULONG_PTR)width * components, &end)) {
        JglCommandError(GL_INVALID_VALUE);
        return FALSE;
    }
    layout->First = (const unsigned char *)first;
    layout->BitOffset = 0;
    layout->Stride = layout->RowBytes = width * components;
    return TRUE;
}
static BOOL Validate1D(GLint level, GLsizei width, GLint border, GLenum format, GLenum type,
                       ULONG *components) {
    if (!(*components = PixelBytes(format, type)))
        return FALSE;
    if (level < 0 || level > DG_GL_MAX_TEXTURE_LEVEL || width < 0 || border < 0 || border > 1 ||
        width < border * 2 ||
        (ULONG)width > ((ULONG)DG_GL_MAX_TEXTURE_DIMENSION >> level) + 2 * (ULONG)border) {
        JglCommandError(GL_INVALID_VALUE);
        return FALSE;
    }
    return TRUE;
}
void APIENTRY glTexImage1D(GLenum target, GLint level, GLint internal_format, GLsizei width,
                           GLint border, GLenum format, GLenum type, const void *pixels) {
    if (target == 0x8063 /* PROXY_TEXTURE_1D */) {
        ProxyImage(target, level, internal_format, width, 1, border, format, type);
        return;
    }
    ULONG components, capacity, args[8];
    JGL_PIXEL_LAYOUT layout;
    if (!JglCommandReady())
        return;
    if (target != GL_TEXTURE_1D) {
        JglCommandError(GL_INVALID_ENUM);
        return;
    }
    if (!NormalizeType(format, &type) ||
        !Validate1D(level, width, border, format, type, &components))
        return;
    if (!InternalFormat(internal_format)) {
        JglCommandError(GL_INVALID_VALUE);
        return;
    }
    if ((border && !JglTextureBordersAvailable()) || (!width && !JglTextureImagesAvailable())) {
        JglCommandError(GL_INVALID_OPERATION);
        return;
    }
    if (!width)
        pixels = NULL;
    capacity = pixels ? Capacity(WirePixelBytes(type, components)) : 0;
    if (pixels && (!capacity || !(type == 0x1a00 ? LayoutBitmap(width, 1, TRUE, pixels, &layout)
                                                 : Layout1D(width, components, pixels, &layout))))
        return;
    args[0] = target;
    args[1] = level;
    args[2] = internal_format;
    args[3] = width;
    args[4] = 1;
    args[5] = border;
    args[6] = WireFormat(format, type);
    args[7] = WireType(type);
    if (DirectPixels(type) && pixels && layout.RowBytes <= capacity) {
        JglData(FEnum_glTexImage1D, args, 8, layout.First, layout.RowBytes);
        return;
    }
    if (!JglData(FEnum_glTexImage1D, args, 8, NULL, 0))
        return;
    if (pixels)
        Upload(target, level, -border, 0, width, 1, format, type, components, &layout, capacity);
}
void APIENTRY glTexSubImage1D(GLenum target, GLint level, GLint xoffset, GLsizei width,
                              GLenum format, GLenum type, const void *pixels) {
    ULONG components, capacity;
    JGL_PIXEL_LAYOUT layout;
    if (!JglCommandReady())
        return;
    if (target != GL_TEXTURE_1D) {
        JglCommandError(GL_INVALID_ENUM);
        return;
    }
    // The native level supplies the real border and extent. During list
    // compilation these execution-dependent checks belong to native replay.
    if (!NormalizeType(format, &type) || !(components = PixelBytes(format, type)))
        return;
    if (level < 0 || level > DG_GL_MAX_TEXTURE_LEVEL || width < 0 || xoffset < -1 ||
        (ULONG)width > ((ULONG)DG_GL_MAX_TEXTURE_DIMENSION >> level) + 2) {
        JglCommandError(GL_INVALID_VALUE);
        return;
    }
    capacity = width ? Capacity(WirePixelBytes(type, components)) : 0;
    if (width && !capacity)
        return;
    // One ordinary packet is validated atomically by the host. Preflight a
    // split rectangle before its first packet, and border-specific offsets.
    if (!DirectPixels(type) && capacity > (DG_GL_MAX_TEXTURE_DIMENSION + 2) * 4)
        capacity = (DG_GL_MAX_TEXTURE_DIMENSION + 2) * 4;
    const ULONG wire_components = WirePixelBytes(type, components);
    const BOOL compiling = JglCompiling();
    // A compiled rectangle must stay one native operation: its actual level
    // bounds are not known until replay. Never record a partially valid split.
    if (compiling && width && (ULONG)width > capacity / wire_components) {
        JglCommandError(GL_OUT_OF_MEMORY);
        return;
    }
    if (!compiling &&
        (xoffset < 0 || (ULONG)xoffset > ((ULONG)DG_GL_MAX_TEXTURE_DIMENSION >> level) ||
         (ULONG)width > ((ULONG)DG_GL_MAX_TEXTURE_DIMENSION >> level) - (ULONG)xoffset ||
         (width && (ULONG)width > capacity / wire_components))) {
        GLint extent = 0, border = 0;
        ULONG query[] = {target, (ULONG)level, 0x1000 /* TEXTURE_WIDTH */}, bytes = 0;
        if (!JglQuery(FEnum_glGetTexLevelParameteriv, query, DG_GL_RESULT_INT, &extent,
                      sizeof(extent), &bytes))
            return;
        if (bytes != sizeof(extent)) {
            JglCommandError(GL_INVALID_OPERATION);
            return;
        }
        query[2] = 0x1005; /* TEXTURE_BORDER */
        if (!JglQuery(FEnum_glGetTexLevelParameteriv, query, DG_GL_RESULT_INT, &border,
                      sizeof(border), &bytes))
            return;
        if (bytes != sizeof(border) || border < 0 || border > 1 || extent < 2 * border ||
            (ULONG)extent > DG_GL_MAX_TEXTURE_DIMENSION + 2) {
            JglCommandError(GL_INVALID_OPERATION);
            return;
        }
        const GLint end = extent - border;
        if (xoffset < -border || xoffset > end || width > end - xoffset) {
            JglCommandError(GL_INVALID_VALUE);
            return;
        }
    }
    if (!width)
        return;
    if (!pixels) {
        JglCommandError(GL_INVALID_VALUE);
        return;
    }
    if (!(type == 0x1a00 ? LayoutBitmap(width, 1, TRUE, pixels, &layout)
                         : Layout1D(width, components, pixels, &layout)))
        return;
    Upload(target, level, xoffset, 0, width, 1, format, type, components, &layout, capacity);
}

} /* extern C */
