/* SPDX-License-Identifier: GPL-2.0-or-later
 * EXT secondary color: canonical three-float wire calls, with the same
 * signed/unsigned normalization as client color arrays. */
#include "internal.h"
#include "packing.h"

extern "C" {
JGL_PACKING_ONLY void APIENTRY glSecondaryColor3fvEXT(const GLfloat *v) {
    if (v)
        JglScalarVector(FEnum_glSecondaryColor3f, 3, v);
}
#define SECONDARY(suffix, T, token)                                                                \
    void APIENTRY glSecondaryColor3##suffix##EXT(T r, T g, T b) {                                  \
        GLfloat value[3] = {JglColorComponent((const BYTE *)&r, token),                            \
                            JglColorComponent((const BYTE *)&g, token),                            \
                            JglColorComponent((const BYTE *)&b, token)};                           \
        JglScalarVector(FEnum_glSecondaryColor3f, 3, value);                                       \
    }                                                                                              \
    void APIENTRY glSecondaryColor3##suffix##vEXT(const T *v) {                                    \
        GLfloat value[3];                                                                          \
        ULONG i;                                                                                   \
        if (!v)                                                                                    \
            return;                                                                                \
        for (i = 0; i < 3; ++i)                                                                    \
            value[i] = JglColorComponent((const BYTE *)(v + i), token);                            \
        JglScalarVector(FEnum_glSecondaryColor3f, 3, value);                                       \
    }
SECONDARY(b, signed char, GL_BYTE)
SECONDARY(s, short, GL_SHORT)
SECONDARY(i, GLint, GL_INT)
SECONDARY(d, GLdouble, GL_DOUBLE)
SECONDARY(ub, GLubyte, GL_UNSIGNED_BYTE)
SECONDARY(us, GLushort, GL_UNSIGNED_SHORT)
SECONDARY(ui, GLuint, GL_UNSIGNED_INT)
#undef SECONDARY

} /* extern C */
