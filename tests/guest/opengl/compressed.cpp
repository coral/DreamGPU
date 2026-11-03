/* SPDX-License-Identifier: GPL-2.0-or-later
 * DG_CAP_GL_COMPRESSED_TEXTURES through the actual frontend on the strict
 * transport fixture: negotiation, local validation (nothing the host would
 * reject as malformed may leave the frontend), exact DATA records and byte
 * slices, banding, chunked readback, compressed TexImage2D formats and lists. */
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
#include "texture.cpp"
#include "compressed.cpp"
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

#define DXT1 0x83f0U
#define DXT1A 0x83f1U
#define DXT3 0x83f2U
#define DXT5 0x83f3U
#define PROXY 0x8064U
#define COMPRESSION_HINT 0x84efU
#define NICEST 0x1102U
#define TEXTURE_COMPRESSED_IMAGE_SIZE 0x86a0U
#define TEXTURE_COMPRESSED 0x86a1U
#define NUM_COMPRESSED_TEXTURE_FORMATS 0x86a2U
#define COMPRESSED_TEXTURE_FORMATS 0x86a3U
#define COMPRESSED_NAMES "GL_ARB_texture_compression GL_EXT_texture_compression_s3tc"
/* Payload bytes of one 7-word image record and one 8-word sub-image record. */
#define SMALL_PACKET 256U /* 64 words: 188 and 184 bytes */
#define TINY_PACKET 128U  /* 32 words, the negotiated minimum: 60 and 56 bytes */

static const char *const Names[6] = {"glCompressedTexImage2D",    "glCompressedTexImage2DARB",
                                     "glCompressedTexSubImage2D", "glCompressedTexSubImage2DARB",
                                     "glGetCompressedTexImage",   "glGetCompressedTexImageARB"};
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

/* Records [first, end) are the sub-image bands of one rectangle: whole block
 * rows across the full width (or chunks of whole blocks of one row), each
 * within capacity, edges partial only at the rectangle's own edges, every
 * block exactly once, and the payloads reassemble the caller's bytes. */
static void Reassembles(size_t first, ULONG level, ULONG x, ULONG y, ULONG width, ULONG height,
                        ULONG format, ULONG block, const BYTE *expected, ULONG capacity) {
    const ULONG across = (width + 3) / 4, down = (height + 3) / 4;
    std::vector<BYTE> image(across * down * block, 0);
    std::vector<ULONG> covered(across * down, 0);
    assert(first < Log.size());
    for (size_t n = first; n < Log.size(); ++n) {
        const std::vector<ULONG> &r = Log[n];
        const ULONG *a = r.data() + 10;
        assert(r[0] == DG_GL_DATA_CALL && r[8] == FEnum_glCompressedTexSubImage2D);
        assert(a[0] == GL_TEXTURE_2D && a[1] == level && a[6] == format);
        assert(a[2] >= x && !((a[2] - x) % 4) && a[3] >= y && !((a[3] - y) % 4) && a[4] && a[5]);
        assert(!(a[4] % 4) || a[2] + a[4] == x + width);
        assert(!(a[5] % 4) || a[3] + a[5] == y + height);
        const ULONG bx = (a[2] - x) / 4, by = (a[3] - y) / 4, bw = (a[4] + 3) / 4,
                    bh = (a[5] + 3) / 4;
        assert(bx + bw <= across && by + bh <= down && (bh == 1 || (!bx && bw == across)));
        assert(a[7] == bw * bh * block && r[9] == a[7] && a[7] <= capacity);
        assert(r.size() == 18 + a[7] / 4);
        const BYTE *payload = (const BYTE *)(r.data() + 18);
        for (ULONG j = 0; j < bh; ++j)
            for (ULONG i = 0; i < bw; ++i) {
                const ULONG at = (by + j) * across + bx + i;
                ++covered[at];
                memcpy(&image[at * block], payload + (j * bw + i) * block, block);
            }
    }
    for (ULONG count : covered)
        assert(count == 1);
    assert(!memcmp(image.data(), expected, image.size()));
}

/* Host answers: level state and the native compressed image, in words. */
static GLint LevelCompressed, LevelSize;
static ULONG ReadLevel, ShortChunk, NextWord, LevelQueries;
static std::vector<std::pair<ULONG, ULONG>> Chunks; /* first word, words */
static BOOL Answer(const ULONG *r, DG_ESCAPE_REPLY *reply, ULONG capacity) {
    GLint values[4] = {(GLint)DXT1, (GLint)DXT1A, (GLint)DXT3, (GLint)DXT5};
    ULONG count = 0;
    if (r[8] == FEnum_glGetTexLevelParameteriv &&
        (r[11] == TEXTURE_COMPRESSED || r[11] == TEXTURE_COMPRESSED_IMAGE_SIZE)) {
        assert(r[9] == GL_TEXTURE_2D && r[10] == ReadLevel && capacity >= 4);
        values[0] = r[11] == TEXTURE_COMPRESSED ? LevelCompressed : LevelSize;
        count = 1;
        ++LevelQueries;
    } else if (r[8] == FEnum_glGetCompressedTexImage) {
        const ULONG words = r[10] >> DG_GL_TEXTURE_READ_COUNT_SHIFT, at = r[11];
        assert(r[9] == GL_TEXTURE_2D && (r[10] & 0xffff) == ReadLevel && words);
        assert(capacity == words * 4 && at == NextWord && (at + words) * 4 <= (ULONG)LevelSize);
        memcpy(reply + 1, Source + at * 4, words * 4);
        reply->ResultBytes = words * 4 - (Chunks.size() == ShortChunk ? 4 : 0);
        reply->ResultType = DG_GL_RESULT_INT;
        Chunks.emplace_back(at, words);
        NextWord += words;
        return TRUE;
    } else if (r[8] == FEnum_glGetIntegerv && r[9] == NUM_COMPRESSED_TEXTURE_FORMATS) {
        values[0] = 4;
        count = 1;
    } else if (r[8] == FEnum_glGetIntegerv && r[9] == COMPRESSED_TEXTURE_FORMATS) {
        count = 4;
    } else {
        return FALSE;
    }
    assert(capacity >= count * 4);
    memcpy(reply + 1, values, count * 4);
    reply->ResultBytes = count * 4;
    reply->ResultType = DG_GL_RESULT_INT;
    return TRUE;
}

