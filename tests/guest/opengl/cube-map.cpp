/* SPDX-License-Identifier: GPL-2.0-or-later
 * DG_CAP_GL_TEXTURE_CUBE_MAP through the actual frontend on the strict
 * transport fixture: negotiation and the unchanged frontend without it, local
 * validation (nothing the host would reject as malformed may leave the
 * frontend), exact face records, aligned face tiles, compressed faces, face
 * readback, host-answered queries and display lists. */
#include <utility>
#include <vector> /* before the Win32 fixture's min() macro */
#define main existing_frontend_suite_not_run
#include "frontend-harness.cpp"
#undef main
#include "arrays.cpp"
#include "query.cpp"
#define GL_ALPHA 0x1906
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_LUMINANCE 0x1909
#define GL_LUMINANCE_ALPHA 0x190a
#define GL_TEXTURE_WIDTH 0x1000
#define GL_TEXTURE_HEIGHT 0x1001
#include "texture.cpp"
#include "compressed.cpp"
#include "fixed.cpp"
/* Readback keeps its own (C-linkage) arithmetic helpers under other names. */
#define Add ReadbackAdd
#define Multiply ReadbackMultiply
#include "readback.cpp"
#undef Multiply
#undef Add
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
#include "extension-names.h"

#define CUBE 0x8513U
#define BINDING_CUBE 0x8514U
#define POSITIVE_X 0x8515U
#define NEGATIVE_Z 0x851aU
#define PROXY_CUBE 0x851bU
#define MAX_CUBE_SIZE 0x851cU
#define PROXY_2D 0x8064U
#define WRAP_R 0x8072U
#define WRAP_S 0x2802U
#define MIN_FILTER 0x2801U
#define CLAMP_TO_EDGE 0x812fU
#define NORMAL_MAP 0x8511U
#define REFLECTION_MAP 0x8512U
#define TEXTURE_GEN_R 0x0c62U
#define DXT1 0x83f0U
#define CUBE_NAMES "GL_ARB_texture_cube_map GL_EXT_texture_cube_map"
#define SMALL_PACKET 256U /* 64 words: 184 payload bytes per sub-image record */

static BYTE Source[1 << 16];

