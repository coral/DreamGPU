/* SPDX-License-Identifier: GPL-2.0-or-later
 * DG_CAP_GL_TEXTURE_RECTANGLE (ARB_texture_rectangle, NV_texture_rectangle)
 * through the actual frontend on the strict transport fixture: negotiation and
 * the unchanged frontend without it, local validation (nothing the host would
 * reject as malformed may leave the frontend: every record is checked against
 * the host's rectangle shape rules and the NT validator), exact binding,
 * parameter, image, subimage, copy, readback, query and attachment records,
 * bounded tiles of a large rectangle and the rectangle restrictions (level
 * zero, no border, no compressed or depth format, no generated mipmaps). */
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
#include "dg-gl-validate.h"

constexpr GLenum TEX_RECT = 0x84f5, BINDING_RECT = 0x84f6, PROXY_RECT = 0x84f7, MAX_RECT = 0x84f8;
constexpr GLenum CUBE_X = 0x8515, FRAMEBUFFER = 0x8d40, COLOR0 = 0x8ce0;
constexpr GLenum MIN_FILTER = 0x2801, WRAP_S = 0x2802, LINEAR = 0x2601,
                 LINEAR_MIPMAP_LINEAR = 0x2703, CLAMP_TO_EDGE = 0x812f, REPEAT = 0x2901,
                 GENERATE_MIPMAP = 0x8191, TEXTURE_BORDER = 0x1005;
constexpr GLenum RGBA8 = 0x8058, RGB5 = 0x8050, DXT1 = 0x83f0, DXT5 = 0x83f3,
                 COMPRESSED_RGBA = 0x84ee, DEPTH24 = 0x81a6;
constexpr ULONG SMALL_PACKET = 512, HOST_ANSWER = 77;
constexpr ULONG RECTANGLE = DG_CAP_GL_TEXTURE_RECTANGLE;
constexpr ULONG OTHERS =
    DG_CAP_GL_MULTITEXTURE | DG_CAP_GL_FIXED_STATE | DG_CAP_GL_TEXTURE_CONTROL |
    DG_CAP_GL_COMPRESSED_TEXTURES | DG_CAP_GL_FOG_COORD | DG_CAP_GL_TEXTURE_CUBE_MAP |
    DG_CAP_GL_FRAGMENT_PROGRAM | DG_CAP_GL_VERTEX_PROGRAM | DG_CAP_GL_TEXTURE_3D |
    DG_CAP_GL_VERTEX_BUFFER_OBJECT | DG_CAP_GL_FRAMEBUFFER_OBJECT | DG_CAP_GL_VERTEX_ARRAY_BGRA |
    DG_CAP_GL_OCCLUSION_QUERY | DG_CAP_GL_TEXTURE_IMAGES | DG_CAP_GL_TEXTURE_BORDERS |
    DG_CAP_GL_BULK_READBACK;
#define RECTANGLE_NAMES "GL_ARB_texture_rectangle GL_NV_texture_rectangle"

/* The host's rectangle rules (gl.h, gl_validation.rs): a rectangle record
 * outside them is a batch fault that fails the context. */