static JGL_CONTEXT *Open(ULONG caps, ULONG packet = DG_ESCAPE_MAX_BYTES,
                         ULONG result = DG_ESCAPE_MAX_RESULT_BYTES) {
    Reset();
    HostCaps = caps;
    HostMaxBytes = packet;
    HostMaxResultBytes = result;
    SubmittedRecord = Capture;
    AnswerQuery = Answer;
    LevelCompressed = 1;
    LevelSize = ReadLevel = NextWord = LevelQueries = 0;
    ShortChunk = ~0u;
    Chunks.clear();
    JGL_CONTEXT *c = Lookup(Create());
    Clear(c);
    return c;
}
static void Close(JGL_CONTEXT *c) {
    assert(wglMakeCurrent(NULL, NULL) && wglDeleteContext((HGLRC)(ULONG_PTR)c->Id));
    Log.clear();
}

/* Without the capability nothing new leaves the frontend. */
static void Unnegotiated(void) {
    JGL_CONTEXT *c = Open(DG_CAP_GL_MULTITEXTURE | DG_CAP_GL_FIXED_STATE |
                          DG_CAP_GL_TEXTURE_CONTROL | DG_CAP_GL_TEXTURE_IMAGES);
    BYTE image[64];
    memset(image, 0xa5, sizeof(image));
    assert(!JglCompressedTexturesAvailable());
    assert(!strstr((const char *)glGetString(GL_EXTENSIONS), "compression"));
    for (const char *name : Names)
        assert(!wglGetProcAddress(name));
    assert(wglGetProcAddress("glBlendColorEXT") == Marker &&
           wglGetProcAddress("glTexImage2D") == Marker);

    glCompressedTexImage2D(GL_TEXTURE_2D, 0, DXT1, 4, 4, 0, 8, image);
    Rejected(c, GL_INVALID_OPERATION);
    glCompressedTexImage2DARB(GL_TEXTURE_2D, 0, DXT5, 4, 4, 0, 16, NULL);
    Rejected(c, GL_INVALID_OPERATION);
    glCompressedTexImage2D(PROXY, 0, DXT1, 4, 4, 0, 8, NULL);
    Rejected(c, GL_INVALID_OPERATION);
    glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 4, 4, DXT1, 8, image);
    Rejected(c, GL_INVALID_OPERATION);
    glCompressedTexSubImage2DARB(GL_TEXTURE_2D, 0, 0, 0, 4, 4, DXT1, 8, image);
    Rejected(c, GL_INVALID_OPERATION);
    const ULONG queries = QueryCalls;
    glGetCompressedTexImage(GL_TEXTURE_2D, 0, image);
    Rejected(c, GL_INVALID_OPERATION);
    glGetCompressedTexImageARB(GL_TEXTURE_2D, 0, image);
    Rejected(c, GL_INVALID_OPERATION);
    assert(QueryCalls == queries);
    for (BYTE value : image)
        assert(value == 0xa5);
    glHint(COMPRESSION_HINT, NICEST);
    Rejected(c, GL_INVALID_ENUM);
    // TexImage2D formats stay exactly as before: invalid internal formats.
    for (GLint format : {(GLint)DXT1, (GLint)DXT5, 0x84e9, 0x84ee}) {
        glTexImage2D(GL_TEXTURE_2D, 0, format, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
        Rejected(c, GL_INVALID_VALUE);
        glTexImage2D(PROXY, 0, format, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        Rejected(c, GL_INVALID_VALUE);
    }

    /* A display list defers command errors in the existing error record. The
     * proxy executes immediately, so its error is immediate. */
    assert(JglListMode(GL_COMPILE));
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, DXT1, 4, 4, 0, 8, image);
    glCompressedTexSubImage2DARB(GL_TEXTURE_2D, 0, 0, 0, 4, 4, DXT1, 8, image);
    glHint(COMPRESSION_HINT, NICEST);
    glTexImage2D(GL_TEXTURE_2D, 0, DXT1, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
    assert(!c->Error);
    glCompressedTexImage2D(PROXY, 0, DXT1, 4, 4, 0, 8, NULL);
    assert(c->Error == GL_INVALID_OPERATION && c->Records == 4);
    c->Error = 0;
    Submit(c);
    assert(Log.size() == 4);
    ExpectDeferred(Log[0], GL_INVALID_OPERATION);
    ExpectDeferred(Log[1], GL_INVALID_OPERATION);
    ExpectDeferred(Log[2], GL_INVALID_ENUM);
    ExpectDeferred(Log[3], GL_INVALID_VALUE);
    assert(JglListMode(0));
    Close(c);
}

/* Negotiated lookup, extension strings, the hint and host-answered queries. */
static void Negotiation(void) {
    JGL_CONTEXT *c = Open(DG_CAP_GL_COMPRESSED_TEXTURES);
    assert(JglCompressedTexturesAvailable());
    assert(!strcmp((const char *)glGetString(GL_EXTENSIONS), COMPRESSED_NAMES));
    for (const char *name : Names)
        assert(wglGetProcAddress(name) == Marker);
    // Families stay independent.
    assert(!wglGetProcAddress("glBlendColorEXT") && !wglGetProcAddress("glActiveTextureARB"));
    glHint(COMPRESSION_HINT, NICEST);
    glHint(COMPRESSION_HINT, 0x1100);
    assert(c->Records == 2 && !c->Error);
    glHint(COMPRESSION_HINT, 0x1234);
    assert(c->Records == 2 && c->Error == GL_INVALID_ENUM);
    c->Error = 0;
    Submit(c);
    assert(Log.size() == 2);
    ExpectCall(Log[0], FEnum_glHint, {COMPRESSION_HINT, NICEST});
    ExpectCall(Log[1], FEnum_glHint, {COMPRESSION_HINT, 0x1100});
    {
        // Queries reach the host; the format list has four values.
        GLint count = 0, formats[5] = {0, 0, 0, 0, 0x5a5a5a5a}, size = 0;
        Log.clear();
        glGetIntegerv(NUM_COMPRESSED_TEXTURE_FORMATS, &count);
        glGetIntegerv(COMPRESSED_TEXTURE_FORMATS, formats);
        assert(count == 4 && formats[0] == (GLint)DXT1 && formats[1] == (GLint)DXT1A &&
               formats[2] == (GLint)DXT3 && formats[3] == (GLint)DXT5 && formats[4] == 0x5a5a5a5a);
        assert(LastQueryFunction == FEnum_glGetIntegerv &&
               LastQueryArgument == COMPRESSED_TEXTURE_FORMATS);
        LevelSize = 1234;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, TEXTURE_COMPRESSED_IMAGE_SIZE, &size);
        assert(size == 1234 && LevelQueries == 1 && !c->Error);
        HostInteger = NICEST;
        GLint hint = 0;
        glGetIntegerv(COMPRESSION_HINT, &hint);
        assert(hint == (GLint)NICEST && LastQueryArgument == COMPRESSION_HINT);
    }
    Close(c);

    // Appended after every established group, verbatim.
    Reset();
    HostCaps = DG_CAP_GL_MULTITEXTURE | DG_CAP_GL_FIXED_STATE | DG_CAP_GL_TEXTURE_CONTROL |
               DG_CAP_GL_COMPRESSED_TEXTURES;
    HostSecondaryWords = 3;
    HGLRC context = Create();
    assert(!strcmp(
        (const char *)glGetString(GL_EXTENSIONS),
        "GL_ARB_multitexture GL_ARB_texture_env_combine GL_EXT_texture_env_combine "
        "GL_ARB_texture_env_dot3 GL_EXT_secondary_color GL_EXT_separate_specular_color "
        "GL_EXT_blend_color GL_EXT_blend_minmax GL_EXT_blend_subtract GL_EXT_blend_func_separate "
        "GL_EXT_blend_equation_separate GL_EXT_stencil_wrap GL_ATI_separate_stencil "
        "GL_ARB_texture_border_clamp GL_ARB_texture_mirrored_repeat GL_EXT_texture_edge_clamp "
        "GL_SGIS_texture_edge_clamp GL_EXT_texture_filter_anisotropic GL_EXT_texture_lod_bias "
        "GL_ARB_point_parameters GL_EXT_point_parameters GL_ARB_point_sprite "
        "GL_ARB_texture_env_add GL_EXT_texture_env_add GL_ATI_texture_env_combine3 "
        "GL_ATI_texture_mirror_once GL_SGIS_generate_mipmap " COMPRESSED_NAMES));
    assert(wglMakeCurrent(NULL, NULL) && wglDeleteContext(context));
}

