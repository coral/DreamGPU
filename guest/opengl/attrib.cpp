/* SPDX-License-Identifier: GPL-2.0-or-later
 * ARB_vertex_program generic vertex attributes (DG_CAP_GL_VERTEX_PROGRAM).
 * Every VertexAttrib*ARB variant submits the one VertexAttrib4fARB record:
 * index, then x,y,z,w float bits; absent y and z are zero and w is one. The
 * shared scalar path rejects the record without the capability and for an
 * index at or beyond DG_GL_MAX_VERTEX_ATTRIBS, and admits it inside Begin/End
 * (where index 0 provokes a vertex) and in display lists. The 4N variants
 * normalize like GL 1.x Table 2.6, the other integer variants convert without
 * normalization and doubles convert to float, as native immediate mode does.
 *
 * Generic attribute arrays are client state, never aliased with the
 * conventional arrays except that array 0 is the position; arrays.cpp draws
 * them through the attribute draw, or the buffer draw when an array latched a
 * buffer object (ARB_vertex_buffer_object). Their state queries are answered here, the
 * current values (attributes 1-15; attribute 0 has none) by the host.
 */
#include "internal.h"
#include "packing.h"

extern "C" {

#define JGL_ATTRIB_ARRAY_ENABLED 0x8622
#define JGL_ATTRIB_ARRAY_SIZE 0x8623
#define JGL_ATTRIB_ARRAY_STRIDE 0x8624
#define JGL_ATTRIB_ARRAY_TYPE 0x8625
#define JGL_CURRENT_VERTEX_ATTRIB 0x8626
#define JGL_ATTRIB_ARRAY_POINTER 0x8645
#define JGL_ATTRIB_ARRAY_NORMALIZED 0x886a
#define JGL_ATTRIB_ARRAY_BUFFER_BINDING 0x889f /* ARB_vertex_buffer_object */

/* Float arguments and vectors are copied as bits and never loaded: an x87
 * load would quiet signaling NaNs and change status flags (packing.h). */
#define JGL_BITS(...)                                                                              \
    do {                                                                                           \
        ULONG words[5] = {index, 0, 0, 0, 0x3f800000};                                             \
        __VA_ARGS__;                                                                               \
        JglScalarVector(FEnum_glVertexAttrib4fARB, 5, words);                                      \
    } while (0)
JGL_PACKING_ONLY void APIENTRY glVertexAttrib1fARB(GLuint index, GLfloat x) {
    JGL_BITS(__builtin_memcpy(words + 1, &x, 4));
}
JGL_PACKING_ONLY void APIENTRY glVertexAttrib2fARB(GLuint index, GLfloat x, GLfloat y) {
    JGL_BITS(__builtin_memcpy(words + 1, &x, 4), __builtin_memcpy(words + 2, &y, 4));
}
JGL_PACKING_ONLY void APIENTRY glVertexAttrib3fARB(GLuint index, GLfloat x, GLfloat y, GLfloat z) {
    JGL_BITS(__builtin_memcpy(words + 1, &x, 4), __builtin_memcpy(words + 2, &y, 4),
             __builtin_memcpy(words + 3, &z, 4));
}
JGL_PACKING_ONLY void APIENTRY glVertexAttrib4fARB(GLuint index, GLfloat x, GLfloat y, GLfloat z,
                                                   GLfloat w) {
    JGL_BITS(__builtin_memcpy(words + 1, &x, 4), __builtin_memcpy(words + 2, &y, 4),
             __builtin_memcpy(words + 3, &z, 4), __builtin_memcpy(words + 4, &w, 4));
}
/* A null vector submits nothing, like the other float vector aliases. */
JGL_PACKING_ONLY void APIENTRY glVertexAttrib1fvARB(GLuint index, const GLfloat *v) {
    if (v)
        JGL_BITS(__builtin_memcpy(words + 1, v, 4));
}
JGL_PACKING_ONLY void APIENTRY glVertexAttrib2fvARB(GLuint index, const GLfloat *v) {
    if (v)
        JGL_BITS(__builtin_memcpy(words + 1, v, 8));
}
JGL_PACKING_ONLY void APIENTRY glVertexAttrib3fvARB(GLuint index, const GLfloat *v) {
    if (v)
        JGL_BITS(__builtin_memcpy(words + 1, v, 12));
}
JGL_PACKING_ONLY void APIENTRY glVertexAttrib4fvARB(GLuint index, const GLfloat *v) {
    if (v)
        JGL_BITS(__builtin_memcpy(words + 1, v, 16));
}
#undef JGL_BITS

static void SubmitAttribute(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w) {
    const struct {
        ULONG Index;
        GLfloat Value[4];
    } words = {index, {x, y, z, w}};
    static_assert(sizeof(words) == 20);
    JglScalarVector(FEnum_glVertexAttrib4fARB, 5, &words);
}
/* Short and double scalars and vectors of one to three components. */
#define JGL_CONVERT(suffix, T)                                                                     \
    void APIENTRY glVertexAttrib1##suffix##ARB(GLuint index, T x) {                                \
        SubmitAttribute(index, (GLfloat)x, 0, 0, 1);                                               \
    }                                                                                              \
    void APIENTRY glVertexAttrib2##suffix##ARB(GLuint index, T x, T y) {                           \
        SubmitAttribute(index, (GLfloat)x, (GLfloat)y, 0, 1);                                      \
    }                                                                                              \
    void APIENTRY glVertexAttrib3##suffix##ARB(GLuint index, T x, T y, T z) {                      \
        SubmitAttribute(index, (GLfloat)x, (GLfloat)y, (GLfloat)z, 1);                             \
    }                                                                                              \
    void APIENTRY glVertexAttrib4##suffix##ARB(GLuint index, T x, T y, T z, T w) {                 \
        SubmitAttribute(index, (GLfloat)x, (GLfloat)y, (GLfloat)z, (GLfloat)w);                    \
    }                                                                                              \
    void APIENTRY glVertexAttrib1##suffix##vARB(GLuint index, const T *v) {                        \
        if (v)                                                                                     \
            SubmitAttribute(index, (GLfloat)v[0], 0, 0, 1);                                        \
    }                                                                                              \
    void APIENTRY glVertexAttrib2##suffix##vARB(GLuint index, const T *v) {                        \
        if (v)                                                                                     \
            SubmitAttribute(index, (GLfloat)v[0], (GLfloat)v[1], 0, 1);                            \
    }                                                                                              \
    void APIENTRY glVertexAttrib3##suffix##vARB(GLuint index, const T *v) {                        \
        if (v)                                                                                     \
            SubmitAttribute(index, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], 1);                \
    }
