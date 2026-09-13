/* SPDX-License-Identifier: GPL-2.0-or-later
 * Pixel-transfer state preserves the original numeric types. Payloads never
 * contain application pointers; map results stage privately until validated.
 * OpenGL1.1 section3.6.1: https://registry.khronos.org/OpenGL/specs/gl/glspec11.pdf
 */
namespace dg_icd_pixels {
constexpr ULONG MapLimit = 256;
static bool Map(GLenum map) {
    return map >= GL_PIXEL_MAP_I_TO_I && map <= GL_PIXEL_MAP_A_TO_A;
}
static bool Transfer(GLenum pname) {
    switch (pname) {
        case GL_MAP_COLOR:
        case GL_MAP_STENCIL:
        case GL_INDEX_SHIFT:
        case GL_INDEX_OFFSET:
        case GL_RED_SCALE:
        case GL_RED_BIAS:
        case GL_GREEN_SCALE:
        case GL_GREEN_BIAS:
        case GL_BLUE_SCALE:
        case GL_BLUE_BIAS:
        case GL_ALPHA_SCALE:
        case GL_ALPHA_BIAS:
        case GL_DEPTH_SCALE:
        case GL_DEPTH_BIAS:
            return true;
        default:
            return false;
    }
}
static bool Integer(GLenum pname, GLint *value) {
    ULONG args[3] = {pname, 0, 0}, bytes = 0;
    GLint result = 0;
    if (!JglQuery(FEnum_glGetIntegerv, args, DG_GL_RESULT_INT, &result, sizeof(result), &bytes))
        return false;
    if (bytes != sizeof(result)) {
        JglSetError(GL_INVALID_OPERATION);
        return false;
    }
    *value = result;
    return true;
}
static void Set(ULONG function, GLenum map, GLsizei count, const void *values, ULONG unit) {
    if (!JglCommandReady())
        return;
    if (!Map(map)) {
        JglCommandError(GL_INVALID_ENUM);
        return;
    }
    if (count < 1 || (ULONG)count > MapLimit ||
        (map <= GL_PIXEL_MAP_I_TO_A && ((ULONG)count & ((ULONG)count - 1))) || !values) {
        JglCommandError(GL_INVALID_VALUE);
        return;
    }
    GLint maximum;
    if (!Integer(GL_MAX_PIXEL_MAP_TABLE, &maximum))
        return;
    if (maximum < 1 || maximum > (GLint)MapLimit) {
        JglCommandError(GL_INVALID_OPERATION);
        return;
    }
    if (count > maximum) {
        JglCommandError(GL_INVALID_VALUE);
        return;
    }
    ULONG args[2] = {map, (ULONG)count};
    JglData(function, args, 2, values, (ULONG)count * unit);
}
static void Get(ULONG function, GLenum map, void *values, bool narrow) {
    if (!JglReady())
        return;
    if (!Map(map)) {
        JglSetError(GL_INVALID_ENUM);
        return;
    }
    if (!values) {
        JglSetError(GL_INVALID_VALUE);
        return;
    }
    GLint count;
    // The ten GL1.1 size selectors are offset0x40 from their map selectors.
    if (!Integer(map + (GL_PIXEL_MAP_I_TO_I_SIZE - GL_PIXEL_MAP_I_TO_I), &count))
        return;
    if (count < 1 || (ULONG)count > MapLimit) {
        JglSetError(GL_INVALID_OPERATION);
        return;
    }
    ULONG args[3] = {map, (ULONG)count, 0}, result[MapLimit], bytes = 0;
    ULONG kind = function == FEnum_glGetPixelMapfv ? DG_GL_RESULT_FLOAT : DG_GL_RESULT_INT;
    if (!JglQuery(function, args, kind, result, (ULONG)count * 4, &bytes))
        return;
    if (bytes != (ULONG)count * 4) {
        JglSetError(GL_INVALID_OPERATION);
        return;
    }
    if (narrow) {
        // Native ushort results are widened to immutable32-bit wire words.
        // Validate every word before touching the caller's output array.
        for (GLint i = 0; i < count; ++i)
            if (result[i] > 65535) {
                JglSetError(GL_INVALID_OPERATION);
                return;
            }
        for (GLint i = 0; i < count; ++i)
            ((GLushort *)values)[i] = (GLushort)result[i];
    } else {
        memcpy(values, result, bytes);
    }
}
} // namespace dg_icd_pixels
static void APIENTRY AliasPixelZoom(GLfloat x, GLfloat y) {
    if (!JglCommandReady())
        return;
    GLfloat values[2] = {x, y};
    JglScalarVector(FEnum_glPixelZoom, 2, values);
}
static void APIENTRY AliasPixelTransferf(GLenum pname, GLfloat value) {
    if (!JglCommandReady())
        return;
    if (!dg_icd_pixels::Transfer(pname)) {
        JglCommandError(GL_INVALID_ENUM);
        return;
    }
    ULONG words[2] = {pname, 0};
    memcpy(words + 1, &value, sizeof(value));
    JglScalarVector(FEnum_glPixelTransferf, 2, words);
}
static void APIENTRY AliasPixelTransferi(GLenum pname, GLint value) {
    if (!JglCommandReady())
        return;
    if (!dg_icd_pixels::Transfer(pname)) {
        JglCommandError(GL_INVALID_ENUM);
        return;
    }
    ULONG words[2] = {pname, (ULONG)value};
    JglScalarVector(FEnum_glPixelTransferi, 2, words);
}
static void APIENTRY AliasPixelMapfv(GLenum map, GLsizei count, const GLfloat *values) {
    dg_icd_pixels::Set(FEnum_glPixelMapfv, map, count, values, sizeof(*values));
}
static void APIENTRY AliasPixelMapuiv(GLenum map, GLsizei count, const GLuint *values) {
    dg_icd_pixels::Set(FEnum_glPixelMapuiv, map, count, values, sizeof(*values));
}
static void APIENTRY AliasPixelMapusv(GLenum map, GLsizei count, const GLushort *values) {
    dg_icd_pixels::Set(FEnum_glPixelMapusv, map, count, values, sizeof(*values));
}
static void APIENTRY AliasGetPixelMapfv(GLenum map, GLfloat *values) {
    dg_icd_pixels::Get(FEnum_glGetPixelMapfv, map, values, false);
}
static void APIENTRY AliasGetPixelMapuiv(GLenum map, GLuint *values) {
    dg_icd_pixels::Get(FEnum_glGetPixelMapuiv, map, values, false);
}
static void APIENTRY AliasGetPixelMapusv(GLenum map, GLushort *values) {
    dg_icd_pixels::Get(FEnum_glGetPixelMapusv, map, values, true);
}

static void APIENTRY AliasCopyPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum type) {
    if (!JglCommandReady())
        return;
    if (type != GL_COLOR && type != GL_DEPTH && type != GL_STENCIL) {
        JglCommandError(GL_INVALID_ENUM);
        return;
    }
    if (width < 0 || height < 0) {
        JglCommandError(GL_INVALID_VALUE);
        return;
    }
    if (!width || !height)
        return;
    ULONG words[5] = {(ULONG)x, (ULONG)y, (ULONG)width, (ULONG)height, type};
    JglScalarVector(FEnum_glCopyPixels, 5, words);
}