static BOOL HostInternalFormat(ULONG f) {
    return (f >= 1 && f <= 4) || (f >= 0x1906 && f <= 0x190a) || f == 0x8049 || f == 0x2a10 ||
           (f >= 0x803b && f <= 0x8048) || (f >= 0x804a && f <= 0x804d) ||
           (f >= 0x804f && f <= 0x805b);
}
static void HostShape(const std::vector<ULONG> &r) {
    if (r[0] == DG_GL_DATA_CALL) {
        const ULONG *a = r.data() + 10;
        const BOOL rectangle = a[0] == TEX_RECT || a[0] == PROXY_RECT;
        switch (r[8]) {
            case FEnum_glTexImage2D:
                if (rectangle)
                    assert(a[1] == 0 && a[5] == 0 && HostInternalFormat(a[2]) &&
                           a[3] <= DG_GL_MAX_TEXTURE_DIMENSION &&
                           a[4] <= DG_GL_MAX_TEXTURE_DIMENSION && (a[0] == TEX_RECT || !r[9]));
                break;
            case FEnum_glTexSubImage2D:
                if (rectangle)
                    assert(a[0] == TEX_RECT && a[1] == 0 && a[4] && a[5] &&
                           a[4] <= DG_GL_MAX_TEXTURE_DIMENSION + 2 &&
                           a[5] <= DG_GL_MAX_TEXTURE_DIMENSION + 2);
                break;
            case FEnum_glCompressedTexImage2D:
            case FEnum_glCompressedTexSubImage2D:
                assert(!rectangle);
                break;
            default:
                break;
        }
        return;
    }
    if (r[0] == DG_GL_QUERY) {
        const ULONG *a = r.data() + 9;
        if (r[8] == FEnum_glGetTexImage && a[0] == TEX_RECT)
            assert(!(a[1] & DG_GL_TEXTURE_READ_LEVEL_MASK));
        if (r[8] == FEnum_glGetTexLevelParameteriv && (a[0] == TEX_RECT || a[0] == PROXY_RECT))
            assert(a[1] == 0);
        if (r[8] == FEnum_glGetCompressedTexImage)
            assert(a[0] != TEX_RECT);
        if (r[8] == FEnum_glGetTexParameteriv && a[0] == TEX_RECT)
            assert(a[1] != GENERATE_MIPMAP);
        return;
    }
    if (r[0] != DG_GL_CALL)
        return;
    const ULONG *a = r.data() + 9;
    switch (r[8]) {
        case FEnum_glCopyTexImage2D:
            if (a[0] == TEX_RECT)
                assert(a[1] == 0 && a[7] == 0 && a[2] > 4 && HostInternalFormat(a[2]) &&
                       a[5] <= DG_GL_MAX_TEXTURE_DIMENSION && a[6] <= DG_GL_MAX_TEXTURE_DIMENSION);
            assert(a[0] != PROXY_RECT);
            break;
        case FEnum_glCopyTexSubImage2D:
            if (a[0] == TEX_RECT)
                assert(a[1] == 0);
            break;
        case FEnum_glTexParameteri:
        case FEnum_glTexParameterf:
            if (a[0] == TEX_RECT)
                assert(a[1] != GENERATE_MIPMAP);
            assert(a[0] != PROXY_RECT);
            break;
        case FEnum_glBindTexture:
            assert(a[0] != PROXY_RECT);
            break;
        case FEnum_glFramebufferTexture2D:
            assert(a[2] == GL_TEXTURE_2D || a[2] == TEX_RECT || a[2] - CUBE_X < 6);
            assert(a[4] <= DG_GL_MAX_TEXTURE_LEVEL);
            break;
        case FEnum_glGenerateMipmap:
            assert(a[0] != TEX_RECT);
            break;
        default:
            break;
    }
}
/* Argument words of the records this suite submits, as the host reports them
 * to the NT kernel; others are not checked here. */