/* Every pre-validated rule: exact error, nothing sent, caller memory unread
 * (a bogus pointer would fault under the sanitizers). */
static void Validation(void) {
    JGL_CONTEXT *c = Open(DG_CAP_GL_COMPRESSED_TEXTURES);
    const void *bogus = (const void *)16;
    const struct {
        GLenum target;
        GLint level;
        GLenum format;
        GLsizei width, height;
        GLint border;
        GLsizei size;
        GLenum error;
    } images[] = {
        {GL_TEXTURE_1D, 0, DXT1, 4, 4, 0, 8, GL_INVALID_ENUM},
        {0x8513 /* CUBE_MAP */, 0, DXT1, 4, 4, 0, 8, GL_INVALID_ENUM},
        {0x8063 /* PROXY_1D */, 0, DXT1, 4, 4, 0, 8, GL_INVALID_ENUM},
        {GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, 64, GL_INVALID_ENUM},
        {GL_TEXTURE_2D, 0, 0x84ed /* generic */, 4, 4, 0, 8, GL_INVALID_ENUM},
        {GL_TEXTURE_2D, 0, 0x83f4, 4, 4, 0, 16, GL_INVALID_ENUM},
        {GL_TEXTURE_2D, 0, 0x83ef, 4, 4, 0, 8, GL_INVALID_ENUM},
        {GL_TEXTURE_2D, 0, DXT1, 4, 4, 1, 8, GL_INVALID_VALUE},
        {GL_TEXTURE_2D, 0, DXT1, 4, 4, -1, 8, GL_INVALID_VALUE},
        {GL_TEXTURE_2D, -1, DXT1, 4, 4, 0, 8, GL_INVALID_VALUE},
        {GL_TEXTURE_2D, DG_GL_MAX_TEXTURE_LEVEL + 1, DXT1, 1, 1, 0, 8, GL_INVALID_VALUE},
        {GL_TEXTURE_2D, 0, DXT1, -1, 4, 0, 8, GL_INVALID_VALUE},
        {GL_TEXTURE_2D, 0, DXT1, 4, -4, 0, 8, GL_INVALID_VALUE},
        {GL_TEXTURE_2D, 0, DXT1, DG_GL_MAX_TEXTURE_DIMENSION + 1, 4, 0, 4104, GL_INVALID_VALUE},
        {GL_TEXTURE_2D, 1, DXT1, 4, DG_GL_MAX_TEXTURE_DIMENSION / 2 + 1, 0, 2056, GL_INVALID_VALUE},
        {GL_TEXTURE_2D, DG_GL_MAX_TEXTURE_LEVEL, DXT1, 2, 1, 0, 8, GL_INVALID_VALUE},
        {GL_TEXTURE_2D, 0, DXT1, 8, 8, 0, 31, GL_INVALID_VALUE},
        {GL_TEXTURE_2D, 0, DXT1, 8, 8, 0, 33, GL_INVALID_VALUE},
        {GL_TEXTURE_2D, 0, DXT1, 8, 8, 0, -1, GL_INVALID_VALUE},
        {GL_TEXTURE_2D, 0, DXT3, 8, 8, 0, 32, GL_INVALID_VALUE},
        {GL_TEXTURE_2D, 0, DXT1A, 5, 5, 0, 8, GL_INVALID_VALUE},
        {GL_TEXTURE_2D, 0, DXT5, 0, 4, 0, 16, GL_INVALID_VALUE},
    };
    for (const auto &item : images) {
        glCompressedTexImage2D(item.target, item.level, item.format, item.width, item.height,
                               item.border, item.size, bogus);
        Rejected(c, item.error);
        glCompressedTexImage2DARB(item.target, item.level, item.format, item.width, item.height,
                                  item.border, item.size, bogus);
        Rejected(c, item.error);
    }
    // Proxy validation is the same rule set, raised immediately.
    glCompressedTexImage2D(PROXY, 0, DXT1, DG_GL_MAX_TEXTURE_DIMENSION * 2, 4, 0, 8192, bogus);
    Rejected(c, GL_INVALID_VALUE);
    glCompressedTexImage2D(PROXY, 0, GL_RGBA, 4, 4, 0, 8, bogus);
    Rejected(c, GL_INVALID_ENUM);

    const struct {
        GLenum target;
        GLint level, x, y;
        GLsizei width, height;
        GLenum format;
        GLsizei size;
        const void *data;
        GLenum error;
    } subimages[] = {
        {PROXY, 0, 0, 0, 4, 4, DXT1, 8, bogus, GL_INVALID_ENUM},
        {GL_TEXTURE_1D, 0, 0, 0, 4, 4, DXT1, 8, bogus, GL_INVALID_ENUM},
        {GL_TEXTURE_2D, 0, 0, 0, 4, 4, 0x84ee, 8, bogus, GL_INVALID_ENUM},
        {GL_TEXTURE_2D, 0, 0, 0, 4, 4, GL_RGBA, 64, bogus, GL_INVALID_ENUM},
        {GL_TEXTURE_2D, 0, 2, 0, 4, 4, DXT1, 8, bogus, GL_INVALID_OPERATION},
        {GL_TEXTURE_2D, 0, 0, 6, 4, 4, DXT5, 16, bogus, GL_INVALID_OPERATION},
        {GL_TEXTURE_2D, 0, 1, 1, 4, 4, DXT5, 16, bogus, GL_INVALID_OPERATION},
        {GL_TEXTURE_2D, 0, -4, 0, 4, 4, DXT1, 8, bogus, GL_INVALID_VALUE},
        {GL_TEXTURE_2D, 0, 0, -1, 4, 4, DXT1, 8, bogus, GL_INVALID_VALUE},
        {GL_TEXTURE_2D, -1, 0, 0, 4, 4, DXT1, 8, bogus, GL_INVALID_VALUE},
        {GL_TEXTURE_2D, DG_GL_MAX_TEXTURE_LEVEL + 1, 0, 0, 1, 1, DXT1, 8, bogus, GL_INVALID_VALUE},
        {GL_TEXTURE_2D, 0, 0, 0, -4, 4, DXT1, 8, bogus, GL_INVALID_VALUE},
        {GL_TEXTURE_2D, 0, 0, 0, 4, -1, DXT1, 8, bogus, GL_INVALID_VALUE},
        // No level of the index can hold the rectangle; sizes stay bounded.
        {GL_TEXTURE_2D, 0, DG_GL_MAX_TEXTURE_DIMENSION, 0, 4, 4, DXT1, 8, bogus, GL_INVALID_VALUE},
        {GL_TEXTURE_2D, 1, 1020, 0, 8, 4, DXT1, 16, bogus, GL_INVALID_VALUE},
        {GL_TEXTURE_2D, 0, 0, 4, 4, 0x7ffffff0, DXT1, 8, bogus, GL_INVALID_VALUE},
        {GL_TEXTURE_2D, 0, 0x7ffffffc, 0, 4, 4, DXT1, 8, bogus, GL_INVALID_VALUE},
        {GL_TEXTURE_2D, 0, 0, 0, 8, 4, DXT1, 8, bogus, GL_INVALID_VALUE},
        {GL_TEXTURE_2D, 0, 0, 0, 8, 4, DXT1, -16, bogus, GL_INVALID_VALUE},
        {GL_TEXTURE_2D, 0, 0, 0, 0, 4, DXT1, 8, bogus, GL_INVALID_VALUE},
        {GL_TEXTURE_2D, 0, 0, 0, 4, 4, DXT1, 8, NULL, GL_INVALID_VALUE},
    };
    for (const auto &item : subimages) {
        glCompressedTexSubImage2D(item.target, item.level, item.x, item.y, item.width, item.height,
                                  item.format, item.size, item.data);
        Rejected(c, item.error);
        glCompressedTexSubImage2DARB(item.target, item.level, item.x, item.y, item.width,
                                     item.height, item.format, item.size, item.data);
        Rejected(c, item.error);
    }
    // An empty rectangle is a no-op, without reading data.
    glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 4, 4, 0, 4, DXT1, 0, bogus);
    glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 4, 4, 4, 0, DXT5, 0, NULL);
    Rejected(c, GL_NO_ERROR);

    // Readback rejections never query the host.
    BYTE image[16];
    memset(image, 0xa5, sizeof(image));
    const ULONG queries = QueryCalls;
    glGetCompressedTexImage(PROXY, 0, image);
    Rejected(c, GL_INVALID_ENUM);
    glGetCompressedTexImageARB(GL_TEXTURE_1D, 0, image);
    Rejected(c, GL_INVALID_ENUM);
    glGetCompressedTexImage(GL_TEXTURE_2D, -1, image);
    Rejected(c, GL_INVALID_VALUE);
    glGetCompressedTexImage(GL_TEXTURE_2D, DG_GL_MAX_TEXTURE_LEVEL + 1, image);
    Rejected(c, GL_INVALID_VALUE);
    glGetCompressedTexImage(GL_TEXTURE_2D, 0, NULL);
    Rejected(c, GL_NO_ERROR);
    assert(QueryCalls == queries);
    for (BYTE value : image)
        assert(value == 0xa5);

    // None is legal inside Begin/End.
    Clear(c);
    glBegin(GL_TRIANGLES);
    assert(c->Records == 1);
    c->Records = 0;
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, DXT1, 4, 4, 0, 8, bogus);
    assert(c->Error == GL_INVALID_OPERATION && !c->Records);
    c->Error = 0;
    glCompressedTexImage2D(PROXY, 0, DXT1, 4, 4, 0, 8, bogus);
    assert(c->Error == GL_INVALID_OPERATION && !c->Records);
    c->Error = 0;
    glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 4, 4, DXT1, 8, bogus);
    assert(c->Error == GL_INVALID_OPERATION && !c->Records);
    c->Error = 0;
    glGetCompressedTexImage(GL_TEXTURE_2D, 0, image);
    assert(c->Error == GL_INVALID_OPERATION && !c->Records && QueryCalls == queries);
    c->Error = 0;
    glEnd();
    assert(!c->InBegin && c->Records == 1 && !c->Error);
    Close(c);
}

