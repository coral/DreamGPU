/* SPDX-License-Identifier: GPL-2.0-or-later
 * DG_CAP_GL_FIXED_STATE through the actual frontend, fixed-function vectors,
 * queries and procedure lookup on the strict transport fixture. */
#define main existing_frontend_suite_not_run
#include "frontend-harness.cpp"
#undef main
#include "arrays.cpp"
#include "query.cpp"
#include "fixed.cpp"
typedef void (*PROC)(void);
#define GL_FOG_INDEX 0x0b61
#define GL_FOG_DENSITY 0x0b62
#define GL_FOG_START 0x0b63
#define GL_FOG_END 0x0b64
#define GL_FOG_MODE 0x0b65
#define GL_FOG_COLOR 0x0b66
static void Marker(void) {}
extern "C" PROC GetProcAddress(HINSTANCE, LPCSTR name) {
    return name[0] == 'g' && name[1] == 'l' ? Marker : nullptr;
}
#include "compatibility.cpp"

#define MULTITEXTURE_NAMES                                                                         \
    "GL_ARB_multitexture GL_ARB_texture_env_combine GL_EXT_texture_env_combine "                   \
    "GL_ARB_texture_env_dot3"
#define SECONDARY_NAMES "GL_EXT_secondary_color GL_EXT_separate_specular_color"
#define FIXED_NAMES                                                                                \
    "GL_EXT_blend_color GL_EXT_blend_minmax GL_EXT_blend_subtract GL_EXT_blend_func_separate "     \
    "GL_EXT_blend_equation_separate GL_EXT_stencil_wrap GL_ATI_separate_stencil "                  \
    "GL_ARB_texture_border_clamp GL_ARB_texture_mirrored_repeat GL_EXT_texture_edge_clamp "        \
    "GL_SGIS_texture_edge_clamp GL_EXT_texture_filter_anisotropic GL_EXT_texture_lod_bias "        \
    "GL_ARB_point_parameters GL_EXT_point_parameters GL_ARB_point_sprite "                         \
    "GL_ARB_texture_env_add GL_EXT_texture_env_add"
#define CONTROL_NAMES                                                                              \
    "GL_ATI_texture_env_combine3 GL_ATI_texture_mirror_once GL_SGIS_generate_mipmap"
#define COMPRESSED_NAMES "GL_ARB_texture_compression GL_EXT_texture_compression_s3tc"
#define FOG_COORD_NAMES "GL_EXT_fog_coord"
#define BLEND 0x0be2U
#define POINT_SPRITE 0x8861U

static ULONG Bits(GLfloat value) {
    ULONG bits;
    memcpy(&bits, &value, 4);
    return bits;
}
static void Clear(JGL_CONTEXT *c) {
    assert(!c->InBegin);
    c->Used = c->Records = 0;
    c->Error = 0;
}
static const ULONG *Nth(JGL_CONTEXT *c, unsigned index) {
    const ULONG *r = c->Packet.Words;
    for (unsigned n = 0; n < index; ++n)
        r += r[1] / 4;
    return r;
}
static void Expect(JGL_CONTEXT *c, unsigned index, ULONG function, const ULONG *words,
                   ULONG count) {
    const ULONG *r = Nth(c, index);
    assert(r[0] == DG_GL_CALL && r[8] == function && r[1] == (9 + count) * 4);
    for (ULONG n = 0; n < count; ++n)
        assert(r[9 + n] == words[n]);
}

/* Every generated fixed-state entry point: exact function, words and bits. */
static const GLfloat C[4] = {-0.0f, 0.25f, 1.5f, 1e-40f}; /* sign, denormal */
struct Case {
    void (*call)(void);
    ULONG function, count, words[4];
};
static Case Cases[22];
static ULONG CaseCount;
#define CASE(expression, fn, ...)                                                                  \
    do {                                                                                           \
        const ULONG words[] = {__VA_ARGS__};                                                       \
        Case &entry = Cases[CaseCount++];                                                          \
        entry.call = [] { expression; };                                                           \
        entry.function = fn;                                                                       \
        entry.count = sizeof(words) / 4;                                                           \
        memcpy(entry.words, words, sizeof(words));                                                 \
    } while (0)