JGL_CONVERT(s, GLshort)
JGL_CONVERT(d, GLdouble)
#undef JGL_CONVERT
/* Four-component vectors of every type, unnormalized (VertexAttrib4*vARB) and
 * normalized (VertexAttrib4N*vARB, signed (2c+1)/(2^b-1), unsigned c/(2^b-1)). */
#define JGL_VECTOR4(suffix, T, type)                                                               \
    void APIENTRY glVertexAttrib4##suffix##vARB(GLuint index, const T *v) {                        \
        if (v)                                                                                     \
            SubmitAttribute(index, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], (GLfloat)v[3]);    \
    }                                                                                              \
    void APIENTRY glVertexAttrib4N##suffix##vARB(GLuint index, const T *v) {                       \
        if (v)                                                                                     \
            SubmitAttribute(index, JglColorComponent((const BYTE *)&v[0], type),                   \
                            JglColorComponent((const BYTE *)&v[1], type),                          \
                            JglColorComponent((const BYTE *)&v[2], type),                          \
                            JglColorComponent((const BYTE *)&v[3], type));                         \
    }
JGL_VECTOR4(b, signed char, GL_BYTE) /* GLbyte */
JGL_VECTOR4(s, GLshort, GL_SHORT)
JGL_VECTOR4(i, GLint, GL_INT)
JGL_VECTOR4(ub, GLubyte, GL_UNSIGNED_BYTE)
JGL_VECTOR4(us, GLushort, GL_UNSIGNED_SHORT)
JGL_VECTOR4(ui, GLuint, GL_UNSIGNED_INT)
#undef JGL_VECTOR4
void APIENTRY glVertexAttrib4dvARB(GLuint index, const GLdouble *v) {
    if (v)
        SubmitAttribute(index, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2], (GLfloat)v[3]);
}
void APIENTRY glVertexAttrib4NubARB(GLuint index, GLubyte x, GLubyte y, GLubyte z, GLubyte w) {
    const GLubyte v[4] = {x, y, z, w};
    glVertexAttrib4NubvARB(index, v);
}

/* Client state: legal outside Begin/End only, with the capability, for an
 * attribute the protocol has. FALSE has raised the error. */
