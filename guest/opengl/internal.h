/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef DG_OPENGL_INTERNAL_H
#define DG_OPENGL_INTERNAL_H
#define WIN32_LEAN_AND_MEAN
#define _GDI32_
#include <windows.h>
#include <GL/gl.h>
#include "gl.h"
#include "gl-funcs.h"
/* ImageHeight and SkipImages exist with negotiated 3D textures (EXT_texture3D). */
typedef struct {
    GLint Alignment, RowLength, SkipRows, SkipPixels, SwapBytes, LsbFirst, ImageHeight, SkipImages;
} JGL_UNPACK;
/* Client array slots follow the DrawArraysEXT descriptor order: seven legacy
 * slots, texture coordinates of units 1-7, then the fog coordinate. Without
 * negotiated multitexture the client-active unit stays 0 and slots 7-13 stay
 * disabled; without negotiated fog coordinates slot 14 stays disabled. */
#define JGL_ARRAY_SLOTS 15
#define JGL_FOG_SLOT 14
#define JGL_TEXTURE0 0x84c0 /* GL_TEXTURE0 */
/* CPU client pointers are local to their owning WGL context. Draw commands
 * snapshot them into the existing immutable packed-vertex ABI. Normalized is
 * the generic attribute arrays' flag (ARB_vertex_program); conventional
 * arrays leave it zero. With ARB_vertex_buffer_object, Buffer is the
 * ARRAY_BUFFER binding latched by the pointer call (zero: client memory) and
 * Pointer its byte offset; Serial identifies the object it named then. Bgra
 * is size BGRA (ARB_vertex_array_bgra): Size 4 and Type UNSIGNED_BYTE, the
 * bytes in B, G, R, A order; queries report BGRA as the size. */
typedef struct {
    GLint Size, Stride;
    GLenum Type;
    const void *Pointer;
    BOOL Enabled, Normalized;
    GLuint Buffer;
    ULONG Serial;
    BOOL Bgra;
} JGL_ARRAY;
/* Stack entries borrow the same application-owned client pointers as the live
 * descriptor; snapshots never own/free array storage or copy vertex data. */
/* ARRAY_BUFFER and ELEMENT_ARRAY_BUFFER bindings: a name and the identity of
 * the object it named when bound (vertex-array client state). */
