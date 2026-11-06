/* SPDX-License-Identifier: GPL-2.0-or-later
 * Minimal platform shim for compiling the actual texture.c packing code.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef uint32_t ULONG, GLenum;
typedef uintptr_t ULONG_PTR;
typedef int32_t GLint, GLsizei;
typedef int BOOL;
typedef unsigned char BYTE;
#define CopyMemory(d, s, n) memcpy((d), (s), (n))
#define APIENTRY
#define TRUE 1
#define FALSE 0
#define GL_NO_ERROR 0
#define GL_INVALID_ENUM 0x500
#define GL_INVALID_VALUE 0x501
#define GL_INVALID_OPERATION 0x502
#define GL_OUT_OF_MEMORY 0x505
#define GL_TEXTURE_2D 0x0de1
#define GL_UNSIGNED_BYTE 0x1401
#define GL_ALPHA 0x1906
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_LUMINANCE 0x1909
#define GL_LUMINANCE_ALPHA 0x190a
#define GL_UNPACK_SWAP_BYTES 0x0cf0
#define GL_UNPACK_LSB_FIRST 0x0cf1
#define GL_PACK_SWAP_BYTES 0x0d00
#define GL_PACK_LSB_FIRST 0x0d01
#define GL_UNPACK_ROW_LENGTH 0x0cf2
#define GL_UNPACK_SKIP_ROWS 0x0cf3
#define GL_UNPACK_SKIP_PIXELS 0x0cf4
#define GL_UNPACK_ALIGNMENT 0x0cf5
#define GL_PACK_ROW_LENGTH 0x0d02
#define GL_PACK_SKIP_ROWS 0x0d03
#define GL_PACK_SKIP_PIXELS 0x0d04
#define GL_PACK_ALIGNMENT 0x0d05
#include "gl.h"
#include "gl-funcs.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    GLint Alignment, RowLength, SkipRows, SkipPixels, SwapBytes, LsbFirst, ImageHeight, SkipImages;
} JGL_UNPACK;
BOOL JglReady(void);
void JglSetError(GLenum);
BOOL JglData(ULONG, const ULONG *, ULONG, const void *, ULONG);
typedef void (*JGL_CAPTURE)(void *opaque, BYTE *destination);
BOOL JglCaptureData(ULONG, const ULONG *, ULONG, ULONG, JGL_CAPTURE, void *);
BOOL JglQuery(ULONG, const ULONG *, ULONG, void *, ULONG, ULONG *);
BOOL JglCompiling(void);
BOOL JglTextureImagesAvailable(void);
BOOL JglTextureBordersAvailable(void);
BOOL JglCompressedTexturesAvailable(void);
BOOL JglTextureCubeMapAvailable(void);
/* Cube map target helpers, as in guest/opengl/internal.h. */
#define JGL_TEXTURE_CUBE_MAP 0x8513
#define JGL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515
#define JGL_PROXY_TEXTURE_CUBE_MAP 0x851b
static inline BOOL JglCubeFace(GLenum target) {
    return target - JGL_TEXTURE_CUBE_MAP_POSITIVE_X < 6 && JglTextureCubeMapAvailable();
}
static inline BOOL JglImage2DTarget(GLenum target) {
    return target == 0x0de1 /* TEXTURE_2D */ || JglCubeFace(target);
}
/* 3D texture enums, as in guest/opengl/internal.h. */
BOOL JglTexture3DAvailable(void);
#define JGL_TEXTURE_3D 0x806f
#define JGL_PROXY_TEXTURE_3D 0x8070
#define JGL_TEXTURE_WRAP_R 0x8072
/* Rectangle textures, as in guest/opengl/internal.h. */
BOOL JglTextureRectangleAvailable(void);
#define JGL_TEXTURE_RECTANGLE 0x84f5
#define JGL_PROXY_TEXTURE_RECTANGLE 0x84f7
static inline BOOL JglRectangleTarget(GLenum target) {
    return target == JGL_TEXTURE_RECTANGLE && JglTextureRectangleAvailable();
}
/* Depth textures and DEPTH_STENCIL pixels, as in guest/opengl/internal.h. */
BOOL JglFramebufferObjectAvailable(void);
#define JGL_DEPTH_STENCIL 0x84f9
#define JGL_UNSIGNED_INT_24_8 0x84fa
static inline BOOL JglDepthInternalFormat(GLint format) {
    return format == 0x1902 || (format >= 0x81a5 && format <= 0x81a7) ||
           format == JGL_DEPTH_STENCIL || format == 0x88f0;
}
ULONG JglMaxDataBytes(ULONG);
JGL_UNPACK *JglUnpack(void);
JGL_UNPACK *JglPack(void);
void glPixelStorei(GLenum, GLint);
void glTexImage2D(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
void glTexSubImage2D(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *);

#define GL_TEXTURE_1D 0x0de0

void glTexImage1D(GLenum, GLint, GLint, GLsizei, GLint, GLenum, GLenum, const void *);
void glTexSubImage1D(GLenum, GLint, GLint, GLsizei, GLenum, GLenum, const void *);

#ifdef __cplusplus
}
#endif

/* These isolated arithmetic tests model ordinary execution, outside lists. */
#define JglCommandError JglSetError
#define JglCommandReady JglReady
