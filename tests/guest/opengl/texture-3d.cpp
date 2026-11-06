/* SPDX-License-Identifier: GPL-2.0-or-later
 * DG_CAP_GL_TEXTURE_3D through the actual frontend on the strict transport
 * fixture: negotiation and the unchanged frontend without it, local
 * validation (nothing the host would reject as malformed may leave the
 * frontend), exact image records, UNPACK_IMAGE_HEIGHT/SKIP_IMAGES and the
 * other unpack state resolved into tight payloads, packed types, bounded
 * slabs, subimage splitting checks, copies, readback through PACK image
 * state, host-answered queries and display lists. */
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

#define VOLUME 0x806fU
#define PROXY_VOLUME 0x8070U
#define TEXTURE_DEPTH 0x8071U
#define WRAP_R 0x8072U
#define MAX_VOLUME 0x8073U
#define BINDING_VOLUME 0x806aU
#define PACK_SKIP_IMAGES 0x806bU
#define PACK_IMAGE_HEIGHT 0x806cU
#define UNPACK_SKIP_IMAGES 0x806dU
#define UNPACK_IMAGE_HEIGHT 0x806eU
#define CUBE 0x8513U
#define MIN_FILTER 0x2801U
#define CLAMP_TO_EDGE 0x812fU
#define RGBA8 0x8058U
#define DXT1 0x83f0U
#define BGRA 0x80e1U
#define FLOAT 0x1406U
#define BITMAP 0x1a00U
#define COLOR_INDEX 0x1900U
#define PACKED_8888 0x8035U
#define PACKED_8888_REV 0x8367U
#define PACKED_565 0x8363U
#define SMALL_PACKET 256U /* 64 words: 180 payload bytes per image, 176 per subimage */
#define TEXTURE_3D_NAME "GL_EXT_texture3D"

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
/* The same, after the level queries a split subimage issues first. */
static void RejectedAfterQueries(JGL_CONTEXT *c, GLenum error) {
    assert(c->Error == error && !c->Records && !c->Used);
    Submit(c);
    assert(!Log.empty());
    for (const auto &r : Log)
        assert(r[0] == DG_GL_QUERY && r[8] == FEnum_glGetTexLevelParameteriv);
    Log.clear();
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

/* Host answers: one level's size, state and the level's pixels. */
static GLint LevelWidth, LevelHeight, LevelDepth;
static std::vector<std::vector<ULONG>> Queries; /* function, three arguments */
static BOOL Answer(const ULONG *r, DG_ESCAPE_REPLY *reply, ULONG capacity) {
    GLint value;
    Queries.push_back({r[8], r[9], r[10], r[11]});
    switch (r[8]) {
        case FEnum_glGetTexLevelParameteriv:
            value = r[11] == GL_TEXTURE_WIDTH    ? LevelWidth
                    : r[11] == GL_TEXTURE_HEIGHT ? LevelHeight
                    : r[11] == TEXTURE_DEPTH     ? LevelDepth
                                                 : 0;
            break;
        case FEnum_glGetTexParameteriv:
            value = CLAMP_TO_EDGE;
            break;
        case FEnum_glGetIntegerv:
            value = r[9] == BINDING_VOLUME ? 5 : DG_GL_MAX_3D_TEXTURE_SIZE;
            break;
        case FEnum_glIsEnabled:
            assert(capacity >= 1);
            *(BYTE *)(reply + 1) = 1;
            reply->ResultBytes = 1;
            reply->ResultType = DG_GL_RESULT_BOOL;
            return TRUE;
        case FEnum_glGetTexImage: {
            /* Canonical RGBA8 pixels: the pixel number in x, y, z order. */
            const ULONG count = r[10] >> DG_GL_TEXTURE_READ_COUNT_SHIFT, first = r[11];
            assert(r[9] == VOLUME && count && capacity == count * 4);
            for (ULONG i = 0; i < count; ++i) {
                const ULONG pixel = 0x7f | ((first + i) << 8) | 0xff000000U;
                memcpy((BYTE *)(reply + 1) + i * 4, &pixel, 4);
            }
            reply->ResultBytes = count * 4;
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
    LevelWidth = LevelHeight = LevelDepth = 4;
    Queries.clear();
    JGL_CONTEXT *c = Lookup(Create());
    Clear(c);
    return c;
}
static void Close(JGL_CONTEXT *c) {
    assert(wglMakeCurrent(NULL, NULL) && wglDeleteContext((HGLRC)(ULONG_PTR)c->Id));
    Log.clear();
}
static const ULONG AllCaps = DG_CAP_GL_MULTITEXTURE | DG_CAP_GL_FIXED_STATE |
                             DG_CAP_GL_TEXTURE_CONTROL | DG_CAP_GL_COMPRESSED_TEXTURES |
                             DG_CAP_GL_FOG_COORD | DG_CAP_GL_TEXTURE_CUBE_MAP |
                             DG_CAP_GL_FRAGMENT_PROGRAM | DG_CAP_GL_VERTEX_PROGRAM |
                             DG_CAP_GL_TEXTURE_IMAGES | DG_CAP_GL_BULK_READBACK;

/* Without the capability nothing new leaves the frontend, and every 3D enum
 * keeps the local error it had before. */
static void Unnegotiated(void) {
    JGL_CONTEXT *c = Open(AllCaps);
    const void *bogus = (const void *)16;
    BYTE image[64];
    memset(image, 0xa5, sizeof(image));
    assert(!JglTexture3DAvailable());
    assert(!strstr((const char *)glGetString(GL_EXTENSIONS), "texture3D"));
    for (const char *name : {"glTexImage3DEXT", "glTexImage3D", "glTexSubImage3DEXT",
                             "glTexSubImage3D", "glCopyTexSubImage3DEXT", "glCopyTexSubImage3D"})
        assert(!wglGetProcAddress(name));
    assert(wglGetProcAddress("glActiveTextureARB") == Marker);
    glBindTexture(VOLUME, 1);
    Rejected(c, GL_INVALID_ENUM);
    glTexImage3DEXT(VOLUME, 0, GL_RGBA, 1, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, bogus);
    Rejected(c, GL_INVALID_OPERATION);
    glTexImage3D(PROXY_VOLUME, 0, GL_RGBA, 1, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    Rejected(c, GL_INVALID_OPERATION);
    glTexSubImage3DEXT(VOLUME, 0, 0, 0, 0, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, bogus);
    Rejected(c, GL_INVALID_OPERATION);
    glCopyTexSubImage3DEXT(VOLUME, 0, 0, 0, 0, 0, 0, 1, 1);
    Rejected(c, GL_INVALID_OPERATION);
    glTexParameteri(VOLUME, MIN_FILTER, 0x2600);
    Rejected(c, GL_INVALID_ENUM);
    /* TEXTURE_WRAP_R stays the cube map's alone. */
    glTexParameteri(GL_TEXTURE_2D, WRAP_R, CLAMP_TO_EDGE);
    Rejected(c, GL_INVALID_ENUM);
    glTexParameteri(CUBE, WRAP_R, CLAMP_TO_EDGE);
    assert(c->Records == 1 && !c->Error);
    Clear(c);
    for (GLenum name :
         {UNPACK_IMAGE_HEIGHT, UNPACK_SKIP_IMAGES, PACK_IMAGE_HEIGHT, PACK_SKIP_IMAGES}) {
        glPixelStorei(name, 1);
        Rejected(c, GL_INVALID_ENUM);
    }
    /* Queries stay local errors: no host query is issued. */
    GLint value = 0x5a5a;
    GLboolean flag = 0x5a;
    glGetTexImage(VOLUME, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
    Rejected(c, GL_INVALID_ENUM);
    for (GLenum name : {BINDING_VOLUME, MAX_VOLUME, UNPACK_IMAGE_HEIGHT, UNPACK_SKIP_IMAGES,
                        PACK_IMAGE_HEIGHT, PACK_SKIP_IMAGES, VOLUME}) {
        glGetIntegerv(name, &value);
        Rejected(c, GL_INVALID_ENUM);
    }
    glGetBooleanv(VOLUME, &flag);
    Rejected(c, GL_INVALID_ENUM);
    assert(!glIsEnabled(VOLUME));
    Rejected(c, GL_INVALID_ENUM);
    glGetTexParameteriv(VOLUME, MIN_FILTER, &value);
    Rejected(c, GL_INVALID_ENUM);
    glGetTexParameteriv(GL_TEXTURE_2D, WRAP_R, &value);
    Rejected(c, GL_INVALID_ENUM);
    glGetTexLevelParameteriv(VOLUME, 0, GL_TEXTURE_WIDTH, &value);
    Rejected(c, GL_INVALID_ENUM);
    glGetTexLevelParameteriv(PROXY_VOLUME, 0, GL_TEXTURE_WIDTH, &value);
    Rejected(c, GL_INVALID_ENUM);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, TEXTURE_DEPTH, &value);
    Rejected(c, GL_INVALID_ENUM);
    assert(Queries.empty() && value == 0x5a5a && flag == 0x5a);
    for (BYTE byte : image)
        assert(byte == 0xa5);
    /* The cube map's WRAP_R query still reaches the host. */
    glGetTexParameteriv(CUBE, WRAP_R, &value);
    assert(!c->Error && value == (GLint)CLAMP_TO_EDGE && Queries.size() == 1);
    Log.clear();
    /* Enable keeps passing through, as before. */
    glEnable(VOLUME);
    assert(c->Records == 1 && !c->Error);
    Submit(c);
    ExpectCall(Log[0], FEnum_glEnable, {VOLUME});
    Close(c);
}

/* Negotiated: the extension group is appended last, alone or after all others. */
static void Negotiation(void) {
    JGL_CONTEXT *c = Open(DG_CAP_GL_TEXTURE_3D);
    assert(JglTexture3DAvailable());
    assert(!strcmp((const char *)glGetString(GL_EXTENSIONS), TEXTURE_3D_NAME));
    for (const char *name : {"glTexImage3DEXT", "glTexImage3D", "glTexSubImage3DEXT",
                             "glTexSubImage3D", "glCopyTexSubImage3DEXT", "glCopyTexSubImage3D"})
        assert(wglGetProcAddress(name) == Marker);
    assert(!wglGetProcAddress("glActiveTextureARB") && !wglGetProcAddress("glGenProgramsARB"));
    Close(c);
    Reset();
    HostCaps = AllCaps | DG_CAP_GL_TEXTURE_3D;
    HostSecondaryWords = 3;
    HGLRC context = Create();
    const char *names = (const char *)glGetString(GL_EXTENSIONS);
    /* Every name in its place and under its gate (extension-names.h). */
    assert(!strcmp(names, ExpectedExtensions(HostCaps, TRUE)));
    assert(wglMakeCurrent(NULL, NULL) && wglDeleteContext(context));
}

/* Binding, parameters, pixel storage and host-answered queries. */
static void State(void) {
    JGL_CONTEXT *c = Open(DG_CAP_GL_TEXTURE_3D);
    const GLint wrap = CLAMP_TO_EDGE;
    glBindTexture(VOLUME, 5);
    glBindTexture(VOLUME, 0);
    glTexParameteri(VOLUME, WRAP_R, CLAMP_TO_EDGE);
    glTexParameteri(VOLUME, MIN_FILTER, 0x2600);
    glTexParameteriv(VOLUME, WRAP_R, &wrap);
    /* WRAP_R is a parameter of every target once 3D textures exist. */
    glTexParameteri(GL_TEXTURE_2D, WRAP_R, CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_1D, WRAP_R, CLAMP_TO_EDGE);
    glEnable(VOLUME);
    glDisable(VOLUME);
    assert(c->Records == 9 && !c->Error);
    Submit(c);
    ExpectCall(Log[0], FEnum_glBindTexture, {VOLUME, 5});
    ExpectCall(Log[1], FEnum_glBindTexture, {VOLUME, 0});
    ExpectCall(Log[2], FEnum_glTexParameteri, {VOLUME, WRAP_R, CLAMP_TO_EDGE});
    ExpectCall(Log[3], FEnum_glTexParameteri, {VOLUME, MIN_FILTER, 0x2600});
    ExpectData(Log[4], FEnum_glTexParameteriv, {VOLUME, WRAP_R}, &wrap, 4);
    ExpectCall(Log[5], FEnum_glTexParameteri, {GL_TEXTURE_2D, WRAP_R, CLAMP_TO_EDGE});
    ExpectCall(Log[6], FEnum_glTexParameteri, {GL_TEXTURE_1D, WRAP_R, CLAMP_TO_EDGE});
    ExpectCall(Log[7], FEnum_glEnable, {VOLUME});
    ExpectCall(Log[8], FEnum_glDisable, {VOLUME});
    Log.clear();
    /* A name keeps the target of its first bind; the proxy is no binding target. */
    glBindTexture(GL_TEXTURE_2D, 5);
    Rejected(c, GL_INVALID_OPERATION);
    glBindTexture(PROXY_VOLUME, 6);
    Rejected(c, GL_INVALID_ENUM);
    glTexParameteri(PROXY_VOLUME, MIN_FILTER, 0x2600);
    Rejected(c, GL_INVALID_ENUM);
    /* The image pixel storage is client state, answered locally. */
    glPixelStorei(UNPACK_IMAGE_HEIGHT, 7);
    glPixelStorei(UNPACK_SKIP_IMAGES, 3);
    glPixelStorei(PACK_IMAGE_HEIGHT, 9);
    glPixelStorei(PACK_SKIP_IMAGES, 2);
    assert(!c->Error && !c->Records);
    glPixelStorei(UNPACK_IMAGE_HEIGHT, -1);
    Rejected(c, GL_INVALID_VALUE);
    glPixelStorei(PACK_SKIP_IMAGES, -1);
    Rejected(c, GL_INVALID_VALUE);
    GLint value = 0;
    GLfloat real = 0;
    GLboolean flag = 0;
    glGetIntegerv(UNPACK_IMAGE_HEIGHT, &value);
    assert(value == 7);
    glGetIntegerv(UNPACK_SKIP_IMAGES, &value);
    assert(value == 3);
    glGetFloatv(PACK_IMAGE_HEIGHT, &real);
    assert(real == 9.f);
    glGetBooleanv(PACK_SKIP_IMAGES, &flag);
    assert(flag == 1);
    assert(JglUnpack()->ImageHeight == 7 && JglUnpack()->SkipImages == 3 &&
           JglPack()->ImageHeight == 9 && JglPack()->SkipImages == 2);
    assert(Queries.empty() && !c->Error);
    /* State, parameter and level queries reach the host. */
    glGetIntegerv(BINDING_VOLUME, &value);
    assert(value == 5);
    glGetIntegerv(MAX_VOLUME, &value);
    assert(value == DG_GL_MAX_3D_TEXTURE_SIZE);
    assert(glIsEnabled(VOLUME));
    glGetTexParameteriv(VOLUME, WRAP_R, &value);
    assert(value == (GLint)CLAMP_TO_EDGE);
    glGetTexParameteriv(GL_TEXTURE_2D, WRAP_R, &value);
    glGetTexLevelParameteriv(VOLUME, 2, TEXTURE_DEPTH, &value);
    assert(value == 4);
    glGetTexLevelParameteriv(PROXY_VOLUME, 1, GL_TEXTURE_WIDTH, &value);
    assert(!c->Error && Queries.size() == 7);
    assert(Queries[0] == std::vector<ULONG>({FEnum_glGetIntegerv, BINDING_VOLUME, 0, 0}));
    assert(Queries[5] ==
           std::vector<ULONG>({FEnum_glGetTexLevelParameteriv, VOLUME, 2, TEXTURE_DEPTH}));
    assert(Queries[6] ==
           std::vector<ULONG>({FEnum_glGetTexLevelParameteriv, PROXY_VOLUME, 1, GL_TEXTURE_WIDTH}));
    Close(c);
}

/* The bytes GL 1.2 unpacks for pixel (x, y, z) of a width*height*depth image of
 * `bytes`-byte pixels (components of `unit` bytes) under the current state. */
static const BYTE *Unpacked(const BYTE *base, ULONG x, ULONG y, ULONG z, ULONG width, ULONG height,
                            ULONG bytes) {
    const JGL_UNPACK *u = JglUnpack();
    const ULONG row = (u->RowLength ? u->RowLength : width) * bytes;
    const ULONG stride = (row + u->Alignment - 1) / u->Alignment * u->Alignment;
    const ULONG image = (u->ImageHeight ? u->ImageHeight : height) * stride;
    return base + (u->SkipImages + z) * image + (u->SkipRows + y) * stride +
           (u->SkipPixels + x) * bytes;
}

/* Images: exact records, unpack state, packed types, empty and proxy images,
 * and local validation of every shape the host would refuse. */
static void Images(void) {
    JGL_CONTEXT *c = Open(DG_CAP_GL_TEXTURE_3D | DG_CAP_GL_TEXTURE_IMAGES);
    const void *bogus = (const void *)16;
    glTexImage3DEXT(VOLUME, 1, RGBA8, 2, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, Source);
    glTexImage3D(VOLUME, 0, GL_RGBA, 1, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, Source);
    assert(!c->Error);
    Submit(c);
    ExpectData(Log[0], FEnum_glTexImage3D,
               {VOLUME, 1, RGBA8, 2, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE}, Source, 32);
    ExpectData(Log[1], FEnum_glTexImage3D,
               {VOLUME, 0, GL_RGBA, 1, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE}, Source, 4);
    Log.clear();
    /* Every unpack value at once: rows, pixels and images skipped, padded
     * rows and taller images, gathered into tight rows then images. */
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 5);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 1);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 2);
    glPixelStorei(UNPACK_IMAGE_HEIGHT, 4);
    glPixelStorei(UNPACK_SKIP_IMAGES, 2);
    glTexImage3DEXT(VOLUME, 0, GL_RGB, 3, 2, 3, 0, GL_RGB, GL_UNSIGNED_BYTE, Source);
    assert(!c->Error);
    Submit(c);
    std::vector<BYTE> tight;
    for (ULONG z = 0; z < 3; ++z)
        for (ULONG y = 0; y < 2; ++y)
            for (ULONG x = 0; x < 3; ++x) {
                const BYTE *p = Unpacked(Source, x, y, z, 3, 2, 3);
                tight.insert(tight.end(), p, p + 3);
            }
    ExpectData(Log[0], FEnum_glTexImage3D,
               {VOLUME, 0, GL_RGB, 3, 2, 3, 0, GL_RGB, GL_UNSIGNED_BYTE}, tight.data(), 54);
    Log.clear();
    *JglUnpack() = (JGL_UNPACK){4, 0, 0, 0, 0, 0, 0, 0};
    /* Wine's dummy texels: UNSIGNED_INT_8_8_8_8 travels as RGBA8 bytes;
     * 8_8_8_8_REV is the byte order; swapped 16-bit and float components. */
    const ULONG dummy = 0x11223344;
    glTexImage3DEXT(VOLUME, 0, RGBA8, 1, 1, 1, 0, GL_RGBA, PACKED_8888, &dummy);
    glTexImage3DEXT(VOLUME, 0, RGBA8, 1, 1, 1, 0, BGRA, PACKED_8888_REV, &dummy);
    glPixelStorei(GL_UNPACK_SWAP_BYTES, 1);
    /* Each one-texel row pads to the four-byte alignment, so does each image. */
    const unsigned short rgb565[4] = {0xf800, 0xdead, 0x07e0, 0xbeef};
    glTexImage3DEXT(VOLUME, 0, GL_RGB, 1, 1, 2, 0, GL_RGB, PACKED_565, rgb565);
    const float luminance[2] = {0.25f, 1.f};
    glTexImage3DEXT(VOLUME, 0, GL_LUMINANCE, 1, 1, 2, 0, GL_LUMINANCE, FLOAT, luminance);
    glPixelStorei(GL_UNPACK_SWAP_BYTES, 0);
    assert(!c->Error);
    Submit(c);
    const BYTE rgba[4] = {0x11, 0x22, 0x33, 0x44};
    ExpectData(Log[0], FEnum_glTexImage3D,
               {VOLUME, 0, RGBA8, 1, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE}, rgba, 4);
    ExpectData(Log[1], FEnum_glTexImage3D, {VOLUME, 0, RGBA8, 1, 1, 1, 0, BGRA, GL_UNSIGNED_BYTE},
               &dummy, 4);
    const BYTE swapped565[4] = {0xf8, 0x00, 0x07, 0xe0};
    ExpectData(Log[2], FEnum_glTexImage3D, {VOLUME, 0, GL_RGB, 1, 1, 2, 0, GL_RGB, PACKED_565},
               swapped565, 4);
    BYTE swapped[8];
    for (ULONG i = 0; i < 8; ++i)
        swapped[i] = ((const BYTE *)luminance)[i / 4 * 4 + 3 - i % 4];
    ExpectData(Log[3], FEnum_glTexImage3D,
               {VOLUME, 0, GL_LUMINANCE, 1, 1, 2, 0, GL_LUMINANCE, FLOAT}, swapped, 8);
    Log.clear();
    /* Empty definitions and NULL pixels allocate without reading memory. */
    glTexImage3DEXT(VOLUME, 2, RGBA8, 4, 4, 0, 0, GL_RGBA, GL_UNSIGNED_BYTE, bogus);
    glTexImage3DEXT(VOLUME, 0, RGBA8, 256, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    /* Proxies execute immediately, even beyond the limits or with a border:
     * the host rejects those levels. */
    glTexImage3DEXT(PROXY_VOLUME, 0, RGBA8, 512, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexImage3DEXT(PROXY_VOLUME, 3, RGBA8, 4, 4, 4, 1, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    assert(!c->Error);
    Submit(c);
    ExpectData(Log[0], FEnum_glTexImage3D,
               {VOLUME, 2, RGBA8, 4, 4, 0, 0, GL_RGBA, GL_UNSIGNED_BYTE}, NULL, 0);
    ExpectData(Log[1], FEnum_glTexImage3D,
               {VOLUME, 0, RGBA8, 256, 256, 256, 0, GL_RGBA, GL_UNSIGNED_BYTE}, NULL, 0);
    ExpectData(Log[2], FEnum_glTexImage3D,
               {PROXY_VOLUME, 0, RGBA8, 512, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE}, NULL, 0);
    ExpectData(Log[3], FEnum_glTexImage3D,
               {PROXY_VOLUME, 3, RGBA8, 4, 4, 4, 1, GL_RGBA, GL_UNSIGNED_BYTE}, NULL, 0);
    Log.clear();
    /* Shapes the host would refuse are local errors. */
    glTexImage3DEXT(GL_TEXTURE_2D, 0, RGBA8, 1, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, bogus);
    Rejected(c, GL_INVALID_ENUM);
    glTexImage3DEXT(VOLUME, 12, RGBA8, 0, 0, 0, 0, GL_RGBA, GL_UNSIGNED_BYTE, bogus);
    Rejected(c, GL_INVALID_VALUE);
    glTexImage3DEXT(VOLUME, 0, RGBA8, 257, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, bogus);
    Rejected(c, GL_INVALID_VALUE);
    glTexImage3DEXT(VOLUME, 1, RGBA8, 1, 129, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, bogus);
    Rejected(c, GL_INVALID_VALUE);
    glTexImage3DEXT(VOLUME, 9, RGBA8, 1, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, bogus);
    Rejected(c, GL_INVALID_VALUE);
    glTexImage3DEXT(VOLUME, 0, RGBA8, 1, 1, -1, 0, GL_RGBA, GL_UNSIGNED_BYTE, bogus);
    Rejected(c, GL_INVALID_VALUE);
    glTexImage3DEXT(VOLUME, 0, RGBA8, 4, 4, 4, 2, GL_RGBA, GL_UNSIGNED_BYTE, bogus);
    Rejected(c, GL_INVALID_VALUE);
    glTexImage3DEXT(VOLUME, 0, RGBA8, 4, 4, 4, 1, GL_RGBA, GL_UNSIGNED_BYTE, bogus);
    Rejected(c, GL_INVALID_OPERATION); /* 3D images are borderless */
    glTexImage3DEXT(VOLUME, 0, DXT1, 4, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, bogus);
    Rejected(c, GL_INVALID_VALUE);
    glTexImage3DEXT(VOLUME, 0, 0x84ee /* COMPRESSED_RGBA */, 4, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                    bogus);
    Rejected(c, GL_INVALID_VALUE);
    glTexImage3DEXT(VOLUME, 0, 5, 4, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, bogus);
    Rejected(c, GL_INVALID_VALUE);
    glTexImage3DEXT(VOLUME, 0, GL_ALPHA, 4, 4, 4, 0, COLOR_INDEX, BITMAP, bogus);
    Rejected(c, GL_INVALID_ENUM);
    glTexImage3DEXT(VOLUME, 0, RGBA8, 4, 4, 4, 0, 0x1234, GL_UNSIGNED_BYTE, bogus);
    Rejected(c, GL_INVALID_ENUM);
    glTexImage3DEXT(VOLUME, 0, RGBA8, 4, 4, 4, 0, GL_RGB, PACKED_8888, bogus);
    Rejected(c, GL_INVALID_OPERATION);
    glTexImage3DEXT(PROXY_VOLUME, 0, RGBA8, -1, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    Rejected(c, GL_INVALID_VALUE);
    glTexImage3DEXT(PROXY_VOLUME, 0, RGBA8, 1, 4, 4, 1, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    Rejected(c, GL_INVALID_VALUE);
    glTexImage3DEXT(PROXY_VOLUME, 0, DXT1, 4, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    Rejected(c, GL_INVALID_VALUE);
    Close(c);
}

/* Paints `bytes` of each record's payload into its place in a w*h*d volume
 * of RGBA8 texels, counting coverage. */
static void Reassemble(size_t from, ULONG function, ULONG w, ULONG h, ULONG d,
                       std::vector<BYTE> &volume, ULONG capacity) {
    std::vector<ULONG> covered(w * h * d, 0);
    volume.assign(w * h * d * 4, 0);
    assert(Log.size() > from + 1);
    for (size_t n = from; n < Log.size(); ++n) {
        const ULONG *a = Log[n].data() + 10;
        assert(Log[n][8] == function && a[0] == VOLUME && a[5] && a[6] && a[7]);
        assert(Log[n][9] == a[5] * a[6] * a[7] * 4 && Log[n][9] <= capacity);
        /* Tiles of one image start on four-texel boundaries, as in 2D. */
        assert(!(a[2] % 4) && !(a[3] % 4));
        const BYTE *payload = (const BYTE *)(Log[n].data() + 20);
        for (ULONG z = 0; z < a[7]; ++z)
            for (ULONG y = 0; y < a[6]; ++y)
                for (ULONG x = 0; x < a[5]; ++x) {
                    const ULONG at = ((a[4] + z) * h + a[3] + y) * w + a[2] + x;
                    ++covered[at];
                    memcpy(&volume[at * 4], payload + ((z * a[6] + y) * a[5] + x) * 4, 4);
                }
    }
    for (ULONG count : covered)
        assert(count == 1);
}

/* Large volumes: a payload-free image then bounded TexSubImage3D slabs of
 * whole images, or 2D tiles of one image; split subimages are checked
 * against the level first and never compiled. */
static void Slabs(void) {
    JGL_CONTEXT *c = Open(DG_CAP_GL_TEXTURE_3D, SMALL_PACKET);
    std::vector<BYTE> volume;
    /* 4x4x6: two 64-byte images per 176-byte subimage. */
    glTexImage3DEXT(VOLUME, 0, RGBA8, 4, 4, 6, 0, GL_RGBA, GL_UNSIGNED_BYTE, Source);
    assert(!c->Error);
    Submit(c);
    ExpectData(Log[0], FEnum_glTexImage3D,
               {VOLUME, 0, RGBA8, 4, 4, 6, 0, GL_RGBA, GL_UNSIGNED_BYTE}, NULL, 0);
    assert(Log.size() == 4 && Log[1][17] == 2);
    Reassemble(1, FEnum_glTexSubImage3D, 4, 4, 6, volume, 176);
    assert(!memcmp(volume.data(), Source, volume.size()));
    Log.clear();
    /* 12x12x2: one image exceeds a record, so 2D tiles of each image. */
    glTexImage3DEXT(VOLUME, 0, RGBA8, 12, 12, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, Source);
    assert(!c->Error);
    Submit(c);
    Reassemble(1, FEnum_glTexSubImage3D, 12, 12, 2, volume, 176);
    assert(!memcmp(volume.data(), Source, volume.size()));
    Log.clear();
    /* A split subimage first reads the level's size: inside it, slabs. */
    LevelWidth = 8;
    LevelHeight = 4;
    LevelDepth = 6;
    glTexSubImage3DEXT(VOLUME, 0, 4, 0, 1, 4, 4, 5, GL_RGBA, GL_UNSIGNED_BYTE, Source);
    assert(!c->Error && Queries.size() == 3);
    assert(Queries[2] ==
           std::vector<ULONG>({FEnum_glGetTexLevelParameteriv, VOLUME, 0, TEXTURE_DEPTH}));
    Submit(c);
    ULONG slabs = 0;
    for (const auto &r : Log)
        if (r[0] == DG_GL_DATA_CALL) {
            assert(r[8] == FEnum_glTexSubImage3D && r[12] == 4 && r[13] == 0 && r[15] == 4);
            slabs += r[17];
        }
    assert(slabs == 5);
    Log.clear();
    Queries.clear();
    /* Beyond it, or on an undefined level, nothing is sent. */
    glTexSubImage3DEXT(VOLUME, 0, 0, 0, 2, 4, 4, 5, GL_RGBA, GL_UNSIGNED_BYTE, Source);
    RejectedAfterQueries(c, GL_INVALID_VALUE);
    LevelDepth = 0;
    glTexSubImage3DEXT(VOLUME, 0, 0, 0, 0, 4, 4, 5, GL_RGBA, GL_UNSIGNED_BYTE, Source);
    RejectedAfterQueries(c, GL_INVALID_OPERATION);
    /* A subimage of one record needs no query; the host checks its box. */
    Queries.clear();
    glTexSubImage3D(VOLUME, 0, 250, 1, 2, 2, 1, 3, GL_RGBA, GL_UNSIGNED_BYTE, Source);
    assert(!c->Error && Queries.empty());
    Submit(c);
    ExpectData(Log[0], FEnum_glTexSubImage3D,
               {VOLUME, 0, 250, 1, 2, 2, 1, 3, GL_RGBA, GL_UNSIGNED_BYTE}, Source, 24);
    Log.clear();
    /* Local subimage shapes the host would refuse. */
    glTexSubImage3DEXT(VOLUME, 0, 0, 0, 0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, Source);
    assert(!c->Error && !c->Records); /* empty: nothing to do */
    glTexSubImage3DEXT(VOLUME, 0, 0, 0, 0, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    Rejected(c, GL_INVALID_VALUE);
    glTexSubImage3DEXT(VOLUME, 0, -1, 0, 0, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, Source);
    Rejected(c, GL_INVALID_VALUE);
    glTexSubImage3DEXT(VOLUME, 1, 0, 0, 120, 1, 1, 9, GL_RGBA, GL_UNSIGNED_BYTE, Source);
    Rejected(c, GL_INVALID_VALUE);
    glTexSubImage3DEXT(VOLUME, 12, 0, 0, 0, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, Source);
    Rejected(c, GL_INVALID_VALUE);
    glTexSubImage3DEXT(CUBE, 0, 0, 0, 0, 1, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, Source);
    Rejected(c, GL_INVALID_ENUM);
    /* Indices need the extended image contract; with it, BITMAP is no 3D type. */
    glTexSubImage3DEXT(VOLUME, 0, 0, 0, 0, 1, 1, 1, COLOR_INDEX, BITMAP, Source);
    Rejected(c, GL_INVALID_OPERATION);
    Close(c);
    c = Open(DG_CAP_GL_TEXTURE_3D | DG_CAP_GL_TEXTURE_IMAGES);
    glTexSubImage3DEXT(VOLUME, 0, 0, 0, 0, 1, 1, 1, COLOR_INDEX, BITMAP, Source);
    Rejected(c, GL_INVALID_ENUM);
    Close(c);
}

/* CopyTexSubImage3D: one CALL record, checked locally like the host. */
static void Copies(void) {
    JGL_CONTEXT *c = Open(DG_CAP_GL_TEXTURE_3D);
    glCopyTexSubImage3DEXT(VOLUME, 1, 1, 2, 3, 4, 5, 6, 7);
    glCopyTexSubImage3D(VOLUME, 0, 0, 0, 255, 0, 0, 256, 0);
    assert(!c->Error);
    Submit(c);
    ExpectCall(Log[0], FEnum_glCopyTexSubImage3D, {VOLUME, 1, 1, 2, 3, 4, 5, 6, 7});
    ExpectCall(Log[1], FEnum_glCopyTexSubImage3D, {VOLUME, 0, 0, 0, 255, 0, 0, 256, 0});
    Log.clear();
    glCopyTexSubImage3DEXT(GL_TEXTURE_2D, 0, 0, 0, 0, 0, 0, 1, 1);
    Rejected(c, GL_INVALID_ENUM);
    glCopyTexSubImage3DEXT(VOLUME, 12, 0, 0, 0, 0, 0, 1, 1);
    Rejected(c, GL_INVALID_VALUE);
    glCopyTexSubImage3DEXT(VOLUME, 0, 0, 0, 0, 0, 0, 257, 1);
    Rejected(c, GL_INVALID_VALUE);
    glCopyTexSubImage3DEXT(VOLUME, 0, 0, 0, 0, 0, 0, 1, -1);
    Rejected(c, GL_INVALID_VALUE);
    /* Not inside Begin/End. */
    glBegin(4 /* TRIANGLES */);
    glCopyTexSubImage3DEXT(VOLUME, 0, 0, 0, 0, 0, 0, 1, 1);
    assert(c->Error == GL_INVALID_OPERATION);
    glEnd();
    c->Error = 0;
    Submit(c);
    assert(Log.size() == 2 && Log[0][8] == FEnum_glBegin && Log[1][8] == FEnum_glEnd);
    Close(c);
}

/* GetTexImage of a 3D level: its three sizes, then its pixels placed through
 * PACK_IMAGE_HEIGHT and PACK_SKIP_IMAGES. */
static void Readback(void) {
    JGL_CONTEXT *c = Open(DG_CAP_GL_TEXTURE_3D | DG_CAP_GL_BULK_READBACK);
    BYTE image[1024];
    memset(image, 0xa5, sizeof(image));
    LevelWidth = 3;
    LevelHeight = 2;
    LevelDepth = 2;
    glPixelStorei(GL_PACK_ALIGNMENT, 8);
    glPixelStorei(PACK_IMAGE_HEIGHT, 3);
    glPixelStorei(PACK_SKIP_IMAGES, 1);
    glGetTexImage(VOLUME, 1, GL_RGBA, GL_UNSIGNED_BYTE, image);
    assert(!c->Error && Queries.size() == 4);
    assert(Queries[0] ==
           std::vector<ULONG>({FEnum_glGetTexLevelParameteriv, VOLUME, 1, GL_TEXTURE_WIDTH}));
    assert(Queries[2] ==
           std::vector<ULONG>({FEnum_glGetTexLevelParameteriv, VOLUME, 1, TEXTURE_DEPTH}));
    assert(Queries[3][0] == FEnum_glGetTexImage &&
           (Queries[3][2] & DG_GL_TEXTURE_READ_LEVEL_MASK) == 1 && !Queries[3][3]);
    /* Rows of 16 bytes (12 aligned to 8), images three rows apart, one skipped. */
    std::vector<bool> written(sizeof(image), false);
    for (ULONG z = 0; z < 2; ++z)
        for (ULONG y = 0; y < 2; ++y)
            for (ULONG x = 0; x < 3; ++x) {
                const ULONG at = (1 + z) * 48 + y * 16 + x * 4, n = (z * 2 + y) * 3 + x;
                const ULONG pixel = 0x7f | (n << 8) | 0xff000000U;
                assert(!memcmp(image + at, &pixel, 4));
                for (ULONG i = 0; i < 4; ++i)
                    written[at + i] = true;
            }
    for (ULONG i = 0; i < sizeof(image); ++i)
        assert(written[i] || image[i] == 0xa5);
    /* An undefined level reads nothing; the proxy has no image. */
    Queries.clear();
    LevelDepth = 0;
    glGetTexImage(VOLUME, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
    assert(!c->Error && Queries.size() == 3);
    Queries.clear();
    Log.clear();
    glGetTexImage(PROXY_VOLUME, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
    Rejected(c, GL_INVALID_ENUM);
    assert(Queries.empty());
    Close(c);
}

/* Compiling a list: image records are recorded for the host to capture,
 * command errors deferred, split subimages refused, proxies immediate. */
static void Lists(void) {
    JGL_CONTEXT *c = Open(DG_CAP_GL_TEXTURE_3D, SMALL_PACKET);
    assert(JglListMode(GL_COMPILE));
    Clear(c);
    glBindTexture(VOLUME, 9);
    glTexImage3DEXT(VOLUME, 0, RGBA8, 1, 1, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, Source);
    glTexImage3DEXT(VOLUME, 0, RGBA8, 4, 4, 4, 1, GL_RGBA, GL_UNSIGNED_BYTE, Source);
    glTexSubImage3DEXT(VOLUME, 0, 0, 0, 0, 4, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, Source);
    glCopyTexSubImage3DEXT(VOLUME, 0, 0, 0, 0, 0, 0, 300, 1);
    assert(!c->Error);
    glTexImage3DEXT(PROXY_VOLUME, 0, RGBA8, 2, 2, -2, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    assert(c->Error == GL_INVALID_VALUE);
    c->Error = 0;
    glTexImage3DEXT(PROXY_VOLUME, 0, RGBA8, 2, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    assert(!c->Error && Queries.empty());
    Submit(c);
    assert(Log.size() == 6);
    ExpectCall(Log[0], FEnum_glBindTexture, {VOLUME, 9});
    ExpectData(Log[1], FEnum_glTexImage3D,
               {VOLUME, 0, RGBA8, 1, 1, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE}, Source, 8);
    ExpectDeferred(Log[2], GL_INVALID_OPERATION);
    ExpectDeferred(Log[3], GL_OUT_OF_MEMORY);
    ExpectDeferred(Log[4], GL_INVALID_VALUE);
    ExpectData(Log[5], FEnum_glTexImage3D,
               {PROXY_VOLUME, 0, RGBA8, 2, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE}, NULL, 0);
    assert(JglListMode(0));
    Close(c);
}

int main() {
    for (ULONG i = 0; i < sizeof(Source); ++i)
        Source[i] = (BYTE)(i * 131 + (i >> 8) * 7 + 3);
    Unnegotiated();
    Negotiation();
    State();
    Images();
    Slabs();
    Copies();
    Readback();
    Lists();
    Reset();
    assert(!Allocations);
    puts("PASS 3D textures actual frontend: unchanged without negotiation; appended extension; "
         "3D binding, WRAP_R and image pixel storage; exact image records through every unpack "
         "value and packed type; bounded slabs; checked split subimages; copies; readback "
         "through pack image state; deferred list errors and immediate proxies");
    return 0;
}