typedef struct {
    GLuint Name;
    ULONG Serial;
} JGL_BUFFER_BINDING;
typedef struct {
    GLbitfield Mask;
    JGL_ARRAY Attribute[JGL_ARRAY_SLOTS];
    JGL_ARRAY Generic[DG_GL_MAX_VERTEX_ATTRIBS];
    ULONG ClientTexture;
    JGL_BUFFER_BINDING ArrayBuffer, ElementBuffer;
    ULONG LockFirst, LockCount;
    JGL_UNPACK Pack, Unpack;
} JGL_CLIENT_SNAPSHOT;
typedef struct {
    void *SelectPointer, *FeedbackPointer;
    ULONG SelectSize, FeedbackSize, CaptureMode;
    ULONG ListMode, ListAware;
    ULONG ClientDepth;
    JGL_CLIENT_SNAPSHOT ClientStack[16];
    /* PushAttrib entries: the mask, the window's draw buffer and the draw
     * framebuffer name (EXT_framebuffer_object; zero is the window). */
    ULONG ServerDepth, ServerMasks[16], ServerDrawBuffers[16], ServerDrawFramebuffers[16];
    /* position, color, normal, texture coordinate (unit 0), secondary color,
     * index, edge, texture coordinates of units 1-7, fog coordinate */
    JGL_ARRAY Attribute[JGL_ARRAY_SLOTS];
    /* ARB_vertex_program generic attribute arrays 0-15; never aliased with
     * the conventional arrays, except that array 0 is the position. */
    JGL_ARRAY Generic[DG_GL_MAX_VERTEX_ATTRIBS];
    ULONG ClientTexture; /* client-active unit, 0-7 */
    JGL_BUFFER_BINDING ArrayBuffer, ElementBuffer;
    /* EXT_compiled_vertex_array: the locked range (count zero: unlocked). */
    ULONG LockFirst, LockCount;
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
/* ARB_texture_cube_map: the cube map binding target, six face image targets,
 * its proxy, TEXTURE_WRAP_R and the NORMAL_MAP/REFLECTION_MAP texgen modes. */
BOOL JglTextureCubeMapAvailable(void);
#define JGL_TEXTURE_CUBE_MAP 0x8513
#define JGL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515
#define JGL_PROXY_TEXTURE_CUBE_MAP 0x851b
/* A face target, POSITIVE_X..NEGATIVE_Z, once negotiated. */
static inline BOOL JglCubeFace(GLenum target) {
    return target - JGL_TEXTURE_CUBE_MAP_POSITIVE_X < 6 && JglTextureCubeMapAvailable();
}
/* Image targets of the 2D records: TEXTURE_2D or, once negotiated, one face. */
static inline BOOL JglImage2DTarget(GLenum target) {
    return target == 0x0de1 /* TEXTURE_2D */ || JglCubeFace(target);
}
/* Cube map enums (TEXTURE_CUBE_MAP through MAX_CUBE_MAP_TEXTURE_SIZE) as query
 * names: without negotiation they stay the local GL_INVALID_ENUM they were. */
static inline BOOL JglCubeMapQueryRejected(GLenum name) {
    return name - JGL_TEXTURE_CUBE_MAP <= 0x851c - JGL_TEXTURE_CUBE_MAP &&
           !JglTextureCubeMapAvailable();
}
/* EXT_texture3D: the TEXTURE_3D binding target, its proxy, 3D images, subimages,
 * copies and readback, TEXTURE_WRAP_R on every target, TEXTURE_DEPTH and the
 * IMAGE_HEIGHT/SKIP_IMAGES pixel storage. */
BOOL JglTexture3DAvailable(void);
#define JGL_TEXTURE_3D 0x806f
#define JGL_PROXY_TEXTURE_3D 0x8070
#define JGL_TEXTURE_WRAP_R 0x8072
/* EXT_texture3D enums as query names (TEXTURE_BINDING_3D, the pixel storage,
 * TEXTURE_3D, its proxy, TEXTURE_DEPTH, MAX_3D_TEXTURE_SIZE): without
 * negotiation they stay the local GL_INVALID_ENUM they were. TEXTURE_WRAP_R is
 * a parameter name, gated with the target (JglTextureParameterCount). */
static inline BOOL JglTexture3DQueryRejected(GLenum name) {
    return name - 0x806a <= 0x8073 - 0x806a && name != JGL_TEXTURE_WRAP_R &&
           !JglTexture3DAvailable();
}
/* ARB_texture_rectangle (NV_texture_rectangle): the TEXTURE_RECTANGLE binding
 * target, its enable and proxy, level-zero borderless uncompressed images of
 * any size up to DG_GL_MAX_TEXTURE_DIMENSION through the 2D image, copy,
 * readback and framebuffer attachment records, and its state queries. */
BOOL JglTextureRectangleAvailable(void);
#define JGL_TEXTURE_RECTANGLE 0x84f5
#define JGL_PROXY_TEXTURE_RECTANGLE 0x84f7
/* TEXTURE_RECTANGLE, once negotiated. */
static inline BOOL JglRectangleTarget(GLenum target) {
    return target == JGL_TEXTURE_RECTANGLE && JglTextureRectangleAvailable();
}
/* Rectangle enums (TEXTURE_RECTANGLE, TEXTURE_BINDING_RECTANGLE, its proxy and
 * MAX_RECTANGLE_TEXTURE_SIZE) as query names: without negotiation they stay
 * the local GL_INVALID_ENUM they were. */
static inline BOOL JglRectangleQueryRejected(GLenum name) {
    return name - JGL_TEXTURE_RECTANGLE <= 0x84f8 - JGL_TEXTURE_RECTANGLE &&
           !JglTextureRectangleAvailable();
}
/* ARB_fragment_program: program objects, ProgramString, env/local parameters,
 * the FRAGMENT_PROGRAM_ARB enable and its state queries. */
BOOL JglFragmentProgramAvailable(void);
#define JGL_FRAGMENT_PROGRAM 0x8804
/* ARB_vertex_program: the same program functions for the VERTEX_PROGRAM_ARB
 * target, generic vertex attributes, the vertex program modes and queries. */
BOOL JglVertexProgramAvailable(void);
#define JGL_VERTEX_PROGRAM 0x8620
/* Either program extension: the shared program object entry points. */
static inline BOOL JglProgramsAvailable(void) {
    return JglFragmentProgramAvailable() || JglVertexProgramAvailable();
}
/* The modes ARB_vertex_program adds to Enable/Disable/IsEnabled:
 * VERTEX_PROGRAM, VERTEX_PROGRAM_POINT_SIZE and VERTEX_PROGRAM_TWO_SIDE. */
static inline BOOL JglVertexProgramMode(GLenum cap) {
    return cap == JGL_VERTEX_PROGRAM || cap == 0x8642 || cap == 0x8643;
}
/* State names the program extensions add to Get*v and IsEnabled. Fragment
 * only: FRAGMENT_PROGRAM and MAX_TEXTURE_COORDS/IMAGE_UNITS. Vertex only: the
 * vertex program modes and MAX_VERTEX_ATTRIBS. Either: PROGRAM_ERROR_POSITION,
 * CURRENT_MATRIX(_STACK_DEPTH), TRANSPOSE_CURRENT_MATRIX and
 * MAX_PROGRAM_MATRICES/_STACK_DEPTH. Without negotiation they stay the local
 * GL_INVALID_ENUM the host returned before. */
static inline BOOL JglProgramQueryRejected(GLenum name) {
    switch (name) {
        case JGL_FRAGMENT_PROGRAM:
        case 0x8871:
        case 0x8872:
            return !JglFragmentProgramAvailable();
        case JGL_VERTEX_PROGRAM:
        case 0x8642:
        case 0x8643:
        case 0x8869:
            return !JglVertexProgramAvailable();
        case 0x864b:
        case 0x8641:
        case 0x8640:
        case 0x88b7:
        case 0x862f:
        case 0x862e:
            return !JglProgramsAvailable();
        default:
            return FALSE;
    }
}
/* ARB_vertex_buffer_object: buffer objects in the shared namespace, their
 * bindings, data, mapping and queries (buffer.cpp), and draws reading them. */
BOOL JglVertexBufferObjectAvailable(void);
/* EXT_compiled_vertex_array (LockArraysEXT, UnlockArraysEXT and the lock
 * queries) comes with the buffer draw, which carries indexed client-array
 * draws in bulk: without it, applications keep their unlocked paths. */
static inline BOOL JglCompiledVertexArrayAvailable(void) {
    return JglVertexBufferObjectAvailable();
}
/* GLsizeiptrARB and GLintptrARB: pointer-sized signed integers. */
typedef __PTRDIFF_TYPE__ JGL_SIZEIPTR;
#define JGL_ARRAY_BUFFER 0x8892
#define JGL_ELEMENT_ARRAY_BUFFER 0x8893
/* One buffer object of a share group. Data is the frontend's copy of the
 * contents (16-byte aligned, never NULL), which records keep equal to the
 * host's: mapping returns it, reads and display lists dereference it. */
typedef struct {
    GLuint Name;
    ULONG Serial, Size;
    GLenum Usage, Access;
    BOOL Mapped;
    BYTE *Data;
    void *Allocation;
} JGL_BUFFER;
/* The share group's buffer table slot of the current context, under the
 * shared lock (buffers.h), recording how the group's last context releases it
 * unless release is NULL; NULL without a context or memory. */
void **JglSharedBuffers(void (*release)(void *table));
void JglLockShared(void);
void JglUnlockShared(void);
void *JglAllocate(ULONG bytes);
void JglFree(void *memory);
/* Reset the current context's bindings of a deleted buffer object. */
void JglForgetBuffer(GLuint name, ULONG serial);
/* ARB_vertex_array_bgra: size BGRA for colour, secondary colour and generic
 * attribute arrays (arrays.cpp, attrib.cpp). */
BOOL JglVertexArrayBgraAvailable(void);
#define JGL_BGRA 0x80e1
/* EXT_framebuffer_object with EXT_framebuffer_blit and EXT_packed_depth_stencil:
 * framebuffer objects of one context, renderbuffers of the share group, their
 * commands and queries (framebuffer.cpp), COLOR_ATTACHMENTn draw and read
 * buffers, depth textures and DEPTH_STENCIL pixels. */
BOOL JglFramebufferObjectAvailable(void);
#define JGL_FRAMEBUFFER 0x8d40
#define JGL_READ_FRAMEBUFFER 0x8ca8
#define JGL_DRAW_FRAMEBUFFER 0x8ca9
#define JGL_RENDERBUFFER 0x8d41
#define JGL_COLOR_ATTACHMENT0 0x8ce0
#define JGL_DEPTH_STENCIL 0x84f9
#define JGL_UNSIGNED_INT_24_8 0x84fa
/* COLOR_ATTACHMENT0..DG_GL_MAX_COLOR_ATTACHMENTS-1. */
static inline BOOL JglColorAttachment(GLenum name) {
    return name - JGL_COLOR_ATTACHMENT0 < DG_GL_MAX_COLOR_ATTACHMENTS;
}
/* The depth texture internal formats: DEPTH_COMPONENT(16/24/32), DEPTH_STENCIL
 * and DEPTH24_STENCIL8 (borderless TEXTURE_2D images only; with depth textures,
 * below, also TEXTURE_1D and TEXTURE_RECTANGLE). */
static inline BOOL JglDepthInternalFormat(GLint format) {
    return format == 0x1902 || (format >= 0x81a5 && format <= 0x81a7) ||
           format == JGL_DEPTH_STENCIL || format == 0x88f0;
}
/* The current context's bindings: the draw and read framebuffer names (zero:
 * the window) and the renderbuffer name, with its framebuffer name table. */
typedef struct {
    GLuint Draw, Read, Renderbuffer;
    void *Names; /* framebuffer.cpp's table, freed with the context */
} JGL_FRAMEBUFFER_STATE;
JGL_FRAMEBUFFER_STATE *JglFramebufferState(void);
/* The share group's renderbuffer name table slot of the current context, under
 * the shared lock; the table is one allocation freed with the group. NULL
 * without a context or memory. */
void **JglSharedRenderbuffers(void);
/* FRAMEBUFFER_BINDING (the draw binding), READ_FRAMEBUFFER_BINDING and
 * RENDERBUFFER_BINDING of the current context, once negotiated. */
BOOL JglFramebufferBinding(GLenum pname, GLint *value);
/* A framebuffer object is bound for reading: ReadPixels need not fit the
 * drawable. */
BOOL JglReadFramebufferBound(void);
/* Those bindings, MAX_COLOR_ATTACHMENTS and MAX_RENDERBUFFER_SIZE as query
 * names: without negotiation they stay the local GL_INVALID_ENUM. */
static inline BOOL JglFramebufferQueryRejected(GLenum name) {
    return (name == 0x8ca6 || name == 0x8ca7 || name == 0x8caa || name == 0x8cdf ||
            name == 0x84e8) &&
           !JglFramebufferObjectAvailable();
}
/* ARB_occlusion_query (SAMPLES_PASSED) and NV_fence (occlusion.cpp). Query
 * and fence objects belong to one context and live on the host; the frontend
 * keeps what the host reported, so it answers repeated polls locally: query
 * results (Values) and fences known signalled (Values unused). Entries are
 * replaced oldest first and dropped when the name is begun, set or deleted, or
 * whenever a display list executes (lists may begin queries and set fences). */
BOOL JglOcclusionQueryAvailable(void);
#define JGL_SETTLED_SLOTS 64
typedef struct {
    ULONG Count, Next;
    GLuint Names[JGL_SETTLED_SLOTS], Values[JGL_SETTLED_SLOTS];
} JGL_SETTLED;
typedef struct {
    JGL_SETTLED Results, Fences;
} JGL_QUERY_STATE;
/* The current context's state; NULL without a context. */
JGL_QUERY_STATE *JglQueryState(void);
/* Per-context NUL-terminated PROGRAM_ERROR_STRING_ARB storage (DG_GL_MAX_RESULT_BYTES),
 * stable until the context is deleted; NULL without a context or memory. */
GLubyte *JglProgramErrorBuffer(void);
/* ARB_depth_texture, ARB_shadow and, with ARB_fragment_program,
 * ARB_fragment_program_shadow: the first feature of the second capability
 * word (gl.h DG_CAP2_GL_DEPTH_TEXTURE). Borderless depth images of TEXTURE_1D,
 * TEXTURE_2D and TEXTURE_RECTANGLE and their proxies in the existing image,
 * copy and readback records (texture.cpp, readback.cpp), the compare and depth
 * texture mode parameters and the depth level sizes. Without it every name
 * below stays the local GL_INVALID_ENUM it was. */
BOOL JglDepthTextureAvailable(void);
#define JGL_TEXTURE_DEPTH_SIZE 0x884a
#define JGL_DEPTH_TEXTURE_MODE 0x884b
#define JGL_TEXTURE_COMPARE_MODE 0x884c
#define JGL_TEXTURE_COMPARE_FUNC 0x884d
#define JGL_COMPARE_R_TO_TEXTURE 0x884e
#define JGL_TEXTURE_STENCIL_SIZE 0x88f1
static inline BOOL JglDepthTextureQueryRejected(GLenum name) {
    return ((name >= JGL_TEXTURE_DEPTH_SIZE && name <= JGL_TEXTURE_COMPARE_FUNC) ||
            name == JGL_TEXTURE_STENCIL_SIZE) &&
           !JglDepthTextureAvailable();
}
/* Values of the depth texture parameters. Native GL accepts more (all eight
 * functions, RED, COMPARE_REF_TO_TEXTURE's alias); ARB_shadow without
 * EXT_shadow_funcs, as a GeForce FX's GL also never reported, takes exactly
 * these, so anything else is the local GL_INVALID_ENUM. Other names' values
 * stay native GL's to judge. */
static inline BOOL JglTextureParameterValueValid(GLenum pname, GLint value) {
    switch (pname) {
        case JGL_TEXTURE_COMPARE_MODE:
            return value == 0 /* NONE */ || value == JGL_COMPARE_R_TO_TEXTURE;
        case JGL_TEXTURE_COMPARE_FUNC:
            return value == 0x0203 /* LEQUAL */ || value == 0x0206 /* GEQUAL */;
        case JGL_DEPTH_TEXTURE_MODE:
            return value == 0x1909 /* LUMINANCE */ || value == 0x8049 /* INTENSITY */ ||
                   value == 0x1906 /* ALPHA */;
        default:
            return TRUE;
    }
}
/* An enum-valued parameter given as the bits of a float: GL truncates it to an
 * integer. Integer arithmetic only, as frontend.cpp packs without the FPU;
 * values no int32 holds (and NaN, infinities) are -1, never a valid enum. */
static inline GLint JglFloatBitsEnum(ULONG bits) {
    const ULONG exponent = (bits >> 23) & 0xff, mantissa = (bits & 0x7fffff) | 0x800000;
    if (exponent < 127)
        return 0;
    if (exponent > 127 + 30)
        return -1;
    const ULONG shift = exponent - 127,
                magnitude = shift >= 23 ? mantissa << (shift - 23) : mantissa >> (23 - shift);
    return bits >> 31 ? -(GLint)magnitude : (GLint)magnitude;
}
/* Value counts of the TexParameter/TexEnv names the host accepts, and Hint
 * validity. Scalar and vector entrypoints share them: an unknown name is a local
 * GL_INVALID_ENUM, never a record the host rejects as malformed (which would
 * fail the whole batch and the context). Zero means invalid. */
static inline ULONG JglTextureParameterCount(GLenum target, GLenum pname) {
    const BOOL cube = target == JGL_TEXTURE_CUBE_MAP && JglTextureCubeMapAvailable();
    const BOOL volume = target == JGL_TEXTURE_3D && JglTexture3DAvailable();
    const BOOL rectangle = JglRectangleTarget(target);
    if (target != 0x0de0 && target != 0x0de1 && !cube && !volume &&
        !rectangle) /* TEXTURE_1D, TEXTURE_2D */
        return 0;
    switch (pname) {
        case JGL_TEXTURE_WRAP_R: /* the cube map's, and every target's with 3D textures */
            return cube || JglTexture3DAvailable() ? 1 : 0;
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
        case 0x8191: /* GENERATE_MIPMAP_SGIS: a rectangle has one level */
            return JglTextureControlAvailable() && !rectangle ? 1 : 0;
        case JGL_TEXTURE_COMPARE_MODE: /* ARB_shadow, every target */
        case JGL_TEXTURE_COMPARE_FUNC:
        case JGL_DEPTH_TEXTURE_MODE: /* ARB_depth_texture, every target */
            return JglDepthTextureAvailable() ? 1 : 0;
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
