/* SPDX-License-Identifier: GPL-2.0-or-later
 * DG_CAP2_GL_DEPTH_TEXTURE (ARB_depth_texture, ARB_shadow and, with
 * ARB_fragment_program, ARB_fragment_program_shadow), the first feature of
 * the second capability word, through the actual frontend on the strict
 * transport fixture: negotiation from the CAPABILITIES reply's DeviceError
 * and the unchanged frontend without it (an older host, or an older driver
 * that leaves DeviceError zero); local validation of the compare and depth
 * texture mode values; exact records of 1D, 2D and rectangle depth images,
 * proxies, subimages and copies, all within the host's rules and admitted by
 * the NT validator; depth targets that have no depth images; depth readback
 * as DEPTH_COMPONENT (every scalar type) and packed DEPTH_STENCIL words after
 * the level's depth and stencil sizes; and deferred errors in display lists. */
#include <array>
#include <utility>
#include <vector> /* before the Win32 fixture's min() macro */
#define main existing_frontend_suite_not_run
#include "frontend-harness.cpp"
#undef main
#include "arrays.cpp"
#include "query.cpp"
#include "texture.cpp"
#include "compressed.cpp"
#include "fixed.cpp"
/* Readback keeps its own (C-linkage) arithmetic helpers under other names. */
#define Add ReadbackAdd
#define Multiply ReadbackMultiply
#include "readback.cpp"
#undef Multiply
#undef Add
#include "framebuffer.cpp"
typedef void (*PROC)(void);
static void Marker(void) {}
extern "C" PROC GetProcAddress(HINSTANCE, LPCSTR name) {
    return name[0] == 'g' && name[1] == 'l' ? Marker : nullptr;
}
#include "compatibility.cpp"
#include "extension-names.h"
#include "dg-gl-validate.h"

constexpr GLenum TEX_1D = 0x0de0, TEX_2D = 0x0de1, TEX_RECT = 0x84f5, TEX_3D = 0x806f,
                 CUBE = 0x8513, CUBE_X = 0x8515, PROXY_1D = 0x8063, PROXY_2D = 0x8064,
                 PROXY_RECT = 0x84f7, PROXY_CUBE = 0x851b;
constexpr GLenum COMPARE_MODE = 0x884c, COMPARE_FUNC = 0x884d, DEPTH_MODE = 0x884b,
                 COMPARE_R = 0x884e, DEPTH_SIZE = 0x884a, STENCIL_SIZE = 0x88f1, INTENSITY = 0x8049,
                 LEQUAL = 0x0203, GEQUAL = 0x0206, LESS = 0x0201, RED = 0x1903, MIN_FILTER = 0x2801,
                 LINEAR = 0x2601;
constexpr GLenum DEPTH = 0x1902, DEPTH16 = 0x81a5, DEPTH24 = 0x81a6, DEPTH32 = 0x81a7,
                 DEPTH_STENCIL = 0x84f9, DEPTH24_STENCIL8 = 0x88f0, UINT_24_8 = 0x84fa,
                 RGBA8 = 0x8058;
constexpr ULONG OTHERS =
    DG_CAP_GL_MULTITEXTURE | DG_CAP_GL_FIXED_STATE | DG_CAP_GL_TEXTURE_CONTROL |
    DG_CAP_GL_COMPRESSED_TEXTURES | DG_CAP_GL_FOG_COORD | DG_CAP_GL_TEXTURE_CUBE_MAP |
    DG_CAP_GL_VERTEX_PROGRAM | DG_CAP_GL_TEXTURE_3D | DG_CAP_GL_VERTEX_BUFFER_OBJECT |
    DG_CAP_GL_FRAMEBUFFER_OBJECT | DG_CAP_GL_VERTEX_ARRAY_BGRA | DG_CAP_GL_OCCLUSION_QUERY |
    DG_CAP_GL_TEXTURE_RECTANGLE | DG_CAP_GL_TEXTURE_IMAGES | DG_CAP_GL_TEXTURE_BORDERS |
    DG_CAP_GL_BULK_READBACK;
constexpr ULONG PROGRAMS = DG_CAP_GL_FRAGMENT_PROGRAM, DEPTH_TEXTURE = DG_CAP2_GL_DEPTH_TEXTURE;

/* The host's depth image rules (gl.h, gl_validation.rs): any depth record
 * outside them is a batch fault that fails the context. */
static BOOL HostDepthFormat(ULONG f) {
    return f == DEPTH || (f >= DEPTH16 && f <= DEPTH32) || f == DEPTH_STENCIL ||
           f == DEPTH24_STENCIL8;
}
static BOOL HostDepthTarget(ULONG t) {
    return t == TEX_1D || t == TEX_2D || t == TEX_RECT || t == PROXY_1D || t == PROXY_2D ||
           t == PROXY_RECT;
}
static BOOL HostDepthPixels(ULONG format, ULONG type) {
    return (format == DEPTH && type >= 0x1400 && type <= 0x1406) ||
           (format == DEPTH_STENCIL && type == UINT_24_8);
}
static void HostShape(const std::vector<ULONG> &r) {
    if (r[0] == DG_GL_DATA_CALL &&
        (r[8] == FEnum_glTexImage1D || r[8] == FEnum_glTexImage2D ||
         r[8] == FEnum_glTexSubImage1D || r[8] == FEnum_glTexSubImage2D)) {
        const ULONG *a = r.data() + 10;
        const BOOL image = r[8] == FEnum_glTexImage1D || r[8] == FEnum_glTexImage2D;
        if (image && HostDepthFormat(a[2]))
            assert(HostDepthTarget(a[0]) && a[5] == 0);
        if (a[6] == DEPTH || a[6] == DEPTH_STENCIL || a[7] == UINT_24_8)
            assert(HostDepthTarget(a[0]) && HostDepthPixels(a[6], a[7]));
        return;
    }
    if (r[0] == DG_GL_QUERY && r[8] == FEnum_glGetTexImage && (r[10] & DG_GL_TEXTURE_READ_DEPTH))
        assert(r[9] == TEX_1D || r[9] == TEX_2D || r[9] == TEX_RECT);
    if (r[0] == DG_GL_CALL && (r[8] == FEnum_glCopyTexImage2D || r[8] == FEnum_glCopyTexImage1D) &&
        HostDepthFormat(r[11]))
        assert(HostDepthTarget(r[9]) && !r[r[8] == FEnum_glCopyTexImage2D ? 16 : 15]);
}
/* Argument words of the records this suite submits, as the host reports them
 * to the NT kernel. */