static ULONG KernelWords(void *, ULONG function) {
    switch (function) {
        case FEnum_glBindTexture:
            return 2;
        case FEnum_glTexParameteri:
        case FEnum_glTexParameterf:
            return 3;
        case FEnum_glEnable:
        case FEnum_glDisable:
        case DG_GL_RECORD_ERROR:
            return 1;
        case FEnum_glFramebufferTexture2D:
            return 5;
        case FEnum_glCopyTexImage2D:
        case FEnum_glCopyTexSubImage2D:
            return 8;
        case FEnum_glTexImage2D:
        case FEnum_glTexSubImage2D:
            return DG_GL_FUNCTION_INLINE_DATA | 8;
        case FEnum_glGetIntegerv:
        case FEnum_glIsEnabled:
            return DG_GL_FUNCTION_QUERY | 1;
        case FEnum_glGetTexParameteriv:
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

/* Host answers: one level's size, parameters, limits and the level's pixels
 * (canonical RGBA8 words numbering each pixel). */
static std::vector<std::array<ULONG, 4>> Queries;
static GLint LevelWidth = 3, LevelHeight = 5;
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
                                                             : 0;
            return put(DG_GL_RESULT_INT, &value, 4);
        }
        case FEnum_glGetTexParameteriv: {
            const GLint value = CLAMP_TO_EDGE;
            return put(DG_GL_RESULT_INT, &value, 4);
        }
        case FEnum_glGetIntegerv:
            return put(DG_GL_RESULT_INT, &HOST_ANSWER, 4);
        case FEnum_glIsEnabled: {
            const BYTE value = 1;
            return put(DG_GL_RESULT_BOOL, &value, 1);
        }
        case FEnum_glGetTexImage: {
            const ULONG count = r[10] >> DG_GL_TEXTURE_READ_COUNT_SHIFT, first = r[11];
            assert(r[9] == TEX_RECT && count && capacity == count * 4);
            std::vector<ULONG> out(count);
            for (ULONG i = 0; i < count; ++i)
                out[i] = 0x7f | ((first + i) << 8) | 0xff000000U;
            return put(DG_GL_RESULT_INT, out.data(), count * 4);
        }
        default:
            return FALSE;
    }
}
static JGL_CONTEXT *Open(ULONG caps, ULONG packet = DG_ESCAPE_MAX_BYTES) {
    Reset();
    HostCaps = caps;
    HostMaxBytes = packet;
    SubmittedRecord = Capture;
    AnswerQuery = Answer;
    LevelWidth = 3;
    LevelHeight = 5;
    Queries.clear();
    JGL_CONTEXT *c = Lookup(Create());
    Clear(c);
    return c;
}
static void Close(JGL_CONTEXT *c) {
    assert(wglMakeCurrent(NULL, NULL) && wglDeleteContext((HGLRC)(ULONG_PTR)c->Id));
    Log.clear();
}

/* Without the capability nothing new leaves the frontend, and every rectangle
 * enum keeps the GL_INVALID_ENUM it had before (now raised locally). */
