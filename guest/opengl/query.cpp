/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "internal.h"

extern "C" {

static BOOL LocalUnpack(GLenum name, GLint *value) {
    JGL_UNPACK *unpack = JglUnpack();
    if (!unpack)
        return FALSE;
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
#define STATE_QUERY(name, type, kind)                                                              \
    void APIENTRY name(GLenum pname, type *output) {                                               \
        ULONG args[3] = {pname, 0, 0}, bytes;                                                      \
        type result[DG_GL_MAX_RESULT_BYTES / sizeof(type)];                                        \
        GLint local;                                                                               \
        if (!output || !JglReady())                                                                \
            return;                                                                                \
        if (LocalUnpack(pname, &local) || JglArrayQuery(pname, &local)) {                          \
            *output = kind == DG_GL_RESULT_BOOL ? (type)(local != 0) : (type)local;                \
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
        if (output && JglQuery(FEnum_##name, args, kind, result, sizeof(result), &bytes))          \
            CopyMemory(output, result, bytes);                                                     \
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
    if (!JglReady())
        return GL_FALSE;
    if ((cap == GL_VERTEX_ARRAY || cap == GL_COLOR_ARRAY || cap == GL_NORMAL_ARRAY ||
         cap == GL_TEXTURE_COORD_ARRAY || cap == 0x845e) &&
        JglArrayQuery(cap, &local))
        return local != 0;
    JglQuery(FEnum_glIsEnabled, args, DG_GL_RESULT_BOOL, &result, sizeof(result), &bytes);
    return result;
}
/* The host supplies stable identity strings and no extension/version claim.
 * These public pointers stay valid across later queries and context switches. */
const GLubyte *APIENTRY glGetString(GLenum name) {
    static const GLubyte vendor[] = "DreamGPU";
    static const GLubyte renderer[] = "DreamGPU (native host OpenGL)";
    static const GLubyte extensions[] = "";
    static const GLubyte secondary[] = "GL_EXT_secondary_color GL_EXT_separate_specular_color";
    static const GLubyte version[] = "1.1 DreamGPU";
    if (!JglReady())
        return NULL;
    switch (name) {
        case GL_VENDOR:
            return vendor;
        case GL_RENDERER:
            return renderer;
        case GL_EXTENSIONS:
            return JglSupportsSecondary() ? secondary : extensions;
        /* A valid identity pointer for legacy diagnostic output, with no claim
         * that this evolving frontend implements a complete OpenGL version. */
        case GL_VERSION:
            return version;
        default:
            JglSetError(GL_INVALID_ENUM);
            return NULL;
    }
}

} /* extern C */
