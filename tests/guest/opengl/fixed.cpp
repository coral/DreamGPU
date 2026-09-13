/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include "fixed.cpp"
#include "query.cpp"
static BOOL Ready = TRUE, Secondary = TRUE;
static ULONG Error, Calls, Function, Bytes, Words, Queries, ResultCount = 4;
static BYTE Data[32];
static ULONG Arguments[3];
static JGL_UNPACK Pack = {4, 0, 0, 0, 0, 0}, Unpack = {4, 0, 0, 0, 0, 0};
BOOL JglSupportsSecondary(void) {
    return Secondary;
}
BOOL JglReady(void) {
    return Ready;
}
void JglSetError(GLenum error) {
    Error = error;
}
JGL_UNPACK *JglPack(void) {
    return &Pack;
}
JGL_UNPACK *JglUnpack(void) {
    return &Unpack;
}
BOOL JglArrayQuery(GLenum pname, GLint *value) {
    if (pname != GL_VERTEX_ARRAY && pname != GL_VERTEX_ARRAY_SIZE)
        return FALSE;
    *value = pname == GL_VERTEX_ARRAY ? TRUE : 3;
    return TRUE;
}
BOOL JglData(ULONG fn, const ULONG *args, ULONG words, const void *data, ULONG bytes) {
    assert(Ready || fn == FEnum_glMaterialfv);
    assert(bytes <= sizeof(Data) && words <= 2);
    ++Calls;
    Function = fn;
    Words = words;
    Bytes = bytes;
    memcpy(Arguments, args, words * 4);
    memcpy(Data, data, bytes);
    return TRUE;
}
BOOL JglQuery(ULONG fn, const ULONG args[3], ULONG type, void *output, ULONG capacity,
              ULONG *bytes) {
    ULONG i;
    ++Queries;
    Function = fn;
    memcpy(Arguments, args, 12);
    *bytes = ResultCount * (type == DG_GL_RESULT_DOUBLE ? 8 : type == DG_GL_RESULT_BOOL ? 1 : 4);
    assert(*bytes <= capacity);
    for (i = 0; i < ResultCount; ++i) {
        if (type == DG_GL_RESULT_DOUBLE)
            ((GLdouble *)output)[i] = i + 0.5;
        else if (type == DG_GL_RESULT_FLOAT)
            ((GLfloat *)output)[i] = i + 0.5f;
        else if (type == DG_GL_RESULT_INT)
            ((GLint *)output)[i] = i + 1;
        else
            ((GLboolean *)output)[i] = TRUE;
    }
    return TRUE;
}
int main(void) {
    GLfloat four[4] = {1, 2, 3, 4}, three[3] = {5, 6, 7};
    GLdouble plane[4] = {1, 0, 0, 3};
    GLint ints[4] = {1, 2, 3, 4};
    struct {
        GLdouble v[4];
        GLdouble guard;
    } result = {{0}, 99};
    ULONG saved;
    GLint local = 0;
    glLightfv(GL_LIGHT0, GL_POSITION, four);
    assert(Function == FEnum_glLightfv && Bytes == 16 && Words == 2 && !memcmp(Data, four, 16));
    glLightfv(GL_LIGHT0, GL_SPOT_DIRECTION, three);
    assert(Bytes == 12 && !memcmp(Data, three, 12));
    glMaterialfv(GL_FRONT, GL_SHININESS, four);
    assert(Bytes == 4);
    glTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, ints);
    assert(Bytes == 16 && !memcmp(Data, ints, 16));
    glTexEnvfv(GL_TEXTURE_ENV, 0x8573, four);
    assert(Bytes == 4 && !memcmp(Data, four, 4));
    glTexEnviv(GL_TEXTURE_ENV, 0x8582, ints);
    assert(Bytes == 4 && !memcmp(Data, ints, 4));
    saved = Calls;
    glTexEnviv(GL_TEXTURE_ENV, 0xdead, (const GLint *)1);
    assert(Calls == saved && Error == GL_INVALID_ENUM);
    Error = 0;
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, four);
    assert(Bytes == 16);
    glTexGenfv(GL_S, GL_OBJECT_PLANE, four);
    assert(Bytes == 16);
    glTexGendv(GL_T, GL_EYE_PLANE, plane);
    assert(Bytes == 32 && !memcmp(Data, plane, 32));
    glClipPlane(GL_CLIP_PLANE0, plane);
    assert(Bytes == 32 && Words == 1);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, four);
    assert(Bytes == 16 && Words == 1);
    Ready = FALSE;
    glMaterialfv(GL_FRONT, GL_AMBIENT, four);
    assert(Function == FEnum_glMaterialfv && Bytes == 16);
    saved = Calls;
    glLightfv(GL_LIGHT0, GL_AMBIENT, four);
    assert(Calls == saved);
    Ready = TRUE;
    glLightfv(0, GL_POSITION, (const GLfloat *)1);
    assert(Error == GL_INVALID_ENUM && Calls == saved);
    glTexGenfv(GL_S, 0, (const GLfloat *)1);
    assert(Error == GL_INVALID_ENUM && Calls == saved);
    glClipPlane(GL_CLIP_PLANE0, NULL);
    assert(Error == GL_INVALID_VALUE && Calls == saved);
    glGetClipPlane(GL_CLIP_PLANE0, result.v);
    assert(Function == FEnum_glGetClipPlane && result.v[3] == 3.5 && result.guard == 99);
    glGetTexGendv(GL_S, GL_OBJECT_PLANE, result.v);
    assert(Function == FEnum_glGetTexGendv && Arguments[0] == GL_S &&
           Arguments[1] == GL_OBJECT_PLANE);
    glGetLightfv(GL_LIGHT0, GL_AMBIENT, four);
    assert(four[0] == .5f && four[3] == 3.5f);
    glGetMaterialiv(GL_FRONT, GL_DIFFUSE, ints);
    assert(ints[0] == 1 && ints[3] == 4);
    saved = Queries;
    glGetIntegerv(GL_VERTEX_ARRAY_SIZE, &local);
    assert(local == 3 && Queries == saved);
    assert(glIsEnabled(GL_VERTEX_ARRAY) && Queries == saved);
    ResultCount = 1;
    glIsEnabled(GL_VERTEX_ARRAY_SIZE);
    assert(Queries == saved + 1); /* Invalid cap must reach enum validation. */
    {
        const GLenum names[] = {GL_UNPACK_SWAP_BYTES, GL_UNPACK_LSB_FIRST, GL_PACK_SWAP_BYTES,
                                GL_PACK_LSB_FIRST,    GL_PACK_ALIGNMENT,   GL_UNPACK_ALIGNMENT};
        const GLint expected[] = {1, 0, 0, 1, 8, 4};
        ULONG n;
        saved = Queries;
        Unpack.SwapBytes = 1;
        Pack.LsbFirst = 1;
        Pack.Alignment = 8;
        for (n = 0; n < sizeof(names) / sizeof(names[0]); ++n) {
            struct {
                GLint value, guard;
            } iv = {-1, 99};
            struct {
                GLboolean value, guard;
            } bv = {9, 99};
            struct {
                GLfloat value, guard;
            } fv = {-1, 99};
            struct {
                GLdouble value, guard;
            } dv = {-1, 99};
            glGetIntegerv(names[n], &iv.value);
            glGetBooleanv(names[n], &bv.value);
            glGetFloatv(names[n], &fv.value);
            glGetDoublev(names[n], &dv.value);
            assert(iv.value == expected[n] && bv.value == (expected[n] != 0) &&
                   fv.value == expected[n] && dv.value == expected[n]);
            assert(iv.guard == 99 && bv.guard == 99 && fv.guard == 99 && dv.guard == 99);
        }
        assert(Queries == saved); /* Pixel store queries never cross guest/host IPC. */
        Ready = FALSE;
        local = 77;
        glGetIntegerv(GL_PACK_SWAP_BYTES, &local);
        assert(local == 77 && Queries == saved);
        Ready = TRUE;
    }
    {
        struct {
            GLfloat v[4], guard;
        } fv = {{0}, 99};
        struct {
            GLdouble v[4], guard;
        } dv = {{0}, 99};
        struct {
            GLint v[4], guard;
        } iv = {{0}, 99};
        struct {
            GLboolean v[4], guard;
        } bv = {{0}, 99};
        GLint separate = 0x81fa;
        ResultCount = 4;
        glGetFloatv(0x8459, fv.v);
        glGetDoublev(0x8459, dv.v);
        glGetIntegerv(0x8459, iv.v);
        glGetBooleanv(0x8459, bv.v);
        assert(fv.v[3] == 3.5f && dv.v[3] == 3.5 && iv.v[3] == 4 && bv.v[3] == TRUE);
        assert(fv.guard == 99 && dv.guard == 99 && iv.guard == 99 && bv.guard == 99);
        glLightModeliv(0x81f8, &separate);
        assert(Bytes == 4 && *(GLfloat *)Data == 0x81fa);
        assert(strstr((const char *)glGetString(GL_EXTENSIONS), "GL_EXT_secondary_color"));
        Secondary = FALSE;
        assert(!*glGetString(GL_EXTENSIONS));
        saved = Calls;
        glLightModeliv(0x81f8, (const GLint *)1);
        assert(Calls == saved && Error == GL_INVALID_ENUM);
    }
    puts("PASS actual fixed-function wrappers: bounded vector sizes, immutable bits, material "
         "Begin exception, typed query canaries and local array queries");
    return 0;
}
