/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include "fixed.cpp"
#include "query.cpp"
static BOOL Ready = TRUE, Secondary = TRUE, Multitexture, FixedState, TextureControl;
static const GLubyte Negotiated[] = "negotiated extensions";
static ULONG ScalarFunctions[4], ScalarWords[4][4], ScalarCount;
static ULONG Error, Calls, Function, Bytes, Words, Queries, ResultCount = 4;
static BYTE Data[32];
static ULONG Arguments[3];
static JGL_UNPACK Pack = {4, 0, 0, 0, 0, 0}, Unpack = {4, 0, 0, 0, 0, 0};
BOOL JglSupportsSecondary(void) {
    return Secondary;
}
BOOL JglMultitextureAvailable(void) {
    return Multitexture;
}
BOOL JglFixedStateAvailable(void) {
    return FixedState;
}
BOOL JglTextureControlAvailable(void) {
    return TextureControl;
}
BOOL JglTextureCubeMapAvailable(void) {
    return FALSE;
}
BOOL JglFragmentProgramAvailable(void) {
    return FALSE;
}
GLubyte *JglProgramErrorBuffer(void) {
    return NULL;
}
const GLubyte *JglExtensions(void) {
    return Negotiated;
}
void JglScalarVector(ULONG fn, ULONG words, const void *p) {
    assert(ScalarCount < 4 && words == 4);
    ScalarFunctions[ScalarCount] = fn;
    memcpy(ScalarWords[ScalarCount++], p, 16);
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
    assert(*bytes <= capacity && ResultCount <= 4);
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
/* This isolated vector/query harness models ordinary execution outside lists. */
void JglCommandError(GLenum error) {
    JglSetError(error);
}
BOOL JglCommandReady(void) {
    return JglReady();
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
        const GLenum subpixelBits = 0x0d50;
        struct {
            GLint value, guard;
        } iv = {0, 99};
        struct {
            GLboolean value, guard;
        } bv = {0, 99};
        struct {
            GLfloat value, guard;
        } fv = {0, 99};
        struct {
            GLdouble value, guard;
        } dv = {0, 99};
        saved = Queries;
        ResultCount = 1;
        glGetIntegerv(subpixelBits, &iv.value);
        assert(Function == FEnum_glGetIntegerv && Arguments[0] == subpixelBits);
        glGetBooleanv(subpixelBits, &bv.value);
        assert(Function == FEnum_glGetBooleanv && Arguments[0] == subpixelBits);
        glGetFloatv(subpixelBits, &fv.value);
        assert(Function == FEnum_glGetFloatv && Arguments[0] == subpixelBits);
        glGetDoublev(subpixelBits, &dv.value);
        assert(Function == FEnum_glGetDoublev && Arguments[0] == subpixelBits);
        assert(iv.value == 1 && bv.value == TRUE && fv.value == 0.5f && dv.value == 0.5);
        assert(iv.guard == 99 && bv.guard == 99 && fv.guard == 99 && dv.guard == 99);
        assert(Queries == saved + 4);
    }
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
        // The frontend owns the negotiated string (its own exact tests).
        assert(glGetString(GL_EXTENSIONS) == Negotiated);
        Secondary = FALSE;
        saved = Calls;
        glLightModeliv(0x81f8, (const GLint *)1);
        assert(Calls == saved && Error == GL_INVALID_ENUM);
    }
    {
        saved = Queries;
        for (ULONG n = 0; n < 4; ++n) {
            Multitexture = n >= 2;
            FixedState = n & 1;
            assert(glGetString(GL_EXTENSIONS) == Negotiated);
            assert(!strcmp((const char *)glGetString(GL_VERSION), "1.1 DreamGPU"));
        }
        FixedState = FALSE;
        /* Unit-dependent server state is never answered locally. */
        ResultCount = 1;
        assert(glIsEnabled(GL_TEXTURE_2D) && Function == FEnum_glIsEnabled &&
               Arguments[0] == GL_TEXTURE_2D);
        static const GLenum host[] = {0x84e0, 0x84e2}; /* ACTIVE_TEXTURE, MAX_TEXTURE_UNITS */
        for (GLenum pname : host) {
            glGetIntegerv(pname, &local);
            assert(Function == FEnum_glGetIntegerv && Arguments[0] == pname && local == 1);
        }
        assert(Queries == saved + 3);
        Multitexture = FALSE;
        Secondary = TRUE;
    }
    {
        /* Fixed-state parameters exist only when negotiated; every size is
         * known before caller memory is read, and nothing partial is sent. */
        const GLfloat attenuation[3] = {1, .5f, .25f};
        static const GLenum single[] = {0x8126, 0x8127, 0x8128}; /* SIZE_MIN/MAX, FADE */
        void(APIENTRY *const point[3])(GLenum, const GLfloat *) = {
            glPointParameterfv, glPointParameterfvARB, glPointParameterfvEXT};
        Error = 0;
        saved = Calls;
        glPointParameterfvARB(0x8126, four);
        assert(Calls == saved && Error == GL_INVALID_OPERATION);
        Error = 0;
        glStencilFuncSeparateATI(0x0207, 0x0200, 3, 0xff);
        assert(!ScalarCount && Error == GL_INVALID_OPERATION);
        Error = 0;
        glTexParameterfv(GL_TEXTURE_2D, 0x84fe, four);
        assert(Calls == saved && Error == GL_INVALID_ENUM);
        Error = 0;
        glTexParameterfv(GL_TEXTURE_2D, 0x8191, four); /* GENERATE_MIPMAP_SGIS */
        assert(Calls == saved && Error == GL_INVALID_ENUM);
        Error = 0;
        TextureControl = TRUE;
        glTexParameterfv(GL_TEXTURE_2D, 0x8191, four);
        assert(Calls == saved + 1 && !Error && Function == FEnum_glTexParameterfv &&
               Arguments[1] == 0x8191 && Bytes == 4);
        glTexParameterfv(GL_TEXTURE_1D, 0x813d, four); /* TEXTURE_MAX_LEVEL is GL1.2 core */
        assert(Calls == saved + 2 && !Error && Bytes == 4);
        glTexParameterfv(0x8513, 0x8191, four); /* no cube maps */
        assert(Calls == saved + 2 && Error == GL_INVALID_ENUM);
        TextureControl = FALSE;
        saved = Calls;
        Error = 0;
        glTexEnviv(0x8500, 0x8501, ints);
        assert(Calls == saved && Error == GL_INVALID_ENUM);
        Error = 0;
        glTexEnvfv(0x8861, 0x8862, four);
        assert(Calls == saved && Error == GL_INVALID_ENUM);
        Error = 0;
        FixedState = TRUE;
        for (auto entry : point) {
            for (GLenum pname : single) {
                entry(pname, four);
                assert(Function == FEnum_glPointParameterfv && Words == 1 &&
                       Arguments[0] == pname && Bytes == 4 && !memcmp(Data, four, 4));
            }
            entry(0x8129, attenuation); /* DISTANCE_ATTENUATION */
            assert(Words == 1 && Arguments[0] == 0x8129 && Bytes == 12 &&
                   !memcmp(Data, attenuation, 12));
            saved = Calls;
            static const GLenum invalid[] = {0x8125, 0x812a, 0x0b11 /* POINT_SIZE */};
            for (GLenum pname : invalid) {
                entry(pname, (const GLfloat *)1);
                assert(Calls == saved && Error == GL_INVALID_ENUM);
                Error = 0;
            }
            entry(0x8129, NULL);
            assert(Calls == saved && Error == GL_INVALID_VALUE);
            Error = 0;
            Ready = FALSE;
            entry(0x8126, four);
            assert(Calls == saved && !Error);
            Ready = TRUE;
        }
        glTexParameterfv(GL_TEXTURE_2D, 0x84fe, four);
        assert(Function == FEnum_glTexParameterfv && Words == 2 && Arguments[1] == 0x84fe &&
               Bytes == 4);
        glTexParameteriv(GL_TEXTURE_1D, 0x84fe, ints);
        assert(Function == FEnum_glTexParameteriv && Arguments[0] == GL_TEXTURE_1D && Bytes == 4);
        glTexEnvfv(0x8500, 0x8501, four);
        assert(Function == FEnum_glTexEnvfv && Arguments[0] == 0x8500 && Bytes == 4);
        glTexEnviv(0x8861, 0x8862, ints);
        assert(Function == FEnum_glTexEnviv && Arguments[1] == 0x8862 && Bytes == 4);
        saved = Calls;
        static const GLenum pairs[][2] = {{0x8500, 0x8862},
                                          {0x8861, 0x8501},
                                          {GL_TEXTURE_ENV, 0x8501},
                                          {0x8500, GL_TEXTURE_ENV_MODE}};
        for (const auto &pair : pairs) {
            glTexEnvfv(pair[0], pair[1], (const GLfloat *)1);
            assert(Calls == saved && Error == GL_INVALID_ENUM);
            Error = 0;
        }
        // ATI: FRONT then BACK records sharing ref/mask; both functions first.
        glStencilFuncSeparateATI(0x0207, 0x0201, -1, 0xff);
        const ULONG front[4] = {GL_FRONT, 0x0207, 0xffffffff, 0xff},
                    back[4] = {GL_BACK, 0x0201, 0xffffffff, 0xff};
        assert(ScalarCount == 2 && ScalarFunctions[0] == FEnum_glStencilFuncSeparate &&
               ScalarFunctions[1] == FEnum_glStencilFuncSeparate &&
               !memcmp(ScalarWords[0], front, 16) && !memcmp(ScalarWords[1], back, 16));
        ScalarCount = 0;
        glStencilFuncSeparateATI(0x0208, 0x0200, 0, 0);
        glStencilFuncSeparateATI(0x0200, 0x01ff, 0, 0);
        assert(!ScalarCount && Error == GL_INVALID_ENUM);
        Error = 0;
        Ready = FALSE;
        glStencilFuncSeparateATI(0x0200, 0x0200, 0, 0);
        assert(!ScalarCount && !Error);
        Ready = TRUE;

        // Every new state query reaches the host with its own pname.
        static const GLenum integers[] = {0x8009, 0x883d, 0x80c9, 0x80c8, 0x80cb, 0x80ca,
                                          0x8800, 0x8801, 0x8802, 0x8803, 0x8ca3, 0x8ca4,
                                          0x8ca5, 0x84ff, 0x84fd, 0x8126, 0x8127, 0x8128};
        saved = Queries;
        ResultCount = 1;
        for (GLenum pname : integers) {
            glGetIntegerv(pname, &local);
            assert(Function == FEnum_glGetIntegerv && Arguments[0] == pname && local == 1);
        }
        struct {
            GLfloat v[4], guard;
        } color = {{0}, 99};
        ResultCount = 4;
        glGetFloatv(0x8005, color.v); /* BLEND_COLOR */
        assert(Function == FEnum_glGetFloatv && Arguments[0] == 0x8005 && color.v[3] == 3.5f &&
               color.guard == 99);
        ResultCount = 3;
        glGetFloatv(0x8129, color.v);
        assert(Arguments[0] == 0x8129 && color.v[2] == 2.5f);
        ResultCount = 1;
        glGetTexParameterfv(GL_TEXTURE_2D, 0x84fe, color.v);
        assert(Function == FEnum_glGetTexParameterfv && Arguments[1] == 0x84fe);
        glGetTexEnviv(0x8500, 0x8501, &local);
        assert(Function == FEnum_glGetTexEnviv && Arguments[0] == 0x8500);
        glGetTexEnviv(0x8861, 0x8862, &local);
        assert(Function == FEnum_glGetTexEnviv && Arguments[1] == 0x8862);
        assert(glIsEnabled(0x8861) && Function == FEnum_glIsEnabled && Arguments[0] == 0x8861);
        assert(Queries == saved + sizeof(integers) / sizeof(integers[0]) + 6);
        FixedState = FALSE;
    }
    puts("PASS actual fixed-function wrappers: bounded vector sizes, immutable bits, material "
         "Begin exception, typed query canaries and local array queries; fixed-state point "
         "parameters, anisotropy/LOD/sprite sizing, ATI stencil expansion and host queries");
    return 0;
}