/* Records that fit carry the exact payload; caller memory is copied. */
static void SingleRecords(void) {
    JGL_CONTEXT *c = Open(DG_CAP_GL_COMPRESSED_TEXTURES);
    BYTE caller[64];
    memcpy(caller, Source, sizeof(caller));
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, DXT5, 8, 8, 0, 64, caller);
    memset(caller, 0xee, sizeof(caller)); /* still before submission */
    glCompressedTexImage2DARB(GL_TEXTURE_2D, 3, DXT1, 5, 3, 0, 16, Source + 64);
    glCompressedTexImage2D(GL_TEXTURE_2D, DG_GL_MAX_TEXTURE_LEVEL, DXT1A, 1, 1, 0, 8, Source);
    glCompressedTexImage2D(GL_TEXTURE_2D, 1, DXT3, 1024, 0, 0, 0, (const void *)16);
    glCompressedTexSubImage2D(GL_TEXTURE_2D, 2, 4, 8, 8, 4, DXT1, 16, Source + 80);
    glCompressedTexSubImage2DARB(GL_TEXTURE_2D, 0, 2044, 2040, 4, 8, DXT5, 32, Source + 96);
    glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 12, 0, 6, 3, DXT3, 32, Source + 128);
    assert(c->Records == 7 && !c->Error);
    Submit(c);
    assert(Log.size() == 7);
    ExpectData(Log[0], FEnum_glCompressedTexImage2D, {GL_TEXTURE_2D, 0, DXT5, 8, 8, 0, 64}, Source,
               64);
    ExpectData(Log[1], FEnum_glCompressedTexImage2D, {GL_TEXTURE_2D, 3, DXT1, 5, 3, 0, 16},
               Source + 64, 16);
    ExpectData(Log[2], FEnum_glCompressedTexImage2D,
               {GL_TEXTURE_2D, DG_GL_MAX_TEXTURE_LEVEL, DXT1A, 1, 1, 0, 8}, Source, 8);
    // An empty level: no payload, never reading the pointer.
    ExpectData(Log[3], FEnum_glCompressedTexImage2D, {GL_TEXTURE_2D, 1, DXT3, 1024, 0, 0, 0}, NULL,
               0);
    ExpectData(Log[4], FEnum_glCompressedTexSubImage2D, {GL_TEXTURE_2D, 2, 4, 8, 8, 4, DXT1, 16},
               Source + 80, 16);
    ExpectData(Log[5], FEnum_glCompressedTexSubImage2D,
               {GL_TEXTURE_2D, 0, 2044, 2040, 4, 8, DXT5, 32}, Source + 96, 32);
    // Partial edge blocks are the host's to judge against the level.
    ExpectData(Log[6], FEnum_glCompressedTexSubImage2D, {GL_TEXTURE_2D, 0, 12, 0, 6, 3, DXT3, 32},
               Source + 128, 32);

    // No data defines zero storage with no payload, at any size; a proxy
    // never carries or reads one.
    Clear(c);
    glCompressedTexImage2D(GL_TEXTURE_2D, 2, DXT3, 64, 64, 0, 4096, NULL);
    glCompressedTexImage2DARB(GL_TEXTURE_2D, 0, DXT5, DG_GL_MAX_TEXTURE_DIMENSION,
                              DG_GL_MAX_TEXTURE_DIMENSION, 0, 0x400000, NULL);
    glCompressedTexImage2D(PROXY, 0, DXT1, 16, 16, 0, 128, (const void *)16);
    glCompressedTexImage2DARB(PROXY, 1, DXT5, 1024, 1024, 0, 0x100000, (const void *)16);
    assert(c->Records == 4 && !c->Error);
    Submit(c);
    assert(Log.size() == 4);
    ExpectData(Log[0], FEnum_glCompressedTexImage2D, {GL_TEXTURE_2D, 2, DXT3, 64, 64, 0, 4096},
               NULL, 0);
    ExpectData(Log[1], FEnum_glCompressedTexImage2D,
               {GL_TEXTURE_2D, 0, DXT5, DG_GL_MAX_TEXTURE_DIMENSION, DG_GL_MAX_TEXTURE_DIMENSION, 0,
                0x400000},
               NULL, 0);
    ExpectData(Log[2], FEnum_glCompressedTexImage2D, {PROXY, 0, DXT1, 16, 16, 0, 128}, NULL, 0);
    ExpectData(Log[3], FEnum_glCompressedTexImage2D, {PROXY, 1, DXT5, 1024, 1024, 0, 0x100000},
               NULL, 0);
    // The fixture validated framing and zero DATA padding on submission.
    assert(!c->Failed && !c->Error);
    Close(c);
}