static void BuildCases(void) {
    const ULONG c0 = Bits(C[0]), c1 = Bits(C[1]), c2 = Bits(C[2]), c3 = Bits(C[3]);
    CaseCount = 0;
    CASE(glBlendColor(C[0], C[1], C[2], C[3]), FEnum_glBlendColor, c0, c1, c2, c3);
    CASE(glBlendColorEXT(C[0], C[1], C[2], C[3]), FEnum_glBlendColor, c0, c1, c2, c3);
    CASE(glBlendEquation(0x8007), FEnum_glBlendEquation, 0x8007);
    CASE(glBlendEquationEXT(0x800b), FEnum_glBlendEquation, 0x800b);
    CASE(glBlendFuncSeparate(0x0302, 0x0303, 1, 0), FEnum_glBlendFuncSeparate, 0x0302, 0x0303, 1,
         0);
    CASE(glBlendFuncSeparateEXT(0x8001, 0x8002, 0x8003, 0x8004), FEnum_glBlendFuncSeparate, 0x8001,
         0x8002, 0x8003, 0x8004);
    CASE(glBlendEquationSeparate(0x8006, 0x8008), FEnum_glBlendEquationSeparate, 0x8006, 0x8008);
    CASE(glBlendEquationSeparateEXT(0x800a, 0x800b), FEnum_glBlendEquationSeparate, 0x800a, 0x800b);
    CASE(glStencilOpSeparate(GL_FRONT, 0x1e00, 0x8507, 0x8508), FEnum_glStencilOpSeparate, GL_FRONT,
         0x1e00, 0x8507, 0x8508);
    CASE(glStencilOpSeparateATI(GL_BACK, 0x8508, 0x150a, 0x8507), FEnum_glStencilOpSeparate,
         GL_BACK, 0x8508, 0x150a, 0x8507);
    CASE(glStencilFuncSeparate(GL_FRONT_AND_BACK, 0x0203, -2, 0xf0f0), FEnum_glStencilFuncSeparate,
         GL_FRONT_AND_BACK, 0x0203, 0xfffffffe, 0xf0f0);
    CASE(glPointParameterf(0x8126, C[1]), FEnum_glPointParameterf, 0x8126, c1);
    CASE(glPointParameterfARB(0x8127, C[2]), FEnum_glPointParameterf, 0x8127, c2);
    CASE(glPointParameterfEXT(0x8128, C[3]), FEnum_glPointParameterf, 0x8128, c3);
}
#undef CASE
/* Hand-written entry points: one is enough to name each family here. */
static void Handwritten(void) {
    static const GLfloat values[3] = {1, 2, 3};
    glStencilFuncSeparateATI(0x0207, 0x0201, 1, 0xff);
    glPointParameterfv(0x8129, values);
    glPointParameterfvARB(0x8126, values);
    glPointParameterfvEXT(0x8127, values);
}

