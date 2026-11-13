/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "internal.h"

extern "C" {

static BOOL LocalUnpack(GLenum name, GLint *value) {
    JGL_UNPACK *unpack = JglUnpack();
    if (!unpack)
        return FALSE;
    /* PACK/UNPACK_SKIP_IMAGES and _IMAGE_HEIGHT (EXT_texture3D) */
    if (name - 0x806b <= 3 && JglTexture3DAvailable()) {
        if (name < 0x806d)
            unpack = JglPack();
        *value = name & 1 ? unpack->SkipImages : unpack->ImageHeight;
        return TRUE;
    }
    if (name == GL_PACK_SWAP_BYTES || name == GL_PACK_LSB_FIRST || name == GL_PACK_ALIGNMENT ||
        name == GL_PACK_ROW_LENGTH || name == GL_PACK_SKIP_ROWS || name == GL_PACK_SKIP_PIXELS) {
        unpack = JglPack();
        name += GL_UNPACK_ALIGNMENT - GL_PACK_ALIGNMENT;
    }
    switch (name) {
        case GL_UNPACK_SWAP_BYTES:
            *value = unpack->SwapBytes;
            return TRUE;
        case GL_UNPACK_LSB_FIRST:
            *value = unpack->LsbFirst;
            return TRUE;
        case GL_UNPACK_ALIGNMENT:
            *value = unpack->Alignment;
            return TRUE;
        case GL_UNPACK_ROW_LENGTH:
            *value = unpack->RowLength;
            return TRUE;
        case GL_UNPACK_SKIP_ROWS:
            *value = unpack->SkipRows;
            return TRUE;
        case GL_UNPACK_SKIP_PIXELS:
            *value = unpack->SkipPixels;
            return TRUE;
        default:
            return FALSE;
    }
}
/* Known enables and the viewport (JglStateQuery) are integers converted as
 * native GL converts them: booleans compare with zero, floats are exact. */
#define STATE_QUERY(name, type, kind)                                                              \
    void APIENTRY name(GLenum pname, type *output) {                                               \
        ULONG args[3] = {pname, 0, 0}, bytes, known;                                               \
        type result[DG_GL_MAX_RESULT_BYTES / sizeof(type)];                                        \
        GLint local;                                                                               \
        LONG values[4];                                                                            \
        if (!output || !JglReady())                                                                \
            return;                                                                                \
        if ((known = JglStateQuery(pname, values)) != 0) {                                         \
            for (ULONG i = 0; i < known; ++i)                                                      \
                output[i] = kind == DG_GL_RESULT_BOOL ? (type)(values[i] != 0) : (type)values[i];  \
            return;                                                                                \
        }                                                                                          \
        if (LocalUnpack(pname, &local) || JglArrayQuery(pname, &local) ||                          \
            JglFramebufferBinding(pname, &local)) {                                                \
            *output = kind == DG_GL_RESULT_BOOL ? (type)(local != 0) : (type)local;                \
            return;                                                                                \
        }                                                                                          \
        if (JglCubeMapQueryRejected(pname) || JglProgramQueryRejected(pname) ||                    \
            JglTexture3DQueryRejected(pname) || JglFramebufferQueryRejected(pname) ||              \
            JglRectangleQueryRejected(pname)) {                                                    \
            JglSetError(GL_INVALID_ENUM);                                                          \
            return;                                                                                \
        }                                                                                          \
        if (JglQuery(FEnum_##name, args, kind, result, sizeof(result), &bytes))                    \
            CopyMemory(output, result, bytes);                                                     \
    }
STATE_QUERY(glGetIntegerv, GLint, DG_GL_RESULT_INT)
STATE_QUERY(glGetBooleanv, GLboolean, DG_GL_RESULT_BOOL)
STATE_QUERY(glGetFloatv, GLfloat, DG_GL_RESULT_FLOAT)
STATE_QUERY(glGetDoublev, GLdouble, DG_GL_RESULT_DOUBLE)
#undef STATE_QUERY
#define PAIR_QUERY(name, type, kind)                                                               \
    void APIENTRY name(GLenum target, GLenum pname, type *output) {                                \
        ULONG args[3] = {target, pname, 0}, bytes;                                                 \
        type result[DG_GL_MAX_RESULT_BYTES / sizeof(type)];                                        \
        if (output && JglReady() &&                                                                \
            (JglCubeMapQueryRejected(target) || JglTexture3DQueryRejected(target) ||               \
             JglRectangleQueryRejected(target) ||                                                  \
             ((FEnum_##name == FEnum_glGetTexParameterfv ||                                        \
               FEnum_##name == FEnum_glGetTexParameteriv) &&                                       \
              (WrapRRejected(target, pname) || RectangleMipmapRejected(target, pname) ||           \
               LodRejected(pname) || JglDepthTextureQueryRejected(pname))))) {                     \
            JglSetError(GL_INVALID_ENUM);                                                          \
            return;                                                                                \
        }                                                                                          \
        if (output && JglQuery(FEnum_##name, args, kind, result, sizeof(result), &bytes))          \
            CopyMemory(output, result, bytes);                                                     \
    }
/* TEXTURE_WRAP_R belongs to the cube map, and to every target once 3D
 * textures are negotiated; otherwise it stays the local GL_INVALID_ENUM. */
static BOOL WrapRRejected(GLenum target, GLenum pname) {
    return pname == JGL_TEXTURE_WRAP_R && !JglTexture3DAvailable() &&
           !(target == JGL_TEXTURE_CUBE_MAP && JglTextureCubeMapAvailable());
}
/* The GL 1.2 LOD clamps exist with DG_CAP2_GL_CORE_1_2 only. */
static BOOL LodRejected(GLenum pname) {
    return (pname == JGL_TEXTURE_MIN_LOD || pname == JGL_TEXTURE_MAX_LOD) &&
           !JglCore12Available();
}
/* GENERATE_MIPMAP is no rectangle parameter: a rectangle has one level. */
static BOOL RectangleMipmapRejected(GLenum target, GLenum pname) {
    return pname == 0x8191 && JglRectangleTarget(target);
}
PAIR_QUERY(glGetTexParameterfv, GLfloat, DG_GL_RESULT_FLOAT)
PAIR_QUERY(glGetTexParameteriv, GLint, DG_GL_RESULT_INT)
PAIR_QUERY(glGetTexEnvfv, GLfloat, DG_GL_RESULT_FLOAT)
PAIR_QUERY(glGetTexEnviv, GLint, DG_GL_RESULT_INT)
PAIR_QUERY(glGetLightfv, GLfloat, DG_GL_RESULT_FLOAT)
PAIR_QUERY(glGetLightiv, GLint, DG_GL_RESULT_INT)
PAIR_QUERY(glGetMaterialfv, GLfloat, DG_GL_RESULT_FLOAT)
PAIR_QUERY(glGetMaterialiv, GLint, DG_GL_RESULT_INT)
PAIR_QUERY(glGetTexGenfv, GLfloat, DG_GL_RESULT_FLOAT)
PAIR_QUERY(glGetTexGeniv, GLint, DG_GL_RESULT_INT)
PAIR_QUERY(glGetTexGendv, GLdouble, DG_GL_RESULT_DOUBLE)
#undef PAIR_QUERY
void APIENTRY glGetClipPlane(GLenum plane, GLdouble *equation) {
    ULONG args[3] = {plane, 0, 0}, bytes;
    GLdouble result[4];
    if (equation &&
        JglQuery(FEnum_glGetClipPlane, args, DG_GL_RESULT_DOUBLE, result, sizeof(result), &bytes))
        CopyMemory(equation, result, bytes);
}
#define LEVEL_QUERY(name, type, kind)                                                              \
    void APIENTRY name(GLenum target, GLint level, GLenum pname, type *output) {                   \
        ULONG args[3] = {target, (ULONG)level, pname}, bytes;                                      \
        type result[DG_GL_MAX_RESULT_BYTES / sizeof(type)];                                        \
        if (output && JglReady() &&                                                                \
            (JglCubeMapQueryRejected(target) || JglTexture3DQueryRejected(target) ||               \
             JglTexture3DQueryRejected(pname) || JglRectangleQueryRejected(target) ||              \
             JglDepthTextureQueryRejected(pname))) {                                               \
            JglSetError(GL_INVALID_ENUM);                                                          \
            return;                                                                                \
        }                                                                                          \
        /* A rectangle (and its proxy) has level zero only. */                                     \
        if (output && JglReady() && level &&                                                       \
            (JglRectangleTarget(target) || target == JGL_PROXY_TEXTURE_RECTANGLE)) {               \
            JglSetError(GL_INVALID_VALUE);                                                         \
            return;                                                                                \
        }                                                                                          \
        if (output && JglQuery(FEnum_##name, args, kind, result, sizeof(result), &bytes))          \
            CopyMemory(output, result, bytes);                                                     \
    }
LEVEL_QUERY(glGetTexLevelParameterfv, GLfloat, DG_GL_RESULT_FLOAT)
LEVEL_QUERY(glGetTexLevelParameteriv, GLint, DG_GL_RESULT_INT)
#undef LEVEL_QUERY
GLboolean APIENTRY glIsEnabled(GLenum cap) {
    ULONG args[3] = {cap, 0, 0}, bytes;
    GLboolean result = GL_FALSE;
    GLint local;
    LONG values[4];
    if (!JglReady())
        return GL_FALSE;
    if ((cap == GL_VERTEX_ARRAY || cap == GL_COLOR_ARRAY || cap == GL_NORMAL_ARRAY ||
         cap == GL_TEXTURE_COORD_ARRAY || cap == 0x845e || cap == 0x8457) &&
        JglArrayQuery(cap, &local))
        return local != 0;
    /* A known enable; VIEWPORT is no capability (the host's GL_INVALID_ENUM). */
    if (cap != 0x0ba2 /* VIEWPORT */ && JglStateQuery(cap, values) == 1)
        return values[0] != 0;
    if (JglCubeMapQueryRejected(cap) || JglProgramQueryRejected(cap) ||
        JglTexture3DQueryRejected(cap) || JglRectangleQueryRejected(cap)) {
        JglSetError(GL_INVALID_ENUM);
        return GL_FALSE;
    }
    JglQuery(FEnum_glIsEnabled, args, DG_GL_RESULT_BOOL, &result, sizeof(result), &bytes);
    return result;
}
/* PROGRAM_ERROR_STRING_ARB: the host returns DG_GL_MAX_RESULT_BYTES of
 * NUL-padded ASCII into per-context storage. The pointer stays valid for the
 * context; its contents until the next query of the string. */
static const GLubyte *ProgramErrorString(void) {
    ULONG args[3] = {0x8874, 0, 0}, bytes = 0;
    GLubyte *buffer = JglProgramErrorBuffer();
    if (!buffer || !JglQuery(FEnum_glGetString, args, DG_GL_RESULT_STRING, buffer,
                             DG_GL_MAX_RESULT_BYTES, &bytes))
        return NULL;
    buffer[DG_GL_MAX_RESULT_BYTES - 1] = 0;
    return buffer;
}
/* The host supplies stable identity strings and no extension/version claim.
 * These public pointers stay valid across later queries and context switches.
 * Extensions name only negotiated host contracts; the version is 1.1 unless
 * every GL 1.2 feature is negotiated (JglCore12), so callers never assume
 * promoted core features the host lacks. Homeworld 2 refuses a 1.1 renderer
 * outright (its float comparison finds "1.1" below 1.1). */
const GLubyte *APIENTRY glGetString(GLenum name) {
    static const GLubyte vendor[] = "DreamGPU";
    static const GLubyte renderer[] = "DreamGPU (native host OpenGL)";
    static const GLubyte version[] = "1.1 DreamGPU";
    static const GLubyte version12[] = "1.2 DreamGPU";
    if (!JglReady())
        return NULL;
    switch (name) {
        case GL_VENDOR:
            return vendor;
        case GL_RENDERER:
            return renderer;
        case GL_EXTENSIONS:
            return JglExtensions();
        /* A valid identity pointer for legacy diagnostic output, with no claim
         * that this evolving frontend implements a complete OpenGL version. */
        case GL_VERSION:
            return JglCore12() ? version12 : version;
        case 0x8874: /* PROGRAM_ERROR_STRING_ARB, of either program extension */
            if (JglProgramsAvailable())
                return ProgramErrorString();
            JglSetError(GL_INVALID_ENUM);
            return NULL;
        default:
            JglSetError(GL_INVALID_ENUM);
            return NULL;
    }
}

} /* extern C */
