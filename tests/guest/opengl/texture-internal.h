/* SPDX-License-Identifier: GPL-2.0-or-later
 * Minimal platform shim for compiling the actual texture.c packing code.
 */
#include <stddef.h>
#include <stdint.h>
typedef uint32_t ULONG, GLenum;
typedef uintptr_t ULONG_PTR;
typedef int32_t GLint, GLsizei;
typedef int BOOL;
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
    GLint Alignment, RowLength, SkipRows, SkipPixels, SwapBytes, LsbFirst;
} JGL_UNPACK;
BOOL JglReady(void);
void JglSetError(GLenum);
BOOL JglData(ULONG, const ULONG *, ULONG, const void *, ULONG);
BOOL JglQuery(ULONG, const ULONG *, ULONG, void *, ULONG, ULONG *);
BOOL JglCompiling(void);
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