/* Larger images: one allocation record, then whole block-row bands. */
static void Bands(void) {
    // 64-word packets: 188 bytes fit an image record, 184 a sub-image record.
    JGL_CONTEXT *c = Open(DG_CAP_GL_COMPRESSED_TEXTURES, SMALL_PACKET);
    assert(JglMaxDataBytes(7) == 188 && JglMaxDataBytes(8) == 184);
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, DXT1, 92, 4, 0, 184, Source);
    Submit(c);
    assert(Log.size() == 1);
    ExpectData(Log[0], FEnum_glCompressedTexImage2D, {GL_TEXTURE_2D, 0, DXT1, 92, 4, 0, 184},
               Source, 184);

    // 10 x 5 DXT1 blocks: two block rows (160 bytes) per band; the last
    // band is one block row of two texel rows.
    Clear(c);
    glCompressedTexImage2D(GL_TEXTURE_2D, 1, DXT1, 38, 18, 0, 400, Source);
    assert(!c->Error);
    Submit(c);
    assert(Log.size() == 4);
    ExpectData(Log[0], FEnum_glCompressedTexImage2D, {GL_TEXTURE_2D, 1, DXT1, 38, 18, 0, 400}, NULL,
               0);
    ExpectData(Log[1], FEnum_glCompressedTexSubImage2D, {GL_TEXTURE_2D, 1, 0, 0, 38, 8, DXT1, 160},
               Source, 160);
    ExpectData(Log[2], FEnum_glCompressedTexSubImage2D, {GL_TEXTURE_2D, 1, 0, 8, 38, 8, DXT1, 160},
               Source + 160, 160);
    ExpectData(Log[3], FEnum_glCompressedTexSubImage2D, {GL_TEXTURE_2D, 1, 0, 16, 38, 2, DXT1, 80},
               Source + 320, 80);
    Reassembles(1, 1, 0, 0, 38, 18, DXT1, 8, Source, 184);

    // One block row (192 bytes) exceeds a record: 23 blocks, then one.
    Clear(c);
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, DXT1A, 96, 4, 0, 192, Source);
    Submit(c);
    assert(Log.size() == 3);
    ExpectData(Log[0], FEnum_glCompressedTexImage2D, {GL_TEXTURE_2D, 0, DXT1A, 96, 4, 0, 192}, NULL,
               0);
    ExpectData(Log[1], FEnum_glCompressedTexSubImage2D, {GL_TEXTURE_2D, 0, 0, 0, 92, 4, DXT1A, 184},
               Source, 184);
    ExpectData(Log[2], FEnum_glCompressedTexSubImage2D, {GL_TEXTURE_2D, 0, 92, 0, 4, 4, DXT1A, 8},
               Source + 184, 8);

    // Sub-images band the same way at their own offsets.
    Clear(c);
    glCompressedTexSubImage2D(GL_TEXTURE_2D, 2, 8, 12, 40, 10, DXT3, 480, Source + 4);
    Submit(c);
    assert(Log.size() == 3);
    ExpectData(Log[0], FEnum_glCompressedTexSubImage2D, {GL_TEXTURE_2D, 2, 8, 12, 40, 4, DXT3, 160},
               Source + 4, 160);
    ExpectData(Log[1], FEnum_glCompressedTexSubImage2D, {GL_TEXTURE_2D, 2, 8, 16, 40, 4, DXT3, 160},
               Source + 164, 160);
    ExpectData(Log[2], FEnum_glCompressedTexSubImage2D, {GL_TEXTURE_2D, 2, 8, 20, 40, 2, DXT3, 160},
               Source + 324, 160);

    // A large level without data stays one allocation record.
    Clear(c);
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, DXT5, 256, 256, 0, 0x10000, NULL);
    Submit(c);
    assert(Log.size() == 1);
    ExpectData(Log[0], FEnum_glCompressedTexImage2D, {GL_TEXTURE_2D, 0, DXT5, 256, 256, 0, 0x10000},
               NULL, 0);
    Close(c);

    // The negotiated minimum packet forces horizontal chunks of three DXT5
    // blocks (48 of 56 bytes); the last chunk of a row is narrower.
    c = Open(DG_CAP_GL_COMPRESSED_TEXTURES, TINY_PACKET);
    assert(JglMaxDataBytes(7) == 60 && JglMaxDataBytes(8) == 56);
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, DXT5, 62, 6, 0, 512, Source);
    Submit(c);
    assert(Log.size() == 13 && !c->Error);
    ExpectData(Log[0], FEnum_glCompressedTexImage2D, {GL_TEXTURE_2D, 0, DXT5, 62, 6, 0, 512}, NULL,
               0);
    for (ULONG row = 0; row < 2; ++row)
        for (ULONG chunk = 0; chunk < 6; ++chunk) {
            const ULONG blocks = chunk < 5 ? 3 : 1;
            ExpectData(Log[1 + row * 6 + chunk], FEnum_glCompressedTexSubImage2D,
                       {GL_TEXTURE_2D, 0, chunk * 12, row * 4, chunk < 5 ? 12U : 2U, row ? 2U : 4U,
                        DXT5, blocks * 16},
                       Source + row * 256 + chunk * 48, blocks * 16);
        }
    Reassembles(1, 0, 0, 0, 62, 6, DXT5, 16, Source, 56);
    // 60 bytes still fit a seven-word image record: one record.
    Clear(c);
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, DXT1, 28, 4, 0, 56, Source);
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, DXT1, 32, 4, 0, 64, Source);
    Submit(c);
    assert(Log.size() == 4);
    ExpectData(Log[0], FEnum_glCompressedTexImage2D, {GL_TEXTURE_2D, 0, DXT1, 28, 4, 0, 56}, Source,
               56);
    ExpectData(Log[1], FEnum_glCompressedTexImage2D, {GL_TEXTURE_2D, 0, DXT1, 32, 4, 0, 64}, NULL,
               0);
    Reassembles(2, 0, 0, 0, 32, 4, DXT1, 8, Source, 56);
    // Sub-image chunks at an offset, partial on both edges, every format.
    for (ULONG format : {DXT1, DXT1A, DXT3, DXT5}) {
        const ULONG block = format < DXT3 ? 8 : 16;
        const ULONG bytes = 13 * 4 * block; /* 50 x 15 texels */
        Clear(c);
        glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 100, 36, 50, 15, format, bytes, Source + 8);
        Submit(c);
        assert(!c->Error);
        Reassembles(0, 0, 100, 36, 50, 15, format, block, Source + 8, 56);
    }
    Close(c);

    // An image record has one scalar word less than a band: with 33-word
    // packets 64 bytes fit the image record (64) but not a band (60).
    c = Open(DG_CAP_GL_COMPRESSED_TEXTURES, 33 * 4);
    assert(JglMaxDataBytes(7) == 64 && JglMaxDataBytes(8) == 60);
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, DXT1, 32, 4, 0, 64, Source);
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, DXT3, 12, 8, 0, 96, Source);
    Submit(c);
    assert(Log.size() == 4 && !c->Error);
    ExpectData(Log[0], FEnum_glCompressedTexImage2D, {GL_TEXTURE_2D, 0, DXT1, 32, 4, 0, 64}, Source,
               64);
    ExpectData(Log[1], FEnum_glCompressedTexImage2D, {GL_TEXTURE_2D, 0, DXT3, 12, 8, 0, 96}, NULL,
               0);
    ExpectData(Log[2], FEnum_glCompressedTexSubImage2D, {GL_TEXTURE_2D, 0, 0, 0, 12, 4, DXT3, 48},
               Source, 48);
    ExpectData(Log[3], FEnum_glCompressedTexSubImage2D, {GL_TEXTURE_2D, 0, 0, 4, 12, 4, DXT3, 48},
               Source + 48, 48);
    Close(c);
}