/* Every submitted record, in order, as the host received it. */
static std::vector<std::vector<ULONG>> Log;
static void Capture(const ULONG *record) {
    Log.emplace_back(record, record + record[1] / 4);
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
static void ExpectData(const std::vector<ULONG> &r, ULONG function,
                       std::initializer_list<ULONG> words, const void *payload, ULONG bytes) {
    assert(r[0] == DG_GL_DATA_CALL && r[8] == function && r[9] == bytes);
    assert(r[1] == (8 + 2 + words.size() + (bytes + 3) / 4) * 4 && r.size() == r[1] / 4);
    ULONG n = 10;
    for (ULONG word : words)
        assert(r[n++] == word);
    assert(!bytes || !memcmp(r.data() + n, payload, bytes));
}
static void ExpectCall(const std::vector<ULONG> &r, ULONG function,
                       std::initializer_list<ULONG> words) {
    assert(r[0] == DG_GL_CALL && r[8] == function && r[1] == (9 + words.size()) * 4);
    ULONG n = 9;
    for (ULONG word : words)
        assert(r[n++] == word);
}
static void ExpectDeferred(const std::vector<ULONG> &r, GLenum error) {
    ExpectCall(r, DG_GL_RECORD_ERROR, {error});
}

/* Host answers: one face level, parameters, state and the face's pixels. */
static GLint LevelWidth, LevelHeight;
static std::vector<std::vector<ULONG>> Queries; /* function, three arguments */
static BOOL Answer(const ULONG *r, DG_ESCAPE_REPLY *reply, ULONG capacity) {
    GLint value;
    Queries.push_back({r[8], r[9], r[10], r[11]});
    switch (r[8]) {
        case FEnum_glGetTexLevelParameteriv:
            value = r[11] == GL_TEXTURE_WIDTH    ? LevelWidth
                    : r[11] == GL_TEXTURE_HEIGHT ? LevelHeight
                                                 : 0;
            break;
        case FEnum_glGetTexParameteriv:
            value = CLAMP_TO_EDGE;
            break;
        case FEnum_glGetIntegerv:
            value = r[9] == BINDING_CUBE ? 5 : DG_GL_MAX_TEXTURE_DIMENSION;
            break;
        case FEnum_glGetBooleanv:
            assert(capacity >= 1);
            *(BYTE *)(reply + 1) = 1;
            reply->ResultBytes = 1;
            reply->ResultType = DG_GL_RESULT_BOOL;
            return TRUE;
        case FEnum_glGetTexImage: {
            /* Canonical RGBA8 pixels: the face index and the pixel number. */
            const ULONG count = r[10] >> DG_GL_TEXTURE_READ_COUNT_SHIFT, first = r[11];
            assert(count && capacity == count * 4);
            for (ULONG i = 0; i < count; ++i) {
                const ULONG pixel = (r[9] - POSITIVE_X + 1) | ((first + i) << 8) | 0xff000000U;
                memcpy((BYTE *)(reply + 1) + i * 4, &pixel, 4);
            }
            reply->ResultBytes = count * 4;
            reply->ResultType = DG_GL_RESULT_INT;
            return TRUE;
        }
        case FEnum_glGetCompressedTexImage: {
            const ULONG words = r[10] >> DG_GL_TEXTURE_READ_COUNT_SHIFT;
            memcpy(reply + 1, Source + r[11] * 4, words * 4);
            reply->ResultBytes = words * 4;
            reply->ResultType = DG_GL_RESULT_INT;
            return TRUE;
        }
        default:
            return FALSE;
    }
    assert(capacity >= 4);
    memcpy(reply + 1, &value, 4);
    reply->ResultBytes = 4;
    reply->ResultType = DG_GL_RESULT_INT;
    return TRUE;
}

static JGL_CONTEXT *Open(ULONG caps, ULONG packet = DG_ESCAPE_MAX_BYTES) {
    Reset();
    HostCaps = caps;
    HostMaxBytes = packet;
    SubmittedRecord = Capture;
    AnswerQuery = Answer;
    LevelWidth = LevelHeight = 4;
    Queries.clear();
    JGL_CONTEXT *c = Lookup(Create());
    Clear(c);
    return c;
}
static void Close(JGL_CONTEXT *c) {
    assert(wglMakeCurrent(NULL, NULL) && wglDeleteContext((HGLRC)(ULONG_PTR)c->Id));
    Log.clear();
}

/* Without the capability nothing new leaves the frontend, and every cube map
 * enum keeps the local error it had before. */
static void Unnegotiated(void) {
    JGL_CONTEXT *c =
        Open(DG_CAP_GL_MULTITEXTURE | DG_CAP_GL_FIXED_STATE | DG_CAP_GL_TEXTURE_CONTROL |
             DG_CAP_GL_COMPRESSED_TEXTURES | DG_CAP_GL_FOG_COORD | DG_CAP_GL_TEXTURE_IMAGES);
    const void *bogus = (const void *)16;
    BYTE image[64];
    memset(image, 0xa5, sizeof(image));
    assert(!JglTextureCubeMapAvailable());
    assert(!strstr((const char *)glGetString(GL_EXTENSIONS), "cube_map"));
    glBindTexture(CUBE, 1);
    Rejected(c, GL_INVALID_ENUM);
    glBindTexture(CUBE, 0);
    Rejected(c, GL_INVALID_ENUM);
    glTexImage2D(POSITIVE_X, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
    Rejected(c, GL_INVALID_ENUM);
    glTexImage2D(PROXY_CUBE, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    Rejected(c, GL_INVALID_ENUM);
    glTexSubImage2D(NEGATIVE_Z, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, image);
    Rejected(c, GL_INVALID_ENUM);
    glTexParameteri(CUBE, MIN_FILTER, 0x2600);
    Rejected(c, GL_INVALID_ENUM);
    glTexParameteri(GL_TEXTURE_2D, WRAP_R, CLAMP_TO_EDGE);
    Rejected(c, GL_INVALID_ENUM);
    const GLint wrap = CLAMP_TO_EDGE;
    glTexParameteriv(CUBE, WRAP_R, &wrap);
    Rejected(c, GL_INVALID_ENUM);
    glCopyTexImage2D(POSITIVE_X, 0, GL_RGBA, 0, 0, 4, 4, 0);
    Rejected(c, GL_INVALID_ENUM);
    glCopyTexSubImage2D(POSITIVE_X, 0, 0, 0, 0, 0, 4, 4);
    Rejected(c, GL_INVALID_ENUM);
    glCompressedTexImage2D(POSITIVE_X, 0, DXT1, 4, 4, 0, 8, bogus);
    Rejected(c, GL_INVALID_ENUM);
    glCompressedTexImage2D(PROXY_CUBE, 0, DXT1, 4, 4, 0, 8, NULL);
    Rejected(c, GL_INVALID_ENUM);
    glCompressedTexSubImage2D(POSITIVE_X, 0, 0, 0, 4, 4, DXT1, 8, bogus);
    Rejected(c, GL_INVALID_ENUM);
    /* Queries stay local errors: no host query is issued. */
    GLint value = 0x5a5a;
    GLboolean flag = 0x5a;
    glGetTexImage(POSITIVE_X, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
    Rejected(c, GL_INVALID_ENUM);
    glGetCompressedTexImage(POSITIVE_X, 0, image);
    Rejected(c, GL_INVALID_ENUM);
    glGetIntegerv(BINDING_CUBE, &value);
    Rejected(c, GL_INVALID_ENUM);
    glGetIntegerv(MAX_CUBE_SIZE, &value);
    Rejected(c, GL_INVALID_ENUM);
    glGetBooleanv(CUBE, &flag);
    Rejected(c, GL_INVALID_ENUM);
    assert(!glIsEnabled(CUBE));
    Rejected(c, GL_INVALID_ENUM);
    glGetTexParameteriv(CUBE, WRAP_S, &value);
    Rejected(c, GL_INVALID_ENUM);
    glGetTexLevelParameteriv(NEGATIVE_Z, 0, GL_TEXTURE_WIDTH, &value);
    Rejected(c, GL_INVALID_ENUM);
    glGetTexLevelParameteriv(PROXY_CUBE, 0, GL_TEXTURE_WIDTH, &value);
    Rejected(c, GL_INVALID_ENUM);
    assert(Queries.empty() && value == 0x5a5a && flag == 0x5a);
    for (BYTE byte : image)
        assert(byte == 0xa5);
    /* Enable keeps passing through, as before; texgen values are never judged. */
    glEnable(CUBE);
    glTexGeni(GL_R, GL_TEXTURE_GEN_MODE, REFLECTION_MAP);
    assert(c->Records == 2 && !c->Error);
    Submit(c);
    ExpectCall(Log[0], FEnum_glEnable, {CUBE});
    const GLfloat reflection = (GLfloat)REFLECTION_MAP;
    ExpectData(Log[1], FEnum_glTexGenfv, {GL_R, GL_TEXTURE_GEN_MODE}, &reflection, 4);
    Close(c);
}

/* Negotiated: the extension group is appended last, alone or after all others. */
static void Negotiation(void) {
    JGL_CONTEXT *c = Open(DG_CAP_GL_TEXTURE_CUBE_MAP);
    assert(JglTextureCubeMapAvailable());
    assert(!strcmp((const char *)glGetString(GL_EXTENSIONS), CUBE_NAMES));
    /* No entry point belongs to the extension; other families stay hidden. */
    assert(!wglGetProcAddress("glActiveTextureARB") &&
           !wglGetProcAddress("glCompressedTexImage2DARB") &&
           wglGetProcAddress("glTexImage2D") == Marker);
    Close(c);
    Reset();
    HostCaps = DG_CAP_GL_MULTITEXTURE | DG_CAP_GL_FIXED_STATE | DG_CAP_GL_TEXTURE_CONTROL |
               DG_CAP_GL_COMPRESSED_TEXTURES | DG_CAP_GL_FOG_COORD | DG_CAP_GL_TEXTURE_CUBE_MAP;
    HostSecondaryWords = 3;
    HGLRC context = Create();
    /* Every name in its place and under its gate (extension-names.h). */
    assert(!strcmp((const char *)glGetString(GL_EXTENSIONS), ExpectedExtensions(HostCaps, TRUE)));
    assert(wglMakeCurrent(NULL, NULL) && wglDeleteContext(context));
}

/* Binding, parameters, enables, texgen and host-answered queries. */
static void State(void) {
    JGL_CONTEXT *c = Open(DG_CAP_GL_TEXTURE_CUBE_MAP);
    const GLint wrap = CLAMP_TO_EDGE;
    glBindTexture(CUBE, 5);
    glBindTexture(CUBE, 0);
    glTexParameteri(CUBE, WRAP_R, CLAMP_TO_EDGE);
    glTexParameteri(CUBE, MIN_FILTER, 0x2600);
    glTexParameteriv(CUBE, WRAP_R, &wrap);
    glEnable(CUBE);
    glEnable(TEXTURE_GEN_R);
    glTexGeni(GL_R, GL_TEXTURE_GEN_MODE, NORMAL_MAP);
    glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, REFLECTION_MAP);
    glDisable(CUBE);
    assert(c->Records == 10 && !c->Error);
    Submit(c);
    assert(Log.size() == 10);
    ExpectCall(Log[0], FEnum_glBindTexture, {CUBE, 5});
    ExpectCall(Log[1], FEnum_glBindTexture, {CUBE, 0});
    ExpectCall(Log[2], FEnum_glTexParameteri, {CUBE, WRAP_R, CLAMP_TO_EDGE});
    ExpectCall(Log[3], FEnum_glTexParameteri, {CUBE, MIN_FILTER, 0x2600});
    ExpectData(Log[4], FEnum_glTexParameteriv, {CUBE, WRAP_R}, &wrap, 4);
    ExpectCall(Log[5], FEnum_glEnable, {CUBE});
    ExpectCall(Log[6], FEnum_glEnable, {TEXTURE_GEN_R});
    const GLfloat normal = (GLfloat)NORMAL_MAP, reflection = (GLfloat)REFLECTION_MAP;
    ExpectData(Log[7], FEnum_glTexGenfv, {GL_R, GL_TEXTURE_GEN_MODE}, &normal, 4);
    ExpectData(Log[8], FEnum_glTexGenfv, {GL_S, GL_TEXTURE_GEN_MODE}, &reflection, 4);
    ExpectCall(Log[9], FEnum_glDisable, {CUBE});
    Log.clear();
    /* A name keeps the target of its first bind, either way round; WRAP_R is
     * a cube map parameter only and faces are not parameter targets. */
    glBindTexture(GL_TEXTURE_2D, 5);
    Rejected(c, GL_INVALID_OPERATION);
    glBindTexture(GL_TEXTURE_2D, 6);
    glBindTexture(CUBE, 6);
    assert(c->Error == GL_INVALID_OPERATION && c->Records == 1);
    c->Error = 0;
    Submit(c);
    ExpectCall(Log[0], FEnum_glBindTexture, {GL_TEXTURE_2D, 6});
    Log.clear();
    assert(glIsTexture(5) && glIsTexture(6));
    glTexParameteri(GL_TEXTURE_2D, WRAP_R, CLAMP_TO_EDGE);
    Rejected(c, GL_INVALID_ENUM);
    glTexParameteri(POSITIVE_X, MIN_FILTER, 0x2600);
    Rejected(c, GL_INVALID_ENUM);
    glTexParameteriv(PROXY_CUBE, WRAP_R, &wrap);
    Rejected(c, GL_INVALID_ENUM);
    glBindTexture(POSITIVE_X, 7);
    Rejected(c, GL_INVALID_ENUM);
    /* State, parameter and level queries reach the host. */
    GLint value = 0;
    GLboolean flag = 0;
    glGetIntegerv(BINDING_CUBE, &value);
    assert(value == 5);
    glGetIntegerv(MAX_CUBE_SIZE, &value);
    assert(value == DG_GL_MAX_TEXTURE_DIMENSION);
    HostEnabled = 1;
    assert(glIsEnabled(CUBE));
    glGetBooleanv(CUBE, &flag);
    assert(flag == 1);
    glGetTexParameteriv(CUBE, WRAP_R, &value);
    assert(value == (GLint)CLAMP_TO_EDGE);
    LevelWidth = 16;
    glGetTexLevelParameteriv(NEGATIVE_Z, 2, GL_TEXTURE_WIDTH, &value);
    assert(value == 16);
    glGetTexLevelParameteriv(PROXY_CUBE, 0, GL_TEXTURE_HEIGHT, &value);
    assert(!c->Error);
    assert(Queries.size() == 7);
    assert(Queries[0] == std::vector<ULONG>({FEnum_glGetIntegerv, BINDING_CUBE, 0, 0}));
    assert(Queries[1] == std::vector<ULONG>({FEnum_glGetIntegerv, MAX_CUBE_SIZE, 0, 0}));
    assert(Queries[2] == std::vector<ULONG>({FEnum_glIsEnabled, CUBE, 0, 0}));
    assert(Queries[3] == std::vector<ULONG>({FEnum_glGetBooleanv, CUBE, 0, 0}));
    assert(Queries[4] == std::vector<ULONG>({FEnum_glGetTexParameteriv, CUBE, WRAP_R, 0}));
    assert(Queries[5] ==
           std::vector<ULONG>({FEnum_glGetTexLevelParameteriv, NEGATIVE_Z, 2, GL_TEXTURE_WIDTH}));
    assert(Queries[6] ==
           std::vector<ULONG>({FEnum_glGetTexLevelParameteriv, PROXY_CUBE, 0, GL_TEXTURE_HEIGHT}));
    assert(LastQueryFunction == FEnum_glGetTexLevelParameteriv);
    Close(c);
}

/* Face images: square, borderless, 2D level limits, each its own record. */
static void Faces(void) {
    JGL_CONTEXT *c = Open(DG_CAP_GL_TEXTURE_CUBE_MAP | DG_CAP_GL_TEXTURE_IMAGES |
                          DG_CAP_GL_TEXTURE_BORDERS | DG_CAP_GL_COMPRESSED_TEXTURES);
    const void *bogus = (const void *)16;
    for (ULONG face = 0; face < 6; ++face)
        glTexImage2D(POSITIVE_X + face, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     Source + face * 16);
    glTexImage2D(NEGATIVE_Z, 1, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, Source);
    glTexImage2D(POSITIVE_X + 2, 3, 0x83f3 /* DXT5 */, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexImage2D(POSITIVE_X + 1, 0, GL_RGBA, 0, 0, 0, GL_RGBA, GL_UNSIGNED_BYTE, bogus);
    glTexSubImage2D(POSITIVE_X + 3, 0, 1, 0, 1, 2, GL_RGBA, GL_UNSIGNED_BYTE, Source + 100);
    glCopyTexImage2D(POSITIVE_X + 4, 1, GL_RGB, 3, 5, 8, 8, 0);
    glCopyTexImage2D(POSITIVE_X + 5, 0, 0x8058 /* RGBA8 */, 0, 0, 0, 0, 0);
    glCopyTexSubImage2D(NEGATIVE_Z, 0, 1, 1, 2, 3, 1, 1);
    assert(c->Records == 13 && !c->Error);
    Submit(c);
    assert(Log.size() == 13);
    for (ULONG face = 0; face < 6; ++face)
        ExpectData(Log[face], FEnum_glTexImage2D,
                   {POSITIVE_X + face, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE},
                   Source + face * 16, 16);
    ExpectData(Log[6], FEnum_glTexImage2D,
               {NEGATIVE_Z, 1, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE}, Source, 4);
    ExpectData(Log[7], FEnum_glTexImage2D,
               {POSITIVE_X + 2, 3, 0x83f3, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE}, NULL, 0);
    ExpectData(Log[8], FEnum_glTexImage2D,
               {POSITIVE_X + 1, 0, GL_RGBA, 0, 0, 0, GL_RGBA, GL_UNSIGNED_BYTE}, NULL, 0);
    /* A 1x2 column of the caller's 1-pixel-wide rows, packed. */
    BYTE column[8];
    memcpy(column, Source + 100, 4);
    memcpy(column + 4, Source + 104, 4);
    ExpectData(Log[9], FEnum_glTexSubImage2D,
               {POSITIVE_X + 3, 0, 1, 0, 1, 2, GL_RGBA, GL_UNSIGNED_BYTE}, column, 8);
    ExpectCall(Log[10], FEnum_glCopyTexImage2D, {POSITIVE_X + 4, 1, GL_RGB, 3, 5, 8, 8, 0});
    ExpectCall(Log[11], FEnum_glCopyTexImage2D, {POSITIVE_X + 5, 0, 0x8058, 0, 0, 0, 0, 0});
    ExpectCall(Log[12], FEnum_glCopyTexSubImage2D, {NEGATIVE_Z, 0, 1, 1, 2, 3, 1, 1});
    Log.clear();
    /* TEXTURE_2D copies are unchanged: not square, still sent. */
    Clear(c);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, 8, 4, 0);
    Submit(c);
    ExpectCall(Log[0], FEnum_glCopyTexImage2D, {GL_TEXTURE_2D, 0, GL_RGBA, 0, 0, 8, 4, 0});
    Log.clear();

    /* Every rule the host checks on a face record is a local error. */
    const struct {
        GLenum target;
        GLint level, format;
        GLsizei width, height;
        GLint border;
        GLenum error;
    } images[] = {
        {POSITIVE_X, 0, GL_RGBA, 4, 2, 0, GL_INVALID_VALUE},
        {NEGATIVE_Z, 0, GL_RGBA, 1, 2, 0, GL_INVALID_VALUE},
        {POSITIVE_X + 1, 0, GL_RGBA, 4, 4, 1, GL_INVALID_VALUE},
        {POSITIVE_X + 1, 0, GL_RGBA, 6, 6, 1, GL_INVALID_VALUE},
        {POSITIVE_X, 1, GL_RGBA, DG_GL_MAX_TEXTURE_DIMENSION, DG_GL_MAX_TEXTURE_DIMENSION, 0,
         GL_INVALID_VALUE},
        {POSITIVE_X, DG_GL_MAX_TEXTURE_LEVEL + 1, GL_RGBA, 1, 1, 0, GL_INVALID_VALUE},
        {POSITIVE_X, 0, 0x1234, 4, 4, 0, GL_INVALID_VALUE},
        {CUBE, 0, GL_RGBA, 4, 4, 0, GL_INVALID_ENUM},
        {NEGATIVE_Z + 2, 0, GL_RGBA, 4, 4, 0, GL_INVALID_ENUM},
    };
    for (const auto &item : images) {
        glTexImage2D(item.target, item.level, item.format, item.width, item.height, item.border,
                     GL_RGBA, GL_UNSIGNED_BYTE, bogus);
        Rejected(c, item.error);
    }
    glTexSubImage2D(CUBE, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, bogus);
    Rejected(c, GL_INVALID_ENUM);
    glTexImage1D(POSITIVE_X, 0, GL_RGBA, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, bogus);
    Rejected(c, GL_INVALID_ENUM);
    const struct {
        GLenum target;
        GLint level, format, width, height, border;
        GLenum error;
    } copies[] = {
        {POSITIVE_X, 0, GL_RGBA, 8, 4, 0, GL_INVALID_VALUE},
        {POSITIVE_X, 0, GL_RGBA, 6, 6, 1, GL_INVALID_VALUE},
        {POSITIVE_X, 0, 3, 4, 4, 0, GL_INVALID_VALUE}, /* component counts are not copy formats */
        {POSITIVE_X, 0, 0x83f0, 4, 4, 0, GL_INVALID_VALUE},
        {POSITIVE_X, 0, GL_RGBA, -4, -4, 0, GL_INVALID_VALUE},
        {POSITIVE_X, 2, GL_RGBA, 1024, 1024, 0, GL_INVALID_VALUE},
        {POSITIVE_X, DG_GL_MAX_TEXTURE_LEVEL + 1, GL_RGBA, 1, 1, 0, GL_INVALID_VALUE},
        {CUBE, 0, GL_RGBA, 4, 4, 0, GL_INVALID_ENUM},
        {PROXY_CUBE, 0, GL_RGBA, 4, 4, 0, GL_INVALID_ENUM},
        {GL_TEXTURE_1D, 0, GL_RGBA, 4, 4, 0, GL_INVALID_ENUM},
    };
    for (const auto &item : copies) {
        glCopyTexImage2D(item.target, item.level, item.format, 0, 0, item.width, item.height,
                         item.border);
        Rejected(c, item.error);
    }
    glCopyTexSubImage2D(NEGATIVE_Z, 0, 0, 0, 0, 0, -1, 1);
    Rejected(c, GL_INVALID_VALUE);
    glCopyTexSubImage2D(NEGATIVE_Z, DG_GL_MAX_TEXTURE_LEVEL + 1, 0, 0, 0, 0, 1, 1);
    Rejected(c, GL_INVALID_VALUE);
    glCopyTexSubImage2D(CUBE, 0, 0, 0, 0, 0, 1, 1);
    Rejected(c, GL_INVALID_ENUM);

    /* Proxies: square, borderless, compressed formats allowed, immediate. */
    glTexImage2D(PROXY_CUBE, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexImage2D(PROXY_CUBE, 2, DXT1, 512, 512, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    assert(!c->Error);
    glTexImage2D(PROXY_CUBE, 0, GL_RGBA, 64, 32, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    assert(c->Error == GL_INVALID_VALUE);
    c->Error = 0;
    glTexImage2D(PROXY_CUBE, 0, GL_RGBA, 66, 66, 1, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    assert(c->Error == GL_INVALID_VALUE);
    c->Error = 0;
    Submit(c);
    assert(Log.size() == 2);
    ExpectData(Log[0], FEnum_glTexImage2D,
               {PROXY_CUBE, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE}, NULL, 0);
    ExpectData(Log[1], FEnum_glTexImage2D,
               {PROXY_CUBE, 2, DXT1, 512, 512, 0, GL_RGBA, GL_UNSIGNED_BYTE}, NULL, 0);
    Close(c);
}

/* A face too large for one record: an allocation, then sub-image tiles on
 * four-pixel boundaries (compressed face storage accepts no other), all
 * through the face's own target, reassembling the caller's pixels. */
static void Tiles(void) {
    JGL_CONTEXT *c = Open(DG_CAP_GL_TEXTURE_CUBE_MAP, SMALL_PACKET);
    const ULONG side = 12, stride = side * 4;
    glTexImage2D(NEGATIVE_Z - 1, 1, DXT1, side, side, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    assert(c->Error == GL_INVALID_VALUE); /* compression is not negotiated here */
    Close(c);
    c = Open(DG_CAP_GL_TEXTURE_CUBE_MAP | DG_CAP_GL_COMPRESSED_TEXTURES, SMALL_PACKET);
    glTexImage2D(NEGATIVE_Z - 1, 1, DXT1, side, side, 0, GL_RGBA, GL_UNSIGNED_BYTE, Source);
    assert(!c->Error);
    Submit(c);
    ExpectData(Log[0], FEnum_glTexImage2D,
               {NEGATIVE_Z - 1, 1, DXT1, side, side, 0, GL_RGBA, GL_UNSIGNED_BYTE}, NULL, 0);
    std::vector<BYTE> image(side * stride, 0);
    std::vector<ULONG> covered(side * side, 0);
    assert(Log.size() > 2);
    for (size_t n = 1; n < Log.size(); ++n) {
        const ULONG *a = Log[n].data() + 10;
        assert(Log[n][8] == FEnum_glTexSubImage2D && a[0] == NEGATIVE_Z - 1 && a[1] == 1);
        assert(!(a[2] % 4) && !(a[3] % 4) && a[4] && a[5]);
        assert(!(a[4] % 4) || a[2] + a[4] == side);
        assert(!(a[5] % 4) || a[3] + a[5] == side);
        assert(Log[n][9] == a[4] * a[5] * 4 && Log[n][9] <= 184);
        const BYTE *payload = (const BYTE *)(Log[n].data() + 18);
        for (ULONG y = 0; y < a[5]; ++y)
            for (ULONG x = 0; x < a[4]; ++x) {
                ++covered[(a[3] + y) * side + a[2] + x];
                memcpy(&image[(a[3] + y) * stride + (a[2] + x) * 4], payload + (y * a[4] + x) * 4,
                       4);
            }
    }
    for (ULONG count : covered)
        assert(count == 1);
    assert(!memcmp(image.data(), Source, image.size()));
    Close(c);
}

/* S3TC on faces: square images, bands through the face, face readback. */
static void Compressed(void) {
    JGL_CONTEXT *c = Open(DG_CAP_GL_TEXTURE_CUBE_MAP | DG_CAP_GL_COMPRESSED_TEXTURES, SMALL_PACKET);
    const void *bogus = (const void *)16;
    glCompressedTexImage2D(POSITIVE_X + 1, 0, DXT1, 8, 8, 0, 32, Source);
    glCompressedTexImage2D(PROXY_CUBE, 0, DXT1, 16, 16, 0, 128, NULL);
    glCompressedTexSubImage2DARB(NEGATIVE_Z, 0, 4, 0, 4, 4, DXT1, 8, Source + 32);
    /* Forty blocks: an allocation and block-row bands through the face. */
    glCompressedTexImage2D(POSITIVE_X + 3, 0, DXT1, 40, 40, 0, 800, Source);
    assert(!c->Error);
    Submit(c);
    ExpectData(Log[0], FEnum_glCompressedTexImage2D, {POSITIVE_X + 1, 0, DXT1, 8, 8, 0, 32}, Source,
               32);
    ExpectData(Log[1], FEnum_glCompressedTexImage2D, {PROXY_CUBE, 0, DXT1, 16, 16, 0, 128}, NULL,
               0);
    ExpectData(Log[2], FEnum_glCompressedTexSubImage2D, {NEGATIVE_Z, 0, 4, 0, 4, 4, DXT1, 8},
               Source + 32, 8);
    ExpectData(Log[3], FEnum_glCompressedTexImage2D, {POSITIVE_X + 3, 0, DXT1, 40, 40, 0, 800},
               NULL, 0);
    assert(Log.size() > 5);
    ULONG bytes = 0;
    for (size_t n = 4; n < Log.size(); ++n) {
        assert(Log[n][8] == FEnum_glCompressedTexSubImage2D && Log[n][10] == POSITIVE_X + 3);
        bytes += Log[n][9];
    }
    assert(bytes == 800);
    Log.clear();
    glCompressedTexImage2D(POSITIVE_X, 0, DXT1, 8, 4, 0, 16, bogus);
    Rejected(c, GL_INVALID_VALUE);
    glCompressedTexImage2D(PROXY_CUBE, 0, DXT1, 8, 4, 0, 16, NULL);
    Rejected(c, GL_INVALID_VALUE);
    glCompressedTexImage2D(CUBE, 0, DXT1, 4, 4, 0, 8, bogus);
    Rejected(c, GL_INVALID_ENUM);
    glCompressedTexSubImage2D(CUBE, 0, 0, 0, 4, 4, DXT1, 8, bogus);
    Rejected(c, GL_INVALID_ENUM);
    /* Readback of one face: its own level queries, then its words. */
    BYTE image[32];
    memset(image, 0xa5, sizeof(image));
    LevelWidth = 8;
    Queries.clear();
    AnswerQuery = [](const ULONG *r, DG_ESCAPE_REPLY *reply, ULONG capacity) -> BOOL {
        if (r[8] == FEnum_glGetTexLevelParameteriv) {
            const GLint value = r[11] == 0x86a1 /* TEXTURE_COMPRESSED */ ? 1 : 32;
            assert(r[9] == POSITIVE_X + 4 && capacity >= 4);
            Queries.push_back({r[8], r[9], r[10], r[11]});
            memcpy(reply + 1, &value, 4);
            reply->ResultBytes = 4;
            reply->ResultType = DG_GL_RESULT_INT;
            return TRUE;
        }
        return Answer(r, reply, capacity);
    };
    glGetCompressedTexImageARB(POSITIVE_X + 4, 0, image);
    assert(!c->Error && Queries.size() == 3 && Queries[2][0] == FEnum_glGetCompressedTexImage &&
           Queries[2][1] == POSITIVE_X + 4 && !memcmp(image, Source, 32));
    Close(c);
}

/* GetTexImage of a face queries that face's level and reads its pixels. */
static void Readback(void) {
    JGL_CONTEXT *c = Open(DG_CAP_GL_TEXTURE_CUBE_MAP | DG_CAP_GL_BULK_READBACK);
    BYTE image[3 * 2 * 4 + 4];
    memset(image, 0xa5, sizeof(image));
    LevelWidth = 3;
    LevelHeight = 2;
    glGetTexImage(POSITIVE_X + 2, 1, GL_RGBA, GL_UNSIGNED_BYTE, image);
    assert(!c->Error);
    assert(Queries.size() == 3);
    assert(Queries[0] == std::vector<ULONG>({FEnum_glGetTexLevelParameteriv, POSITIVE_X + 2, 1,
                                             GL_TEXTURE_WIDTH}));
    assert(Queries[1] == std::vector<ULONG>({FEnum_glGetTexLevelParameteriv, POSITIVE_X + 2, 1,
                                             GL_TEXTURE_HEIGHT}));
    assert(Queries[2][0] == FEnum_glGetTexImage && Queries[2][1] == POSITIVE_X + 2 &&
           (Queries[2][2] & DG_GL_TEXTURE_READ_LEVEL_MASK) == 1 && !Queries[2][3]);
    for (ULONG i = 0; i < 6; ++i) {
        const ULONG pixel = 3 | (i << 8) | 0xff000000U;
        assert(!memcmp(image + i * 4, &pixel, 4));
    }
    assert(image[24] == 0xa5);
    /* The binding target and the proxy have no image. */
    Queries.clear();
    Log.clear();
    glGetTexImage(CUBE, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
    Rejected(c, GL_INVALID_ENUM);
    glGetTexImage(PROXY_CUBE, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
    Rejected(c, GL_INVALID_ENUM);
    assert(Queries.empty());
    Close(c);
}

/* Compiling a list: face records are recorded for the host to capture,
 * command errors deferred, proxies immediate. */
static void Lists(void) {
    /* Proxies keep requiring the extended image contract, as for 2D. */
    JGL_CONTEXT *c = Open(DG_CAP_GL_TEXTURE_CUBE_MAP);
    glTexImage2D(PROXY_CUBE, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    Rejected(c, GL_INVALID_OPERATION);
    Close(c);
    c = Open(DG_CAP_GL_TEXTURE_CUBE_MAP | DG_CAP_GL_TEXTURE_IMAGES);
    assert(JglListMode(GL_COMPILE));
    Clear(c);
    glBindTexture(CUBE, 9);
    glTexImage2D(POSITIVE_X, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, Source);
    glTexImage2D(POSITIVE_X, 0, GL_RGBA, 2, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, Source);
    glCopyTexImage2D(POSITIVE_X, 0, GL_RGBA, 0, 0, 2, 1, 0);
    glTexParameteri(CUBE, WRAP_R, CLAMP_TO_EDGE);
    assert(!c->Error);
    glTexImage2D(PROXY_CUBE, 0, GL_RGBA, 2, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    assert(c->Error == GL_INVALID_VALUE);
    c->Error = 0;
    glTexImage2D(PROXY_CUBE, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    assert(!c->Error);
    Submit(c);
    assert(Log.size() == 6);
    ExpectCall(Log[0], FEnum_glBindTexture, {CUBE, 9});
    ExpectData(Log[1], FEnum_glTexImage2D,
               {POSITIVE_X, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE}, Source, 4);
    ExpectDeferred(Log[2], GL_INVALID_VALUE);
    ExpectDeferred(Log[3], GL_INVALID_VALUE);
    ExpectCall(Log[4], FEnum_glTexParameteri, {CUBE, WRAP_R, CLAMP_TO_EDGE});
    ExpectData(Log[5], FEnum_glTexImage2D,
               {PROXY_CUBE, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE}, NULL, 0);
    assert(JglListMode(0));
    Close(c);
}

int main() {
    for (ULONG i = 0; i < sizeof(Source); ++i)
        Source[i] = (BYTE)(i * 131 + (i >> 8) * 7 + 3);
    Unnegotiated();
    Negotiation();
    State();
    Faces();
    Tiles();
    Compressed();
    Readback();
    Lists();
    Reset();
    assert(!Allocations);
    puts("PASS cube maps actual frontend: unchanged without negotiation; appended extension "
         "group; cube binding, WRAP_R and texgen records; square borderless face images, copies "
         "and proxies validated locally; aligned face tiles; compressed faces; face readback; "
         "deferred list errors");
    return 0;
}
