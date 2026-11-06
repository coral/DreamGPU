/* SPDX-License-Identifier: GPL-2.0-or-later
 * Fixed-function vector state. Sizes are determined from pname before touching
 * caller memory; native validation independently checks the same contract.
 * Parameters of DG_CAP_GL_FIXED_STATE and DG_CAP_GL_TEXTURE_CONTROL are sized
 * only when negotiated (internal.h). */
#include "internal.h"

extern "C" {

static ULONG Count(ULONG function, GLenum target, GLenum pname) {
    switch (function) {
        case FEnum_glTexParameterfv:
        case FEnum_glTexParameteriv:
            return JglTextureParameterCount(target, pname);
        case FEnum_glTexEnvfv:
        case FEnum_glTexEnviv:
            return JglTextureEnvCount(target, pname);
        case FEnum_glLightfv:
            if (target < GL_LIGHT0 || target > GL_LIGHT7)
                return 0;
            switch (pname) {
                case GL_AMBIENT:
                case GL_DIFFUSE:
                case GL_SPECULAR:
                case GL_POSITION:
                    return 4;
                case GL_SPOT_DIRECTION:
                    return 3;
                case GL_SPOT_EXPONENT:
                case GL_SPOT_CUTOFF:
                case GL_CONSTANT_ATTENUATION:
                case GL_LINEAR_ATTENUATION:
                case GL_QUADRATIC_ATTENUATION:
                    return 1;
                default:
                    return 0;
            }
        case FEnum_glMaterialfv:
            if (target != GL_FRONT && target != GL_BACK && target != GL_FRONT_AND_BACK)
                return 0;
            switch (pname) {
                case GL_AMBIENT:
                case GL_DIFFUSE:
                case GL_SPECULAR:
                case GL_EMISSION:
                case GL_AMBIENT_AND_DIFFUSE:
                    return 4;
                case GL_COLOR_INDEXES:
                    return 3;
                case GL_SHININESS:
                    return 1;
                default:
                    return 0;
            }
        case FEnum_glLightModelfv:
            return target == GL_LIGHT_MODEL_AMBIENT ? 4
                   : target == GL_LIGHT_MODEL_LOCAL_VIEWER || target == GL_LIGHT_MODEL_TWO_SIDE ||
                           (target == 0x81f8 && JglSupportsSecondary())
                       ? 1
                       : 0;
        case FEnum_glTexGenfv:
        case FEnum_glTexGendv:
            if (target != GL_S && target != GL_T && target != GL_R && target != GL_Q)
                return 0;
            return pname == GL_TEXTURE_GEN_MODE                        ? 1
                   : pname == GL_OBJECT_PLANE || pname == GL_EYE_PLANE ? 4
                                                                       : 0;
        case FEnum_glClipPlane:
            return target >= GL_CLIP_PLANE0 && target <= GL_CLIP_PLANE5 ? 4 : 0;
        case FEnum_glPointParameterfv:
            /* POINT_SIZE_MIN/MAX, POINT_FADE_THRESHOLD_SIZE; DISTANCE_ATTENUATION */
            return target >= 0x8126 && target <= 0x8128 ? 1 : target == 0x8129 ? 3 : 0;
        default:
            return 0;
    }
}
static void Vector(ULONG function, GLenum target, GLenum pname, const void *values, ULONG bytes,
                   ULONG words) {
    ULONG args[2] = {target, pname}, count;
    /* Material vectors are legal inside Begin/End; JglData enforces this
     * exception itself. Every other vector must check readiness first. */
    if (function != FEnum_glMaterialfv && !JglCommandReady())
        return;
    count = Count(function, target, pname);
    if (!count) {
        JglCommandError(GL_INVALID_ENUM);
        return;
    }
    if (!values) {
        JglCommandError(GL_INVALID_VALUE);
        return;
    }
    /* The depth texture parameters' values (ARB_shadow, ARB_depth_texture). */
    if (function == FEnum_glTexParameteriv || function == FEnum_glTexParameterfv) {
        ULONG word;
        CopyMemory(&word, values, sizeof(word));
        const GLint value =
            function == FEnum_glTexParameteriv ? (GLint)word : JglFloatBitsEnum(word);
        if (count == 1 && !JglTextureParameterValueValid(pname, value)) {
            JglCommandError(GL_INVALID_ENUM);
            return;
        }
    }
    JglData(function, args, words, values, count * bytes);
}
#define PAIR(name, type)                                                                           \
    void APIENTRY name(GLenum target, GLenum pname, const type *values) {                          \
        Vector(FEnum_##name, target, pname, values, sizeof(type), 2);                              \
    }
PAIR(glTexParameterfv, GLfloat)
PAIR(glTexParameteriv, GLint)
PAIR(glTexEnvfv, GLfloat)
PAIR(glTexEnviv, GLint)
PAIR(glLightfv, GLfloat)
PAIR(glMaterialfv, GLfloat)
PAIR(glTexGenfv, GLfloat)
PAIR(glTexGendv, GLdouble)
#undef PAIR
void APIENTRY glLightModelfv(GLenum pname, const GLfloat *values) {
    Vector(FEnum_glLightModelfv, pname, 0, values, sizeof(*values), 1);
}
void APIENTRY glClipPlane(GLenum plane, const GLdouble *equation) {
    Vector(FEnum_glClipPlane, plane, 0, equation, sizeof(*equation), 1);
}
void APIENTRY glTexGeni(GLenum target, GLenum pname, GLint value) {
    GLfloat number = (GLfloat)value;
    if (!JglCommandReady())
        return;
    if (Count(FEnum_glTexGenfv, target, pname) != 1) {
        JglCommandError(GL_INVALID_ENUM);
        return;
    }
    glTexGenfv(target, pname, &number);
}

/* Point parameters: one DATA record, pname then inline floats. */
static void PointParameter(GLenum pname, const GLfloat *values) {
    ULONG args[1] = {pname}, count;
    if (!JglCommandReady())
        return;
    if (!JglFixedStateAvailable()) {
        JglCommandError(GL_INVALID_OPERATION);
        return;
    }
    count = Count(FEnum_glPointParameterfv, pname, 0);
    if (!count) {
        JglCommandError(GL_INVALID_ENUM);
        return;
    }
    if (!values) {
        JglCommandError(GL_INVALID_VALUE);
        return;
    }
    JglData(FEnum_glPointParameterfv, args, 1, values, count * sizeof(*values));
}
void APIENTRY glPointParameterfv(GLenum pname, const GLfloat *values) {
    PointParameter(pname, values);
}
void APIENTRY glPointParameterfvARB(GLenum pname, const GLfloat *values) {
    PointParameter(pname, values);
}
void APIENTRY glPointParameterfvEXT(GLenum pname, const GLfloat *values) {
    PointParameter(pname, values);
}
/* ATI_separate_stencil sets both faces' functions with one reference and mask:
 * two core face records, FRONT then BACK. Both functions (NEVER..ALWAYS) are
 * checked first, so a rejected call never changes one face alone. */
void APIENTRY glStencilFuncSeparateATI(GLenum front, GLenum back, GLint ref, GLuint mask) {
    ULONG words[4] = {GL_FRONT, front, (ULONG)ref, mask};
    if (!JglCommandReady())
        return;
    if (!JglFixedStateAvailable()) {
        JglCommandError(GL_INVALID_OPERATION);
        return;
    }
    if (front - 0x0200 > 7 || back - 0x0200 > 7) {
        JglCommandError(GL_INVALID_ENUM);
        return;
    }
    JglScalarVector(FEnum_glStencilFuncSeparate, 4, words);
    words[0] = GL_BACK;
    words[1] = back;
    JglScalarVector(FEnum_glStencilFuncSeparate, 4, words);
}

void APIENTRY glLightModeliv(GLenum pname, const GLint *values) {
    GLfloat converted[4];
    ULONG count, i;
    if (!JglCommandReady())
        return;
    count = Count(FEnum_glLightModelfv, pname, 0);
    if (!count) {
        JglCommandError(GL_INVALID_ENUM);
        return;
    }
    if (!values) {
        JglCommandError(GL_INVALID_VALUE);
        return;
    }
    for (i = 0; i < count; ++i)
        converted[i] = pname == GL_LIGHT_MODEL_AMBIENT
                           ? (GLfloat)((2.0 * values[i] + 1.0) / 4294967295.0)
                           : (GLfloat)values[i];
    Vector(FEnum_glLightModelfv, pname, 0, converted, sizeof(*converted), 1);
}

} /* extern C */