/* GetCompressedTexImage: level checks, then bounded word chunks. */
static void Readback(void) {
    BYTE image[2048];
    JGL_CONTEXT *c = Open(DG_CAP_GL_COMPRESSED_TEXTURES);
    for (ULONG n = 0; n < 2; ++n) {
        // Whole image in one query with the full result buffer.
        memset(image, 0xa5, sizeof(image));
        ReadLevel = 2;
        LevelSize = 1288;
        NextWord = LevelQueries = 0;
        Chunks.clear();
        (n ? glGetCompressedTexImageARB : glGetCompressedTexImage)(GL_TEXTURE_2D, 2, image);
        assert(!c->Error && LevelQueries == 2 && Chunks.size() == 1 && Chunks[0].first == 0 &&
               Chunks[0].second == 322);
        assert(!memcmp(image, Source, 1288));
        for (ULONG i = 1288; i < sizeof(image); ++i)
            assert(image[i] == 0xa5);
    }
    // Not a compressed level, or an impossible size: no image query.
    for (GLint compressed : {0, 1}) {
        for (GLint size :
             {64, 6, -4, (DG_GL_MAX_TEXTURE_DIMENSION + 2) * (DG_GL_MAX_TEXTURE_DIMENSION + 2) + 4,
              0x7ffffffc}) {
            if (compressed && size == 64)
                continue;
            memset(image, 0xa5, sizeof(image));
            LevelCompressed = compressed;
            LevelSize = size;
            ReadLevel = 0;
            Chunks.clear();
            glGetCompressedTexImage(GL_TEXTURE_2D, 0, image);
            assert(c->Error == GL_INVALID_OPERATION && Chunks.empty());
            c->Error = 0;
            for (BYTE value : image)
                assert(value == 0xa5);
        }
    }
    LevelCompressed = 1;
    LevelSize = 0;
    glGetCompressedTexImage(GL_TEXTURE_2D, 0, image);
    assert(!c->Error && Chunks.empty() && image[0] == 0xa5);
    Close(c);

    // The legacy result size gives 128-word chunks; the last is shorter.
    c = Open(DG_CAP_GL_COMPRESSED_TEXTURES, DG_ESCAPE_MAX_BYTES, DG_ESCAPE_LEGACY_RESULT_BYTES);
    memset(image, 0xa5, sizeof(image));
    ReadLevel = 1;
    LevelSize = 1288;
    NextWord = 0;
    Chunks.clear();
    glGetCompressedTexImageARB(GL_TEXTURE_2D, 1, image);
    assert(!c->Error && Chunks.size() == 3);
    assert(Chunks[0] == std::make_pair(0U, 128U) && Chunks[1] == std::make_pair(128U, 128U) &&
           Chunks[2] == std::make_pair(256U, 66U));
    assert(!memcmp(image, Source, 1288) && image[1288] == 0xa5);
    // A short chunk fails the read.
    NextWord = 0;
    Chunks.clear();
    ShortChunk = 1;
    glGetCompressedTexImage(GL_TEXTURE_2D, 1, image);
    assert(c->Error == GL_INVALID_OPERATION && Chunks.size() == 2);
    c->Error = 0;
    ShortChunk = ~0u;

    // Readback is immediate inside a display list: errors and queries alike.
    assert(JglListMode(GL_COMPILE));
    Clear(c);
    glGetCompressedTexImage(PROXY, 0, image);
    assert(c->Error == GL_INVALID_ENUM && !c->Records);
    c->Error = 0;
    NextWord = 0;
    Chunks.clear();
    memset(image, 0, sizeof(image));
    glGetCompressedTexImage(GL_TEXTURE_2D, 1, image);
    assert(!c->Error && Chunks.size() == 3 && !memcmp(image, Source, 1288));
    assert(JglListMode(0));
    Close(c);
}

