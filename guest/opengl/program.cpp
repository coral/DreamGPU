/* SPDX-License-Identifier: GPL-2.0-or-later
 * ARB_fragment_program entry points (DG_CAP_GL_FRAGMENT_PROGRAM). Only the
 * FRAGMENT_PROGRAM_ARB target exists. Everything the record itself can show to
 * be invalid (target, format, pname, length, counts) is a local GL error, never
 * a record the host would reject as malformed. Validity that depends on objects
 * or program text (wrong-target names, parse errors, non-ASCII bytes, unit
 * limits, exhausted storage) is decided by the host and reported as GL errors
 * with PROGRAM_ERROR_POSITION/STRING. Names, objects and the default object are
 * host state shared with the context's texture namespace.
 *
 * Without the capability every entry point fails locally with
 * GL_INVALID_OPERATION and submits nothing; wglGetProcAddress hides them.
 * Float parameters are copied as bits (packing.h); double variants convert to
 * the float record, as native GL does.
 */
#include "internal.h"
#include "packing.h"

extern "C" {

#define JGL_PROGRAM_FORMAT_ASCII 0x8875
#define JGL_PROGRAM_LENGTH 0x8627
#define JGL_PROGRAM_STRING 0x8628

/* GetProgramivARB names of a fragment program target. The address register
 * names (0x88b0-0x88b3) exist only with ARB_vertex_program. */
static BOOL ProgramParameter(GLenum pname) {
    return pname == JGL_PROGRAM_LENGTH || pname == 0x8876 /* FORMAT */ ||
           pname == 0x8677 /* BINDING */ || (pname >= 0x88a0 && pname <= 0x88af) ||
           (pname >= 0x88b4 && pname <= 0x88b6) || (pname >= 0x8805 && pname <= 0x8810);
}
/* Commands compiled into display lists defer their errors (CommandError); the
 * immediate ones (Gen, Delete, Is and queries) raise them at once. */
static BOOL Ready(BOOL compiled) {
    if (!(compiled ? JglCommandReady() : JglReady()))
        return FALSE;
    if (!JglFragmentProgramAvailable()) {
        if (compiled)
            JglCommandError(GL_INVALID_OPERATION);
        else
            JglSetError(GL_INVALID_OPERATION);
        return FALSE;
    }
    return TRUE;
}
static BOOL Target(GLenum target, BOOL compiled) {
    if (target == JGL_FRAGMENT_PROGRAM)
        return TRUE;
    if (compiled)
        JglCommandError(GL_INVALID_ENUM);
    else
        JglSetError(GL_INVALID_ENUM);
    return FALSE;
}

void APIENTRY glGenProgramsARB(GLsizei count, GLuint *programs) {
    if (!Ready(FALSE))
        return;
    if (count < 0 || (count && !programs)) {
        JglSetError(GL_INVALID_VALUE);
        return;
    }
    /* Each query reserves up to one batch of host names. A failed query has
     * no names (the host records GL_OUT_OF_MEMORY) and writes none. */
    while (count) {
        const ULONG batch =
            (ULONG)count < DG_GL_PROGRAM_NAME_BATCH ? (ULONG)count : DG_GL_PROGRAM_NAME_BATCH;
        ULONG args[3] = {batch, 0, 0}, bytes = 0;
        GLuint names[DG_GL_PROGRAM_NAME_BATCH];
        if (!JglQuery(FEnum_glGenProgramsARB, args, DG_GL_RESULT_INT, names, batch * 4, &bytes) ||
            bytes != batch * 4 || !names[0])
            return;
        CopyMemory(programs, names, bytes);
        programs += batch;
        count -= batch;
    }
}
void APIENTRY glDeleteProgramsARB(GLsizei count, const GLuint *programs) {
    ULONG capacity, batch;
    if (!Ready(FALSE))
        return;
    if (count < 0 || (count && !programs)) {
        JglSetError(GL_INVALID_VALUE);
        return;
    }
    capacity = JglMaxDataBytes(1) / sizeof(*programs);
    if (capacity > DG_GL_MAX_PROGRAMS)
        capacity = DG_GL_MAX_PROGRAMS;
    if (count && !capacity) {
        JglSetError(GL_OUT_OF_MEMORY);
        return;
    }
    while (count) {
        batch = (ULONG)count < capacity ? (ULONG)count : capacity;
        if (!JglData(FEnum_glDeleteProgramsARB, &batch, 1, programs, batch * sizeof(*programs)))
            return;
        programs += batch;
        count -= batch;
    }
}
GLboolean APIENTRY glIsProgramARB(GLuint program) {
    ULONG args[3] = {program, 0, 0}, bytes = 0;
    GLboolean result = GL_FALSE;
    if (!Ready(FALSE) || !program)
        return GL_FALSE;
    if (!JglQuery(FEnum_glIsProgramARB, args, DG_GL_RESULT_BOOL, &result, 1, &bytes) || bytes != 1)
        return GL_FALSE;
    return result != 0;
}
void APIENTRY glBindProgramARB(GLenum target, GLuint program) {
    if (!Ready(TRUE) || !Target(target, TRUE))
        return;
    const ULONG words[2] = {target, program};
    JglScalarVector(FEnum_glBindProgramARB, 2, words);
}
void APIENTRY glProgramStringARB(GLenum target, GLenum format, GLsizei length, const void *string) {
    if (!Ready(TRUE) || !Target(target, TRUE))
        return;
    if (format != JGL_PROGRAM_FORMAT_ASCII) {
        JglCommandError(GL_INVALID_ENUM);
        return;
    }
    if (length < 0 || (length && !string)) {
        JglCommandError(GL_INVALID_VALUE);
        return;
    }
    const ULONG args[3] = {target, format, (ULONG)length};
    /* A string beyond the protocol or packet bound travels without its bytes:
     * the host fails the load at position length, keeping the bound program. */
    const ULONG fits =
        JglMaxDataBytes(3) < DG_GL_MAX_PROGRAM_BYTES ? JglMaxDataBytes(3) : DG_GL_MAX_PROGRAM_BYTES;
    if ((ULONG)length > fits)
        JglData(FEnum_glProgramStringARB, args, 3, NULL, 0);
    else
        JglData(FEnum_glProgramStringARB, args, 3, string, (ULONG)length);
}

/* One six-word record: target, index, then four float words. */
#define JGL_PARAMETER_BITS(function, ...)                                                          \
    {                                                                                              \
        ULONG words[6] = {target, index, 0, 0, 0, 0};                                              \
        if (!Ready(TRUE) || !Target(target, TRUE))                                                 \
            return;                                                                                \
        __VA_ARGS__;                                                                               \
        JglScalarVector(function, 6, words);                                                       \
    }
#define JGL_PARAMETER_FLOATS(name, function)                                                       \
    JGL_PACKING_ONLY void APIENTRY name##4fARB(GLenum target, GLuint index, GLfloat x, GLfloat y,  \
                                               GLfloat z, GLfloat w)                               \
        JGL_PARAMETER_BITS(                                                                        \
            function, __builtin_memcpy(words + 2, &x, 4), __builtin_memcpy(words + 3, &y, 4),      \
            __builtin_memcpy(words + 4, &z, 4),                                                    \
            __builtin_memcpy(                                                                      \
                words + 5, &w,                                                                     \
                4)) /* A null vector submits nothing, like the other float vector aliases. */      \
        JGL_PACKING_ONLY void APIENTRY name##4fvARB(GLenum target, GLuint index, const GLfloat *v) \
            JGL_PARAMETER_BITS(function, if (!v) return; __builtin_memcpy(words + 2, v, 16))
