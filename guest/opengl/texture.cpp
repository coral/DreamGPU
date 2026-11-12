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

/* UNSIGNED_INT_24_8 (EXT_packed_depth_stencil) travels like a scalar: one
 * four-byte unit, unchanged or swapped as a whole. Only depth images take it. */
static ULONG UnitBytes(GLenum type) {
    return type == JGL_UNSIGNED_INT_24_8 ? 4 : ScalarBytes(type);
}

/* Depth images (EXT_packed_depth_stencil, TEXTURE_2D): DEPTH_COMPONENT with
 * one BYTE..FLOAT component, or DEPTH_STENCIL as UNSIGNED_INT_24_8. */
static ULONG DepthPixelBytes(GLenum format, GLenum type, void (*error)(GLenum)) {
    if (format == JGL_DEPTH_STENCIL) {
        if (type == JGL_UNSIGNED_INT_24_8)
            return 4;
        error(GL_INVALID_ENUM);
        return 0;
    }
    if (type == JGL_UNSIGNED_INT_24_8) {
        error(format == 0x1902 || Components(format) ? GL_INVALID_OPERATION : GL_INVALID_ENUM);
        return 0;
    }
    if (ScalarBytes(type))
        return ScalarBytes(type); /* DEPTH_COMPONENT */
    /* GL 1.2 packed types are colour formats; BITMAP is no depth type. */
    error((type >= 0x8032 && type <= 0x8036) || (type >= 0x8362 && type <= 0x8368)
              ? GL_INVALID_OPERATION
              : GL_INVALID_ENUM);
    return 0;
}

/* GL 1.2 packed inputs; anything outside 16-bit direct transport is expanded to
 * RGBA8. Depth images only where depth is set. */
static ULONG PixelBytes(GLenum format, GLenum type, BOOL immediate = FALSE, BOOL depth = FALSE) {
    void (*error)(GLenum) = immediate ? JglSetError : JglCommandError;
    if (depth && (format == 0x1902 /* DEPTH_COMPONENT */ || format == JGL_DEPTH_STENCIL ||
                  type == JGL_UNSIGNED_INT_24_8)) {
        if (((ScalarBytes(type) && type != GL_UNSIGNED_BYTE) || type == JGL_UNSIGNED_INT_24_8) &&
            !JglTextureImagesAvailable()) {
            error(GL_INVALID_OPERATION);
            return 0;
        }
        return DepthPixelBytes(format, type, error);
    }
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
        case 0x8035: /* UNSIGNED_INT_8_8_8_8 */
        case 0x8036: /* UNSIGNED_INT_10_10_10_2 */
        case 0x8368: /* UNSIGNED_INT_2_10_10_10_REV */
            if (format == GL_RGBA || format == 0x80e1)
                return 4;
            break;
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
    return UnitBytes(type) == 1 || ((UnitBytes(type) || Packed16(type)) && !JglUnpack()->SwapBytes);
}

static ULONG WirePixelBytes(GLenum type, ULONG components) {
    return UnitBytes(type) || Packed16(type) || type == 0x1a00 ? components : 4;
}

static GLenum WireFormat(GLenum format, GLenum type) {
    return UnitBytes(type) || Packed16(type) || type == 0x1a00 ? format : GL_RGBA;
}
static GLenum WireType(GLenum type) {
    return UnitBytes(type) || Packed16(type) ? type : GL_UNSIGNED_BYTE;
}

static unsigned char Expand(ULONG value, ULONG maximum) {
    return (unsigned char)((value * 255 + maximum / 2) / maximum);
}

static void ConvertPacked(unsigned char *out, const unsigned char *in, ULONG count, GLenum format,
                          GLenum type) {
    ULONG i, value, a, b, c, d, j;
    ULONG bytes = type == 0x8032 || type == 0x8362                                       ? 1
                  : type == 0x8367 || type == 0x8035 || type == 0x8036 || type == 0x8368 ? 4
                                                                                         : 2;
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
            case 0x8035:
                a = value >> 24;
                b = (value >> 16) & 255;
                c = (value >> 8) & 255;
                d = value & 255;
                break;
            case 0x8036:
                a = Expand(value >> 22, 1023);
                b = Expand((value >> 12) & 1023, 1023);
                c = Expand((value >> 2) & 1023, 1023);
                d = (value & 3) * 85;
                break;
            case 0x8368:
                a = Expand(value & 1023, 1023);
                b = Expand((value >> 10) & 1023, 1023);
                c = Expand((value >> 20) & 1023, 1023);
                d = (value >> 30) * 85;
                break;
            case 0x8032:
                a = Expand(value >> 5, 7);
                b = Expand((value >> 2) & 7, 7);
                c = Expand(value & 3, 3);
                d = 255;
                break;
            case 0x8362:
                a = Expand(value & 7, 7);
                b = Expand((value >> 3) & 7, 7);
                c = Expand(value >> 6, 3);
                d = 255;
                break;
            case 0x8364:
                a = Expand(value & 31, 31);
                b = Expand((value >> 5) & 63, 63);
                c = Expand(value >> 11, 31);
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
           (format >= 0x804a && format <= 0x804d) || (format >= 0x804f && format <= 0x805b) ||
           JglRgb565Format(format);
}

