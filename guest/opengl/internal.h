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
/* Stack entries borrow the same application-owned client pointers as the live
 * descriptor; snapshots never own/free array storage or copy vertex data. */
typedef struct {
    GLbitfield Mask;
    JGL_ARRAY Attribute[7];
    JGL_UNPACK Pack, Unpack;
} JGL_CLIENT_SNAPSHOT;
typedef struct {
    void *SelectPointer, *FeedbackPointer;
    ULONG SelectSize, FeedbackSize, CaptureMode;
    ULONG ListMode, ListAware;
    ULONG ClientDepth;
    JGL_CLIENT_SNAPSHOT ClientStack[16];
    ULONG ServerDepth, ServerMasks[16], ServerDrawBuffers[16];
    JGL_ARRAY Attribute[7]; /* position, color, normal, texture coordinate, secondary color, index,
                               edge */
    BYTE Scratch[DG_GL_MAX_VERTICES < 1024 ? DG_GL_MAX_VERTICES * DG_GL_VERTEX_BYTES : 65536];
} JGL_ARRAY_STATE;
#ifdef __cplusplus
extern "C" {
#endif
JGL_ARRAY_STATE *JglArrays(void);
BOOL JglArrayQuery(GLenum pname, GLint *value);
void JglArrayElement(GLint index);
void JglInterleavedArrays(GLenum format, GLsizei stride, const void *pointer);
BOOL JglReady(void);
BOOL JglCommandReady(void);
BOOL JglListMode(ULONG mode);
BOOL JglCompiling(void);
void JglCallList(ULONG name);
BOOL JglCallLists(ULONG count, const ULONG *offsets);
BOOL JglSupportsSecondary(void);
GLfloat JglColorComponent(const BYTE *value, GLenum type);
void JglScalarVector(ULONG function, ULONG words, const void *arguments);
HINSTANCE JglModule(void);
void JglSetError(GLenum error);
void JglCommandError(GLenum error);
void JglForgetTextures(ULONG count, const GLuint *textures);
BOOL JglData(ULONG function, const ULONG *arguments, ULONG words, const void *payload, ULONG bytes);
ULONG JglMaxDataBytes(ULONG words);
ULONG JglNextImageId(void);
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