JGL_PARAMETER_FLOATS(glProgramEnvParameter, FEnum_glProgramEnvParameter4fARB)
JGL_PARAMETER_FLOATS(glProgramLocalParameter, FEnum_glProgramLocalParameter4fARB)
#undef JGL_PARAMETER_FLOATS
#undef JGL_PARAMETER_BITS

static void Parameter(ULONG function, GLenum target, GLuint index, GLfloat x, GLfloat y, GLfloat z,
                      GLfloat w) {
    const struct {
        ULONG Target, Index;
        GLfloat Value[4];
    } words = {target, index, {x, y, z, w}};
    static_assert(sizeof(words) == 24);
    if (!Ready(TRUE) || !Target(target, TRUE))
        return;
    JglScalarVector(function, 6, &words);
}
#define JGL_PARAMETER_DOUBLES(name, function)                                                      \
    void APIENTRY name##4dARB(GLenum target, GLuint index, GLdouble x, GLdouble y, GLdouble z,     \
                              GLdouble w) {                                                        \
        Parameter(function, target, index, (GLfloat)x, (GLfloat)y, (GLfloat)z, (GLfloat)w);        \
    }                                                                                              \
    void APIENTRY name##4dvARB(GLenum target, GLuint index, const GLdouble *v) {                   \
        if (v)                                                                                     \
            Parameter(function, target, index, (GLfloat)v[0], (GLfloat)v[1], (GLfloat)v[2],        \
                      (GLfloat)v[3]);                                                              \
    }