/* COMPRESSED_RGB/RGBA_S3TC_DXT1, COMPRESSED_RGBA_S3TC_DXT3/DXT5 */
static BOOL S3tcInternalFormat(GLint format) {
    return format >= 0x83f0 && format <= 0x83f3;
}
/* Negotiated compression lets a 2D image (or its proxy) request an S3TC or a
 * generic COMPRESSED_ALPHA..RGBA internal format; native GL compresses it. */
static BOOL CompressedInternalFormat2D(GLint format) {
    return (S3tcInternalFormat(format) || (format >= 0x84e9 && format <= 0x84ee)) &&
           JglCompressedTexturesAvailable();
}

/* Depth textures: TEXTURE_2D images with EXT_packed_depth_stencil, and with
 * ARB_depth_texture (the second capability word) TEXTURE_1D and, once
 * negotiated, TEXTURE_RECTANGLE images too; proxies only with the latter. */
static BOOL DepthTarget(GLenum target) {
    return (target == GL_TEXTURE_2D && JglFramebufferObjectAvailable()) ||
           (JglDepthTextureAvailable() &&
            (target == GL_TEXTURE_2D || target == GL_TEXTURE_1D || JglRectangleTarget(target)));
}
static BOOL DepthProxy(GLenum target) {
    return JglDepthTextureAvailable() &&
           (target == 0x8063 /* PROXY_TEXTURE_1D */ || target == 0x8064 /* PROXY_TEXTURE_2D */ ||
            (target == JGL_PROXY_TEXTURE_RECTANGLE && JglTextureRectangleAvailable()));
}
static BOOL DepthPixels(GLenum format) {
    return format == 0x1902 /* DEPTH_COMPONENT */ || format == JGL_DEPTH_STENCIL;
}
/* ARB_depth_texture: a depth internal format or depth pixels for a target that
 * has no depth images (cube maps, 3D textures) is INVALID_OPERATION. Without
 * it these stay the INVALID_VALUE/INVALID_ENUM of unknown formats. */
static BOOL DepthMisplaced(GLint internal_format, GLenum format) {
    return JglDepthTextureAvailable() &&
           (JglDepthInternalFormat(internal_format) || DepthPixels(format));
}

/* 2D records target TEXTURE_2D or, once negotiated, one cube map face or
 * TEXTURE_RECTANGLE, whose only level is zero (ARB_texture_rectangle). */