int main() {
    BuildCases();
    assert(CaseCount == 14);

    /* GL_EXTENSIONS: exact for all 64 negotiated combinations, built from
     * the established groups, in one static buffer stable across calls and
     * contexts. Without FIXED_STATE the M1/secondary strings are unchanged. */
    {
        const GLubyte *pointer = NULL;
        for (ULONG n = 0; n < 64; ++n) {
            Reset();
            HostCaps = (n & 1 ? DG_CAP_GL_MULTITEXTURE : 0) | (n & 4 ? DG_CAP_GL_FIXED_STATE : 0) |
                       (n & 8 ? DG_CAP_GL_TEXTURE_CONTROL : 0) |
                       (n & 16 ? DG_CAP_GL_COMPRESSED_TEXTURES : 0) |
                       (n & 32 ? DG_CAP_GL_FOG_COORD : 0);
            HostSecondaryWords = n & 2 ? 3 : 0;
            HGLRC context = Create();
            char expected[sizeof(MULTITEXTURE_NAMES " " SECONDARY_NAMES " " FIXED_NAMES
                                                    " " CONTROL_NAMES " " COMPRESSED_NAMES
                                                    " " FOG_COORD_NAMES)] = "";
            static const char *const groups[6] = {MULTITEXTURE_NAMES, SECONDARY_NAMES,
                                                  FIXED_NAMES,        CONTROL_NAMES,
                                                  COMPRESSED_NAMES,   FOG_COORD_NAMES};
            for (ULONG group = 0; group < 6; ++group)
                if (n & (1U << group)) {
                    if (*expected)
                        strcat(expected, " ");
                    strcat(expected, groups[group]);
                }
            const GLubyte *value = glGetString(GL_EXTENSIONS);
            assert(!strcmp((const char *)value, expected));
            assert(!pointer || value == pointer);
            pointer = value;
            assert(!strcmp((const char *)glGetString(GL_VERSION), "1.1 DreamGPU"));
            HGLRC second = wglCreateContext((HDC)2);
            assert(second && wglMakeCurrent((HDC)2, second));
            assert(glGetString(GL_EXTENSIONS) == pointer &&
                   !strcmp((const char *)pointer, expected));
            assert(wglMakeCurrent(NULL, NULL) && wglDeleteContext(second));
            assert(wglMakeCurrent((HDC)1, context));
            assert(glGetString(GL_EXTENSIONS) == pointer && JglExtensions() == pointer);
            assert(JglFixedStateAvailable() == !!(n & 4));
            assert(JglTextureControlAvailable() == !!(n & 8));
            assert(JglCompressedTexturesAvailable() == !!(n & 16));
            assert(JglFogCoordAvailable() == !!(n & 32));
            assert(wglMakeCurrent(NULL, NULL) && wglDeleteContext(context));
        }
        // The earlier combined string, verbatim.
        Reset();
        HostCaps = DG_CAP_GL_MULTITEXTURE;
        HostSecondaryWords = 3;
        HGLRC context = Create();
        assert(!strcmp((const char *)glGetString(GL_EXTENSIONS),
                       "GL_ARB_multitexture GL_ARB_texture_env_combine GL_EXT_texture_env_combine "
                       "GL_ARB_texture_env_dot3 GL_EXT_secondary_color "
                       "GL_EXT_separate_specular_color"));
        assert(wglMakeCurrent(NULL, NULL) && wglDeleteContext(context));
    }

    /* Not negotiated (here: multitexture only): no record of any kind leaves
     * the frontend, lookup hides every name and the vectors stay 1.1-sized. */
    Reset();
    HostCaps = DG_CAP_GL_MULTITEXTURE;
    HGLRC context = Create();
    JGL_CONTEXT *c = Lookup(context);
    for (ULONG n = 0; n < CaseCount; ++n) {
        Clear(c);
        Cases[n].call();
        assert(!c->Records && !c->Used && c->Error == GL_INVALID_OPERATION);
    }
    Clear(c);
    Handwritten();
    assert(!c->Records && c->Error == GL_INVALID_OPERATION);
    {
        const GLfloat one = 1;
        Clear(c);
        glTexParameterfv(GL_TEXTURE_2D, 0x84fe, &one);
        assert(!c->Records && c->Error == GL_INVALID_ENUM);
        Clear(c);
        glTexEnvfv(0x8500, 0x8501, &one);
        assert(!c->Records && c->Error == GL_INVALID_ENUM);
    }
    for (const char *name :
         {"glBlendColor", "glBlendColorEXT", "glBlendEquationEXT", "glBlendFuncSeparateEXT",
          "glBlendEquationSeparateEXT", "glStencilOpSeparateATI", "glStencilFuncSeparateATI",
          "glStencilOpSeparate", "glPointParameterfARB", "glPointParameterfvEXT"})
        assert(!wglGetProcAddress(name));
    assert(wglGetProcAddress("glBlendFunc") == Marker &&
           wglGetProcAddress("glActiveTextureARB") == Marker);
    // A display list defers the rejection with the existing error record.
    assert(JglListMode(GL_COMPILE));
    Clear(c);
    glBlendEquationEXT(0x8007);
    glStencilFuncSeparateATI(0x0207, 0x0201, 1, 0xff);
    assert(c->Records == 2 && !c->Error);
    const ULONG invalidOperation = GL_INVALID_OPERATION;
    Expect(c, 0, DG_GL_RECORD_ERROR, &invalidOperation, 1);
    Expect(c, 1, DG_GL_RECORD_ERROR, &invalidOperation, 1);
    assert(JglListMode(0));
    assert(wglMakeCurrent(NULL, NULL) && wglDeleteContext(context));

    /* Negotiated, without multitexture: families are independent. */
    Reset();
    HostCaps = DG_CAP_GL_FIXED_STATE;
    BindCapabilities = DG_WINDOW_CAP_FRONT_ONLY;
    context = Create();
    c = Lookup(context);
    Clear(c);
    glActiveTextureARB(0x84c1);
    assert(!c->Records && c->Error == GL_INVALID_OPERATION);
    assert(!wglGetProcAddress("glActiveTextureARB") && wglGetProcAddress("glBlendColorEXT"));
    for (const char *name : {"glBlendColor", "glBlendEquationSeparateEXT", "glStencilOpSeparateATI",
                             "glStencilFuncSeparateATI", "glPointParameterfvARB"})
        assert(wglGetProcAddress(name) == Marker);

    // Exact one-record encodings, float bits included.
    for (ULONG n = 0; n < CaseCount; ++n) {
        Clear(c);
        Cases[n].call();
        assert(c->Records == 1 && !c->Error);
        Expect(c, 0, Cases[n].function, Cases[n].words, Cases[n].count);
    }
    {
        // ATI separate stencil: FRONT then BACK core records, shared ref/mask.
        Clear(c);
        glStencilFuncSeparateATI(0x0207, 0x0201, -1, 0x80);
        const ULONG front[4] = {GL_FRONT, 0x0207, 0xffffffff, 0x80},
                    back[4] = {GL_BACK, 0x0201, 0xffffffff, 0x80};
        assert(c->Records == 2 && !c->Error);
        Expect(c, 0, FEnum_glStencilFuncSeparate, front, 4);
        Expect(c, 1, FEnum_glStencilFuncSeparate, back, 4);
        Clear(c);
        glStencilFuncSeparateATI(0x0207, 0x0208, 0, 0);
        assert(!c->Records && c->Error == GL_INVALID_ENUM);
        // Point parameter vectors: one DATA record, pname word, inline floats.
        const GLfloat attenuation[3] = {1, .5f, C[3]};
        for (auto entry : {glPointParameterfv, glPointParameterfvARB, glPointParameterfvEXT}) {
            Clear(c);
            entry(0x8129, attenuation);
            entry(0x8128, C);
            const ULONG *r = Nth(c, 0);
            assert(c->Records == 2 && !c->Error && r[0] == DG_GL_DATA_CALL);
            assert(r[1] == (8 + 2 + 1 + 3) * 4 && r[8] == FEnum_glPointParameterfv && r[9] == 12 &&
                   r[10] == 0x8129 && !memcmp(r + 11, attenuation, 12));
            r = Nth(c, 1);
            assert(r[1] == (8 + 2 + 1 + 1) * 4 && r[9] == 4 && r[10] == 0x8128 &&
                   r[11] == Bits(C[0]));
            Clear(c);
            entry(0x0b11, attenuation);
            assert(!c->Records && c->Error == GL_INVALID_ENUM);
        }
        // New parameters of existing vectors are sized when negotiated.
        Clear(c);
        glTexParameterfv(GL_TEXTURE_2D, 0x84fe, C + 2);
        glTexEnvfv(0x8500, 0x8501, C + 1);
        const GLint coordReplace = 1;
        glTexEnviv(POINT_SPRITE, 0x8862, &coordReplace);
        assert(c->Records == 3 && !c->Error);
        const ULONG *r = Nth(c, 0);
        assert(r[8] == FEnum_glTexParameterfv && r[9] == 4 && r[11] == 0x84fe &&
               r[12] == Bits(C[2]));
        r = Nth(c, 1);
        assert(r[8] == FEnum_glTexEnvfv && r[10] == 0x8500 && r[11] == 0x8501 &&
               r[12] == Bits(C[1]));
        r = Nth(c, 2);
        assert(r[8] == FEnum_glTexEnviv && r[10] == POINT_SPRITE && r[12] == 1);
        // New values of existing calls pass through unchanged.
        Clear(c);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x8370);
        glStencilOp(0x8507, 0x8508, 0x1e00);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, 0x0104);
        assert(c->Records == 3 && !c->Error && Nth(c, 0)[11] == 0x8370 && Nth(c, 1)[9] == 0x8507 &&
               Nth(c, 2)[11] == 0x0104);
        // The fixture validates framing and zero DATA padding on submission.
        glFlush();
        assert(!c->Failed && !c->Error);
    }

    // None of them is legal inside Begin/End; each is rejected unsent.
    Clear(c);
    glBegin(GL_TRIANGLES);
    for (ULONG n = 0; n < CaseCount; ++n) {
        Cases[n].call();
        assert(c->Records == 1 && c->Error == GL_INVALID_OPERATION);
        c->Error = 0;
    }
    Handwritten();
    assert(c->Records == 1 && c->Error == GL_INVALID_OPERATION);
    c->Error = 0;
    glEnd();
    assert(c->Records == 2 && !c->Error);

    /* BlendFunc cache: BlendFuncSeparate and unmodelled factors (constant
     * colors) make it unknown; BlendEquation does not. POINT_SPRITE is an
     * unknown cap and never cached. */
    Clear(c);
    glBlendFunc(1, 0);
    glBlendFunc(1, 0);
    glBlendEquation(0x8008);
    glBlendFunc(1, 0);
    glBlendFuncSeparateEXT(0x0302, 0x0303, 1, 0);
    glBlendFunc(1, 0);
    glBlendFunc(1, 0);
    glBlendFunc(0x8001, 0);
    glBlendFunc(0x8001, 0);
    glBlendFunc(1, 0);
    glEnable(POINT_SPRITE);
    glEnable(POINT_SPRITE);
    glEnable(BLEND);
    glEnable(BLEND);
    assert(c->Records == 10 && !c->Error);
    static const ULONG sequence[10] = {
        FEnum_glBlendFunc, FEnum_glBlendEquation, FEnum_glBlendFuncSeparate, FEnum_glBlendFunc,
        FEnum_glBlendFunc, FEnum_glBlendFunc,     FEnum_glBlendFunc,         FEnum_glEnable,
        FEnum_glEnable,    FEnum_glEnable};
    for (ULONG n = 0; n < 10; ++n)
        assert(Nth(c, n)[8] == sequence[n]);
    assert(Nth(c, 4)[9] == 0x8001 && Nth(c, 5)[9] == 0x8001 && Nth(c, 6)[9] == 1);

    // Lists record both ATI records and every scalar; nothing is cached.
    assert(JglListMode(GL_COMPILE));
    Clear(c);
    glBlendColorEXT(1, 1, 1, 1);
    glStencilFuncSeparateATI(0x0200, 0x0207, 0, 0);
    glBlendFuncSeparate(1, 0, 1, 0);
    glBlendFunc(1, 0);
    glBlendFunc(1, 0);
    assert(c->Records == 6 && !c->Error && Nth(c, 2)[8] == FEnum_glStencilFuncSeparate &&
           Nth(c, 2)[9] == GL_BACK);
    assert(JglListMode(0));

    // State queries are answered by the host, never locally.
    {
        ULONG queries = QueryCalls;
        GLint value = 0;
        HostInteger = 0x0207;
        glGetIntegerv(0x8800, &value); /* STENCIL_BACK_FUNC */
        assert(value == 0x0207 && LastQueryFunction == FEnum_glGetIntegerv &&
               LastQueryArgument == 0x8800);
        HostEnabled = 1;
        assert(glIsEnabled(POINT_SPRITE) && LastQueryArgument == POINT_SPRITE);
        assert(QueryCalls == queries + 2);
    }
    assert(wglMakeCurrent(NULL, NULL) && wglDeleteContext(context));

    /* Name-sized scalars: an unknown, vector-only or unnegotiated name is a
     * local GL_INVALID_ENUM. The host would reject the record as malformed and
     * fail the batch, losing the context. Values are never judged locally. */
    for (ULONG negotiated = 0; negotiated < 2; ++negotiated) {
        Reset();
        HostCaps = DG_CAP_GL_MULTITEXTURE | DG_CAP_GL_FIXED_STATE |
                   (negotiated ? DG_CAP_GL_TEXTURE_CONTROL : 0);
        context = Create();
        c = Lookup(context);
        const struct {
            void (*call)(void);
            BOOL valid;
        } scalars[] = {
            {[] { glTexParameteri(GL_TEXTURE_2D, 0x813b, 0); }, FALSE}, /* MAX_LOD: GL1.2 */
            {[] { glTexParameterf(GL_TEXTURE_2D, 0x1004, 1); }, FALSE}, /* BORDER_COLOR */
            {[] { glTexParameteri(0x8513, 0x2801, 0x2600); }, FALSE},   /* cube map */
            {[] { glTexEnvi(0x2300, 0x8583, 0x1702); }, FALSE},         /* SOURCE3_RGB_NV */
            {[] { glTexEnvf(0x2300, 0x2201, 0); }, FALSE},              /* ENV_COLOR */
            {[] { glHint(0x0c54, 0x1234); }, FALSE},
            {[] { glHint(0x8457, 0x1102); }, FALSE}, /* FOG_COORDINATE_SOURCE */
            {[] { glTexParameteri(GL_TEXTURE_2D, 0x2802, 0x8743); }, TRUE}, /* MIRROR_ONCE */
            {[] { glTexParameterf(GL_TEXTURE_1D, 0x813c, 1); }, TRUE},      /* BASE_LEVEL */
            {[] { glTexEnvi(0x2300, 0x8571, 0x8744); }, TRUE},              /* MODULATE_ADD */
            {[] { glHint(0x0c50, 0x1102); }, TRUE},
            {[] { glTexParameteri(GL_TEXTURE_2D, 0x8191, 1); }, 2}, /* GENERATE_MIPMAP */
            {[] { glHint(0x8192, 0x1102); }, 2},                    /* GENERATE_MIPMAP_HINT */
        };
        for (const auto &item : scalars) {
            BOOL valid = item.valid == 2 ? negotiated : item.valid;
            Clear(c);
            item.call();
            assert(valid ? c->Records == 1 && !c->Error
                         : !c->Records && c->Error == GL_INVALID_ENUM);
        }
        // Within a display list the rejection is deferred, never recorded raw.
        assert(JglListMode(GL_COMPILE));
        Clear(c);
        glHint(0x8192, 0x1102);
        glTexParameteri(GL_TEXTURE_2D, 0x813b, 0);
        assert(c->Records == 2 && !c->Error);
        const ULONG invalidEnum = GL_INVALID_ENUM;
        if (!negotiated)
            Expect(c, 0, DG_GL_RECORD_ERROR, &invalidEnum, 1);
        else
            assert(Nth(c, 0)[8] == FEnum_glHint);
        Expect(c, 1, DG_GL_RECORD_ERROR, &invalidEnum, 1);
        assert(JglListMode(0));
        assert(wglMakeCurrent(NULL, NULL) && wglDeleteContext(context));
    }
    BindCapabilities = 0;
    Reset();
    assert(!Allocations);
    puts("PASS fixed state actual frontend: exact negotiated extension strings and stable "
         "pointers; unnegotiated hosts unchanged; exact records/bits, ATI expansion, point "
         "vectors, new pnames; Begin rejection; blend cache; lists; host queries; name-sized "
         "scalar pre-validation and texture-control negotiation");
    return 0;
}