JGL_PARAMETER_DOUBLES(glProgramEnvParameter, FEnum_glProgramEnvParameter4fARB)
JGL_PARAMETER_DOUBLES(glProgramLocalParameter, FEnum_glProgramLocalParameter4fARB)
#undef JGL_PARAMETER_DOUBLES

/* Four floats of an env or local parameter; FALSE leaves the output alone. */
static BOOL GetParameter(ULONG function, GLenum target, GLuint index, GLfloat *values) {
    ULONG args[3] = {target, index, 0}, bytes = 0;
    if (!Ready(FALSE) || !Target(target, FALSE) || !values)
        return FALSE;
    return JglQuery(function, args, DG_GL_RESULT_FLOAT, values, 16, &bytes) && bytes == 16;
}
void APIENTRY glGetProgramEnvParameterfvARB(GLenum target, GLuint index, GLfloat *params) {
    GLfloat values[4];
    if (GetParameter(FEnum_glGetProgramEnvParameterfvARB, target, index, values))
        CopyMemory(params, values, sizeof(values));
}
void APIENTRY glGetProgramLocalParameterfvARB(GLenum target, GLuint index, GLfloat *params) {
    GLfloat values[4];
    if (GetParameter(FEnum_glGetProgramLocalParameterfvARB, target, index, values))
        CopyMemory(params, values, sizeof(values));
}
void APIENTRY glGetProgramEnvParameterdvARB(GLenum target, GLuint index, GLdouble *params) {
    GLfloat values[4];
    if (GetParameter(FEnum_glGetProgramEnvParameterfvARB, target, index, values))
        for (ULONG i = 0; i < 4; ++i)
            params[i] = values[i];
}
void APIENTRY glGetProgramLocalParameterdvARB(GLenum target, GLuint index, GLdouble *params) {
    GLfloat values[4];
    if (GetParameter(FEnum_glGetProgramLocalParameterfvARB, target, index, values))
        for (ULONG i = 0; i < 4; ++i)
            params[i] = values[i];
}
void APIENTRY glGetProgramivARB(GLenum target, GLenum pname, GLint *params) {
    ULONG args[3] = {target, pname, 0}, bytes = 0;
    GLint value;
    if (!Ready(FALSE) || !Target(target, FALSE) || !params)
        return;
    if (!ProgramParameter(pname)) {
        JglSetError(GL_INVALID_ENUM);
        return;
    }
    if (JglQuery(FEnum_glGetProgramivARB, args, DG_GL_RESULT_INT, &value, 4, &bytes) && bytes == 4)
        *params = value;
}
/* PROGRAM_LENGTH_ARB bytes of the bound program's string, in bounded windows. */
void APIENTRY glGetProgramStringARB(GLenum target, GLenum pname, void *string) {
    ULONG length = 0, done = 0, bytes = 0, *buffer;
    GLint value = 0;
    if (!Ready(FALSE) || !Target(target, FALSE))
        return;
    if (pname != JGL_PROGRAM_STRING) {
        JglSetError(GL_INVALID_ENUM);
        return;
    }
    {
        ULONG args[3] = {target, JGL_PROGRAM_LENGTH, 0};
        if (!JglQuery(FEnum_glGetProgramivARB, args, DG_GL_RESULT_INT, &value, 4, &bytes) ||
            bytes != 4 || value < 0)
            return;
    }
    length = (ULONG)value < DG_GL_MAX_PROGRAM_BYTES ? (ULONG)value : DG_GL_MAX_PROGRAM_BYTES;
    if (!length || !string)
        return;
    buffer = JglReadbackBuffer();
    if (!buffer)
        return;
    while (done < length) {
        const ULONG words = (length - done + 3) / 4 < JglReadbackCapacity() / 4
                                ? (length - done + 3) / 4
                                : JglReadbackCapacity() / 4;
        const ULONG copy = words * 4 < length - done ? words * 4 : length - done;
        ULONG args[3] = {target, words, done / 4};
        if (!JglQuery(FEnum_glGetProgramStringARB, args, DG_GL_RESULT_INT, buffer, words * 4,
                      &bytes) ||
            bytes != words * 4)
            return;
        CopyMemory((BYTE *)string + done, buffer, copy);
        done += copy;
    }
}
} /* extern C */