static ULONG KernelWords(void *, ULONG function) {
    switch (function) {
        case FEnum_glBindTexture:
            return 2;
        case FEnum_glTexParameteri:
        case FEnum_glTexParameterf:
            return 3;
        case FEnum_glTexParameteriv:
        case FEnum_glTexParameterfv:
            return DG_GL_FUNCTION_INLINE_DATA | 2;
        case DG_GL_RECORD_ERROR:
            return 1;
        case FEnum_glCopyTexImage1D:
            return 7;
        case FEnum_glCopyTexImage2D:
        case FEnum_glCopyTexSubImage2D:
            return 8;
        case FEnum_glTexImage1D:
        case FEnum_glTexSubImage1D:
        case FEnum_glTexImage2D:
        case FEnum_glTexSubImage2D:
            return DG_GL_FUNCTION_INLINE_DATA | 8;
        case FEnum_glGetTexParameteriv:
        case FEnum_glGetTexParameterfv:
            return DG_GL_FUNCTION_QUERY | 2;
        case FEnum_glGetTexLevelParameteriv:
        case FEnum_glGetTexImage:
            return DG_GL_FUNCTION_QUERY | 3;
        default:
            return (ULONG)-1;
    }
}
typedef std::vector<std::vector<ULONG>> Records;
static Records Log;
static void Capture(const ULONG *record) {
    Log.emplace_back(record, record + record[1] / 4);
    const std::vector<ULONG> &r = Log.back();
    if (r[0] != DG_GL_CALL && r[0] != DG_GL_DATA_CALL && r[0] != DG_GL_QUERY)
        return;
    HostShape(r);
    if (KernelWords(NULL, r[8]) == (ULONG)-1)
        return;
    std::vector<ULONG> copy(r);
    const DG_GL_LIMITS limits = {7, DG_ESCAPE_MAX_BYTES, DG_GL_MAX_RECORDS, 1};
    assert(DgValidateUserGl(copy.data(), copy.size() * 4, 42, 7, &limits, KernelWords, NULL));
}
static void Clear(JGL_CONTEXT *c) {
    assert(!c->InBegin);
    c->Used = c->Records = 0;
    c->Error = 0;
    Log.clear();
}
static void Submit(JGL_CONTEXT *c) {
    assert(Flush(c) && !c->Failed);
}
/* The call raised exactly this error and nothing reached the host. */
static void Rejected(JGL_CONTEXT *c, GLenum error) {
    assert(c->Error == error && !c->Records && !c->Used);
    Submit(c);
    assert(Log.empty());
    c->Error = 0;
}
/* The records of everything so far, which raised no error. */
static Records Sent(JGL_CONTEXT *c) {
    assert(!c->Error);
    Submit(c);
    Records out;
    out.swap(Log);
    return out;
}
static void ExpectCall(const std::vector<ULONG> &r, ULONG function,
                       std::initializer_list<ULONG> words) {
    assert(r[0] == DG_GL_CALL && r[8] == function && r[1] == (9 + words.size()) * 4);
    ULONG n = 9;
    for (ULONG word : words)
        assert(r[n++] == word);
}
static void ExpectData(const std::vector<ULONG> &r, ULONG function,
                       std::initializer_list<ULONG> words, const void *payload, ULONG bytes) {
    assert(r[0] == DG_GL_DATA_CALL && r[8] == function && r[9] == bytes);
    assert(r[1] == (10 + words.size()) * 4 + ((bytes + 3) & ~3U));
    ULONG n = 10;
    for (ULONG word : words)
        assert(r[n++] == word);
    assert(!bytes || !payload || !memcmp(r.data() + n, payload, bytes));
}
static ULONG Bits(GLfloat value) {
    ULONG bits;
    memcpy(&bits, &value, 4);
    return bits;
}

/* Host answers: one level's size and depth/stencil bits, a parameter value,
 * and the level's depth (float32 i/8) or packed words (0x00123400 | i). */