/* TexImage2D accepts compressed internal formats only when negotiated and
 * only for 2D; the uncompressed upload itself is unchanged. */
static void UncompressedUploads(void) {
    const BYTE pixels[64] = {1, 2, 3, 4, 5, 6, 7, 8};
    for (ULONG images = 0; images < 2; ++images) {
        // Ordinary images need only the compressed contract; proxies keep
        // requiring the extended image contract as before.
        JGL_CONTEXT *c =
            Open(DG_CAP_GL_COMPRESSED_TEXTURES | (images ? DG_CAP_GL_TEXTURE_IMAGES : 0));
        for (GLint format : {(GLint)DXT1, (GLint)DXT1A, (GLint)DXT3, (GLint)DXT5, 0x84e9, 0x84ea,
                             0x84eb, 0x84ec, 0x84ed, 0x84ee}) {
            Clear(c);
            glTexImage2D(GL_TEXTURE_2D, 0, format, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            assert(!c->Error);
            glTexImage2D(PROXY, 0, format, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
            assert(c->Error == (images ? GL_NO_ERROR : GL_INVALID_OPERATION));
            c->Error = 0;
            Submit(c);
            assert(Log.size() == 1 + images);
            ExpectData(Log[0], FEnum_glTexImage2D,
                       {GL_TEXTURE_2D, 0, (ULONG)format, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE},
                       pixels, 64);
            if (images)
                ExpectData(Log[1], FEnum_glTexImage2D,
                           {PROXY, 0, (ULONG)format, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE}, NULL, 0);
            Log.clear();
            glTexImage1D(GL_TEXTURE_1D, 0, format, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            Rejected(c, GL_INVALID_VALUE);
        }
        glTexImage2D(GL_TEXTURE_2D, 0, 0x84ef, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        Rejected(c, GL_INVALID_VALUE);
        Close(c);
    }

    /* Split uploads into a compressed level stay block-aligned on the real
     * record path: 60-row bands, and padded RGB rows gathered into one tile. */
    static BYTE big[256 * 256 * 4];
    for (ULONG i = 0; i < sizeof(big); ++i)
        big[i] = (BYTE)(i * 7 + (i >> 9));
    JGL_CONTEXT *c = Open(DG_CAP_GL_COMPRESSED_TEXTURES);
    glTexImage2D(GL_TEXTURE_2D, 0, DXT5, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, big);
    glTexImage2D(GL_TEXTURE_2D, 1, DXT1, 10, 5, 0, GL_RGB, GL_UNSIGNED_BYTE, big);
    assert(!c->Error);
    Submit(c);
    assert(Log.size() == 8);
    ExpectData(Log[0], FEnum_glTexImage2D,
               {GL_TEXTURE_2D, 0, DXT5, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE}, NULL, 0);
    for (ULONG band = 0; band < 4; ++band)
        ExpectData(Log[1 + band], FEnum_glTexSubImage2D,
                   {GL_TEXTURE_2D, 0, 0, band * 60, 256, 60, GL_RGBA, GL_UNSIGNED_BYTE},
                   big + band * 61440, 61440);
    ExpectData(Log[5], FEnum_glTexSubImage2D,
               {GL_TEXTURE_2D, 0, 0, 240, 256, 16, GL_RGBA, GL_UNSIGNED_BYTE}, big + 245760, 16384);
    ExpectData(Log[6], FEnum_glTexImage2D,
               {GL_TEXTURE_2D, 1, DXT1, 10, 5, 0, GL_RGB, GL_UNSIGNED_BYTE}, NULL, 0);
    BYTE rows[150];
    for (ULONG row = 0; row < 5; ++row)
        memcpy(rows + row * 30, big + row * 32, 30); /* 30 of 32 bytes per row */
    ExpectData(Log[7], FEnum_glTexSubImage2D,
               {GL_TEXTURE_2D, 1, 0, 0, 10, 5, GL_RGB, GL_UNSIGNED_BYTE}, rows, 150);
    Close(c);
}

/* Compiling a list: valid records (allocation and bands too) are recorded for
 * the host to capture; command errors are deferred; the proxy is immediate. */
static void Lists(void) {
    JGL_CONTEXT *c = Open(DG_CAP_GL_COMPRESSED_TEXTURES, SMALL_PACKET);
    assert(JglListMode(GL_COMPILE));
    Clear(c);
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, DXT1, 8, 8, 0, 32, Source);
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, 0x84ed, 8, 8, 0, 32, Source);
    glCompressedTexImage2D(GL_TEXTURE_2D, 0, DXT1, 8, 8, 0, 31, Source);
    glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 2, 0, 4, 4, DXT1, 8, Source);
    glCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 4, 4, DXT1, 8, NULL);
    glCompressedTexImage2D(GL_TEXTURE_2D, 1, DXT1, 38, 18, 0, 400, Source);
    assert(!c->Error);
    glCompressedTexImage2D(PROXY, 0, DXT1, 8, 8, 1, 32, NULL);
    assert(c->Error == GL_INVALID_VALUE);
    c->Error = 0;
    glCompressedTexImage2D(PROXY, 0, DXT1, 8, 8, 0, 32, NULL);
    glHint(COMPRESSION_HINT, NICEST);
    assert(!c->Error);
    Submit(c);
    assert(Log.size() == 11);
    ExpectData(Log[0], FEnum_glCompressedTexImage2D, {GL_TEXTURE_2D, 0, DXT1, 8, 8, 0, 32}, Source,
               32);
    ExpectDeferred(Log[1], GL_INVALID_ENUM);
    ExpectDeferred(Log[2], GL_INVALID_VALUE);
    ExpectDeferred(Log[3], GL_INVALID_OPERATION);
    ExpectDeferred(Log[4], GL_INVALID_VALUE);
    ExpectData(Log[5], FEnum_glCompressedTexImage2D, {GL_TEXTURE_2D, 1, DXT1, 38, 18, 0, 400}, NULL,
               0);
    for (ULONG band = 0; band < 3; ++band)
        assert(Log[6 + band][8] == FEnum_glCompressedTexSubImage2D);
    ExpectData(Log[9], FEnum_glCompressedTexImage2D, {PROXY, 0, DXT1, 8, 8, 0, 32}, NULL, 0);
    ExpectCall(Log[10], FEnum_glHint, {COMPRESSION_HINT, NICEST});
    // The bands are exactly those of immediate execution.
    Log.erase(Log.begin() + 9, Log.end());
    Reassembles(6, 1, 0, 0, 38, 18, DXT1, 8, Source, 184);
    assert(JglListMode(0));
    Close(c);
}

int main() {
    for (ULONG i = 0; i < sizeof(Source); ++i)
        Source[i] = (BYTE)(i * 131 + (i >> 8) * 7 + 3);
    Unnegotiated();
    Negotiation();
    Validation();
    SingleRecords();
    Bands();
    Readback();
    UncompressedUploads();
    Lists();
    Reset();
    assert(!Allocations);
    puts("PASS compressed textures actual frontend: negotiation and hidden names; local "
         "validation with nothing sent; exact image/sub-image records and payloads; allocation "
         "plus whole block-row bands and block chunks; proxies; chunked readback; compressed "
         "TexImage2D formats; deferred list errors");
    return 0;
}