static JGL_ARRAY *Array(GLuint index) {
    if (!JglReady())
        return NULL;
    if (!JglVertexProgramAvailable()) {
        JglSetError(GL_INVALID_OPERATION);
        return NULL;
    }
    if (index >= DG_GL_MAX_VERTEX_ATTRIBS) {
        JglSetError(GL_INVALID_VALUE);
        return NULL;
    }
    return &JglArrays()->Generic[index];
}
void APIENTRY glVertexAttribPointerARB(GLuint index, GLint size, GLenum type, GLboolean normalized,
                                       GLsizei stride, const void *pointer) {
    JGL_ARRAY *a = Array(index);
    if (!a)
        return;
    if (size < 1 || size > 4 || stride < 0) {
        JglSetError(GL_INVALID_VALUE);
        return;
    }
    switch (type) {
        case GL_BYTE:
        case GL_UNSIGNED_BYTE:
        case GL_SHORT:
        case GL_UNSIGNED_SHORT:
        case GL_INT:
        case GL_UNSIGNED_INT:
        case GL_FLOAT:
        case GL_DOUBLE:
            break;
        default:
            JglSetError(GL_INVALID_ENUM);
            return;
    }
    a->Size = size;
    a->Type = type;
    a->Stride = stride;
    a->Pointer = pointer;
    a->Normalized = normalized != GL_FALSE;
    /* ARB_vertex_buffer_object: the pointer is an offset into the bound buffer. */
    a->Buffer = JglArrays()->ArrayBuffer.Name;
    a->Serial = JglArrays()->ArrayBuffer.Serial;
}
void APIENTRY glEnableVertexAttribArrayARB(GLuint index) {
    JGL_ARRAY *a = Array(index);
    if (a)
        a->Enabled = TRUE;
}
void APIENTRY glDisableVertexAttribArrayARB(GLuint index) {
    JGL_ARRAY *a = Array(index);
    if (a)
        a->Enabled = FALSE;
}
void APIENTRY glGetVertexAttribPointervARB(GLuint index, GLenum pname, void **pointer) {
    JGL_ARRAY *a = Array(index);
    if (!a || !pointer)
        return;
    if (pname != JGL_ATTRIB_ARRAY_POINTER) {
        JglSetError(GL_INVALID_ENUM);
        return;
    }
    *pointer = (void *)a->Pointer;
}
/* One array state value, or the four current components (count 4) from the
 * host; zero after a local error or a failed query. */
static ULONG Get(GLuint index, GLenum pname, GLint *state, GLfloat *current) {
    const JGL_ARRAY *a = Array(index);
    if (!a)
        return 0;
    switch (pname) {
        case JGL_ATTRIB_ARRAY_ENABLED:
            *state = a->Enabled;
            return 1;
        case JGL_ATTRIB_ARRAY_SIZE:
            *state = a->Size;
            return 1;
        case JGL_ATTRIB_ARRAY_STRIDE:
            *state = a->Stride;
            return 1;
        case JGL_ATTRIB_ARRAY_TYPE:
            *state = (GLint)a->Type;
            return 1;
        case JGL_ATTRIB_ARRAY_NORMALIZED:
            *state = a->Normalized;
            return 1;
        case JGL_ATTRIB_ARRAY_BUFFER_BINDING:
            if (!JglVertexBufferObjectAvailable())
                break;
            *state = (GLint)a->Buffer;
            return 1;
        case JGL_CURRENT_VERTEX_ATTRIB: {
            /* Attribute 0 is the vertex position: it has no current value. */
            ULONG args[3] = {index, pname, 0}, bytes = 0;
            if (!index) {
                JglSetError(GL_INVALID_OPERATION);
                return 0;
            }
            return JglQuery(FEnum_glGetVertexAttribfvARB, args, DG_GL_RESULT_FLOAT, current, 16,
                            &bytes) &&
                           bytes == 16
                       ? 4
                       : 0;
        }
        default:
            break;
    }
    JglSetError(GL_INVALID_ENUM);
    return 0;
}
void APIENTRY glGetVertexAttribfvARB(GLuint index, GLenum pname, GLfloat *params) {
    GLint state = 0;
    GLfloat current[4];
    const ULONG count = params ? Get(index, pname, &state, current) : 0;
    for (ULONG i = 0; i < count; ++i)
        params[i] = count == 4 ? current[i] : (GLfloat)state;
}
void APIENTRY glGetVertexAttribdvARB(GLuint index, GLenum pname, GLdouble *params) {
    GLint state = 0;
    GLfloat current[4];
    const ULONG count = params ? Get(index, pname, &state, current) : 0;
    for (ULONG i = 0; i < count; ++i)
        params[i] = count == 4 ? (GLdouble)current[i] : (GLdouble)state;
}
/* Current values round to the nearest integer, saturating. */
static GLint Round(GLfloat value) {
    if (!(value > -2147483648.0f))
        return value != value ? 0 : (GLint)0x80000000;
    if (value >= 2147483647.0f)
        return 0x7fffffff;
    return (GLint)(value < 0 ? value - 0.5f : value + 0.5f);
}
void APIENTRY glGetVertexAttribivARB(GLuint index, GLenum pname, GLint *params) {
    GLint state = 0;
    GLfloat current[4];
    const ULONG count = params ? Get(index, pname, &state, current) : 0;
    for (ULONG i = 0; i < count; ++i)
        params[i] = count == 4 ? Round(current[i]) : state;
}

} /* extern C */