static void Unnegotiated(void) {
    JGL_CONTEXT *c = Open(OTHERS);
    BYTE image[64];
    memset(image, 0xa5, sizeof(image));
    assert(!JglTextureRectangleAvailable());
    assert(!strstr((const char *)glGetString(GL_EXTENSIONS), "texture_rectangle"));
    glBindTexture(TEX_RECT, 1);
    Rejected(c, GL_INVALID_ENUM);
    glTexImage2D(TEX_RECT, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
    Rejected(c, GL_INVALID_ENUM);
    glTexImage2D(PROXY_RECT, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    Rejected(c, GL_INVALID_ENUM);
    glTexSubImage2D(TEX_RECT, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, image);
    Rejected(c, GL_INVALID_ENUM);
    glCopyTexImage2D(TEX_RECT, 0, GL_RGBA, 0, 0, 1, 1, 0);
    Rejected(c, GL_INVALID_ENUM);
    glCopyTexSubImage2D(TEX_RECT, 0, 0, 0, 0, 0, 1, 1);
    Rejected(c, GL_INVALID_ENUM);
    glTexParameteri(TEX_RECT, MIN_FILTER, LINEAR);
    Rejected(c, GL_INVALID_ENUM);
    glFramebufferTexture2DEXT(FRAMEBUFFER, COLOR0, TEX_RECT, 1, 0);
    Rejected(c, GL_INVALID_ENUM);
    /* Queries stay local errors: no host query is issued. */
    GLint value = 0x5a5a;
    GLboolean flag = 0x5a;
    glGetTexImage(TEX_RECT, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
    Rejected(c, GL_INVALID_ENUM);
    for (GLenum name : {TEX_RECT, BINDING_RECT, PROXY_RECT, MAX_RECT}) {
        glGetIntegerv(name, &value);
        Rejected(c, GL_INVALID_ENUM);
    }
    glGetBooleanv(BINDING_RECT, &flag);
    Rejected(c, GL_INVALID_ENUM);
    assert(!glIsEnabled(TEX_RECT));
    Rejected(c, GL_INVALID_ENUM);
    glGetTexParameteriv(TEX_RECT, MIN_FILTER, &value);
    Rejected(c, GL_INVALID_ENUM);
    glGetTexLevelParameteriv(TEX_RECT, 0, GL_TEXTURE_WIDTH, &value);
    Rejected(c, GL_INVALID_ENUM);
    glGetTexLevelParameteriv(PROXY_RECT, 0, GL_TEXTURE_WIDTH, &value);
    Rejected(c, GL_INVALID_ENUM);
    assert(Queries.empty() && value == 0x5a5a && flag == 0x5a);
    for (BYTE byte : image)
        assert(byte == 0xa5);
    /* Enable keeps passing through, as before. */
    glEnable(TEX_RECT);
    const Records r = Sent(c);
    assert(r.size() == 1);
    ExpectCall(r[0], FEnum_glEnable, {TEX_RECT});
    Close(c);
}

/* Negotiated: the extension group is appended last, alone or after all others. */
static void Negotiation(void) {
    JGL_CONTEXT *c = Open(RECTANGLE);
    assert(JglTextureRectangleAvailable());
    assert(!strcmp((const char *)glGetString(GL_EXTENSIONS), RECTANGLE_NAMES));
    Close(c);
    c = Open(OTHERS | RECTANGLE);
    const char *names = (const char *)glGetString(GL_EXTENSIONS);
    const char *tail = "GL_ARB_occlusion_query GL_NV_fence " RECTANGLE_NAMES;
    assert(strlen(names) > strlen(tail) && !strcmp(names + strlen(names) - strlen(tail), tail) &&
           !strncmp(names, "GL_ARB_multitexture ", 20));
    Close(c);
}

/* Binding, enables, parameters and host-answered queries. */
static void State(void) {
    JGL_CONTEXT *c = Open(OTHERS | RECTANGLE);
    glBindTexture(TEX_RECT, 5);
    glBindTexture(TEX_RECT, 0);
    glTexParameteri(TEX_RECT, MIN_FILTER, LINEAR);
    glTexParameteri(TEX_RECT, WRAP_S, CLAMP_TO_EDGE);
    /* Values are native GL's to judge: these are its INVALID_ENUM. */
    glTexParameteri(TEX_RECT, MIN_FILTER, LINEAR_MIPMAP_LINEAR);
    glTexParameteri(TEX_RECT, WRAP_S, REPEAT);
    glEnable(TEX_RECT);
    glDisable(TEX_RECT);
    Records r = Sent(c);
    assert(r.size() == 8);
    ExpectCall(r[0], FEnum_glBindTexture, {TEX_RECT, 5});
    ExpectCall(r[1], FEnum_glBindTexture, {TEX_RECT, 0});
    ExpectCall(r[2], FEnum_glTexParameteri, {TEX_RECT, MIN_FILTER, LINEAR});
    ExpectCall(r[3], FEnum_glTexParameteri, {TEX_RECT, WRAP_S, CLAMP_TO_EDGE});
    ExpectCall(r[4], FEnum_glTexParameteri, {TEX_RECT, MIN_FILTER, LINEAR_MIPMAP_LINEAR});
    ExpectCall(r[5], FEnum_glTexParameteri, {TEX_RECT, WRAP_S, REPEAT});
    ExpectCall(r[6], FEnum_glEnable, {TEX_RECT});
    ExpectCall(r[7], FEnum_glDisable, {TEX_RECT});
    /* A rectangle has one level: GENERATE_MIPMAP is no rectangle parameter,
     * though it stays one of 2D textures. */
    glTexParameteri(TEX_RECT, GENERATE_MIPMAP, 1);
    Rejected(c, GL_INVALID_ENUM);
    const GLint on = 1;
    glTexParameteriv(TEX_RECT, GENERATE_MIPMAP, &on);
    Rejected(c, GL_INVALID_ENUM);
    glTexParameteri(GL_TEXTURE_2D, GENERATE_MIPMAP, 1);
    r = Sent(c);
    assert(r.size() == 1);
    ExpectCall(r[0], FEnum_glTexParameteri, {GL_TEXTURE_2D, GENERATE_MIPMAP, 1});
    /* A name keeps the target of its first bind; the proxy is no binding or
     * parameter target. */
    glBindTexture(GL_TEXTURE_2D, 5);
    Rejected(c, GL_INVALID_OPERATION);
    glBindTexture(PROXY_RECT, 6);
    Rejected(c, GL_INVALID_ENUM);
    glTexParameteri(PROXY_RECT, MIN_FILTER, LINEAR);
    Rejected(c, GL_INVALID_ENUM);
    /* The host answers the binding, the limit, the enable and parameters. */
    GLint value = 0;
    glGetIntegerv(BINDING_RECT, &value);
    assert(value == (GLint)HOST_ANSWER);
    glGetIntegerv(MAX_RECT, &value);
    assert(value == (GLint)HOST_ANSWER);
    assert(glIsEnabled(TEX_RECT));
    glGetTexParameteriv(TEX_RECT, WRAP_S, &value);
    assert(value == (GLint)CLAMP_TO_EDGE && !c->Error);
    const std::vector<std::array<ULONG, 4>> expected = {
        {FEnum_glGetIntegerv, BINDING_RECT, 0, 0},
        {FEnum_glGetIntegerv, MAX_RECT, 0, 0},
        {FEnum_glIsEnabled, TEX_RECT, 0, 0},
        {FEnum_glGetTexParameteriv, TEX_RECT, WRAP_S, 0}};
    assert(Queries == expected);
    Log.clear();
    Queries.clear();
    glGetTexParameteriv(TEX_RECT, GENERATE_MIPMAP, &value);
    Rejected(c, GL_INVALID_ENUM);
    assert(Queries.empty());
    Close(c);
}

/* Images: level zero, borderless, uncompressed, non-depth, any size up to the
 * 2D level-zero limit; subimages, proxies and copies. */
static void Images(void) {
    JGL_CONTEXT *c = Open(OTHERS | RECTANGLE);
    BYTE pixels[3 * 5 * 4];
    for (ULONG i = 0; i < sizeof(pixels); ++i)
        pixels[i] = (BYTE)(i * 7 + 1);
    glTexImage2D(TEX_RECT, 0, RGBA8, 3, 5, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexImage2D(TEX_RECT, 0, RGB5, 640, 480, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexImage2D(TEX_RECT, 0, GL_RGBA, DG_GL_MAX_TEXTURE_DIMENSION, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 NULL);
    glTexSubImage2D(TEX_RECT, 0, 1, 2, 2, 3, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexImage2D(PROXY_RECT, 0, RGBA8, DG_GL_MAX_TEXTURE_DIMENSION, DG_GL_MAX_TEXTURE_DIMENSION, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glCopyTexImage2D(TEX_RECT, 0, RGBA8, 0, 0, 640, 480, 0);
    glCopyTexSubImage2D(TEX_RECT, 0, 1, 2, 3, 4, 5, 6);
    Records r = Sent(c);
    assert(r.size() == 7);
    ExpectData(r[0], FEnum_glTexImage2D, {TEX_RECT, 0, RGBA8, 3, 5, 0, GL_RGBA, GL_UNSIGNED_BYTE},
               pixels, sizeof(pixels));
    ExpectData(r[1], FEnum_glTexImage2D,
               {TEX_RECT, 0, RGB5, 640, 480, 0, GL_RGBA, GL_UNSIGNED_BYTE}, NULL, 0);
    ExpectData(r[2], FEnum_glTexImage2D,
               {TEX_RECT, 0, GL_RGBA, DG_GL_MAX_TEXTURE_DIMENSION, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE},
               NULL, 0);
    ExpectData(r[3], FEnum_glTexSubImage2D, {TEX_RECT, 0, 1, 2, 2, 3, GL_RGBA, GL_UNSIGNED_BYTE},
               NULL, 24);
    for (ULONG row = 0; row < 3; ++row)
        assert(!memcmp((const BYTE *)(r[3].data() + 18) + row * 8, pixels + row * 8, 8));
    ExpectData(r[4], FEnum_glTexImage2D,
               {PROXY_RECT, 0, RGBA8, DG_GL_MAX_TEXTURE_DIMENSION, DG_GL_MAX_TEXTURE_DIMENSION, 0,
                GL_RGBA, GL_UNSIGNED_BYTE},
               NULL, 0);
    ExpectCall(r[5], FEnum_glCopyTexImage2D, {TEX_RECT, 0, RGBA8, 0, 0, 640, 480, 0});
    ExpectCall(r[6], FEnum_glCopyTexSubImage2D, {TEX_RECT, 0, 1, 2, 3, 4, 5, 6});

    /* Level zero only, no border, at most the 2D level-zero size. */
    glTexImage2D(TEX_RECT, 1, RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    Rejected(c, GL_INVALID_VALUE);
    glTexImage2D(TEX_RECT, 0, RGBA8, 3, 3, 1, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    Rejected(c, GL_INVALID_VALUE);
    glTexImage2D(TEX_RECT, 0, RGBA8, DG_GL_MAX_TEXTURE_DIMENSION + 1, 1, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, NULL);
    Rejected(c, GL_INVALID_VALUE);
    glTexSubImage2D(TEX_RECT, 1, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    Rejected(c, GL_INVALID_VALUE);
    /* No compressed internal format (INVALID_ENUM), no depth texture. */
    for (GLint format : {(GLint)DXT1, (GLint)DXT5, (GLint)COMPRESSED_RGBA}) {
        glTexImage2D(TEX_RECT, 0, format, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        Rejected(c, GL_INVALID_ENUM);
        glTexImage2D(PROXY_RECT, 0, format, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        Rejected(c, GL_INVALID_ENUM);
    }
    glTexImage2D(TEX_RECT, 0, DEPTH24, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    Rejected(c, GL_INVALID_VALUE);
    glTexImage2D(TEX_RECT, 0, DEPTH24, 4, 4, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
    Rejected(c, GL_INVALID_ENUM);
    glCompressedTexImage2DARB(TEX_RECT, 0, DXT1, 4, 4, 0, 8, pixels);
    Rejected(c, GL_INVALID_ENUM);
    glCompressedTexImage2DARB(PROXY_RECT, 0, DXT1, 4, 4, 0, 8, NULL);
    Rejected(c, GL_INVALID_ENUM);
    glCompressedTexSubImage2DARB(TEX_RECT, 0, 0, 0, 4, 4, DXT1, 8, pixels);
    Rejected(c, GL_INVALID_ENUM);
    /* The proxy keeps the same rules. */
    glTexImage2D(PROXY_RECT, 1, RGBA8, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    Rejected(c, GL_INVALID_VALUE);
    glTexImage2D(PROXY_RECT, 0, RGBA8, 6, 6, 1, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    Rejected(c, GL_INVALID_VALUE);
    /* Copies: level zero, borderless, a colour format, at most the limit. */
    glCopyTexImage2D(TEX_RECT, 1, RGBA8, 0, 0, 4, 4, 0);
    Rejected(c, GL_INVALID_VALUE);
    glCopyTexImage2D(TEX_RECT, 0, RGBA8, 0, 0, 6, 6, 1);
    Rejected(c, GL_INVALID_VALUE);
    glCopyTexImage2D(TEX_RECT, 0, DEPTH24, 0, 0, 4, 4, 0);
    Rejected(c, GL_INVALID_VALUE);
    glCopyTexImage2D(TEX_RECT, 0, 4, 0, 0, 4, 4, 0);
    Rejected(c, GL_INVALID_VALUE);
    glCopyTexImage2D(TEX_RECT, 0, RGBA8, 0, 0, DG_GL_MAX_TEXTURE_DIMENSION + 1, 4, 0);
    Rejected(c, GL_INVALID_VALUE);
    glCopyTexImage2D(PROXY_RECT, 0, RGBA8, 0, 0, 4, 4, 0);
    Rejected(c, GL_INVALID_ENUM);
    glCopyTexSubImage2D(TEX_RECT, 1, 0, 0, 0, 0, 1, 1);
    Rejected(c, GL_INVALID_VALUE);
    Close(c);
}

/* A large rectangle travels as an allocation and bounded level-zero tiles that
 * cover every pixel exactly once. */
static void Tiles(void) {
    JGL_CONTEXT *c = Open(OTHERS | RECTANGLE, SMALL_PACKET);
    const ULONG width = 37, height = 23;
    std::vector<BYTE> pixels(width * height * 4);
    for (ULONG i = 0; i < pixels.size(); ++i)
        pixels[i] = (BYTE)(i * 13 + 5);
    glTexImage2D(TEX_RECT, 0, RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    Records r = Sent(c);
    assert(r.size() > 2);
    ExpectData(r[0], FEnum_glTexImage2D,
               {TEX_RECT, 0, RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE}, NULL, 0);
    std::vector<BYTE> seen(width * height * 4), covered(width * height);
    for (ULONG i = 1; i < r.size(); ++i) {
        const ULONG *a = r[i].data() + 10;
        assert(r[i][8] == FEnum_glTexSubImage2D && a[0] == TEX_RECT && a[1] == 0);
        for (ULONG y = 0; y < a[5]; ++y)
            for (ULONG x = 0; x < a[4]; ++x) {
                const ULONG at = (a[3] + y) * width + a[2] + x;
                assert(!covered[at]);
                covered[at] = 1;
                memcpy(&seen[at * 4], (const BYTE *)(r[i].data() + 18) + (y * a[4] + x) * 4, 4);
            }
    }
    assert(seen == pixels);
    Close(c);
}

/* Readback and level queries of level zero, the proxy, and attachments. */
static void Readback(void) {
    JGL_CONTEXT *c = Open(OTHERS | RECTANGLE);
    ULONG out[3 * 5];
    memset(out, 0xa5, sizeof(out));
    glGetTexImage(TEX_RECT, 0, GL_RGBA, GL_UNSIGNED_BYTE, out);
    assert(!c->Error);
    for (ULONG i = 0; i < 15; ++i)
        assert(out[i] == (0x7f | (i << 8) | 0xff000000U));
    assert(Queries.size() == 3 && Queries[0][0] == FEnum_glGetTexLevelParameteriv &&
           Queries[0][1] == TEX_RECT && Queries[0][2] == 0 && Queries[1][3] == GL_TEXTURE_HEIGHT &&
           Queries[2][0] == FEnum_glGetTexImage && Queries[2][1] == TEX_RECT);
    Log.clear();
    Queries.clear();
    glGetTexImage(TEX_RECT, 1, GL_RGBA, GL_UNSIGNED_BYTE, out);
    Rejected(c, GL_INVALID_VALUE);
    GLint value = 0;
    glGetTexLevelParameteriv(TEX_RECT, 1, GL_TEXTURE_WIDTH, &value);
    Rejected(c, GL_INVALID_VALUE);
    glGetTexLevelParameteriv(PROXY_RECT, 1, GL_TEXTURE_WIDTH, &value);
    Rejected(c, GL_INVALID_VALUE);
    glGetCompressedTexImageARB(TEX_RECT, 0, out);
    Rejected(c, GL_INVALID_ENUM);
    assert(Queries.empty());
    glGetTexLevelParameteriv(PROXY_RECT, 0, GL_TEXTURE_WIDTH, &value);
    glGetTexLevelParameteriv(TEX_RECT, 0, TEXTURE_BORDER, &value);
    assert(!c->Error && Queries.size() == 2 && Queries[0][1] == PROXY_RECT &&
           Queries[1][1] == TEX_RECT);
    Log.clear();
    /* FramebufferTexture2D takes the rectangle; a nonzero level is the host's
     * INVALID_VALUE. GenerateMipmap never names it. */
    glFramebufferTexture2DEXT(FRAMEBUFFER, COLOR0, TEX_RECT, 5, 0);
    glFramebufferTexture2DEXT(FRAMEBUFFER, COLOR0, TEX_RECT, 5, 1);
    const Records r = Sent(c);
    assert(r.size() == 2);
    ExpectCall(r[0], FEnum_glFramebufferTexture2D, {FRAMEBUFFER, COLOR0, TEX_RECT, 5, 0});
    ExpectCall(r[1], FEnum_glFramebufferTexture2D, {FRAMEBUFFER, COLOR0, TEX_RECT, 5, 1});
    glFramebufferTexture2DEXT(FRAMEBUFFER, COLOR0, PROXY_RECT, 5, 0);
    Rejected(c, GL_INVALID_ENUM);
    glGenerateMipmapEXT(TEX_RECT);
    Rejected(c, GL_INVALID_ENUM);
    Close(c);
}

int main() {
    Unnegotiated();
    Negotiation();
    State();
    Images();
    Tiles();
    Readback();
    puts("PASS rectangle texture frontend");
    return 0;
}
