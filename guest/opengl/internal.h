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
/* Client array slots follow the DrawArraysEXT descriptor order: seven legacy
 * slots, texture coordinates of units 1-7, then the fog coordinate. Without
 * negotiated multitexture the client-active unit stays 0 and slots 7-13 stay
 * disabled; without negotiated fog coordinates slot 14 stays disabled. */
#define JGL_ARRAY_SLOTS 15
#define JGL_FOG_SLOT 14
#define JGL_TEXTURE0 0x84c0 /* GL_TEXTURE0 */
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
    JGL_ARRAY Attribute[JGL_ARRAY_SLOTS];
    ULONG ClientTexture;
    JGL_UNPACK Pack, Unpack;
} JGL_CLIENT_SNAPSHOT;
typedef struct {
    void *SelectPointer, *FeedbackPointer;
    ULONG SelectSize, FeedbackSize, CaptureMode;
    ULONG ListMode, ListAware;
    ULONG ClientDepth;
    JGL_CLIENT_SNAPSHOT ClientStack[16];
    ULONG ServerDepth, ServerMasks[16], ServerDrawBuffers[16];
    /* position, color, normal, texture coordinate (unit 0), secondary color,
     * index, edge, texture coordinates of units 1-7, fog coordinate */
    JGL_ARRAY Attribute[JGL_ARRAY_SLOTS];
    ULONG ClientTexture; /* client-active unit, 0-7 */
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
/* Capture runs synchronously into reserved packet bytes. It must not issue GL
 * commands, allocate, fail, or retain the destination beyond its return. */
typedef void (*JGL_CAPTURE)(void *opaque, BYTE *destination);
BOOL JglCaptureData(ULONG function, const ULONG *arguments, ULONG words, ULONG bytes,
                    JGL_CAPTURE capture, void *opaque);
BOOL JglData(ULONG function, const ULONG *arguments, ULONG words, const void *payload, ULONG bytes);
ULONG JglMaxDataBytes(ULONG words);
ULONG JglNextImageId(void);
JGL_UNPACK *JglUnpack(void);
JGL_UNPACK *JglPack(void);
void JglDrawableSize(ULONG *width, ULONG *height);
/* Typed canonical color/depth/stencil reads require explicit host negotiation. */
BOOL JglDepthStencilReadbackAvailable(void);
BOOL JglTextureImagesAvailable(void);
BOOL JglTextureBordersAvailable(void);
/* ARB_multitexture, combine and dot3 environments, and DrawArraysEXT. */
BOOL JglMultitextureAvailable(void);
/* Blend color/equation/separate, separate stencil, point parameters/sprites,
 * anisotropy and LOD bias. */
BOOL JglFixedStateAvailable(void);
/* ATI_texture_env_combine3, ATI_texture_mirror_once and SGIS_generate_mipmap. */
BOOL JglTextureControlAvailable(void);
/* ARB_texture_compression with EXT_texture_compression_s3tc, TEXTURE_2D only. */
BOOL JglCompressedTexturesAvailable(void);
/* EXT_fog_coord: FogCoordf, the fog coordinate array and FOG_COORDINATE_SOURCE. */
BOOL JglFogCoordAvailable(void);
/* Value counts of the TexParameter/TexEnv names the host accepts, and Hint
 * validity. Scalar and vector entrypoints share them: an unknown name is a local
 * GL_INVALID_ENUM, never a record the host rejects as malformed (which would
 * fail the whole batch and the context). Zero means invalid. */
static inline ULONG JglTextureParameterCount(GLenum target, GLenum pname) {
    if (target != 0x0de0 && target != 0x0de1) /* TEXTURE_1D, TEXTURE_2D */
        return 0;
    switch (pname) {
        case 0x1004: /* TEXTURE_BORDER_COLOR */
            return 4;
        case 0x2801:
        case 0x2800: /* TEXTURE_MIN/MAG_FILTER */
        case 0x2802:
        case 0x2803: /* TEXTURE_WRAP_S/T */
        case 0x8066: /* TEXTURE_PRIORITY */
        case 0x813c: /* TEXTURE_BASE_LEVEL */
        case 0x813d: /* TEXTURE_MAX_LEVEL */
            return 1;
        case 0x84fe: /* TEXTURE_MAX_ANISOTROPY_EXT */
            return JglFixedStateAvailable() ? 1 : 0;
        case 0x8191: /* GENERATE_MIPMAP_SGIS */
            return JglTextureControlAvailable() ? 1 : 0;
        default:
            return 0;
    }
}
static inline ULONG JglTextureEnvCount(GLenum target, GLenum pname) {
    /* TEXTURE_FILTER_CONTROL/TEXTURE_LOD_BIAS, POINT_SPRITE/COORD_REPLACE */
    if (target == 0x8500 || target == 0x8861)
        return JglFixedStateAvailable() && pname == (target == 0x8500 ? 0x8501U : 0x8862U) ? 1 : 0;
    if (target != 0x2300) /* TEXTURE_ENV */
        return 0;
    switch (pname) {
        case 0x2201: /* TEXTURE_ENV_COLOR */
            return 4;
        case 0x2200: /* TEXTURE_ENV_MODE */
        case 0x8571:
        case 0x8572: /* COMBINE_RGB/ALPHA */
        case 0x8573:
        case 0x0d1c: /* RGB_SCALE/ALPHA_SCALE */
        case 0x8580:
        case 0x8581:
        case 0x8582: /* SOURCE0..2_RGB */
        case 0x8588:
        case 0x8589:
        case 0x858a: /* SOURCE0..2_ALPHA */
        case 0x8590:
        case 0x8591:
        case 0x8592: /* OPERAND0..2_RGB */
        case 0x8598:
        case 0x8599:
        case 0x859a: /* OPERAND0..2_ALPHA */
            return 1;
        default:
            return 0;
    }
}
/* Fog parameter value counts shared by Fogf/Fogi and the vector forms; zero
 * means GL_INVALID_ENUM. Values are never judged locally. */
static inline ULONG JglFogCount(GLenum pname) {
    switch (pname) {
        case 0x0b61: /* FOG_INDEX */
        case 0x0b62: /* FOG_DENSITY */
        case 0x0b63: /* FOG_START */
        case 0x0b64: /* FOG_END */
        case 0x0b65: /* FOG_MODE */
            return 1;
        case 0x0b66: /* FOG_COLOR */
            return 4;
        case 0x8450: /* FOG_COORDINATE_SOURCE_EXT */
            return JglFogCoordAvailable() ? 1 : 0;
        default:
            return 0;
    }
}
static inline BOOL JglHintValid(GLenum target, GLenum mode) {
    if (mode < 0x1100 || mode > 0x1102) /* DONT_CARE, FASTEST, NICEST */
        return FALSE;
    /* PERSPECTIVE_CORRECTION, POINT/LINE/POLYGON_SMOOTH and FOG hints */
    return (target >= 0x0c50 && target <= 0x0c54) ||
           (target == 0x8192 /* GENERATE_MIPMAP_HINT_SGIS */ && JglTextureControlAvailable()) ||
           (target == 0x84ef /* TEXTURE_COMPRESSION_HINT */ && JglCompressedTexturesAvailable());
}
/* Negotiated GL_EXTENSIONS: static storage, valid for the process lifetime. */
const GLubyte *JglExtensions(void);
ULONG JglReadbackCapacity(void);
ULONG *JglReadbackBuffer(void);
BOOL JglQuery(ULONG function, const ULONG arguments[3], ULONG type, void *output, ULONG capacity,
              ULONG *bytes);
#ifdef __cplusplus
}
#endif
#endif
