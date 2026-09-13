/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef DG_OPENGL_INTERNAL_H
#define DG_OPENGL_INTERNAL_H
#define WIN32_LEAN_AND_MEAN
#define _GDI32_
#include <windows.h>
#include <GL/gl.h>
#include "gl.h"
#include "gl-funcs.h"
typedef struct {
    GLint Alignment, RowLength, SkipRows, SkipPixels, SwapBytes, LsbFirst;
} JGL_UNPACK;
/* CPU client pointers are local to their owning WGL context. Draw commands
 * snapshot them into the existing immutable packed-vertex ABI. */
typedef struct {
    GLint Size, Stride;
    GLenum Type;
    const void *Pointer;
    BOOL Enabled;
} JGL_ARRAY;
typedef struct {
    ULONG ServerDepth, ServerMasks[16], ServerDrawBuffers[16];
    JGL_ARRAY Attribute[5]; /* position, color, normal, texture coordinate, secondary color */
    BYTE Scratch[DG_GL_MAX_VERTICES < 1024 ? DG_GL_MAX_VERTICES * DG_GL_VERTEX_BYTES : 65536];
} JGL_ARRAY_STATE;
#ifdef __cplusplus
extern "C" {
#endif
JGL_ARRAY_STATE *JglArrays(void);
BOOL JglArrayQuery(GLenum pname, GLint *value);
BOOL JglReady(void);
BOOL JglSupportsSecondary(void);
GLfloat JglColorComponent(const BYTE *value, GLenum type);
void JglScalarVector(ULONG function, ULONG words, const void *arguments);
HINSTANCE JglModule(void);
void JglSetError(GLenum error);
void JglForgetTextures(ULONG count, const GLuint *textures);
BOOL JglData(ULONG function, const ULONG *arguments, ULONG words, const void *payload, ULONG bytes);
ULONG JglMaxDataBytes(ULONG words);
JGL_UNPACK *JglUnpack(void);
JGL_UNPACK *JglPack(void);
void JglDrawableSize(ULONG *width, ULONG *height);
ULONG JglReadbackCapacity(void);
ULONG *JglReadbackBuffer(void);
BOOL JglQuery(ULONG function, const ULONG arguments[3], ULONG type, void *output, ULONG capacity,
              ULONG *bytes);
#ifdef __cplusplus
}
#endif
#endif
