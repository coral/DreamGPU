/* SPDX-License-Identifier: GPL-2.0-or-later
 * ARB_multitexture coordinates. Every core or ARB-named variant submits the
 * canonical MultiTexCoord4f record: target, then s,t,r,q float bits; absent t
 * and r are zero and q is one. The shared scalar path rejects the record
 * without DG_CAP_GL_MULTITEXTURE and checks the unit and Begin/End legality.
 * MultiTexCoord4f and ActiveTexture are generated scalar wrappers (scalar.inc);
 * ClientActiveTexture is client array state (arrays.cpp).
 */
#include "internal.h"
#include "packing.h"

extern "C" {

/* One body per variant, exported under both the core and the ARB name. */
#define JGL_ENTRY(attributes, name, parameters, ...)                                               \
    attributes void APIENTRY name parameters __VA_ARGS__
#define JGL_BOTH(attributes, name, parameters, ...)                                                \
    JGL_ENTRY(attributes, gl##name, parameters, __VA_ARGS__)                                       \
    JGL_ENTRY(attributes, gl##name##ARB, parameters, __VA_ARGS__)

/* Float arguments and vectors are copied as bits and never loaded: an x87
 * load would quiet signaling NaNs and change status flags (packing.h). */
#define JGL_BITS(...)                                                                              \
    {                                                                                              \
        ULONG words[5] = {target, 0, 0, 0, 0x3f800000};                                            \
        __VA_ARGS__;                                                                               \
        JglScalarVector(FEnum_glMultiTexCoord4f, 5, words);                                        \
    }
JGL_BOTH(JGL_PACKING_ONLY, MultiTexCoord1f, (GLenum target, GLfloat s),
         JGL_BITS(__builtin_memcpy(words + 1, &s, 4)))
JGL_BOTH(JGL_PACKING_ONLY, MultiTexCoord2f, (GLenum target, GLfloat s, GLfloat t),
         JGL_BITS(__builtin_memcpy(words + 1, &s, 4), __builtin_memcpy(words + 2, &t, 4)))
JGL_BOTH(JGL_PACKING_ONLY, MultiTexCoord3f, (GLenum target, GLfloat s, GLfloat t, GLfloat r),
         JGL_BITS(__builtin_memcpy(words + 1, &s, 4), __builtin_memcpy(words + 2, &t, 4),
                  __builtin_memcpy(words + 3, &r, 4)))
/* A null vector submits nothing, like the existing TexCoord*fv aliases. */
#define JGL_FLOAT_VECTOR(n)                                                                        \
    JGL_BOTH(JGL_PACKING_ONLY, MultiTexCoord##n##fv, (GLenum target, const GLfloat *v),            \
             JGL_BITS(if (!v) return; __builtin_memcpy(words + 1, v, n * 4)))
JGL_FLOAT_VECTOR(1)
JGL_FLOAT_VECTOR(2)
JGL_FLOAT_VECTOR(3)
JGL_FLOAT_VECTOR(4)
#undef JGL_FLOAT_VECTOR
#undef JGL_BITS

/* Integer and double coordinates convert like TexCoord: no normalization. */
static void Convert(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q) {
    const struct {
        ULONG Target;
        GLfloat Value[4];
    } words = {target, {s, t, r, q}};
    static_assert(sizeof(words) == 20);
    JglScalarVector(FEnum_glMultiTexCoord4f, 5, &words);
}
#define JGL_CONVERT(suffix, T)                                                                     \
    JGL_BOTH(, MultiTexCoord1##suffix, (GLenum target, T s),                                       \
             { Convert(target, (GLfloat)s, 0, 0, 1); })                                            \
    JGL_BOTH(, MultiTexCoord2##suffix, (GLenum target, T s, T t),                                  \
             { Convert(target, (GLfloat)s, (GLfloat)t, 0, 1); })                                   \
    JGL_BOTH(, MultiTexCoord3##suffix, (GLenum target, T s, T t, T r),                             \
             { Convert(target, (GLfloat)s, (GLfloat)t, (GLfloat)r, 1); })                          \
    JGL_BOTH(, MultiTexCoord4##suffix, (GLenum target, T s, T t, T r, T q),                        \
             { Convert(target, (GLfloat)s, (GLfloat)t, (GLfloat)r, (GLfloat)q); })                 \
    JGL_BOTH(, MultiTexCoord1##suffix##v, (GLenum target, const T *v), {                           \
        if (v)                                                                                     \
            Convert(target, (GLfloat)v[0], 0, 0, 1);                                               \
    })                                                                                             \
    JGL_BOTH(, MultiTexCoord2##suffix##v, (GLenum target, const T *v), {                           \
        if (v)                                                                                     \
            Convert(target, (GLfloat)v[0], (GLfloat)v[1], 0, 1);                                   \
    })                                                                                             \
    JGL_BOTH(, MultiTexCoord3##suffix##v, (GLenum target, const T *v), {                           \
        if (v)                                                                                     \
            Convert(target, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], 1);                       \
    })                                                                                             \
    JGL_BOTH(, MultiTexCoord4##suffix##v, (GLenum target, const T *v), {                           \
        if (v)                                                                                     \
            Convert(target, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], (GLfloat)v[3]);           \
    })
JGL_CONVERT(d, GLdouble)
JGL_CONVERT(i, GLint)
JGL_CONVERT(s, GLshort)
#undef JGL_CONVERT
#undef JGL_BOTH
#undef JGL_ENTRY

} /* extern C */