static std::vector<std::array<ULONG, 4>> Queries;
static GLint LevelWidth, LevelHeight, LevelDepth, LevelStencil;
static BOOL Answer(const ULONG *r, DG_ESCAPE_REPLY *reply, ULONG capacity) {
    Queries.push_back({r[8], r[9], r[10], r[11]});
    auto put = [&](ULONG type, const void *data, ULONG bytes) {
        assert(bytes <= capacity);
        memcpy(reply + 1, data, bytes);
        reply->ResultBytes = bytes;
        reply->ResultType = type;
        return TRUE;
    };
    switch (r[8]) {
        case FEnum_glGetTexLevelParameteriv: {
            const GLint value = r[11] == GL_TEXTURE_WIDTH    ? LevelWidth
                                : r[11] == GL_TEXTURE_HEIGHT ? LevelHeight
                                : r[11] == DEPTH_SIZE        ? LevelDepth
                                : r[11] == STENCIL_SIZE      ? LevelStencil
                                                             : 0;
            return put(DG_GL_RESULT_INT, &value, 4);
        }
        case FEnum_glGetTexParameteriv: {
            const GLint value = GEQUAL;
            return put(DG_GL_RESULT_INT, &value, 4);
        }
        case FEnum_glGetTexParameterfv: {
            const GLfloat value = (GLfloat)INTENSITY;
            return put(DG_GL_RESULT_FLOAT, &value, 4);
        }
        case FEnum_glGetTexImage: {
            const ULONG word = r[10], first = r[11];
            if (word & DG_GL_TEXTURE_READ_SHAPE) {
                /* DG_CAP2_GL_BULK_READ: the level's native answers. */
                const BOOL depth_images = r[9] == TEX_1D || r[9] == TEX_2D || r[9] == TEX_RECT;
                const GLint shape[5] = {LevelWidth, LevelHeight, 1, depth_images ? LevelDepth : 0,
                                        depth_images && LevelDepth ? LevelStencil : 0};
                assert(capacity == 20 && !first && !(word & ~0xfffU & ~DG_GL_TEXTURE_READ_SHAPE));
                return put(DG_GL_RESULT_INT, shape, 20);
            }
            const ULONG count = word & DG_GL_TEXTURE_READ_UNITS
                                    ? (word >> 16) * DG_GL_TEXTURE_READ_UNIT
                                : word >> DG_GL_TEXTURE_READ_COUNT_SHIFT ? word >> 16
                                                                         : 128;
            assert(capacity == count * 4);
            std::vector<ULONG> out(count);
            if (word & DG_GL_TEXTURE_READ_DEPTH) {
                const BOOL packed = (word & DG_GL_TEXTURE_READ_FLOAT) != 0;
                for (ULONG i = 0; i < count; ++i)
                    out[i] = first + i >= (ULONG)(LevelWidth * LevelHeight) ? 0
                             : packed ? 0x00123400U | (first + i)
                                      : Bits((first + i) / 8.0f);
                return put(packed ? DG_GL_RESULT_INT : DG_GL_RESULT_FLOAT, out.data(), count * 4);
            }
            for (ULONG i = 0; i < count; ++i)
                out[i] = 0x7f | ((first + i) << 8) | 0xff000000U;
            return put(DG_GL_RESULT_INT, out.data(), count * 4);
        }
        default:
            return FALSE;
    }
}
static JGL_CONTEXT *Open(ULONG caps, ULONG caps2) {
    Reset();
    HostCaps = caps;
    HostCaps2 = caps2;
    SubmittedRecord = Capture;
    AnswerQuery = Answer;
    LevelWidth = 3;
    LevelHeight = 2;
    LevelDepth = 24;
    LevelStencil = 0;
    Queries.clear();
    JGL_CONTEXT *c = Lookup(Create());
    Clear(c);
    return c;
}
static void Close(JGL_CONTEXT *c) {
    assert(wglMakeCurrent(NULL, NULL) && wglDeleteContext((HGLRC)(ULONG_PTR)c->Id));
    Log.clear();
}

/* Without the second word's bit nothing new leaves the frontend: the names
 * keep the GL_INVALID_ENUM (now raised locally) and the depth images beyond
 * EXT_packed_depth_stencil's TEXTURE_2D the errors they had. An older driver
 * leaves DeviceError zero, an older host reads zero there: either is this. */
