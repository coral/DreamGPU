/* SPDX-License-Identifier: GPL-2.0-or-later
 * ARB_texture_compression with EXT_texture_compression_s3tc, for TEXTURE_2D and,
 * with negotiated cube maps, each (square) cube map face.
 * Compressed images are opaque rows of 4x4 blocks. They travel as exact byte
 * slices of whole block rows, never unpacked, copied before each call returns.
 * The host rejects a malformed record by failing the whole batch and with it
 * the context, so everything it would reject is a local GL error instead.
 * Rules that depend on the existing level (edge-aligned sizes, format match,
 * bounds) are checked by the host against native state and reported as GL
 * errors there; the limits here only keep every record well formed.
 */
#include "internal.h"

extern "C" {

#define JGL_PROXY_TEXTURE_2D 0x8064
#define JGL_TEXTURE_COMPRESSED_IMAGE_SIZE 0x86a0
#define JGL_TEXTURE_COMPRESSED 0x86a1

/* Bytes per block of the four S3TC formats; zero for any other enum. */
static ULONG CompressedBlockBytes(GLenum format) {
    switch (format) {
        case 0x83f0: /* COMPRESSED_RGB_S3TC_DXT1 */
        case 0x83f1: /* COMPRESSED_RGBA_S3TC_DXT1 */
            return 8;
        case 0x83f2: /* COMPRESSED_RGBA_S3TC_DXT3 */
        case 0x83f3: /* COMPRESSED_RGBA_S3TC_DXT5 */
            return 16;
        default:
            return 0;
    }
}
/* Callers bound both dimensions by DG_GL_MAX_TEXTURE_DIMENSION. */
static ULONG CompressedImageBytes(ULONG width, ULONG height, ULONG block) {
    const ULONG side = DG_GL_COMPRESSED_BLOCK;
    return (width + side - 1) / side * ((height + side - 1) / side) * block;
}
static BOOL CompressedLevel(GLint level) {
    return level >= 0 && level <= DG_GL_MAX_TEXTURE_LEVEL;
}
/* Payload bytes of one eight-word sub-image record, a multiple of four. */
static ULONG CompressedCapacity(ULONG block) {
    ULONG capacity = JglMaxDataBytes(8) & ~3UL;
    if (capacity < block) {
        JglCommandError(GL_OUT_OF_MEMORY);
        return 0;
    }
    return capacity;
}

/* Sub-image records of whole block rows: each covers the full width and as
 * many block rows as fit, the last one the remaining (possibly partial)
 * height. A block row larger than one record is split into chunks of whole
 * blocks, the last chunk taking the remaining width. Validation is complete. */
static BOOL CompressedBands(GLenum target, GLint level, ULONG x, ULONG y, ULONG width, ULONG height,
                            GLenum format, ULONG block, ULONG capacity, const BYTE *data) {
    const ULONG side = DG_GL_COMPRESSED_BLOCK;
    const ULONG across = (width + side - 1) / side, down = (height + side - 1) / side;
    const ULONG stride = across * block;
    ULONG row = 0, args[8];
    args[0] = target;
    args[1] = (ULONG)level;
    args[6] = format;
    while (row < down) {
        ULONG rows = 1, first = 0, count;
        if (stride <= capacity) {
            rows = capacity / stride;
            if (rows > down - row)
                rows = down - row;
        }
        do {
            count = across - first;
            if (count * block > capacity)
                count = capacity / block;
            args[2] = x + first * side;
            args[3] = y + row * side;
            args[4] = count * side < width - first * side ? count * side : width - first * side;
            args[5] = rows * side < height - row * side ? rows * side : height - row * side;
            args[7] = count * rows * block;
            if (!JglData(FEnum_glCompressedTexSubImage2D, args, 8,
                         data + row * stride + first * block, args[7]))
                return FALSE;
            first += count;
        } while (first < across);
        row += rows;
    }
    return TRUE;
}

/* Proxies only query native feasibility: they execute immediately, even while
 * compiling a display list, and never read caller memory. */
static void CompressedImage(GLenum target, GLint level, GLenum internal_format, GLsizei width,
                            GLsizei height, GLint border, GLsizei image_size, const void *data) {
    const BOOL cube_proxy = target == JGL_PROXY_TEXTURE_CUBE_MAP && JglTextureCubeMapAvailable();
    const BOOL proxy = target == JGL_PROXY_TEXTURE_2D || cube_proxy;
    void (*error)(GLenum) = proxy ? JglSetError : JglCommandError;
    ULONG block, bytes, capacity;
    if (!(proxy ? JglReady() : JglCommandReady()))
        return;
    if (!JglCompressedTexturesAvailable()) {
        error(GL_INVALID_OPERATION);
        return;
    }
    block = CompressedBlockBytes(internal_format);
    if ((!JglImage2DTarget(target) && !proxy) || !block) {
        error(GL_INVALID_ENUM);
        return;
    }
    /* Cube map faces are square. */
    if (border || !CompressedLevel(level) || width < 0 || height < 0 ||
        ((cube_proxy || JglCubeFace(target)) && width != height) ||
        (ULONG)width > (ULONG)DG_GL_MAX_TEXTURE_DIMENSION >> level ||
        (ULONG)height > (ULONG)DG_GL_MAX_TEXTURE_DIMENSION >> level) {
        error(GL_INVALID_VALUE);
        return;
    }
    bytes = CompressedImageBytes(width, height, block);
    if (image_size < 0 || (ULONG)image_size != bytes) {
        error(GL_INVALID_VALUE);
        return;
    }
    const ULONG args[7] = {target, (ULONG)level, internal_format, (ULONG)width, (ULONG)height,
                           0,      bytes};
    if (proxy || !bytes)
        data = NULL;
    /* No payload defines zero-initialized storage of the level. */
    if (!data || bytes <= JglMaxDataBytes(7)) {
        JglData(FEnum_glCompressedTexImage2D, args, 7, data, data ? bytes : 0);
        return;
    }
    capacity = CompressedCapacity(block);
    if (capacity && JglData(FEnum_glCompressedTexImage2D, args, 7, NULL, 0))
        CompressedBands(target, level, 0, 0, width, height, internal_format, block, capacity,
                        (const BYTE *)data);
}

static void CompressedSubImage(GLenum target, GLint level, GLint x, GLint y, GLsizei width,
                               GLsizei height, GLenum format, GLsizei image_size,
                               const void *data) {
    ULONG block, limit, capacity;
    if (!JglCommandReady())
        return;
    if (!JglCompressedTexturesAvailable()) {
        JglCommandError(GL_INVALID_OPERATION);
        return;
    }
    block = CompressedBlockBytes(format);
    if (!JglImage2DTarget(target) || !block) {
        JglCommandError(GL_INVALID_ENUM);
        return;
    }
    if (!CompressedLevel(level) || x < 0 || y < 0 || width < 0 || height < 0) {
        JglCommandError(GL_INVALID_VALUE);
        return;
    }
    /* No level of this index can contain the rectangle. */
    limit = (ULONG)DG_GL_MAX_TEXTURE_DIMENSION >> level;
    if ((ULONG)x > limit || (ULONG)y > limit || (ULONG)width > limit - x ||
        (ULONG)height > limit - y) {
        JglCommandError(GL_INVALID_VALUE);
        return;
    }
    if ((x | y) % DG_GL_COMPRESSED_BLOCK) {
        JglCommandError(GL_INVALID_OPERATION);
        return;
    }
    if (image_size < 0 || (ULONG)image_size != CompressedImageBytes(width, height, block)) {
        JglCommandError(GL_INVALID_VALUE);
        return;
    }
    if (!width || !height)
        return;
    if (!data) {
        JglCommandError(GL_INVALID_VALUE);
        return;
    }
    capacity = CompressedCapacity(block);
    if (capacity)
        CompressedBands(target, level, x, y, width, height, format, block, capacity,
                        (const BYTE *)data);
}

/* Native compressed bytes, read in bounded QUERY chunks straight into the
 * caller's image. Pixel-store state does not apply to compressed images. */
static void CompressedRead(GLenum target, GLint level, void *image) {
    GLint compressed = 0, size = 0;
    ULONG args[3], bytes = 0, at, words, total, capacity;
    if (!JglReady())
        return;
    if (!JglCompressedTexturesAvailable()) {
        JglSetError(GL_INVALID_OPERATION);
        return;
    }
    if (!JglImage2DTarget(target)) {
        JglSetError(GL_INVALID_ENUM);
        return;
    }
    if (!CompressedLevel(level)) {
        JglSetError(GL_INVALID_VALUE);
        return;
    }
    if (!image)
        return;
    args[0] = target;
    args[1] = (ULONG)level;
    args[2] = JGL_TEXTURE_COMPRESSED;
    if (!JglQuery(FEnum_glGetTexLevelParameteriv, args, DG_GL_RESULT_INT, &compressed,
                  sizeof(compressed), &bytes))
        return;
    if (bytes != sizeof(compressed) || !compressed) {
        JglSetError(GL_INVALID_OPERATION);
        return;
    }
    args[2] = JGL_TEXTURE_COMPRESSED_IMAGE_SIZE;
    if (!JglQuery(FEnum_glGetTexLevelParameteriv, args, DG_GL_RESULT_INT, &size, sizeof(size),
                  &bytes))
        return;
    if (bytes != sizeof(size) || size < 0 || (size & 3) ||
        (ULONG)size >
            (ULONG)(DG_GL_MAX_TEXTURE_DIMENSION + 2) * (DG_GL_MAX_TEXTURE_DIMENSION + 2)) {
        JglSetError(GL_INVALID_OPERATION);
        return;
    }
    total = (ULONG)size / 4;
    /* Bulk reads (DG_CAP2_GL_BULK_READ): whole DG_GL_TEXTURE_READ_UNIT words
     * of the negotiated result size, of which only the image's words are
     * copied out; the host zero-fills the last unit past the image end. */
    capacity = JglBulkCapacity() / 4 / DG_GL_TEXTURE_READ_UNIT * DG_GL_TEXTURE_READ_UNIT;
    if (capacity) {
        for (at = 0; at < total; at += words) {
            words = total - at < capacity ? total - at : capacity;
            const ULONG units = (words + DG_GL_TEXTURE_READ_UNIT - 1) / DG_GL_TEXTURE_READ_UNIT;
            const ULONG *source;
            args[1] =
                (ULONG)level | DG_GL_TEXTURE_READ_UNITS | (units << DG_GL_TEXTURE_READ_COUNT_SHIFT);
            args[2] = at;
            source =
                (const ULONG *)JglQueryBulk(FEnum_glGetCompressedTexImage, args, DG_GL_RESULT_INT,
                                            units * DG_GL_TEXTURE_READ_UNIT * 4, &bytes);
            if (!source)
                return;
            if (bytes != units * DG_GL_TEXTURE_READ_UNIT * 4) {
                JglSetError(GL_INVALID_OPERATION);
                return;
            }
            CopyMemory((BYTE *)image + at * 4, source, words * 4);
        }
        return;
    }
    capacity = JglReadbackCapacity() / 4;
    for (at = 0; at < total; at += words) {
        words = total - at < capacity ? total - at : capacity;
        args[1] = (ULONG)level | (words << DG_GL_TEXTURE_READ_COUNT_SHIFT);
        args[2] = at;
        if (!JglQuery(FEnum_glGetCompressedTexImage, args, DG_GL_RESULT_INT, (BYTE *)image + at * 4,
                      words * 4, &bytes))
            return;
        if (bytes != words * 4) {
            JglSetError(GL_INVALID_OPERATION);
            return;
        }
    }
}

void APIENTRY glCompressedTexImage2D(GLenum target, GLint level, GLenum internal_format,
                                     GLsizei width, GLsizei height, GLint border,
                                     GLsizei image_size, const void *data) {
    CompressedImage(target, level, internal_format, width, height, border, image_size, data);
}
void APIENTRY glCompressedTexImage2DARB(GLenum target, GLint level, GLenum internal_format,
                                        GLsizei width, GLsizei height, GLint border,
                                        GLsizei image_size, const void *data) {
    CompressedImage(target, level, internal_format, width, height, border, image_size, data);
}
void APIENTRY glCompressedTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei width,
                                        GLsizei height, GLenum format, GLsizei image_size,
                                        const void *data) {
    CompressedSubImage(target, level, x, y, width, height, format, image_size, data);
}
void APIENTRY glCompressedTexSubImage2DARB(GLenum target, GLint level, GLint x, GLint y,
                                           GLsizei width, GLsizei height, GLenum format,
                                           GLsizei image_size, const void *data) {
    CompressedSubImage(target, level, x, y, width, height, format, image_size, data);
}
void APIENTRY glGetCompressedTexImage(GLenum target, GLint level, void *image) {
    CompressedRead(target, level, image);
}
void APIENTRY glGetCompressedTexImageARB(GLenum target, GLint level, void *image) {
    CompressedRead(target, level, image);
}

} /* extern C */