static BOOL Validate(GLenum target, GLint level, GLsizei width, GLsizei height, GLenum format,
                     GLenum type, ULONG *components, GLint border = 0) {
    if (!JglImage2DTarget(target) && !JglRectangleTarget(target)) {
        JglCommandError(GL_INVALID_ENUM);
        return FALSE;
    }
    if (JglCubeFace(target) && DepthMisplaced(0, format)) {
        JglCommandError(GL_INVALID_OPERATION);
        return FALSE;
    }
    if (!(*components = PixelBytes(format, type, FALSE, DepthTarget(target))))
        return FALSE;
    if ((level && JglRectangleTarget(target)) || level < 0 || level > DG_GL_MAX_TEXTURE_LEVEL ||
        width < 0 || height < 0 || border < 0 || border > 1 ||
        (ULONG)width > ((ULONG)DG_GL_MAX_TEXTURE_DIMENSION >> level) + 2 * border ||
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

/* Rows and columns of every tile of a split rectangle but the last in each
 * direction, which takes the remainder up to the rectangle's edge. */
typedef struct {
    ULONG Rows, Columns;
} JGL_TILE_SHAPE;

/* A rectangle that fits one record is one tile. Otherwise every 2D tile starts
 * on a four-pixel boundary relative to the rectangle's origin: tile rows and
 * column chunks are multiples of four. A compressed native level accepts only
 * such block-aligned sub-images (EXT_texture_compression_s3tc), and the frontend
 * cannot know the format of a TexSubImage2D level; elsewhere it is harmless.
 * Full-width bands are preferred; narrower chunks are four rows high (or the
 * whole height when it is smaller). Below sixteen pixels per record no aligned
 * tile exists. Only a packet close to the negotiated 32-word minimum is that
 * small, never a real host, so that case, like 1D, keeps the unaligned shape:
 * as many full rows as fit, else single-row chunks. */
static JGL_TILE_SHAPE TileShape(ULONG width, ULONG height, ULONG pixel_bytes, ULONG capacity,
                                BOOL aligned) {
    const ULONG fit = capacity / pixel_bytes / width; /* complete rows per record */
    JGL_TILE_SHAPE shape = {height, width};
    if (fit >= height)
        return shape;
    if (aligned && fit >= 4) {
        shape.Rows = fit & ~3UL;
        return shape;
    }
    if (aligned) {
        shape.Rows = height < 4 ? height : 4;
        shape.Columns = (capacity / pixel_bytes / shape.Rows) & ~3UL;
        if (shape.Columns >= 4)
            return shape;
        shape.Columns = width;
    }
    shape.Rows = fit ? fit : 1;
    if (!fit)
        shape.Columns = capacity / pixel_bytes;
    return shape;
}

/* Rows of a tile copied from their strided caller layout into its record. */
typedef struct {
    const unsigned char *First;
    ULONG_PTR Stride;
    ULONG RowBytes, Rows;
} JGL_GATHER;
static void GatherRows(void *opaque, BYTE *destination) {
    const JGL_GATHER *gather = (const JGL_GATHER *)opaque;
    for (ULONG row = 0; row < gather->Rows; ++row)
        CopyMemory(destination + row * gather->RowBytes, gather->First + row * gather->Stride,
                   gather->RowBytes);
}

static BOOL UploadPacked(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width,
                         GLsizei height, GLenum format, GLenum type, ULONG components,
                         const JGL_PIXEL_LAYOUT *layout, ULONG capacity) {
    /* Swap complete scalar components, or unpack bitmap/legacy packed inputs.
     * Ordinary component and packed16 rows retain their direct transport path. */
    unsigned char converted[(DG_GL_MAX_TEXTURE_DIMENSION + 2) * 4];
    ULONG y, x, row, rows, columns, args[8];
    BOOL words = Packed16(type);
    ULONG unit = UnitBytes(type);
    BOOL bitmap = type == 0x1a00;
    ULONG pixelBytes = WirePixelBytes(type, components);
    if (capacity > sizeof(converted))
        capacity = sizeof(converted);
    const JGL_TILE_SHAPE shape =
        TileShape(width, height, pixelBytes, capacity, target != GL_TEXTURE_1D);
    for (y = 0; y < (ULONG)height; y += rows) {
        rows = shape.Rows < (ULONG)height - y ? shape.Rows : (ULONG)height - y;
        for (x = 0; x < (ULONG)width; x += columns) {
            columns = shape.Columns < (ULONG)width - x ? shape.Columns : (ULONG)width - x;
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
    }
    return TRUE;
}

static BOOL Upload(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width,
                   GLsizei height, GLenum format, GLenum type, ULONG components,
                   const JGL_PIXEL_LAYOUT *layout, ULONG capacity) {
    ULONG y, x, rows, columns, args[8];
    const ULONG function = target == GL_TEXTURE_1D ? FEnum_glTexSubImage1D : FEnum_glTexSubImage2D;
    if (!DirectPixels(type))
        return UploadPacked(target, level, xoffset, yoffset, width, height, format, type,
                            components, layout, capacity);

    const JGL_TILE_SHAPE shape =
        TileShape(width, height, components, capacity, target != GL_TEXTURE_1D);
    for (y = 0; y < (ULONG)height; y += rows) {
        rows = shape.Rows < (ULONG)height - y ? shape.Rows : (ULONG)height - y;
        for (x = 0; x < (ULONG)width; x += columns) {
            columns = shape.Columns < (ULONG)width - x ? shape.Columns : (ULONG)width - x;
            args[0] = target;
            args[1] = level;
            args[2] = xoffset + x;
            args[3] = yoffset + y;
            args[4] = columns;
            args[5] = rows;
            args[6] = format;
            args[7] = type;
            /* Contiguous caller rows are copied as one span; padded rows and
             * narrower chunks are gathered row by row into the record. */
            JGL_GATHER gather = {layout->First + y * layout->Stride + x * components,
                                 layout->Stride, columns * components, rows};
            BOOL sent;
            if (rows == 1 || layout->Stride == gather.RowBytes)
                sent = JglData(function, args, 8, gather.First, rows * gather.RowBytes);
            else
                sent =
                    JglCaptureData(function, args, 8, rows * gather.RowBytes, GatherRows, &gather);
            if (!sent)
                return FALSE;
        }
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
    /* PACK_SKIP_IMAGES/PACK_IMAGE_HEIGHT precede their UNPACK names by two. */
    if ((pname == 0x806b || pname == 0x806c) && JglTexture3DAvailable()) {
        unpack = JglPack();
        pname += 2;
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
        case 0x806d: /* UNPACK_SKIP_IMAGES (EXT_texture3D) */
        case 0x806e: /* UNPACK_IMAGE_HEIGHT */
            if (!JglTexture3DAvailable())
                break;
            if (param < 0)
                goto invalid;
            if (pname == 0x806d)
                unpack->SkipImages = param;
            else
                unpack->ImageHeight = param;
            return;
        default:
            break;
    }
    JglSetError(GL_INVALID_ENUM);
    return;
invalid:
    JglSetError(GL_INVALID_VALUE);
}

/* Proxies query native allocation feasibility, never allocate or read pixels.
 * They execute immediately even while compiling a display list. */
static void ProxyImage(GLenum target, GLint level, GLint internal_format, GLsizei width,
                       GLsizei height, GLint border, GLenum format, GLenum type) {
    const BOOL cube = target == JGL_PROXY_TEXTURE_CUBE_MAP;
    const BOOL rectangle = target == JGL_PROXY_TEXTURE_RECTANGLE;
    /* ARB_depth_texture: depth proxies of 1D, 2D and rectangle images. */
    const BOOL depth = DepthProxy(target) && JglDepthInternalFormat(internal_format);
    if (!JglReady())
        return;
    if (cube && DepthMisplaced(internal_format, format)) {
        JglSetError(GL_INVALID_OPERATION);
        return;
    }
    if (!PixelBytes(format, type, TRUE, DepthProxy(target)))
        return;
    if (!JglTextureImagesAvailable()) {
        JglSetError(GL_INVALID_OPERATION);
        return;
    }
    /* A rectangle is never compressed; it has level zero and no border. */
    if (rectangle && CompressedInternalFormat2D(internal_format)) {
        JglSetError(GL_INVALID_ENUM);
        return;
    }
    if (rectangle && (level || border)) {
        JglSetError(GL_INVALID_VALUE);
        return;
    }
    /* Cube map faces are square and borderless. */
    if (level < 0 || level > DG_GL_MAX_TEXTURE_LEVEL || width < 0 || height < 0 || border < 0 ||
        border > 1 || (cube && (width != height || border)) || (depth && border) ||
        (!InternalFormat(internal_format) && !depth &&
         !(target != 0x8063 && CompressedInternalFormat2D(internal_format)))) {
        JglSetError(GL_INVALID_VALUE);
        return;
    }
    /* Depth images take depth pixels, and only they do. */
    if (DepthProxy(target) && depth != DepthPixels(format)) {
        JglSetError(GL_INVALID_OPERATION);
        return;
    }
    if (border && target == 0x8064 && S3tcInternalFormat(internal_format)) {
        JglSetError(GL_INVALID_OPERATION); /* S3TC images have no border. */
        return;
    }
    ULONG args[] = {target,        (ULONG)level,  (ULONG)internal_format,   (ULONG)width,
                    (ULONG)height, (ULONG)border, WireFormat(format, type), WireType(type)};
    JglData(target == 0x8063 ? FEnum_glTexImage1D : FEnum_glTexImage2D, args, 8, NULL, 0);
}

void APIENTRY glTexImage2D(GLenum target, GLint level, GLint internal_format, GLsizei width,
                           GLsizei height, GLint border, GLenum format, GLenum type,
                           const void *pixels) {
    if (target == 0x8064 /* PROXY_TEXTURE_2D */ ||
        (target == JGL_PROXY_TEXTURE_CUBE_MAP && JglTextureCubeMapAvailable()) ||
        (target == JGL_PROXY_TEXTURE_RECTANGLE && JglTextureRectangleAvailable())) {
        ProxyImage(target, level, internal_format, width, height, border, format, type);
        return;
    }
    ULONG components, capacity, args[8];
    JGL_PIXEL_LAYOUT layout;

    if (!JglCommandReady() || !NormalizeType(format, &type) ||
        !Validate(target, level, width, height, format, type, &components, border))
        return;
    const BOOL compressed = CompressedInternalFormat2D(internal_format);
    const BOOL depth = DepthTarget(target) && JglDepthInternalFormat(internal_format);
    const BOOL rectangle = JglRectangleTarget(target);
    if (JglCubeFace(target) && DepthMisplaced(internal_format, format)) {
        JglCommandError(GL_INVALID_OPERATION);
        return;
    }
    /* Rectangles are never compressed (ARB_texture_rectangle). */
    if (rectangle && compressed) {
        JglCommandError(GL_INVALID_ENUM);
        return;
    }
    /* Cube map faces are square and borderless, as are depth textures and
     * rectangles. */
    if (width < 2 * border || height < 2 * border ||
        (JglCubeFace(target) && (width != height || border)) || (depth && border) ||
        (rectangle && border) || (!InternalFormat(internal_format) && !compressed && !depth)) {
        JglCommandError(GL_INVALID_VALUE);
        return;
    }
    /* Depth textures take depth pixels, and only they do. */
    if (depth != DepthPixels(format)) {
        JglCommandError(GL_INVALID_OPERATION);
        return;
    }
    /* S3TC images have no border. Compressed formats belong to their own
     * negotiated contract, independent of the extended image formats. */
    if ((border && (!JglTextureBordersAvailable() || S3tcInternalFormat(internal_format))) ||
        ((!width || !height || !(LegacyInternalFormat2D(internal_format) || compressed)) &&
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
    // Padded rows no longer split: one record gathers them (Upload).
    ULONG tile_capacity = capacity;
    if (!DirectPixels(type) && tile_capacity > (DG_GL_MAX_TEXTURE_DIMENSION + 2) * 4)
        tile_capacity = (DG_GL_MAX_TEXTURE_DIMENSION + 2) * 4;
    const JGL_TILE_SHAPE shape =
        TileShape(width, height, WirePixelBytes(type, components), tile_capacity, TRUE);
    BOOL split = shape.Rows < (ULONG)height || shape.Columns < (ULONG)width;
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
    if (!(*components = PixelBytes(format, type, FALSE, DepthTarget(GL_TEXTURE_1D))))
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
    /* ARB_depth_texture: a borderless depth image takes depth pixels, and only
     * a depth image does. */
    const BOOL depth = DepthTarget(target) && JglDepthInternalFormat(internal_format);
    if ((!InternalFormat(internal_format) && !depth) || (depth && border)) {
        JglCommandError(GL_INVALID_VALUE);
        return;
    }
    if (DepthTarget(target) && depth != DepthPixels(format)) {
        JglCommandError(GL_INVALID_OPERATION);
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
    if (!NormalizeType(format, &type) ||
        !(components = PixelBytes(format, type, FALSE, DepthTarget(target))))
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

/* EXT_texture3D (DG_CAP_GL_TEXTURE_3D). Everything the host would refuse as a
 * malformed record is a local GL error here; offsets and rectangles against
 * the level's actual image are host GL errors. 3D images are borderless. */

/* Largest width, height or depth of a validated level (0..11). */
static ULONG Limit3D(GLint level) {
    return (ULONG)DG_GL_MAX_3D_TEXTURE_SIZE >> level;
}

/* Rows of the first image, plus the distance between images from
 * UNPACK_IMAGE_HEIGHT and the first image from UNPACK_SKIP_IMAGES. */
typedef struct {
    JGL_PIXEL_LAYOUT Image;
    ULONG_PTR ImageStride;
} JGL_VOLUME_LAYOUT;

static BOOL Layout3D(GLsizei width, GLsizei height, GLsizei depth, ULONG components,
                     const void *pixels, JGL_VOLUME_LAYOUT *layout) {
    JGL_UNPACK *unpack = JglUnpack();
    ULONG_PTR rows = unpack->ImageHeight ? (ULONG_PTR)unpack->ImageHeight : (ULONG_PTR)height;
    ULONG_PTR skip, span, end;
    if (!Layout(width, height, components, pixels, &layout->Image))
        return FALSE;
    /* Layout checked the first image's last row; add the images before it. */
    if (!Multiply(rows, layout->Image.Stride, &layout->ImageStride) ||
        !Multiply((ULONG_PTR)unpack->SkipImages, layout->ImageStride, &skip) ||
        !Multiply((ULONG_PTR)depth - 1, layout->ImageStride, &span) || !Add(skip, span, &span) ||
        !Add((ULONG_PTR)layout->Image.First, span, &end) ||
        !Add(end, ((ULONG_PTR)height - 1) * layout->Image.Stride, &end) ||
        !Add(end, layout->Image.RowBytes, &end)) {
        JglCommandError(GL_INVALID_VALUE);
        return FALSE;
    }
    layout->Image.First += skip;
    return TRUE;
}

/* One row of a record in its wire form: direct components unchanged, swapped
 * multi-byte components or packed pairs, or GL 1.2 packed pixels as RGBA8
 * (the same forms as the 2D records). */
static void WireRow(BYTE *out, const unsigned char *in, ULONG columns, ULONG components,
                    GLenum format, GLenum type) {
    const ULONG unit = UnitBytes(type);
    if (DirectPixels(type)) {
        CopyMemory(out, in, columns * components);
    } else if (unit) {
        for (ULONG i = 0; i < columns * components; i += unit)
            for (ULONG j = 0; j < unit; ++j)
                out[i + j] = in[i + unit - 1 - j];
    } else if (Packed16(type)) {
        for (ULONG i = 0; i < columns; ++i) {
            out[i * 2] = in[i * 2 + 1];
            out[i * 2 + 1] = in[i * 2];
        }
    } else {
        ConvertPacked(out, in, columns, format, type);
    }
}

/* A slab of rows, columns and images of the caller's volume, written straight
 * into its record. */
typedef struct {
    const JGL_VOLUME_LAYOUT *Layout;
    ULONG X, Y, Z, Columns, Rows, Images, Components, WireBytes;
    GLenum Format, Type;
} JGL_VOLUME_TILE;
static void GatherVolume(void *opaque, BYTE *destination) {
    const JGL_VOLUME_TILE *t = (const JGL_VOLUME_TILE *)opaque;
    for (ULONG image = 0; image < t->Images; ++image)
        for (ULONG row = 0; row < t->Rows; ++row)
            WireRow(destination + (image * t->Rows + row) * t->Columns * t->WireBytes,
                    t->Layout->Image.First + (t->Z + image) * t->Layout->ImageStride +
                        (t->Y + row) * t->Layout->Image.Stride + t->X * t->Components,
                    t->Columns, t->Components, t->Format, t->Type);
}

/* Whole images per record when one fits, else the 2D tiles of one image. */
typedef struct {
    ULONG Rows, Columns, Images;
} JGL_SLAB_SHAPE;
static JGL_SLAB_SHAPE SlabShape(ULONG width, ULONG height, ULONG depth, ULONG wire_bytes,
                                ULONG capacity) {
    const ULONG image = width * height * wire_bytes;
    JGL_SLAB_SHAPE shape = {height, width, 1};
    if (image <= capacity) {
        shape.Images = capacity / image < depth ? capacity / image : depth;
        return shape;
    }
    const JGL_TILE_SHAPE tile = TileShape(width, height, wire_bytes, capacity, TRUE);
    shape.Rows = tile.Rows;
    shape.Columns = tile.Columns;
    return shape;
}

static ULONG Capacity3D(ULONG words, ULONG wire_bytes) {
    ULONG capacity = JglMaxDataBytes(words) & ~3UL;
    if (capacity < wire_bytes) {
        JglCommandError(GL_OUT_OF_MEMORY);
        return 0;
    }
    return capacity;
}

/* TexSubImage3D records of a validated volume, slab by slab. */
static BOOL Upload3D(GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width,
                     GLsizei height, GLsizei depth, GLenum format, GLenum type, ULONG components,
                     const JGL_VOLUME_LAYOUT *layout, JGL_SLAB_SHAPE shape) {
    JGL_VOLUME_TILE tile = {
        layout, 0, 0, 0, 0, 0, 0, components, WirePixelBytes(type, components), format, type};
    ULONG args[10];
    for (tile.Z = 0; tile.Z < (ULONG)depth; tile.Z += tile.Images) {
        tile.Images = shape.Images < (ULONG)depth - tile.Z ? shape.Images : (ULONG)depth - tile.Z;
        for (tile.Y = 0; tile.Y < (ULONG)height; tile.Y += tile.Rows) {
            tile.Rows = shape.Rows < (ULONG)height - tile.Y ? shape.Rows : (ULONG)height - tile.Y;
            for (tile.X = 0; tile.X < (ULONG)width; tile.X += tile.Columns) {
                tile.Columns =
                    shape.Columns < (ULONG)width - tile.X ? shape.Columns : (ULONG)width - tile.X;
                args[0] = JGL_TEXTURE_3D;
                args[1] = (ULONG)level;
                args[2] = (ULONG)xoffset + tile.X;
                args[3] = (ULONG)yoffset + tile.Y;
                args[4] = (ULONG)zoffset + tile.Z;
                args[5] = tile.Columns;
                args[6] = tile.Rows;
                args[7] = tile.Images;
                args[8] = WireFormat(format, type);
                args[9] = WireType(type);
                if (!JglCaptureData(FEnum_glTexSubImage3D, args, 10,
                                    tile.Columns * tile.Rows * tile.Images * tile.WireBytes,
                                    GatherVolume, &tile))
                    return FALSE;
            }
        }
    }
    return TRUE;
}

/* PROXY_TEXTURE_3D executes immediately, even while compiling a list. Sizes
 * beyond the virtual limits and a border reject the proxy level on the host. */
void APIENTRY glTexImage3DEXT(GLenum target, GLint level, GLenum internal_format, GLsizei width,
                              GLsizei height, GLsizei depth, GLint border, GLenum format,
                              GLenum type, const void *pixels) {
    const BOOL proxy = target == JGL_PROXY_TEXTURE_3D;
    void (*error)(GLenum) = proxy ? JglSetError : JglCommandError;
    ULONG components, wire, capacity, args[9];
    JGL_VOLUME_LAYOUT layout;
    if (!(proxy ? JglReady() : JglCommandReady()))
        return;
    if (!JglTexture3DAvailable()) {
        error(GL_INVALID_OPERATION);
        return;
    }
    if (target != JGL_TEXTURE_3D && !proxy) {
        error(GL_INVALID_ENUM);
        return;
    }
    if (DepthMisplaced((GLint)internal_format, format)) {
        error(GL_INVALID_OPERATION); /* 3D textures have no depth images. */
        return;
    }
    if ((!proxy && !NormalizeType(format, &type)) ||
        !(components = PixelBytes(format, type, proxy)))
        return;
    if (type == 0x1a00 /* BITMAP */) {
        error(GL_INVALID_ENUM);
        return;
    }
    if (level < 0 || level > DG_GL_MAX_TEXTURE_LEVEL || border < 0 || border > 1 || width < 0 ||
        height < 0 || depth < 0 || width < 2 * border || height < 2 * border ||
        depth < 2 * border || !InternalFormat((GLint)internal_format) ||
        (!proxy && ((ULONG)width > Limit3D(level) + 2 * border ||
                    (ULONG)height > Limit3D(level) + 2 * border ||
                    (ULONG)depth > Limit3D(level) + 2 * border))) {
        error(GL_INVALID_VALUE);
        return;
    }
    args[0] = target;
    args[1] = (ULONG)level;
    args[2] = internal_format;
    args[3] = (ULONG)width;
    args[4] = (ULONG)height;
    args[5] = (ULONG)depth;
    args[6] = (ULONG)border;
    args[7] = WireFormat(format, type);
    args[8] = WireType(type);
    if (proxy) {
        JglData(FEnum_glTexImage3D, args, 9, NULL, 0);
        return;
    }
    if (border) {
        error(GL_INVALID_OPERATION); /* 3D images are borderless (gl.h). */
        return;
    }
    if (!width || !height || !depth)
        pixels = NULL; /* Empty definitions replace storage without reading caller memory. */
    wire = WirePixelBytes(type, components);
    if (pixels) {
        if (!(capacity = Capacity3D(9, wire)) ||
            !Layout3D(width, height, depth, components, pixels, &layout))
            return;
        /* A volume that fits one record defines and fills its image at once. */
        if ((ULONG)width * height * depth <= capacity / wire) {
            JGL_VOLUME_TILE whole = {
                &layout,      0,          0,    0,      (ULONG)width, (ULONG)height,
                (ULONG)depth, components, wire, format, type};
            JglCaptureData(FEnum_glTexImage3D, args, 9, (ULONG)width * height * depth * wire,
                           GatherVolume, &whole);
            return;
        }
    }
    if (!JglData(FEnum_glTexImage3D, args, 9, NULL, 0) || !pixels ||
        !(capacity = Capacity3D(10, wire)))
        return;
    Upload3D(level, 0, 0, 0, width, height, depth, format, type, components, &layout,
             SlabShape(width, height, depth, wire, capacity));
}
void APIENTRY glTexImage3D(GLenum target, GLint level, GLint internal_format, GLsizei width,
                           GLsizei height, GLsizei depth, GLint border, GLenum format, GLenum type,
                           const void *pixels) {
    glTexImage3DEXT(target, level, (GLenum)internal_format, width, height, depth, border, format,
                    type, pixels);
}

void APIENTRY glTexSubImage3DEXT(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                                 GLint zoffset, GLsizei width, GLsizei height, GLsizei depth,
                                 GLenum format, GLenum type, const void *pixels) {
    ULONG components, wire, capacity, limit;
    JGL_VOLUME_LAYOUT layout;
    if (!JglCommandReady())
        return;
    if (!JglTexture3DAvailable()) {
        JglCommandError(GL_INVALID_OPERATION);
        return;
    }
    if (target != JGL_TEXTURE_3D) {
        JglCommandError(GL_INVALID_ENUM);
        return;
    }
    if (DepthMisplaced(0, format)) {
        JglCommandError(GL_INVALID_OPERATION); /* 3D textures have no depth images. */
        return;
    }
    if (!NormalizeType(format, &type) || !(components = PixelBytes(format, type)))
        return;
    if (type == 0x1a00 /* BITMAP */) {
        JglCommandError(GL_INVALID_ENUM);
        return;
    }
    if (level < 0 || level > DG_GL_MAX_TEXTURE_LEVEL) {
        JglCommandError(GL_INVALID_VALUE);
        return;
    }
    limit = Limit3D(level);
    if (xoffset < 0 || yoffset < 0 || zoffset < 0 || width < 0 || height < 0 || depth < 0 ||
        (ULONG)xoffset > limit || (ULONG)yoffset > limit || (ULONG)zoffset > limit ||
        (ULONG)width > limit - xoffset || (ULONG)height > limit - yoffset ||
        (ULONG)depth > limit - zoffset) {
        JglCommandError(GL_INVALID_VALUE);
        return;
    }
    if (!width || !height || !depth)
        return;
    if (!pixels) {
        JglCommandError(GL_INVALID_VALUE);
        return;
    }
    wire = WirePixelBytes(type, components);
    if (!(capacity = Capacity3D(10, wire)) ||
        !Layout3D(width, height, depth, components, pixels, &layout))
        return;
    const JGL_SLAB_SHAPE shape = SlabShape(width, height, depth, wire, capacity);
    // Splitting must not turn an invalid rectangle into a valid partial write.
    // The image extent is execution-dependent while compiling a display list.
    if (shape.Images < (ULONG)depth || shape.Rows < (ULONG)height || shape.Columns < (ULONG)width) {
        GLint extent[3] = {0, 0, 0};
        ULONG query[] = {JGL_TEXTURE_3D, (ULONG)level, 0}, bytes = 0;
        if (JglCompiling()) {
            JglCommandError(GL_OUT_OF_MEMORY);
            return;
        }
        for (ULONG axis = 0; axis < 3; ++axis) {
            query[2] = axis == 2 ? 0x8071 /* TEXTURE_DEPTH */ : 0x1000 + axis; /* WIDTH/HEIGHT */
            if (!JglQuery(FEnum_glGetTexLevelParameteriv, query, DG_GL_RESULT_INT, &extent[axis],
                          sizeof(GLint), &bytes))
                return;
            if (bytes != sizeof(GLint) || extent[axis] < 0 ||
                (ULONG)extent[axis] > DG_GL_MAX_3D_TEXTURE_SIZE) {
                JglCommandError(GL_INVALID_OPERATION);
                return;
            }
        }
        if (!extent[0] || !extent[1] || !extent[2]) {
            JglCommandError(GL_INVALID_OPERATION);
            return;
        }
        if (width > extent[0] - xoffset || height > extent[1] - yoffset ||
            depth > extent[2] - zoffset) {
            JglCommandError(GL_INVALID_VALUE);
            return;
        }
    }
    Upload3D(level, xoffset, yoffset, zoffset, width, height, depth, format, type, components,
             &layout, shape);
}
void APIENTRY glTexSubImage3D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
                              GLint zoffset, GLsizei width, GLsizei height, GLsizei depth,
                              GLenum format, GLenum type, const void *pixels) {
    glTexSubImage3DEXT(target, level, xoffset, yoffset, zoffset, width, height, depth, format, type,
                       pixels);
}

} /* extern C */