static void Unnegotiated(void) {
    const float depths[2] = {0.25f, 0.75f};
    /* A failed or malformed CAPABILITIES reply negotiates neither word. */
    for (int failure : {1, 2, 3, 4, 5, 6, 7}) {
        Reset();
        HostCaps = OTHERS | PROGRAMS;
        HostCaps2 = DEPTH_TEXTURE;
        CapabilityFailure = failure;
        JGL_CONTEXT *c = Lookup(Create());
        assert(!JglDepthTextureAvailable());
        assert(!strstr((const char *)glGetString(GL_EXTENSIONS), "shadow"));
        Close(c);
    }
    {
        JGL_CONTEXT *c = Open(OTHERS | PROGRAMS, 0);
        assert(!JglDepthTextureAvailable());
        const char *names = (const char *)glGetString(GL_EXTENSIONS);
        assert(!strstr(names, "depth_texture") && !strstr(names, "shadow"));
        glTexParameteri(TEX_2D, COMPARE_MODE, COMPARE_R);
        Rejected(c, GL_INVALID_ENUM);
        glTexParameterf(TEX_2D, DEPTH_MODE, (GLfloat)INTENSITY);
        Rejected(c, GL_INVALID_ENUM);
        const GLint function = LEQUAL;
        glTexParameteriv(TEX_2D, COMPARE_FUNC, &function);
        Rejected(c, GL_INVALID_ENUM);
        GLint value = 0x5a5a;
        glGetTexParameteriv(TEX_2D, COMPARE_MODE, &value);
        Rejected(c, GL_INVALID_ENUM);
        glGetTexLevelParameteriv(TEX_2D, 0, DEPTH_SIZE, &value);
        Rejected(c, GL_INVALID_ENUM);
        glGetTexLevelParameteriv(TEX_2D, 0, STENCIL_SIZE, &value);
        Rejected(c, GL_INVALID_ENUM);
        assert(Queries.empty() && value == 0x5a5a);
        /* Depth images beyond TEXTURE_2D, proxies and depth reads. */
        glTexImage1D(TEX_1D, 0, DEPTH24, 2, 0, DEPTH, GL_FLOAT, depths);
        Rejected(c, GL_INVALID_ENUM);
        glTexImage1D(TEX_1D, 0, DEPTH24, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        Rejected(c, GL_INVALID_VALUE);
        glTexImage2D(TEX_RECT, 0, DEPTH24, 2, 1, 0, DEPTH, GL_FLOAT, depths);
        Rejected(c, GL_INVALID_ENUM);
        glTexImage2D(PROXY_2D, 0, DEPTH24, 2, 1, 0, DEPTH, GL_FLOAT, NULL);
        Rejected(c, GL_INVALID_ENUM);
        glCopyTexImage2D(TEX_RECT, 0, DEPTH24, 0, 0, 2, 1, 0);
        Rejected(c, GL_INVALID_VALUE);
        glTexImage2D(CUBE_X, 0, DEPTH24, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        Rejected(c, GL_INVALID_VALUE);
        float out[6] = {};
        glGetTexImage(TEX_2D, 0, DEPTH, GL_FLOAT, out);
        Rejected(c, GL_INVALID_ENUM);
        assert(Queries.empty());
        /* EXT_packed_depth_stencil's TEXTURE_2D depth images are unchanged,
         * and a colour read asks only the level's size. */
        glTexImage2D(TEX_2D, 0, DEPTH24, 2, 1, 0, DEPTH, GL_FLOAT, depths);
        Records r = Sent(c);
        assert(r.size() == 1);
        ExpectData(r[0], FEnum_glTexImage2D, {TEX_2D, 0, DEPTH24, 2, 1, 0, DEPTH, GL_FLOAT}, depths,
                   8);
        ULONG rgba[6];
        glGetTexImage(TEX_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        assert(!c->Error && Queries.size() == 3 && Queries[2][0] == FEnum_glGetTexImage &&
               !(Queries[2][2] & DG_GL_TEXTURE_READ_DEPTH));
        Close(c);
    }
}

/* Negotiated: the names last, ARB_fragment_program_shadow only with programs. */
static void Negotiation(void) {
    JGL_CONTEXT *c = Open(0, DEPTH_TEXTURE);
    assert(JglDepthTextureAvailable());
    assert(!strcmp((const char *)glGetString(GL_EXTENSIONS), "GL_ARB_depth_texture GL_ARB_shadow"));
    Close(c);
    c = Open(PROGRAMS, DEPTH_TEXTURE);
    assert(!strcmp((const char *)glGetString(GL_EXTENSIONS),
                   "GL_ARB_fragment_program GL_ARB_depth_texture GL_ARB_shadow "
                   "GL_ARB_fragment_program_shadow"));
    Close(c);
    c = Open(OTHERS | PROGRAMS, DEPTH_TEXTURE | 0x80000000u);
    assert(!strcmp((const char *)glGetString(GL_EXTENSIONS),
                   ExpectedExtensions(HostCaps, FALSE, DEPTH_TEXTURE)));
    Close(c);
}

/* The compare and depth texture mode parameters on every target: values in
 * every form travel unchanged; others are local INVALID_ENUM. */
static void Parameters(void) {
    JGL_CONTEXT *c = Open(OTHERS, DEPTH_TEXTURE);
    for (GLenum target : {TEX_1D, TEX_2D, TEX_RECT, CUBE, TEX_3D}) {
        glTexParameteri(target, COMPARE_MODE, COMPARE_R);
        glTexParameteri(target, COMPARE_MODE, GL_NONE);
        glTexParameteri(target, COMPARE_FUNC, LEQUAL);
        glTexParameterf(target, COMPARE_FUNC, (GLfloat)GEQUAL);
        glTexParameteri(target, DEPTH_MODE, GL_LUMINANCE);
        glTexParameteri(target, DEPTH_MODE, INTENSITY);
        glTexParameterf(target, DEPTH_MODE, (GLfloat)GL_ALPHA + 0.75f); /* truncates */
        const GLint mode = GL_ALPHA;
        const GLfloat function = (GLfloat)LEQUAL;
        glTexParameteriv(target, DEPTH_MODE, &mode);
        glTexParameterfv(target, COMPARE_FUNC, &function);
        Records r = Sent(c);
        assert(r.size() == 9);
        ExpectCall(r[0], FEnum_glTexParameteri, {target, COMPARE_MODE, COMPARE_R});
        ExpectCall(r[1], FEnum_glTexParameteri, {target, COMPARE_MODE, 0});
        ExpectCall(r[2], FEnum_glTexParameteri, {target, COMPARE_FUNC, LEQUAL});
        ExpectCall(r[3], FEnum_glTexParameterf, {target, COMPARE_FUNC, Bits((GLfloat)GEQUAL)});
        ExpectCall(r[4], FEnum_glTexParameteri, {target, DEPTH_MODE, GL_LUMINANCE});
        ExpectCall(r[5], FEnum_glTexParameteri, {target, DEPTH_MODE, INTENSITY});
        ExpectCall(r[6], FEnum_glTexParameterf,
                   {target, DEPTH_MODE, Bits((GLfloat)GL_ALPHA + 0.75f)});
        ExpectData(r[7], FEnum_glTexParameteriv, {target, DEPTH_MODE}, &mode, 4);
        ExpectData(r[8], FEnum_glTexParameterfv, {target, COMPARE_FUNC}, &function, 4);
    }
    /* ARB_shadow without EXT_shadow_funcs: LEQUAL and GEQUAL only. Native GL
     * would take these, so the frontend reports them. */
    const std::pair<GLenum, GLint> invalid[] = {
        {COMPARE_MODE, LEQUAL}, {COMPARE_MODE, 1},
        {COMPARE_FUNC, LESS},   {COMPARE_FUNC, 0x0207},
        {COMPARE_FUNC, 0x0200}, {DEPTH_MODE, RED},
        {DEPTH_MODE, GL_RGBA},  {DEPTH_MODE, GL_LUMINANCE_ALPHA}};
    for (const auto &[pname, value] : invalid) {
        glTexParameteri(TEX_2D, pname, value);
        Rejected(c, GL_INVALID_ENUM);
        glTexParameterf(TEX_2D, pname, (GLfloat)value);
        Rejected(c, GL_INVALID_ENUM);
        glTexParameteriv(TEX_2D, pname, &value);
        Rejected(c, GL_INVALID_ENUM);
        const GLfloat f = (GLfloat)value;
        glTexParameterfv(TEX_2D, pname, &f);
        Rejected(c, GL_INVALID_ENUM);
    }
    /* Floats no integer holds are no enum; neither is a proxy a target. */
    for (ULONG bits : {0x7fc00000U, 0x7f800000U, 0x4f000000U, 0xcf000001U}) {
        GLfloat f;
        memcpy(&f, &bits, 4);
        glTexParameterf(TEX_2D, COMPARE_MODE, f);
        Rejected(c, GL_INVALID_ENUM);
    }
    glTexParameterf(TEX_2D, COMPARE_MODE, 0.5f); /* NONE */
    ExpectCall(Sent(c)[0], FEnum_glTexParameterf, {TEX_2D, COMPARE_MODE, Bits(0.5f)});
    glTexParameteri(PROXY_2D, COMPARE_MODE, GL_NONE);
    Rejected(c, GL_INVALID_ENUM);
    /* The host answers them, and the level's depth and stencil sizes. */
    GLint value = 0;
    GLfloat f = 0;
    glGetTexParameteriv(TEX_RECT, COMPARE_FUNC, &value);
    assert(value == (GLint)GEQUAL);
    glGetTexParameterfv(CUBE, DEPTH_MODE, &f);
    assert(f == (GLfloat)INTENSITY);
    glGetTexLevelParameteriv(TEX_1D, 2, DEPTH_SIZE, &value);
    assert(value == 24);
    glGetTexLevelParameteriv(PROXY_RECT, 0, STENCIL_SIZE, &value);
    assert(value == 0 && !c->Error);
    const std::vector<std::array<ULONG, 4>> expected = {
        {FEnum_glGetTexParameteriv, TEX_RECT, COMPARE_FUNC, 0},
        {FEnum_glGetTexParameterfv, CUBE, DEPTH_MODE, 0},
        {FEnum_glGetTexLevelParameteriv, TEX_1D, 2, DEPTH_SIZE},
        {FEnum_glGetTexLevelParameteriv, PROXY_RECT, 0, STENCIL_SIZE}};
    assert(Queries == expected);
    Log.clear();
    Close(c);
}

/* Depth images of 1D, 2D and rectangle targets, their proxies, subimages and
 * copies; cube maps and 3D textures have none. */
static void Images(void) {
    JGL_CONTEXT *c = Open(OTHERS, DEPTH_TEXTURE);
    const float depths[6] = {0.f, 0.125f, 0.5f, 0.75f, 0.875f, 1.f};
    const unsigned short shorts[3] = {0, 0x8000, 0xffff};
    const ULONG packed[6] = {0x00000001, 0x80000002, 0xffffff03, 4, 5, 6};
    glTexImage1D(TEX_1D, 0, DEPTH24, 6, 0, DEPTH, GL_FLOAT, depths);
    glTexImage1D(TEX_1D, 1, DEPTH16, 3, 0, DEPTH, GL_UNSIGNED_SHORT, NULL);
    glTexSubImage1D(TEX_1D, 1, 0, 3, DEPTH, GL_UNSIGNED_SHORT, shorts);
    glTexImage1D(TEX_1D, 0, DEPTH24_STENCIL8, 6, 0, DEPTH_STENCIL, UINT_24_8, packed);
    glTexImage1D(PROXY_1D, 0, DEPTH32, 2048, 0, DEPTH, GL_UNSIGNED_INT, NULL);
    glTexImage2D(TEX_RECT, 0, DEPTH24, 3, 2, 0, DEPTH, GL_FLOAT, depths);
    glTexImage2D(TEX_RECT, 0, DEPTH_STENCIL, 640, 480, 0, DEPTH_STENCIL, UINT_24_8, NULL);
    glTexSubImage2D(TEX_RECT, 0, 1, 1, 2, 1, DEPTH, GL_FLOAT, depths);
    glTexImage2D(PROXY_RECT, 0, DEPTH, 2048, 2048, 0, DEPTH, GL_FLOAT, NULL);
    glTexImage2D(PROXY_2D, 3, DEPTH24_STENCIL8, 4, 4, 0, DEPTH_STENCIL, UINT_24_8, NULL);
    glTexImage2D(TEX_2D, 0, DEPTH, 3, 2, 0, DEPTH, GL_FLOAT, depths);
    glCopyTexImage2D(TEX_RECT, 0, DEPTH24, 0, 0, 640, 480, 0);
    glCopyTexSubImage2D(TEX_RECT, 0, 1, 2, 3, 4, 5, 6);
    glCopyTexImage2D(TEX_2D, 0, DEPTH_STENCIL, 0, 0, 4, 4, 0);
    Records r = Sent(c);
    assert(r.size() == 14);
    ExpectData(r[0], FEnum_glTexImage1D, {TEX_1D, 0, DEPTH24, 6, 1, 0, DEPTH, GL_FLOAT}, depths,
               24);
    ExpectData(r[1], FEnum_glTexImage1D, {TEX_1D, 1, DEPTH16, 3, 1, 0, DEPTH, GL_UNSIGNED_SHORT},
               NULL, 0);
    ExpectData(r[2], FEnum_glTexSubImage1D, {TEX_1D, 1, 0, 0, 3, 1, DEPTH, GL_UNSIGNED_SHORT},
               shorts, 6);
    ExpectData(r[3], FEnum_glTexImage1D,
               {TEX_1D, 0, DEPTH24_STENCIL8, 6, 1, 0, DEPTH_STENCIL, UINT_24_8}, packed, 24);
    ExpectData(r[4], FEnum_glTexImage1D, {PROXY_1D, 0, DEPTH32, 2048, 1, 0, DEPTH, GL_UNSIGNED_INT},
               NULL, 0);
    ExpectData(r[5], FEnum_glTexImage2D, {TEX_RECT, 0, DEPTH24, 3, 2, 0, DEPTH, GL_FLOAT}, depths,
               24);
    ExpectData(r[6], FEnum_glTexImage2D,
               {TEX_RECT, 0, DEPTH_STENCIL, 640, 480, 0, DEPTH_STENCIL, UINT_24_8}, NULL, 0);
    ExpectData(r[7], FEnum_glTexSubImage2D, {TEX_RECT, 0, 1, 1, 2, 1, DEPTH, GL_FLOAT}, depths, 8);
    ExpectData(r[8], FEnum_glTexImage2D, {PROXY_RECT, 0, DEPTH, 2048, 2048, 0, DEPTH, GL_FLOAT},
               NULL, 0);
    ExpectData(r[9], FEnum_glTexImage2D,
               {PROXY_2D, 3, DEPTH24_STENCIL8, 4, 4, 0, DEPTH_STENCIL, UINT_24_8}, NULL, 0);
    ExpectData(r[10], FEnum_glTexImage2D, {TEX_2D, 0, DEPTH, 3, 2, 0, DEPTH, GL_FLOAT}, depths, 24);
    ExpectCall(r[11], FEnum_glCopyTexImage2D, {TEX_RECT, 0, DEPTH24, 0, 0, 640, 480, 0});
    ExpectCall(r[12], FEnum_glCopyTexSubImage2D, {TEX_RECT, 0, 1, 2, 3, 4, 5, 6});
    ExpectCall(r[13], FEnum_glCopyTexImage2D, {TEX_2D, 0, DEPTH_STENCIL, 0, 0, 4, 4, 0});
    /* Swapped packed words travel swapped as whole four-byte units. */
    glPixelStorei(GL_UNPACK_SWAP_BYTES, 1);
    glTexSubImage1D(TEX_1D, 0, 0, 2, DEPTH_STENCIL, UINT_24_8, packed);
    r = Sent(c);
    const ULONG swapped[2] = {0x01000000, 0x02000080};
    ExpectData(r[0], FEnum_glTexSubImage1D, {TEX_1D, 0, 0, 0, 2, 1, DEPTH_STENCIL, UINT_24_8},
               swapped, 8);
    glPixelStorei(GL_UNPACK_SWAP_BYTES, 0);

    /* Depth images are borderless and take depth pixels, and only they do. */
    glTexImage1D(TEX_1D, 0, DEPTH24, 4, 1, DEPTH, GL_FLOAT, NULL);
    Rejected(c, GL_INVALID_VALUE);
    glTexImage2D(TEX_2D, 0, DEPTH24, 4, 4, 1, DEPTH, GL_FLOAT, NULL);
    Rejected(c, GL_INVALID_VALUE);
    glTexImage2D(TEX_RECT, 0, DEPTH24, 4, 4, 1, DEPTH, GL_FLOAT, NULL);
    Rejected(c, GL_INVALID_VALUE);
    glTexImage1D(PROXY_1D, 0, DEPTH24, 4, 1, DEPTH, GL_FLOAT, NULL);
    Rejected(c, GL_INVALID_VALUE);
    glTexImage1D(TEX_1D, 0, DEPTH24, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    Rejected(c, GL_INVALID_OPERATION);
    glTexImage1D(TEX_1D, 0, RGBA8, 4, 0, DEPTH, GL_FLOAT, NULL);
    Rejected(c, GL_INVALID_OPERATION);
    glTexImage2D(TEX_RECT, 0, DEPTH24, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    Rejected(c, GL_INVALID_OPERATION);
    glTexImage2D(PROXY_RECT, 0, RGBA8, 4, 4, 0, DEPTH, GL_FLOAT, NULL);
    Rejected(c, GL_INVALID_OPERATION);
    glTexImage2D(PROXY_2D, 0, DEPTH16, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    Rejected(c, GL_INVALID_OPERATION);
    glTexImage1D(TEX_1D, 0, DEPTH24, 4, 0, DEPTH, UINT_24_8, NULL);
    Rejected(c, GL_INVALID_OPERATION);
    glTexImage1D(TEX_1D, 0, DEPTH24, 4, 0, DEPTH_STENCIL, GL_FLOAT, NULL);
    Rejected(c, GL_INVALID_ENUM);
    glCopyTexImage2D(TEX_RECT, 0, DEPTH24, 0, 0, 4, 4, 1);
    Rejected(c, GL_INVALID_VALUE);
    glCopyTexImage2D(TEX_2D, 0, DEPTH24, 0, 0, 4, 4, 1);
    Rejected(c, GL_INVALID_VALUE);
    /* Cube maps and 3D textures have no depth images (ARB_depth_texture). */
    glTexImage2D(CUBE_X, 0, DEPTH24, 4, 4, 0, DEPTH, GL_FLOAT, NULL);
    Rejected(c, GL_INVALID_OPERATION);
    glTexImage2D(CUBE_X, 0, RGBA8, 4, 4, 0, DEPTH, GL_FLOAT, NULL);
    Rejected(c, GL_INVALID_OPERATION);
    glTexImage2D(PROXY_CUBE, 0, DEPTH24, 4, 4, 0, DEPTH, GL_FLOAT, NULL);
    Rejected(c, GL_INVALID_OPERATION);
    glTexSubImage2D(CUBE_X, 0, 0, 0, 1, 1, DEPTH, GL_FLOAT, depths);
    Rejected(c, GL_INVALID_OPERATION);
    glCopyTexImage2D(CUBE_X, 0, DEPTH24, 0, 0, 4, 4, 0);
    Rejected(c, GL_INVALID_OPERATION);
    glTexImage3DEXT(TEX_3D, 0, DEPTH24, 2, 2, 2, 0, DEPTH, GL_FLOAT, NULL);
    Rejected(c, GL_INVALID_OPERATION);
    glTexSubImage3DEXT(TEX_3D, 0, 0, 0, 0, 1, 1, 1, DEPTH, GL_FLOAT, depths);
    Rejected(c, GL_INVALID_OPERATION);
    Close(c);
}

/* Depth readback: the level's size, its depth (and for DEPTH_STENCIL its
 * stencil) bits, then float depth or packed words in the caller's type. */
static void Readback(void) {
    JGL_CONTEXT *c = Open(OTHERS, DEPTH_TEXTURE);
    float floats[6];
    glGetTexImage(TEX_2D, 0, DEPTH, GL_FLOAT, floats);
    assert(!c->Error);
    for (ULONG i = 0; i < 6; ++i)
        assert(floats[i] == i / 8.0f);
    assert(Queries.size() == 4 && Queries[2][0] == FEnum_glGetTexLevelParameteriv &&
           Queries[2][3] == DEPTH_SIZE && Queries[3][0] == FEnum_glGetTexImage &&
           Queries[3][1] == TEX_2D && (Queries[3][2] & 0xffff) == DG_GL_TEXTURE_READ_DEPTH);
    Log.clear();
    /* Every scalar type, with the GL conversion of depth in [0, 1]. */
    unsigned short shorts[6];
    BYTE bytes[6];
    ULONG ints[6];
    signed char signed_bytes[6];
    LevelWidth = 6;
    LevelHeight = 1;
    Queries.clear();
    glGetTexImage(TEX_1D, 2, DEPTH, GL_UNSIGNED_SHORT, shorts);
    glGetTexImage(TEX_RECT, 0, DEPTH, GL_UNSIGNED_BYTE, bytes);
    glGetTexImage(TEX_1D, 0, DEPTH, GL_UNSIGNED_INT, ints);
    glGetTexImage(TEX_2D, 0, DEPTH, GL_BYTE, signed_bytes);
    assert(!c->Error);
    for (ULONG i = 0; i < 6; ++i) {
        assert(shorts[i] == (unsigned short)(i / 8.0 * 65535 + 0.5));
        assert(bytes[i] == (BYTE)(i / 8.0 * 255 + 0.5));
        assert(ints[i] == (ULONG)(i / 8.0 * 4294967295.0 + 0.5));
        assert(signed_bytes[i] == (signed char)(i / 8.0 * 127.5));
    }
    /* 1D: width and depth bits; others: width, height and depth bits. */
    assert(Queries.size() == 14 && Queries[2][0] == FEnum_glGetTexImage &&
           Queries[2][2] == (2 | DG_GL_TEXTURE_READ_DEPTH | (6U << 16)));
    Log.clear();
    /* Packed words of a DEPTH_STENCIL level, swapped when asked. */
    Queries.clear();
    LevelStencil = 8;
    ULONG words[6];
    glGetTexImage(TEX_RECT, 0, DEPTH_STENCIL, UINT_24_8, words);
    assert(!c->Error && Queries.size() == 5 && Queries[3][3] == STENCIL_SIZE &&
           Queries[4][2] == (DG_GL_TEXTURE_READ_DEPTH | DG_GL_TEXTURE_READ_FLOAT | (6U << 16)));
    for (ULONG i = 0; i < 6; ++i)
        assert(words[i] == (0x00123400U | i));
    glPixelStorei(GL_PACK_SWAP_BYTES, 1);
    glGetTexImage(TEX_1D, 0, DEPTH_STENCIL, UINT_24_8, words);
    glPixelStorei(GL_PACK_SWAP_BYTES, 0);
    for (ULONG i = 0; i < 6; ++i)
        assert(words[i] == __builtin_bswap32(0x00123400U | i));
    Log.clear();

    /* A read of the other kind of level is INVALID_OPERATION, decided from
     * its sizes before any pixel is read. */
    memset(floats, 0xa5, sizeof(floats));
    Queries.clear();
    LevelDepth = 0;
    glGetTexImage(TEX_2D, 0, DEPTH, GL_FLOAT, floats);
    assert(c->Error == GL_INVALID_OPERATION && Queries.size() == 3);
    c->Error = 0;
    glGetTexImage(TEX_2D, 0, DEPTH_STENCIL, UINT_24_8, words);
    assert(c->Error == GL_INVALID_OPERATION && Queries.size() == 6);
    c->Error = 0;
    LevelDepth = 24;
    LevelStencil = 0;
    glGetTexImage(TEX_2D, 0, DEPTH_STENCIL, UINT_24_8, words);
    assert(c->Error == GL_INVALID_OPERATION && Queries.size() == 10);
    c->Error = 0;
    glGetTexImage(TEX_RECT, 0, GL_RGBA, GL_UNSIGNED_BYTE, words);
    assert(c->Error == GL_INVALID_OPERATION && Queries.size() == 13);
    c->Error = 0;
    for (ULONG i = 0; i < 6; ++i)
        assert(Bits(floats[i]) == 0xa5a5a5a5U);
    for (const auto &q : Queries)
        assert(q[0] == FEnum_glGetTexLevelParameteriv);
    Log.clear();
    /* Types, and targets without depth images, are local errors. */
    Queries.clear();
    glGetTexImage(TEX_2D, 0, DEPTH, UINT_24_8, words);
    Rejected(c, GL_INVALID_OPERATION);
    glGetTexImage(TEX_2D, 0, DEPTH_STENCIL, GL_UNSIGNED_INT, words);
    Rejected(c, GL_INVALID_ENUM);
    glGetTexImage(TEX_2D, 0, DEPTH, 0x8033 /* UNSIGNED_SHORT_4_4_4_4 */, words);
    Rejected(c, GL_INVALID_OPERATION);
    glGetTexImage(TEX_2D, 0, DEPTH, 0x1a00 /* BITMAP */, words);
    Rejected(c, GL_INVALID_ENUM);
    glGetTexImage(CUBE_X, 0, DEPTH, GL_FLOAT, floats);
    Rejected(c, GL_INVALID_OPERATION);
    glGetTexImage(TEX_3D, 0, DEPTH, GL_FLOAT, floats);
    Rejected(c, GL_INVALID_OPERATION);
    assert(Queries.empty());
    /* Colour reads of cube faces and 3D levels never ask for depth bits. */
    LevelDepth = 0;
    glGetTexImage(CUBE_X, 0, GL_RGBA, GL_UNSIGNED_BYTE, words);
    assert(!c->Error && Queries.size() == 3 && Queries[2][0] == FEnum_glGetTexImage);
    Log.clear();
    Close(c);
}

/* DG_CAP2_GL_BULK_READ: one SHAPE query decides the depth checks exactly as
 * the level-parameter queries did, then one unit-counted read. */
static void BulkReadback(void) {
    JGL_CONTEXT *c = Open(OTHERS, DEPTH_TEXTURE | DG_CAP2_GL_BULK_READ);
    float floats[7];
    ULONG words[7];
    memset(floats, 0xa5, sizeof(floats));
    glGetTexImage(TEX_2D, 0, DEPTH, GL_FLOAT, floats);
    assert(!c->Error && Queries.size() == 2);
    for (ULONG i = 0; i < 6; ++i)
        assert(floats[i] == i / 8.0f);
    assert(Bits(floats[6]) == 0xa5a5a5a5U);
    assert(Queries[0][0] == FEnum_glGetTexImage && Queries[0][2] == DG_GL_TEXTURE_READ_SHAPE);
    assert(Queries[1][2] == (DG_GL_TEXTURE_READ_DEPTH | DG_GL_TEXTURE_READ_UNITS | (1U << 16)));
    LevelStencil = 8;
    memset(words, 0xa5, sizeof(words));
    Queries.clear();
    glGetTexImage(TEX_RECT, 0, DEPTH_STENCIL, UINT_24_8, words);
    assert(!c->Error && Queries.size() == 2 && words[6] == 0xa5a5a5a5U);
    for (ULONG i = 0; i < 6; ++i)
        assert(words[i] == (0x00123400U | i));
    /* The other kind of level: INVALID_OPERATION after the one SHAPE query. */
    LevelDepth = 0;
    Queries.clear();
    glGetTexImage(TEX_2D, 0, DEPTH, GL_FLOAT, floats);
    assert(c->Error == GL_INVALID_OPERATION && Queries.size() == 1);
    c->Error = 0;
    LevelDepth = 24;
    LevelStencil = 0;
    glGetTexImage(TEX_2D, 0, DEPTH_STENCIL, UINT_24_8, words);
    assert(c->Error == GL_INVALID_OPERATION && Queries.size() == 2);
    c->Error = 0;
    glGetTexImage(TEX_1D, 0, GL_RGBA, GL_UNSIGNED_BYTE, words);
    assert(c->Error == GL_INVALID_OPERATION && Queries.size() == 3);
    c->Error = 0;
    /* Colour reads of cube faces never see depth bits. */
    glGetTexImage(CUBE_X, 0, GL_RGBA, GL_UNSIGNED_BYTE, words);
    assert(!c->Error && Queries.size() == 5);
    Log.clear();
    Close(c);
}

/* Display lists capture the parameters; invalid values are deferred errors. */
static void Lists(void) {
    JGL_CONTEXT *c = Open(OTHERS, DEPTH_TEXTURE);
    assert(JglListMode(GL_COMPILE));
    Clear(c);
    glTexParameteri(TEX_2D, COMPARE_MODE, COMPARE_R);
    glTexParameteri(TEX_2D, COMPARE_FUNC, LESS);
    glTexParameterf(TEX_RECT, DEPTH_MODE, (GLfloat)GL_ALPHA);
    glCopyTexImage2D(CUBE_X, 0, DEPTH24, 0, 0, 4, 4, 0);
    assert(!c->Error);
    Submit(c);
    assert(Log.size() == 4);
    ExpectCall(Log[0], FEnum_glTexParameteri, {TEX_2D, COMPARE_MODE, COMPARE_R});
    ExpectCall(Log[1], DG_GL_RECORD_ERROR, {GL_INVALID_ENUM});
    ExpectCall(Log[2], FEnum_glTexParameterf, {TEX_RECT, DEPTH_MODE, Bits((GLfloat)GL_ALPHA)});
    ExpectCall(Log[3], DG_GL_RECORD_ERROR, {GL_INVALID_OPERATION});
    assert(JglListMode(0));
    Close(c);
}

int main() {
    Unnegotiated();
    Negotiation();
    Parameters();
    Images();
    Readback();
    BulkReadback();
    Lists();
    puts("PASS depth texture frontend");
    return 0;
}
