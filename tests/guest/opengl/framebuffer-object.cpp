/* SPDX-License-Identifier: GPL-2.0-or-later
 * DG_CAP_GL_FRAMEBUFFER_OBJECT through the actual frontend on the strict
 * transport fixture: negotiation (alone, after every other capability, and the
 * unchanged frontend without it), exact CALL, DATA and QUERY records, local
 * validation (nothing the host would reject as malformed may leave the
 * frontend: every framebuffer record is checked against the host's shape rules
 * and the NT validator), names and bindings (framebuffers per context,
 * renderbuffers per share group), deletion batches, DrawBuffer/ReadBuffer with
 * an object bound, front-buffer publication, the attribute stack, display
 * lists, ReadPixels clipping and DEPTH_STENCIL composition, depth textures,
 * DEPTH_STENCIL DrawPixels and CopyPixels. */
#include <algorithm>
#include <array>
#include <cmath>
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
using GLvoid = void;
/* The shared ICD fragments define more entry points than this suite uses. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "icd-pixels.inc"
#include "icd-images.inc"
#pragma GCC diagnostic pop
typedef void (*PROC)(void);
static void Marker(void) {}
extern "C" PROC GetProcAddress(HINSTANCE, LPCSTR name) {
    return name[0] == 'g' && name[1] == 'l' ? Marker : nullptr;
}
#include "compatibility.cpp"
#include "extension-names.h"
#include "dg-gl-validate.h"

constexpr GLenum FRAMEBUFFER = 0x8d40, READ_FB = 0x8ca8, DRAW_FB = 0x8ca9, RENDERBUFFER = 0x8d41;
constexpr GLenum FB_BINDING = 0x8ca6, READ_FB_BINDING = 0x8caa, RB_BINDING = 0x8ca7,
                 MAX_ATTACHMENTS = 0x8cdf, MAX_RB_SIZE = 0x84e8;
constexpr GLenum COLOR0 = 0x8ce0, DEPTH_ATT = 0x8d00, STENCIL_ATT = 0x8d20;
constexpr GLenum CUBE = 0x8513, CUBE_X = 0x8515, CUBE_NZ = 0x851a, TEX3D = 0x806f;
constexpr GLenum DEPTH_STENCIL = 0x84f9, UINT_24_8 = 0x84fa, DEPTH16 = 0x81a5, DEPTH24 = 0x81a6,
                 DEPTH32 = 0x81a7, DEPTH24_STENCIL8 = 0x88f0, RGBA8 = 0x8058;
constexpr GLenum NEAREST = 0x2600, LINEAR = 0x2601, COMPLETE = 0x8cd5;
constexpr GLenum OBJECT_TYPE = 0x8cd0, ZOFFSET = 0x8cd4, RB_WIDTH = 0x8d42, RB_FORMAT = 0x8d44,
                 RB_RED = 0x8d50, RB_STENCIL = 0x8d55;
constexpr GLbitfield COLOR_BIT = 0x4000, DEPTH_BIT = 0x100, STENCIL_BIT = 0x400;
constexpr GLenum PACKED_565 = 0x8363, PROXY_2D = 0x8064;
constexpr ULONG SMALL_PACKET = 512, HOST_ANSWER = 77;
constexpr ULONG FBO = DG_CAP_GL_FRAMEBUFFER_OBJECT;
constexpr ULONG ALL_BUT_FBO = DG_CAP_GL_MULTITEXTURE | DG_CAP_GL_FIXED_STATE |
                              DG_CAP_GL_TEXTURE_CONTROL | DG_CAP_GL_COMPRESSED_TEXTURES |
                              DG_CAP_GL_FOG_COORD | DG_CAP_GL_TEXTURE_CUBE_MAP |
                              DG_CAP_GL_FRAGMENT_PROGRAM | DG_CAP_GL_VERTEX_PROGRAM |
                              DG_CAP_GL_TEXTURE_3D | DG_CAP_GL_VERTEX_BUFFER_OBJECT;
/* Scalar images, typed depth/stencil reads and bulk readback. */
constexpr ULONG IMAGES =
    DG_CAP_GL_TEXTURE_IMAGES | DG_CAP_GL_DEPTH_STENCIL_READBACK | DG_CAP_GL_BULK_READBACK;
static const char *const Entries[] = {
    "glIsRenderbufferEXT",          "glBindRenderbufferEXT",
    "glDeleteRenderbuffersEXT",     "glGenRenderbuffersEXT",
    "glRenderbufferStorageEXT",     "glGetRenderbufferParameterivEXT",
    "glIsFramebufferEXT",           "glBindFramebufferEXT",
    "glDeleteFramebuffersEXT",      "glGenFramebuffersEXT",
    "glCheckFramebufferStatusEXT",  "glFramebufferTexture1DEXT",
    "glFramebufferTexture2DEXT",    "glFramebufferTexture3DEXT",
    "glFramebufferRenderbufferEXT", "glGetFramebufferAttachmentParameterivEXT",
    "glGenerateMipmapEXT",          "glBlitFramebufferEXT"};

/* The host's record shapes (gl.h): a framebuffer record outside them is a
 * batch fault that fails the context, so none may leave the frontend. */
static BOOL FramebufferTarget_(ULONG target) {
    return target == FRAMEBUFFER || target == DRAW_FB || target == READ_FB;
}
static BOOL Attachment_(ULONG a) {
    return a - COLOR0 < DG_GL_MAX_COLOR_ATTACHMENTS || a == DEPTH_ATT || a == STENCIL_ATT;
}
static BOOL StorageFormat(ULONG f) {
    return f == GL_RGB || f == GL_RGBA || f == 0x2a10 || (f >= 0x804f && f <= 0x805b) ||
           f == GL_DEPTH_COMPONENT || (f >= DEPTH16 && f <= DEPTH32) || f == GL_STENCIL_INDEX ||
           (f >= 0x8d46 && f <= 0x8d49) || f == DEPTH_STENCIL || f == DEPTH24_STENCIL8;
}
static void HostShape(const std::vector<ULONG> &r) {
    const ULONG *a = r.data() + 9, words = r[1] / 4 - 9;
    if (r[0] == DG_GL_DATA_CALL &&
        (r[8] == FEnum_glDeleteFramebuffers || r[8] == FEnum_glDeleteRenderbuffers)) {
        const ULONG limit =
            r[8] == FEnum_glDeleteFramebuffers ? DG_GL_MAX_FRAMEBUFFERS : DG_GL_MAX_RENDERBUFFERS;
        assert(r[10] && r[10] <= limit && r[9] == r[10] * 4 && r.size() == 11 + r[10]);
        return;
    }
    if (r[0] != DG_GL_CALL)
        return;
    switch (r[8]) {
        case FEnum_glBindFramebuffer:
            assert(words == 2 && FramebufferTarget_(a[0]));
            break;
        case FEnum_glBindRenderbuffer:
            assert(words == 2 && a[0] == RENDERBUFFER);
            break;
        case FEnum_glRenderbufferStorage:
            assert(words == 4 && a[0] == RENDERBUFFER && StorageFormat(a[1]) &&
                   a[2] <= DG_GL_MAX_RENDERBUFFER_SIZE && a[3] <= DG_GL_MAX_RENDERBUFFER_SIZE);
            break;
        case FEnum_glFramebufferTexture1D:
        case FEnum_glFramebufferTexture2D:
        case FEnum_glFramebufferTexture3D: {
            const BOOL volume = r[8] == FEnum_glFramebufferTexture3D;
            assert(words == (volume ? 6U : 5U) && FramebufferTarget_(a[0]) && Attachment_(a[1]) &&
                   a[4] <= DG_GL_MAX_TEXTURE_LEVEL);
            if (r[8] == FEnum_glFramebufferTexture1D)
                assert(a[2] == GL_TEXTURE_1D);
            else if (!volume)
                assert(a[2] == GL_TEXTURE_2D || a[2] == 0x84f5 /* RECTANGLE */ ||
                       a[2] - CUBE_X < 6);
            else
                assert(a[2] == TEX3D && a[5] < DG_GL_MAX_3D_TEXTURE_SIZE);
            break;
        }
        case FEnum_glFramebufferRenderbuffer:
            assert(words == 4 && FramebufferTarget_(a[0]) && Attachment_(a[1]) &&
                   a[2] == RENDERBUFFER);
            break;
        case FEnum_glGenerateMipmap:
            assert(words == 1 && (a[0] == GL_TEXTURE_1D || a[0] == GL_TEXTURE_2D || a[0] == CUBE ||
                                  a[0] == TEX3D));
            break;
        case FEnum_glBlitFramebuffer:
            assert(words == 10 && !(a[8] & ~(COLOR_BIT | DEPTH_BIT | STENCIL_BIT)) &&
                   (a[9] == NEAREST || (a[9] == LINEAR && !(a[8] & (DEPTH_BIT | STENCIL_BIT)))));
            break;
        default:
            break;
    }
}
/* Argument words of the records this suite submits, as the host reports them
 * to the NT kernel; others are not checked here. */
static ULONG KernelWords(void *, ULONG function) {
    switch (function) {
        case FEnum_glBindFramebuffer:
        case FEnum_glBindRenderbuffer:
            return 2;
        case FEnum_glRenderbufferStorage:
        case FEnum_glFramebufferRenderbuffer:
            return 4;
        case FEnum_glFramebufferTexture1D:
        case FEnum_glFramebufferTexture2D:
            return 5;
        case FEnum_glFramebufferTexture3D:
            return 6;
        case FEnum_glGenerateMipmap:
        case FEnum_glDrawBuffer:
        case FEnum_glReadBuffer:
        case FEnum_glClear:
        case DG_GL_RECORD_ERROR:
            return 1;
        case FEnum_glBlitFramebuffer:
            return 10;
        case FEnum_glCopyPixels:
            return 5;
        case FEnum_glCopyTexImage2D:
        case FEnum_glCopyTexSubImage2D:
            return 8;
        case FEnum_glDeleteFramebuffers:
        case FEnum_glDeleteRenderbuffers:
            return DG_GL_FUNCTION_INLINE_DATA | 1;
        case FEnum_glTexImage2D:
        case FEnum_glTexSubImage2D:
        case FEnum_glDrawPixels:
            return DG_GL_FUNCTION_INLINE_DATA | 8;
        case FEnum_glCheckFramebufferStatus:
        case FEnum_glGetIntegerv:
            return DG_GL_FUNCTION_QUERY | 1;
        case FEnum_glGetRenderbufferParameteriv:
            return DG_GL_FUNCTION_QUERY | 2;
        case FEnum_glGetFramebufferAttachmentParameteriv:
        case FEnum_glReadPixels:
        case FEnum_glGetTexLevelParameteriv:
            return DG_GL_FUNCTION_QUERY | 3;
        default:
            return (ULONG)-1;
    }
}
static void Admitted(const std::vector<ULONG> &r) {
    if (r[0] != DG_GL_CALL && r[0] != DG_GL_DATA_CALL && r[0] != DG_GL_QUERY)
        return;
    HostShape(r);
    if (KernelWords(NULL, r[8]) == (ULONG)-1)
        return;
    std::vector<ULONG> copy(r);
    const DG_GL_LIMITS limits = {7, DG_ESCAPE_MAX_BYTES, DG_GL_MAX_RECORDS, 1};
    assert(DgValidateUserGl(copy.data(), copy.size() * 4, 42, 7, &limits, KernelWords, NULL));
}

typedef std::vector<std::vector<ULONG>> Records;
static Records Log;
static void Capture(const ULONG *record) {
    Log.emplace_back(record, record + record[1] / 4);
    Admitted(Log.back());
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
/* The calls so far raised nothing and recorded nothing. */
static void Quiet(JGL_CONTEXT *c) {
    assert(!c->Error && !c->Records && !c->Used);
}
/* Sends what is pending and forgets it. */
static void Drop(JGL_CONTEXT *c) {
    Submit(c);
    Log.clear();
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
/* One CALL record of this function, alone. */
static void Single(JGL_CONTEXT *c, ULONG function, std::initializer_list<ULONG> words) {
    const Records r = Sent(c);
    assert(r.size() == 1);
    ExpectCall(r[0], function, words);
}
static void ExpectData(const std::vector<ULONG> &r, ULONG function,
                       std::initializer_list<ULONG> words, const void *payload, ULONG bytes) {
    assert(r[0] == DG_GL_DATA_CALL && r[8] == function && r[9] == bytes);
    assert(r[1] == (10 + words.size()) * 4 + ((bytes + 3) & ~3U));
    ULONG n = 10;
    for (ULONG word : words)
        assert(r[n++] == word);
    assert(!bytes || !memcmp(r.data() + n, payload, bytes));
}
static void ExpectQuery(const std::vector<ULONG> &r, ULONG function, ULONG a, ULONG b, ULONG c) {
    assert(r[0] == DG_GL_QUERY && r[1] == DG_GL_QUERY_BYTES && r[8] == function && r[9] == a &&
           r[10] == b && r[11] == c);
}
static void ExpectDeferred(const std::vector<ULONG> &r, GLenum error) {
    ExpectCall(r, DG_GL_RECORD_ERROR, {error});
}
static ULONG Bits(GLfloat value) {
    ULONG bits;
    memcpy(&bits, &value, 4);
    return bits;
}

/* Host answers: status, parameters and limits, the level of a split
 * subimage, and framebuffer pixels: colour, depth and stencil by position. */
static std::vector<std::array<ULONG, 4>> Queries;
static GLint HostStatus = COMPLETE, HostLevel = 64;
static GLfloat Depth(ULONG x, ULONG y) {
    static const GLfloat values[] = {0.0f,  1.0f,        0.5f,  -0.25f, 1.5f,        NAN,
                                     0.25f, 1.0f / 3.0f, 1e-9f, -NAN,   0.99999999f, 0.75f};
    return values[(x + 5 * y) % 12];
}
static ULONG Stencil(ULONG x, ULONG y) {
    return 0x100 + x * 3 + y * 5;
}
static ULONG Color(ULONG x, ULONG y) {
    return (x & 255) | (y & 255) << 8 | (x >> 8) << 16 | 0xa0000000U;
}
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
        case FEnum_glCheckFramebufferStatus:
            return put(DG_GL_RESULT_INT, &HostStatus, 4);
        case FEnum_glGetFramebufferAttachmentParameteriv:
        case FEnum_glGetRenderbufferParameteriv:
        case FEnum_glGetIntegerv:
            return put(DG_GL_RESULT_INT, &HOST_ANSWER, 4);
        case FEnum_glGetTexLevelParameteriv: {
            const GLint value = r[11] == 0x1005 /* TEXTURE_BORDER */ ? 0 : HostLevel;
            return put(DG_GL_RESULT_INT, &value, 4);
        }
        case FEnum_glReadPixels: {
            const ULONG x = r[9] & DG_GL_READ_X_MASK, mode = r[9] & ~DG_GL_READ_X_MASK, y = r[10],
                        w = r[11] & 0xffff, h = r[11] >> 16;
            assert(mode == 0 || mode == DG_GL_READ_DEPTH || mode == DG_GL_READ_STENCIL);
            assert(w && h && capacity == w * h * 4 && w * h <= DG_GL_READ_PIXELS_MAX);
            std::vector<ULONG> out(w * h);
            for (ULONG j = 0; j < h; ++j)
                for (ULONG i = 0; i < w; ++i)
                    out[j * w + i] = mode == DG_GL_READ_DEPTH     ? Bits(Depth(x + i, y + j))
                                     : mode == DG_GL_READ_STENCIL ? Stencil(x + i, y + j)
                                                                  : Color(x + i, y + j);
            return put(mode == DG_GL_READ_DEPTH ? DG_GL_RESULT_FLOAT : DG_GL_RESULT_INT, out.data(),
                       w * h * 4);
        }
        default:
            return FALSE;
    }
}
static JGL_CONTEXT *Open(ULONG caps, ULONG packet = DG_ESCAPE_MAX_BYTES, BOOL front = TRUE) {
    Reset();
    HostCaps = caps;
    HostMaxBytes = packet;
    BindCapabilities = front ? DG_WINDOW_CAP_FRONT_ONLY : 0;
    SubmittedRecord = Capture;
    AnswerQuery = Answer;
    HostStatus = COMPLETE;
    Queries.clear();
    JGL_CONTEXT *c = Lookup(Create());
    Clear(c);
    return c;
}
static void Close(JGL_CONTEXT *c) {
    assert(wglMakeCurrent(NULL, NULL) && wglDeleteContext((HGLRC)(ULONG_PTR)c->Id));
    Log.clear();
}
static GLint Integer(GLenum pname) {
    GLint value = 0x5a5a;
    glGetIntegerv(pname, &value);
    return value;
}

/* Every entry point once, with arguments valid under the capability. */
static void EveryEntry(JGL_CONTEXT *c, void (*check)(JGL_CONTEXT *)) {
    GLuint names[2] = {7, 7};
    GLint value = 0x5a5a;
    glGenFramebuffersEXT(2, names);
    check(c);
    glGenRenderbuffersEXT(2, names);
    check(c);
    assert(names[0] == 7 && names[1] == 7);
    glBindFramebufferEXT(FRAMEBUFFER, 1);
    check(c);
    glBindRenderbufferEXT(RENDERBUFFER, 1);
    check(c);
    assert(!glIsFramebufferEXT(1));
    check(c);
    assert(!glIsRenderbufferEXT(1));
    check(c);
    glDeleteFramebuffersEXT(1, names);
    check(c);
    glDeleteRenderbuffersEXT(1, names);
    check(c);
    glRenderbufferStorageEXT(RENDERBUFFER, GL_RGBA, 4, 4);
    check(c);
    glFramebufferTexture1DEXT(FRAMEBUFFER, COLOR0, GL_TEXTURE_1D, 1, 0);
    check(c);
    glFramebufferTexture2DEXT(FRAMEBUFFER, COLOR0, GL_TEXTURE_2D, 1, 0);
    check(c);
    glFramebufferTexture3DEXT(FRAMEBUFFER, COLOR0, TEX3D, 1, 0, 0);
    check(c);
    glFramebufferRenderbufferEXT(FRAMEBUFFER, DEPTH_ATT, RENDERBUFFER, 1);
    check(c);
    glGenerateMipmapEXT(GL_TEXTURE_2D);
    check(c);
    glBlitFramebufferEXT(0, 0, 1, 1, 0, 0, 1, 1, COLOR_BIT, NEAREST);
    check(c);
    assert(!glCheckFramebufferStatusEXT(FRAMEBUFFER));
    check(c);
    glGetFramebufferAttachmentParameterivEXT(FRAMEBUFFER, COLOR0, OBJECT_TYPE, &value);
    check(c);
    glGetRenderbufferParameterivEXT(RENDERBUFFER, RB_WIDTH, &value);
    check(c);
    assert(value == 0x5a5a);
}
static void Inoperable(JGL_CONTEXT *c) {
    assert(c->Error == GL_INVALID_OPERATION && !c->Records && !c->Used);
    c->Error = 0;
}
static void Rejected_(JGL_CONTEXT *c) {
    Rejected(c, GL_INVALID_OPERATION);
}

/* Without the capability nothing new exists: every entry point fails locally,
 * the new state names and enums keep their local errors and nothing else
 * changes. */
static void Unnegotiated(void) {
    JGL_CONTEXT *c = Open(ALL_BUT_FBO | IMAGES);
    assert(!JglFramebufferObjectAvailable() && JglTexture3DAvailable());
    assert(!strstr((const char *)glGetString(GL_EXTENSIONS), "framebuffer"));
    assert(!strstr((const char *)glGetString(GL_EXTENSIONS), "depth_stencil"));
    for (const char *name : Entries)
        assert(!wglGetProcAddress(name));
    assert(!wglGetProcAddress("glBindFramebuffer") && !wglGetProcAddress("glGenerateMipmap"));
    assert(wglGetProcAddress("glBindBufferARB") == Marker);
    EveryEntry(c, Rejected_);
    assert(!c->Framebuffer.Draw && !c->Framebuffer.Read && !c->Framebuffer.Renderbuffer &&
           !c->Framebuffer.Names);
    /* The new state names are local GL_INVALID_ENUM, in every query type. */
    GLint value = 0x5a5a;
    GLboolean flag = 0x5a;
    GLfloat number = 2.5f;
    GLdouble wide = 2.5;
    for (GLenum name : {FB_BINDING, READ_FB_BINDING, RB_BINDING, MAX_ATTACHMENTS, MAX_RB_SIZE}) {
        glGetIntegerv(name, &value);
        Rejected(c, GL_INVALID_ENUM);
        glGetBooleanv(name, &flag);
        Rejected(c, GL_INVALID_ENUM);
        glGetFloatv(name, &number);
        Rejected(c, GL_INVALID_ENUM);
        glGetDoublev(name, &wide);
        Rejected(c, GL_INVALID_ENUM);
    }
    assert(value == 0x5a5a && flag == 0x5a && number == 2.5f && wide == 2.5 && Queries.empty());
    /* Draw and read buffers keep the window's rules. */
    glDrawBuffer(COLOR0);
    Rejected(c, GL_INVALID_ENUM);
    glReadBuffer(COLOR0);
    Rejected(c, GL_INVALID_ENUM);
    glReadBuffer(GL_NONE);
    Rejected(c, GL_INVALID_ENUM);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_BACK);
    const Records buffers = Sent(c);
    assert(buffers.size() == 2);
    ExpectCall(buffers[0], FEnum_glDrawBuffer, {GL_NONE});
    ExpectCall(buffers[1], FEnum_glReadBuffer, {GL_BACK});
    assert(c->DrawBuffer == GL_NONE);
    /* No DEPTH_STENCIL pixels, depth textures or 24_8 types. */
    BYTE pixels[64], out[64];
    memset(pixels, 0x11, sizeof(pixels));
    memset(out, 0xcc, sizeof(out));
    AliasCopyPixels(0, 0, 2, 2, DEPTH_STENCIL);
    Rejected(c, GL_INVALID_ENUM);
    AliasDrawPixels(2, 1, DEPTH_STENCIL, UINT_24_8, pixels);
    Rejected(c, GL_INVALID_ENUM);
    AliasDrawPixels(2, 1, GL_DEPTH_COMPONENT, UINT_24_8, pixels);
    Rejected(c, GL_INVALID_ENUM);
    glReadPixels(0, 0, 2, 1, DEPTH_STENCIL, UINT_24_8, out);
    Rejected(c, GL_INVALID_ENUM);
    glReadPixels(0, 0, 2, 1, GL_RGBA, UINT_24_8, out);
    Rejected(c, GL_INVALID_ENUM);
    glTexImage2D(GL_TEXTURE_2D, 0, DEPTH24, 2, 2, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, pixels);
    Rejected(c, GL_INVALID_ENUM);
    glTexImage2D(GL_TEXTURE_2D, 0, DEPTH24_STENCIL8, 2, 2, 0, DEPTH_STENCIL, UINT_24_8, pixels);
    Rejected(c, GL_INVALID_ENUM);
    glTexImage2D(GL_TEXTURE_2D, 0, DEPTH24, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    Rejected(c, GL_INVALID_VALUE);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 2, 2, GL_DEPTH_COMPONENT, GL_FLOAT, pixels);
    Rejected(c, GL_INVALID_ENUM);
    for (BYTE byte : out)
        assert(byte == 0xcc);
    /* A depth copy still reaches the host, as before. */
    glCopyTexImage2D(GL_TEXTURE_2D, 0, DEPTH24, 0, 0, 4, 4, 0);
    Single(c, FEnum_glCopyTexImage2D, {GL_TEXTURE_2D, 0, DEPTH24, 0, 0, 4, 4, 0});
    Close(c);
}

/* Negotiated: the three extensions are appended last, alone or after all
 * others, and every entry point resolves. */
static void Negotiation(void) {
    JGL_CONTEXT *c = Open(FBO);
    assert(JglFramebufferObjectAvailable() && !JglTexture3DAvailable());
    assert(!strcmp((const char *)glGetString(GL_EXTENSIONS),
                   "GL_EXT_framebuffer_object GL_EXT_framebuffer_blit "
                   "GL_EXT_packed_depth_stencil"));
    for (const char *name : Entries)
        assert(wglGetProcAddress(name) == Marker);
    assert(!wglGetProcAddress("glBindBufferARB"));
    Close(c);
    Reset();
    HostCaps = ALL_BUT_FBO | FBO;
    HostSecondaryWords = 3;
    HGLRC context = Create();
    const char *all = (const char *)glGetString(GL_EXTENSIONS);
    /* Every name in its place and under its gate (extension-names.h). */
    assert(!strcmp(all, ExpectedExtensions(HostCaps, TRUE)));
    assert(strstr(all, "GL_EXT_secondary_color"));
    for (const char *name : Entries)
        assert(wglGetProcAddress(name) == Marker);
    assert(wglMakeCurrent(NULL, NULL) && wglDeleteContext(context));
}

/* Exact records for every command; names, Is and the bindings stay local. */
static void Records_(void) {
    JGL_CONTEXT *c = Open(FBO | DG_CAP_GL_TEXTURE_CUBE_MAP | DG_CAP_GL_TEXTURE_3D);
    GLuint names[3] = {}, name = 0;
    glGenFramebuffersEXT(3, names);
    assert(names[0] == 1 && names[1] == 2 && names[2] == 3);
    assert(!glIsFramebufferEXT(1) && !glIsFramebufferEXT(0)); /* generated, not yet objects */
    glGenRenderbuffersEXT(2, names);
    assert(names[0] == 1 && names[1] == 2 && !glIsRenderbufferEXT(1));
    glGenFramebuffersEXT(0, names);
    glGenFramebuffersEXT(3, NULL);
    glDeleteFramebuffersEXT(1, NULL);
    glDeleteFramebuffersEXT(0, names);
    Quiet(c);
    assert(Queries.empty() && !Integer(FB_BINDING) && !Integer(READ_FB_BINDING) &&
           !Integer(RB_BINDING) && Queries.empty());

    /* Binding creates the object and sends the record; FRAMEBUFFER binds both. */
    glBindFramebufferEXT(FRAMEBUFFER, 1);
    Single(c, FEnum_glBindFramebuffer, {FRAMEBUFFER, 1});
    assert(glIsFramebufferEXT(1) && !glIsFramebufferEXT(2));
    assert(Integer(FB_BINDING) == 1 && Integer(READ_FB_BINDING) == 1);
    glBindFramebufferEXT(DRAW_FB, 9); /* any name */
    Single(c, FEnum_glBindFramebuffer, {DRAW_FB, 9});
    assert(glIsFramebufferEXT(9) && Integer(FB_BINDING) == 9 && Integer(READ_FB_BINDING) == 1);
    glBindFramebufferEXT(READ_FB, 0);
    Single(c, FEnum_glBindFramebuffer, {READ_FB, 0});
    assert(Integer(FB_BINDING) == 9 && !Integer(READ_FB_BINDING));
    GLboolean flag = 0;
    GLfloat number = 0;
    GLdouble wide = 0;
    glGetBooleanv(FB_BINDING, &flag);
    glGetFloatv(FB_BINDING, &number);
    glGetDoublev(FB_BINDING, &wide);
    assert(flag == 1 && number == 9.0f && wide == 9.0 && Queries.empty());
    glBindRenderbufferEXT(RENDERBUFFER, 2);
    Single(c, FEnum_glBindRenderbuffer, {RENDERBUFFER, 2});
    assert(glIsRenderbufferEXT(2) && !glIsRenderbufferEXT(1) && Integer(RB_BINDING) == 2);
    glGenFramebuffersEXT(1, &name);
    assert(name == 4);
    glBindFramebufferEXT(FRAMEBUFFER, 0);
    Single(c, FEnum_glBindFramebuffer, {FRAMEBUFFER, 0});
    assert(!Integer(FB_BINDING) && !Integer(READ_FB_BINDING) && glIsFramebufferEXT(9));
    glBindRenderbufferEXT(RENDERBUFFER, 0);
    Single(c, FEnum_glBindRenderbuffer, {RENDERBUFFER, 0});
    assert(!Integer(RB_BINDING) && glIsRenderbufferEXT(2) && Queries.empty());

    /* The limits are the host's. */
    assert(Integer(MAX_ATTACHMENTS) == (GLint)HOST_ANSWER &&
           Integer(MAX_RB_SIZE) == (GLint)HOST_ANSWER);
    assert(Queries.size() == 2 && Queries[0][0] == FEnum_glGetIntegerv &&
           Queries[0][1] == MAX_ATTACHMENTS && Queries[1][1] == MAX_RB_SIZE);
    Queries.clear();
    Log.clear();

    /* Storage, attachments, mipmaps and blits: core functions, exact words. */
    glRenderbufferStorageEXT(RENDERBUFFER, DEPTH24_STENCIL8, 640, 4096);
    Single(c, FEnum_glRenderbufferStorage, {RENDERBUFFER, DEPTH24_STENCIL8, 640, 4096});
    glRenderbufferStorageEXT(RENDERBUFFER, GL_RGBA, 0, 0);
    Single(c, FEnum_glRenderbufferStorage, {RENDERBUFFER, GL_RGBA, 0, 0});
    glFramebufferTexture1DEXT(READ_FB, COLOR0 + 3, GL_TEXTURE_1D, 5, 11);
    Single(c, FEnum_glFramebufferTexture1D, {READ_FB, COLOR0 + 3, GL_TEXTURE_1D, 5, 11});
    glFramebufferTexture2DEXT(DRAW_FB, DEPTH_ATT, GL_TEXTURE_2D, 0x12345678, 0);
    Single(c, FEnum_glFramebufferTexture2D, {DRAW_FB, DEPTH_ATT, GL_TEXTURE_2D, 0x12345678, 0});
    glFramebufferTexture2DEXT(FRAMEBUFFER, STENCIL_ATT, CUBE_NZ, 6, 2);
    Single(c, FEnum_glFramebufferTexture2D, {FRAMEBUFFER, STENCIL_ATT, CUBE_NZ, 6, 2});
    glFramebufferTexture2DEXT(FRAMEBUFFER, COLOR0, GL_TEXTURE_2D, 0, 0); /* detaches */
    Single(c, FEnum_glFramebufferTexture2D, {FRAMEBUFFER, COLOR0, GL_TEXTURE_2D, 0, 0});
    glFramebufferTexture3DEXT(FRAMEBUFFER, COLOR0 + 1, TEX3D, 7, 3, 255);
    Single(c, FEnum_glFramebufferTexture3D, {FRAMEBUFFER, COLOR0 + 1, TEX3D, 7, 3, 255});
    glFramebufferRenderbufferEXT(DRAW_FB, STENCIL_ATT, RENDERBUFFER, 0xffffffffU);
    Single(c, FEnum_glFramebufferRenderbuffer, {DRAW_FB, STENCIL_ATT, RENDERBUFFER, 0xffffffffU});
    for (GLenum target : {(GLenum)GL_TEXTURE_1D, (GLenum)GL_TEXTURE_2D, CUBE, TEX3D}) {
        glGenerateMipmapEXT(target);
        Single(c, FEnum_glGenerateMipmap, {target});
    }
    glBlitFramebufferEXT(-1, -2, 0x7fffffff, (GLint)0x80000000, 4, 5, 6, 7,
                         COLOR_BIT | DEPTH_BIT | STENCIL_BIT, NEAREST);
    Single(c, FEnum_glBlitFramebuffer,
           {0xffffffffU, 0xfffffffeU, 0x7fffffffU, 0x80000000U, 4, 5, 6, 7,
            COLOR_BIT | DEPTH_BIT | STENCIL_BIT, NEAREST});
    glBlitFramebufferEXT(0, 0, 8, 8, 8, 8, 0, 0, COLOR_BIT, LINEAR);
    Single(c, FEnum_glBlitFramebuffer, {0, 0, 8, 8, 8, 8, 0, 0, COLOR_BIT, LINEAR});
    glBlitFramebufferEXT(0, 0, 8, 8, 0, 0, 8, 8, 0, LINEAR); /* copies nothing */
    Quiet(c);

    /* Host queries: one RESULT_INT each. */
    assert(glCheckFramebufferStatusEXT(DRAW_FB) == COMPLETE);
    HostStatus = 0x8cd6; /* INCOMPLETE_ATTACHMENT */
    assert(glCheckFramebufferStatusEXT(READ_FB) == 0x8cd6);
    GLint value = 0x5a5a;
    glGetFramebufferAttachmentParameterivEXT(FRAMEBUFFER, DEPTH_ATT, ZOFFSET, &value);
    assert(value == (GLint)HOST_ANSWER);
    value = 0x5a5a;
    glGetRenderbufferParameterivEXT(RENDERBUFFER, RB_STENCIL, &value);
    assert(value == (GLint)HOST_ANSWER);
    glGetRenderbufferParameterivEXT(RENDERBUFFER, RB_WIDTH, NULL); /* nowhere to store */
    glGetFramebufferAttachmentParameterivEXT(FRAMEBUFFER, COLOR0, OBJECT_TYPE, NULL);
    const Records queries = Sent(c);
    assert(queries.size() == 4 && Queries.size() == 4);
    ExpectQuery(queries[0], FEnum_glCheckFramebufferStatus, DRAW_FB, 0, 0);
    ExpectQuery(queries[1], FEnum_glCheckFramebufferStatus, READ_FB, 0, 0);
    ExpectQuery(queries[2], FEnum_glGetFramebufferAttachmentParameteriv, FRAMEBUFFER, DEPTH_ATT,
                ZOFFSET);
    ExpectQuery(queries[3], FEnum_glGetRenderbufferParameteriv, RENDERBUFFER, RB_STENCIL, 0);
    /* A failed query returns zero and keeps the error; params stay. */
    Queries.clear();
    FailOperation = DG_GL_QUERY;
    FailStatus = DG_ESCAPE_HOST;
    FailError = DG_GL_ERROR_UNSUPPORTED;
    assert(!glCheckFramebufferStatusEXT(FRAMEBUFFER));
    assert(c->Error == GL_INVALID_ENUM && !c->Failed);
    c->Error = 0;
    FailOperation = DG_GL_QUERY;
    value = 0x5a5a;
    glGetRenderbufferParameterivEXT(RENDERBUFFER, RB_FORMAT, &value);
    assert(c->Error == GL_INVALID_ENUM && !c->Failed && value == 0x5a5a);
    c->Error = 0;
    Log.clear();
    Close(c);
}

/* DeleteFramebuffers and DeleteRenderbuffers name only objects, in batches of
 * a packet; bound ones leave the window (no renderbuffer) bound. */
static void Deletion(void) {
    for (ULONG packet : {SMALL_PACKET, (ULONG)DG_ESCAPE_MAX_BYTES}) {
        JGL_CONTEXT *c = Open(FBO, packet);
        const ULONG batch = packet == SMALL_PACKET ? 117 : 256;
        assert(packet != SMALL_PACKET || JglMaxDataBytes(1) / 4 == batch);
        for (ULONG kind = 0; kind < 2; ++kind) {
            const BOOL fb = !kind;
            const ULONG function = fb ? FEnum_glDeleteFramebuffers : FEnum_glDeleteRenderbuffers;
            std::vector<GLuint> all(300);
            if (fb)
                glGenFramebuffersEXT(300, all.data());
            else
                glGenRenderbuffersEXT(300, all.data());
            for (ULONG i = 0; i < 300; ++i)
                assert(all[i] == i + 1);
            for (ULONG i = 0; i < 250; ++i) {
                if (fb)
                    glBindFramebufferEXT(i & 1 ? DRAW_FB : READ_FB, all[i]);
                else
                    glBindRenderbufferEXT(RENDERBUFFER, all[i]);
            }
            if (fb)
                glBindFramebufferEXT(READ_FB, 7);
            Submit(c);
            Log.clear();
            assert(fb ? Integer(FB_BINDING) == 250 && Integer(READ_FB_BINDING) == 7
                      : Integer(RB_BINDING) == 250);
            std::vector<GLuint> doomed(all);
            doomed.insert(doomed.begin() + 10, {0U, 5000U, all[3]});
            if (fb)
                glDeleteFramebuffersEXT((GLsizei)doomed.size(), doomed.data());
            else
                glDeleteRenderbuffersEXT((GLsizei)doomed.size(), doomed.data());
            assert(fb ? !Integer(FB_BINDING) && !Integer(READ_FB_BINDING) : !Integer(RB_BINDING));
            Submit(c);
            std::vector<GLuint> deleted;
            for (const std::vector<ULONG> &r : Log) {
                assert(r[0] == DG_GL_DATA_CALL && r[8] == function);
                assert(r[10] && r[10] <= batch && r[9] == r[10] * 4);
                assert(&r == &Log.back() || r[10] == batch);
                deleted.insert(deleted.end(), r.begin() + 11, r.begin() + 11 + r[10]);
            }
            assert(Log.size() == (250 + batch - 1) / batch);
            assert(deleted == std::vector<GLuint>(all.begin(), all.begin() + 250));
            Log.clear();
            for (GLuint n : all)
                assert(fb ? !glIsFramebufferEXT(n) : !glIsRenderbufferEXT(n));
            GLuint next = 0;
            if (fb)
                glGenFramebuffersEXT(1, &next);
            else
                glGenRenderbuffersEXT(1, &next);
            assert(next == 301);
            /* Deleting what is not bound keeps the bindings. */
            if (fb) {
                glBindFramebufferEXT(DRAW_FB, 400);
                glBindFramebufferEXT(READ_FB, 401);
                const GLuint other = 401;
                glDeleteFramebuffersEXT(1, &other);
                assert(Integer(FB_BINDING) == 400 && !Integer(READ_FB_BINDING));
                const GLuint draw = 400;
                glDeleteFramebuffersEXT(1, &draw);
                assert(!Integer(FB_BINDING));
            } else {
                glBindRenderbufferEXT(RENDERBUFFER, 400);
                const GLuint other = 401;
                glDeleteRenderbuffersEXT(1, &other);
                assert(Integer(RB_BINDING) == 400);
            }
            Submit(c);
            Log.clear();
        }
        Close(c);
    }
}

/* Every enum, level and size the host would reject as malformed is a local
 * error; Begin/End and the name tables' bounds too. */
static void Validation(void) {
    JGL_CONTEXT *c = Open(FBO);
    GLint value = 0x5a5a;
    /* Targets. */
    for (GLenum target : {0U, RENDERBUFFER, 0x8ca7U, 0x8d3fU, (GLenum)GL_TEXTURE_2D}) {
        glBindFramebufferEXT(target, 1);
        Rejected(c, GL_INVALID_ENUM);
        glFramebufferTexture2DEXT(target, COLOR0, GL_TEXTURE_2D, 1, 0);
        Rejected(c, GL_INVALID_ENUM);
        glFramebufferRenderbufferEXT(target, COLOR0, RENDERBUFFER, 1);
        Rejected(c, GL_INVALID_ENUM);
        assert(!glCheckFramebufferStatusEXT(target));
        Rejected(c, GL_INVALID_ENUM);
        glGetFramebufferAttachmentParameterivEXT(target, COLOR0, OBJECT_TYPE, &value);
        Rejected(c, GL_INVALID_ENUM);
    }
    for (GLenum target : {0U, FRAMEBUFFER, 0x8d40U + 2}) {
        glBindRenderbufferEXT(target, 1);
        Rejected(c, GL_INVALID_ENUM);
        glRenderbufferStorageEXT(target, GL_RGBA, 1, 1);
        Rejected(c, GL_INVALID_ENUM);
        glGetRenderbufferParameterivEXT(target, RB_WIDTH, &value);
        Rejected(c, GL_INVALID_ENUM);
        glFramebufferRenderbufferEXT(FRAMEBUFFER, COLOR0, target, 1);
        Rejected(c, GL_INVALID_ENUM);
    }
    assert(!glIsFramebufferEXT(1) && !glIsRenderbufferEXT(1) && !Integer(FB_BINDING));
    /* Attachments: four colour attachments, depth and stencil. */
    for (GLenum attachment :
         {0U, COLOR0 + 4, COLOR0 - 1, 0x8cefU, DEPTH_ATT + 1, 0x821aU, (GLenum)GL_FRONT}) {
        glFramebufferTexture1DEXT(FRAMEBUFFER, attachment, GL_TEXTURE_1D, 1, 0);
        Rejected(c, GL_INVALID_ENUM);
        glFramebufferTexture2DEXT(FRAMEBUFFER, attachment, GL_TEXTURE_2D, 1, 0);
        Rejected(c, GL_INVALID_ENUM);
        glFramebufferRenderbufferEXT(FRAMEBUFFER, attachment, RENDERBUFFER, 1);
        Rejected(c, GL_INVALID_ENUM);
        glGetFramebufferAttachmentParameterivEXT(FRAMEBUFFER, attachment, OBJECT_TYPE, &value);
        Rejected(c, GL_INVALID_ENUM);
    }
    /* Texture targets, even to detach: no cube faces or 3D textures without
     * their capabilities. */
    for (GLenum textarget : {0U, (GLenum)GL_TEXTURE_2D, CUBE, TEX3D}) {
        glFramebufferTexture1DEXT(FRAMEBUFFER, COLOR0, textarget, 0, 0);
        Rejected(c, GL_INVALID_ENUM);
    }
    for (GLenum textarget : {0U, (GLenum)GL_TEXTURE_1D, CUBE, CUBE_X, CUBE_NZ, TEX3D, PROXY_2D}) {
        glFramebufferTexture2DEXT(FRAMEBUFFER, COLOR0, textarget, 0, 0);
        Rejected(c, GL_INVALID_ENUM);
    }
    for (GLenum textarget : {(GLenum)GL_TEXTURE_2D, TEX3D, 0x8070U}) {
        glFramebufferTexture3DEXT(FRAMEBUFFER, COLOR0, textarget, 0, 0, 0);
        Rejected(c, GL_INVALID_ENUM);
    }
    for (GLenum target : {CUBE, TEX3D, 0U, 0x8064U}) {
        glGenerateMipmapEXT(target);
        Rejected(c, GL_INVALID_ENUM);
    }
    /* Levels. */
    for (GLint level : {-1, DG_GL_MAX_TEXTURE_LEVEL + 1, (GLint)0x80000000}) {
        glFramebufferTexture1DEXT(FRAMEBUFFER, COLOR0, GL_TEXTURE_1D, 1, level);
        Rejected(c, GL_INVALID_VALUE);
        glFramebufferTexture2DEXT(FRAMEBUFFER, COLOR0, GL_TEXTURE_2D, 0, level);
        Rejected(c, GL_INVALID_VALUE);
    }
    glFramebufferTexture2DEXT(FRAMEBUFFER, COLOR0, GL_TEXTURE_2D, 1, DG_GL_MAX_TEXTURE_LEVEL);
    Single(c, FEnum_glFramebufferTexture2D,
           {FRAMEBUFFER, COLOR0, GL_TEXTURE_2D, 1, DG_GL_MAX_TEXTURE_LEVEL});
    /* Storage formats: exactly gl.h's list. */
    const std::vector<GLenum> formats = {GL_RGB,
                                         GL_RGBA,
                                         0x2a10,
                                         0x804f,
                                         0x8050,
                                         0x8051,
                                         0x8052,
                                         0x8053,
                                         0x8054,
                                         0x8055,
                                         0x8056,
                                         0x8057,
                                         0x8058,
                                         0x8059,
                                         0x805a,
                                         0x805b,
                                         GL_DEPTH_COMPONENT,
                                         DEPTH16,
                                         DEPTH24,
                                         DEPTH32,
                                         GL_STENCIL_INDEX,
                                         0x8d46,
                                         0x8d47,
                                         0x8d48,
                                         0x8d49,
                                         DEPTH_STENCIL,
                                         DEPTH24_STENCIL8};
    for (GLenum format : formats) {
        glRenderbufferStorageEXT(RENDERBUFFER, format, 1, 2);
        Single(c, FEnum_glRenderbufferStorage, {RENDERBUFFER, format, 1, 2});
    }
    for (GLenum format : {0U, 1U, 4U, 0x1906U, 0x1909U, 0x804eU, 0x805cU, 0x8049U, 0x8040U, 0x81a4U,
                          0x81a8U, 0x8d45U, 0x8d4aU, 0x88efU, 0x88f1U, 0x84faU, 0x83f0U}) {
        glRenderbufferStorageEXT(RENDERBUFFER, format, 1, 1);
        Rejected(c, GL_INVALID_ENUM);
    }
    /* Sizes. */
    const GLsizei sizes[][2] = {{-1, 1},
                                {1, -1},
                                {DG_GL_MAX_RENDERBUFFER_SIZE + 1, 1},
                                {1, DG_GL_MAX_RENDERBUFFER_SIZE + 1}};
    for (const auto &size : sizes) {
        glRenderbufferStorageEXT(RENDERBUFFER, GL_RGBA, size[0], size[1]);
        Rejected(c, GL_INVALID_VALUE);
    }
    /* Blits: buffers, filters, and LINEAR for colour alone. */
    for (GLbitfield mask : {1U, 0x200U, 0x8000U, 0x80000000U, COLOR_BIT | 0x800U}) {
        glBlitFramebufferEXT(0, 0, 1, 1, 0, 0, 1, 1, mask, NEAREST);
        Rejected(c, GL_INVALID_VALUE);
    }
    for (GLenum filter : {0U, 0x2700U, 0x2602U, 0x25ffU}) {
        glBlitFramebufferEXT(0, 0, 1, 1, 0, 0, 1, 1, COLOR_BIT, filter);
        Rejected(c, GL_INVALID_ENUM);
    }
    for (GLbitfield mask :
         {DEPTH_BIT, STENCIL_BIT, COLOR_BIT | DEPTH_BIT, DEPTH_BIT | STENCIL_BIT}) {
        glBlitFramebufferEXT(0, 0, 1, 1, 0, 0, 1, 1, mask, LINEAR);
        Rejected(c, GL_INVALID_OPERATION);
        glBlitFramebufferEXT(0, 0, 1, 1, 0, 0, 1, 1, mask, NEAREST);
        Single(c, FEnum_glBlitFramebuffer, {0, 0, 1, 1, 0, 0, 1, 1, mask, NEAREST});
    }
    /* Query names. */
    for (GLenum pname : {0x8ccfU, 0x8cd5U, 0U, 0x8cd7U}) {
        glGetFramebufferAttachmentParameterivEXT(FRAMEBUFFER, COLOR0, pname, &value);
        Rejected(c, GL_INVALID_ENUM);
    }
    for (GLenum pname : {0x8d41U, 0x8d45U, 0x8d4fU, 0x8d56U, 0U, (GLenum)GL_TEXTURE_WIDTH}) {
        glGetRenderbufferParameterivEXT(RENDERBUFFER, pname, &value);
        Rejected(c, GL_INVALID_ENUM);
    }
    assert(value == 0x5a5a && Queries.empty());
    for (GLenum pname = OBJECT_TYPE; pname <= ZOFFSET; ++pname)
        glGetFramebufferAttachmentParameterivEXT(FRAMEBUFFER, STENCIL_ATT, pname, &value);
    for (GLenum pname :
         {0x8d42U, 0x8d43U, 0x8d44U, 0x8d50U, 0x8d51U, 0x8d52U, 0x8d53U, 0x8d54U, 0x8d55U})
        glGetRenderbufferParameterivEXT(RENDERBUFFER, pname, &value);
    assert(!c->Error && Queries.size() == 14);
    Queries.clear();
    Log.clear();
    /* Negative counts. */
    GLuint name = 9;
    glGenFramebuffersEXT(-1, &name);
    Rejected(c, GL_INVALID_VALUE);
    glGenRenderbuffersEXT(-1, &name);
    Rejected(c, GL_INVALID_VALUE);
    glDeleteFramebuffersEXT(-1, &name);
    Rejected(c, GL_INVALID_VALUE);
    glDeleteRenderbuffersEXT(-1, &name);
    Rejected(c, GL_INVALID_VALUE);
    assert(name == 9);
    /* zoffset below the 3D size limit. */
    Close(c);
    c = Open(FBO | DG_CAP_GL_TEXTURE_3D | DG_CAP_GL_TEXTURE_CUBE_MAP);
    for (GLint zoffset : {-1, DG_GL_MAX_3D_TEXTURE_SIZE, (GLint)0x80000000}) {
        glFramebufferTexture3DEXT(FRAMEBUFFER, COLOR0, TEX3D, 1, 0, zoffset);
        Rejected(c, GL_INVALID_VALUE);
    }
    glFramebufferTexture3DEXT(FRAMEBUFFER, COLOR0, TEX3D, 1, -1, 0);
    Rejected(c, GL_INVALID_VALUE);
    for (GLenum face = CUBE_X; face <= CUBE_NZ; ++face) {
        glFramebufferTexture2DEXT(FRAMEBUFFER, COLOR0, face, 1, 0);
        Single(c, FEnum_glFramebufferTexture2D, {FRAMEBUFFER, COLOR0, face, 1, 0});
    }
    glFramebufferTexture2DEXT(FRAMEBUFFER, COLOR0, CUBE, 1, 0); /* the cube, not a face */
    Rejected(c, GL_INVALID_ENUM);
    glFramebufferTexture2DEXT(FRAMEBUFFER, COLOR0, CUBE_NZ + 1, 1, 0);
    Rejected(c, GL_INVALID_ENUM);

    /* Inside Begin/End every command and the new state names are errors. */
    c->InBegin = TRUE;
    auto inside = [&](auto call) {
        call();
        assert(c->Error == GL_INVALID_OPERATION && !c->Records && !c->Used);
        c->Error = 0;
    };
    EveryEntry(c, Inoperable);
    inside([&] { assert(Integer(FB_BINDING) == 0x5a5a); });
    inside([&] { assert(Integer(MAX_ATTACHMENTS) == 0x5a5a); });
    c->InBegin = FALSE;
    assert(!glIsFramebufferEXT(1) && !Integer(FB_BINDING) && !Integer(RB_BINDING));
    Close(c);

    /* A context holds at most DG_GL_MAX_FRAMEBUFFERS names, the share group
     * DG_GL_MAX_RENDERBUFFERS. */
    for (ULONG kind = 0; kind < 2; ++kind) {
        const BOOL fb = !kind;
        const ULONG limit = fb ? DG_GL_MAX_FRAMEBUFFERS : DG_GL_MAX_RENDERBUFFERS;
        c = Open(FBO);
        auto gen = [&](GLsizei n, GLuint *out) {
            fb ? glGenFramebuffersEXT(n, out) : glGenRenderbuffersEXT(n, out);
        };
        auto bind = [&](GLuint n) {
            fb ? glBindFramebufferEXT(FRAMEBUFFER, n) : glBindRenderbufferEXT(RENDERBUFFER, n);
        };
        auto binding = [&] { return Integer(fb ? FB_BINDING : RB_BINDING); };
        auto exists = [&](GLuint n) { return fb ? glIsFramebufferEXT(n) : glIsRenderbufferEXT(n); };
        std::vector<GLuint> all(limit);
        gen((GLsizei)limit, all.data());
        for (ULONG i = 0; i < all.size(); ++i)
            assert(all[i] == i + 1);
        GLuint extra[2] = {5, 5};
        gen(1, extra);
        Rejected(c, GL_OUT_OF_MEMORY);
        bind(limit + 1);
        Rejected(c, GL_OUT_OF_MEMORY);
        assert(!binding() && !exists(limit + 1));
        bind(all[7]); /* a generated name has room */
        assert(binding() == (GLint)all[7] && exists(all[7]));
        fb ? glDeleteFramebuffersEXT(1, &all[0]) : glDeleteRenderbuffersEXT(1, &all[0]);
        Submit(c);
        assert(Log.size() == 1); /* the bind; never an object: nothing to delete */
        Log.clear();
        gen(2, extra); /* all or nothing */
        Rejected(c, GL_OUT_OF_MEMORY);
        assert(extra[0] == 5 && extra[1] == 5);
        gen(1, extra);
        assert(extra[0] == limit + 1);
        Close(c);
    }
}

/* With an object bound DrawBuffer and ReadBuffer take colour attachments and
 * NONE, never the window's buffers, without front-buffer presentation; the
 * window keeps its rules, the object's buffers never touch its draw buffer. */
static void Buffers(void) {
    for (BOOL front : {FALSE, TRUE}) {
        JGL_CONTEXT *c = Open(FBO, DG_ESCAPE_MAX_BYTES, front);
        assert(c->DrawBuffer == GL_BACK);
        /* The window. */
        for (GLenum mode : {COLOR0, COLOR0 + 3, (GLenum)GL_FRONT, (GLenum)GL_NONE}) {
            glDrawBuffer(mode);
            if (!front || mode - COLOR0 < 4) {
                Rejected(c, GL_INVALID_OPERATION);
                continue;
            }
            Single(c, FEnum_glDrawBuffer, {mode});
            assert(c->DrawBuffer == mode);
        }
        for (GLenum mode : {COLOR0, (GLenum)GL_NONE, (GLenum)GL_FRONT_AND_BACK, COLOR0 + 4}) {
            glReadBuffer(mode);
            Rejected(c, !front || mode == COLOR0 ? GL_INVALID_OPERATION : GL_INVALID_ENUM);
        }
        const GLenum window = c->DrawBuffer;
        /* A draw framebuffer object: its buffers alone. */
        glBindFramebufferEXT(DRAW_FB, 3);
        Drop(c);
        for (GLenum mode : {COLOR0, COLOR0 + 1, COLOR0 + 2, COLOR0 + 3, (GLenum)GL_NONE}) {
            glDrawBuffer(mode);
            Single(c, FEnum_glDrawBuffer, {mode});
            assert(c->DrawBuffer == window);
        }
        for (GLenum mode : {(GLenum)GL_FRONT, (GLenum)GL_BACK, (GLenum)GL_FRONT_LEFT,
                            (GLenum)GL_BACK_LEFT, (GLenum)GL_LEFT, (GLenum)GL_FRONT_AND_BACK}) {
            glDrawBuffer(mode);
            Rejected(c, GL_INVALID_OPERATION);
        }
        for (GLenum mode : {COLOR0 + 4, 0x8cefU, 0x0407U /* RIGHT */, 0x0409U /* AUX0 */, 1U}) {
            glDrawBuffer(mode);
            Rejected(c, GL_INVALID_ENUM);
        }
        /* The read framebuffer is still the window. */
        glReadBuffer(COLOR0);
        Rejected(c, GL_INVALID_OPERATION);
        glReadBuffer(GL_NONE);
        Rejected(c, front ? GL_INVALID_ENUM : GL_INVALID_OPERATION);
        /* A read framebuffer object, the window for drawing. */
        glBindFramebufferEXT(FRAMEBUFFER, 0);
        glBindFramebufferEXT(READ_FB, 4);
        Drop(c);
        for (GLenum mode : {COLOR0, COLOR0 + 3, (GLenum)GL_NONE}) {
            glReadBuffer(mode);
            Single(c, FEnum_glReadBuffer, {mode});
        }
        for (GLenum mode : {(GLenum)GL_FRONT, (GLenum)GL_BACK, (GLenum)GL_FRONT_LEFT,
                            (GLenum)GL_BACK_LEFT, (GLenum)GL_LEFT}) {
            glReadBuffer(mode);
            Rejected(c, GL_INVALID_OPERATION);
        }
        for (GLenum mode : {COLOR0 + 4, (GLenum)GL_FRONT_AND_BACK, 0x0407U}) {
            glReadBuffer(mode);
            Rejected(c, GL_INVALID_ENUM);
        }
        glDrawBuffer(COLOR0);
        Rejected(c, GL_INVALID_OPERATION);
        assert(c->DrawBuffer == window);
        /* Compiled, a buffer is judged against the binding now; it does not
         * change the window's draw buffer later either. */
        glBindFramebufferEXT(FRAMEBUFFER, 3);
        Drop(c);
        assert(JglListMode(GL_COMPILE));
        Clear(c);
        glDrawBuffer(GL_FRONT);
        glDrawBuffer(COLOR0 + 2);
        const Records compiled = Sent(c);
        assert(compiled.size() == 2);
        ExpectDeferred(compiled[0], GL_INVALID_OPERATION);
        ExpectCall(compiled[1], FEnum_glDrawBuffer, {COLOR0 + 2});
        assert(JglListMode(0) && c->DrawBuffer == window);
        Close(c);
    }
}

/* Drawing into an object never dirties the window's front buffer; a blit
 * into the window's front buffer does. Lists and the attribute stack keep the
 * window's draw buffer. */
static void FrontBuffer(void) {
    JGL_CONTEXT *c = Open(FBO | DG_CAP_GL_TEXTURE_IMAGES);
    static const GLfloat positions[3][2] = {{0, 0}, {1, 0}, {0, 1}};
    const BYTE pixels[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
    glVertexPointer(2, GL_FLOAT, 0, positions);
    glEnableClientState(GL_VERTEX_ARRAY);
    glDrawBuffer(GL_FRONT);
    Submit(c);
    Log.clear();
    auto draws = [&] {
        glClear(GL_COLOR_BUFFER_BIT);
        glBegin(GL_TRIANGLES);
        glVertex2f(0, 0);
        glEnd();
        glDrawArrays(GL_TRIANGLES, 0, 3);
        AliasDrawPixels(2, 2, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        AliasBitmap(8, 2, 0, 0, 0, 0, pixels);
        AliasCopyPixels(0, 0, 2, 2, GL_COLOR);
        glBlitFramebufferEXT(0, 0, 2, 2, 0, 0, 2, 2, COLOR_BIT, LINEAR);
        assert(!c->Error);
    };
    /* Into a draw framebuffer object: nothing for the window. */
    glBindFramebufferEXT(DRAW_FB, 1);
    draws();
    assert(!c->FrontDirty);
    JglCallList(5);
    assert(!c->FrontDirty && c->DrawBuffer == GL_FRONT);
    /* Each form dirties the window's front once the window draws. */
    glBindFramebufferEXT(DRAW_FB, 0);
    for (ULONG form = 0; form < 7; ++form) {
        Submit(c);
        c->FrontDirty = FALSE;
        if (form == 0)
            glClear(GL_COLOR_BUFFER_BIT);
        if (form == 1) {
            glBegin(GL_POINTS);
            glEnd();
        }
        if (form == 2)
            glDrawArrays(GL_POINTS, 0, 1);
        if (form == 3)
            AliasDrawPixels(2, 2, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        if (form == 4)
            AliasCopyPixels(0, 0, 2, 2, GL_COLOR);
        if (form == 5)
            glBlitFramebufferEXT(0, 0, 2, 2, 0, 0, 2, 2, COLOR_BIT, NEAREST);
        if (form == 6)
            JglCallList(5);
        assert(c->FrontDirty && !c->Error);
    }
    /* Blits of depth or stencil, packed pixels and copies leave the colour. */
    c->FrontDirty = FALSE;
    glBindFramebufferEXT(READ_FB, 1);
    glDrawBuffer(GL_FRONT);
    glBlitFramebufferEXT(0, 0, 2, 2, 0, 0, 2, 2, DEPTH_BIT | STENCIL_BIT, NEAREST);
    AliasDrawPixels(2, 1, DEPTH_STENCIL, UINT_24_8, pixels);
    AliasCopyPixels(0, 0, 2, 2, DEPTH_STENCIL);
    assert(!c->FrontDirty && !c->Error);
    /* A colour blit from the object into the window's front. */
    glBlitFramebufferEXT(0, 0, 2, 2, 0, 0, 2, 2, COLOR_BIT | DEPTH_BIT, NEAREST);
    assert(c->FrontDirty);
    const ULONG presents = Presents;
    glFlush();
    assert(Presents == presents + 1 && PresentFlags == DG_WINDOW_PRESENT_FRONT_ONLY &&
           !c->FrontDirty);
    /* Into the back buffer: nothing either. */
    glDrawBuffer(GL_BACK);
    draws();
    assert(!c->FrontDirty);
    /* A compiled blit records and dirties nothing until it executes. */
    glDrawBuffer(GL_FRONT);
    Drop(c);
    assert(JglListMode(GL_COMPILE));
    Clear(c);
    glBlitFramebufferEXT(0, 0, 1, 1, 0, 0, 1, 1, COLOR_BIT, NEAREST);
    glBlitFramebufferEXT(0, 0, 1, 1, 0, 0, 1, 1, DEPTH_BIT, LINEAR);
    glBlitFramebufferEXT(0, 0, 1, 1, 0, 0, 1, 1, 2, NEAREST);
    assert(!c->FrontDirty && !c->Error);
    Records compiled = Sent(c);
    assert(compiled.size() == 3);
    ExpectCall(compiled[0], FEnum_glBlitFramebuffer, {0, 0, 1, 1, 0, 0, 1, 1, COLOR_BIT, NEAREST});
    ExpectDeferred(compiled[1], GL_INVALID_OPERATION);
    ExpectDeferred(compiled[2], GL_INVALID_VALUE);
    assert(JglListMode(GL_COMPILE_AND_EXECUTE));
    glBlitFramebufferEXT(0, 0, 1, 1, 0, 0, 1, 1, COLOR_BIT, NEAREST);
    assert(c->FrontDirty);
    assert(JglListMode(0));
    Close(c);

    /* PushAttrib saves the window's draw buffer with the draw framebuffer; the
     * pop restores it only from and into the window. */
    c = Open(FBO);
    glPushAttrib(GL_COLOR_BUFFER_BIT); /* window, BACK */
    glDrawBuffer(GL_FRONT);
    glBindFramebufferEXT(FRAMEBUFFER, 2);
    glDrawBuffer(COLOR0 + 1);
    glPopAttrib(); /* into the object: the window keeps FRONT */
    assert(c->DrawBuffer == GL_FRONT);
    glPushAttrib(GL_COLOR_BUFFER_BIT); /* the object's */
    glBindFramebufferEXT(FRAMEBUFFER, 0);
    glDrawBuffer(GL_BACK);
    glPopAttrib(); /* from the object */
    assert(c->DrawBuffer == GL_BACK);
    glPushAttrib(GL_COLOR_BUFFER_BIT); /* window, BACK */
    glDrawBuffer(GL_FRONT);
    glBindFramebufferEXT(DRAW_FB, 2);
    glBindFramebufferEXT(DRAW_FB, 0);
    glPopAttrib(); /* from and into the window */
    assert(c->DrawBuffer == GL_BACK);
    glPushAttrib(1 /* CURRENT_BIT */);
    glDrawBuffer(GL_FRONT);
    glPopAttrib();
    assert(c->DrawBuffer == GL_FRONT && !c->Error);
    /* Once lists exist pops are untracked: unknown for the window only. */
    assert(JglListMode(0));
    glBindFramebufferEXT(FRAMEBUFFER, 2);
    glPushAttrib(GL_COLOR_BUFFER_BIT);
    glPopAttrib();
    assert(c->DrawBuffer == GL_FRONT);
    JglCallList(3);
    assert(c->DrawBuffer == GL_FRONT && !c->FrontDirty);
    glBindFramebufferEXT(FRAMEBUFFER, 0);
    glPushAttrib(GL_COLOR_BUFFER_BIT);
    glPopAttrib();
    assert(c->DrawBuffer == ~0u && !c->Error);
    Close(c);
}

/* Every command executes immediately while compiling: its records reach the
 * host as ever, its errors are immediate, the tracking follows. */
static void Lists(void) {
    JGL_CONTEXT *c = Open(FBO);
    GLuint names[2] = {};
    GLint value = 0;
    assert(JglListMode(GL_COMPILE));
    Clear(c);
    glGenFramebuffersEXT(2, names);
    glGenRenderbuffersEXT(1, names + 1);
    assert(names[0] == 1 && names[1] == 1);
    glBindFramebufferEXT(FRAMEBUFFER, 1);
    glBindRenderbufferEXT(RENDERBUFFER, 1);
    glRenderbufferStorageEXT(RENDERBUFFER, DEPTH24_STENCIL8, 4, 4);
    glFramebufferRenderbufferEXT(FRAMEBUFFER, DEPTH_ATT, RENDERBUFFER, 1);
    glGenerateMipmapEXT(GL_TEXTURE_2D);
    assert(glIsFramebufferEXT(1) && glIsRenderbufferEXT(1) && Integer(FB_BINDING) == 1 &&
           Integer(RB_BINDING) == 1);
    Records r = Sent(c);
    assert(r.size() == 5);
    ExpectCall(r[0], FEnum_glBindFramebuffer, {FRAMEBUFFER, 1});
    ExpectCall(r[1], FEnum_glBindRenderbuffer, {RENDERBUFFER, 1});
    ExpectCall(r[2], FEnum_glRenderbufferStorage, {RENDERBUFFER, DEPTH24_STENCIL8, 4, 4});
    ExpectCall(r[3], FEnum_glFramebufferRenderbuffer, {FRAMEBUFFER, DEPTH_ATT, RENDERBUFFER, 1});
    ExpectCall(r[4], FEnum_glGenerateMipmap, {GL_TEXTURE_2D});
    /* Immediate errors, not compiled ones. */
    glBindFramebufferEXT(0x1234, 1);
    Rejected(c, GL_INVALID_ENUM);
    glRenderbufferStorageEXT(RENDERBUFFER, GL_RGBA, -1, 1);
    Rejected(c, GL_INVALID_VALUE);
    glGenFramebuffersEXT(-1, names);
    Rejected(c, GL_INVALID_VALUE);
    /* Queries answer at once. */
    assert(glCheckFramebufferStatusEXT(FRAMEBUFFER) == COMPLETE);
    glGetRenderbufferParameterivEXT(RENDERBUFFER, RB_RED, &value);
    assert(value == (GLint)HOST_ANSWER);
    Log.clear();
    /* Deletion too: the bindings return to the window and none. */
    const GLuint one = 1;
    glDeleteFramebuffersEXT(1, &one);
    glDeleteRenderbuffersEXT(1, &one);
    assert(!Integer(FB_BINDING) && !Integer(RB_BINDING) && !glIsFramebufferEXT(1));
    r = Sent(c);
    assert(r.size() == 2);
    ExpectData(r[0], FEnum_glDeleteFramebuffers, {1}, &one, 4);
    ExpectData(r[1], FEnum_glDeleteRenderbuffers, {1}, &one, 4);
    assert(JglListMode(0));
    Close(c);
}

/* The ReadPixels queries so far (mode, x, y, size), forgotten with their
 * records. */
static std::vector<std::array<ULONG, 4>> Reads(void) {
    std::vector<std::array<ULONG, 4>> out;
    for (const auto &q : Queries) {
        assert(q[0] == FEnum_glReadPixels);
        out.push_back({q[1] & ~DG_GL_READ_X_MASK, q[1] & DG_GL_READ_X_MASK, q[2], q[3]});
    }
    Queries.clear();
    Log.clear();
    return out;
}
/* ReadPixels with a read framebuffer object need not fit the drawable: tiles
 * clip below DG_GL_MAX_DIMENSION, the host clips further. */
static void ReadClipping(void) {
    JGL_CONTEXT *c = Open(FBO | IMAGES);
    std::vector<ULONG> out(400 * 3, 0xcccccccc);
    auto read = [&](GLint x, GLint y, GLsizei w, GLsizei h) {
        std::fill(out.begin(), out.end(), 0xccccccccU);
        glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, out.data());
        assert(!c->Error);
        return Reads();
    };
    /* The window: the drawable (320x240) bounds the tiles. */
    auto reads = read(300, 230, 50, 20);
    assert(reads.size() == 1 && reads[0] == (std::array<ULONG, 4>{0, 300, 230, 20 | 10 << 16}));
    /* A draw framebuffer object alone changes nothing. */
    glBindFramebufferEXT(DRAW_FB, 1);
    assert(read(300, 230, 50, 20) == reads);
    /* A read framebuffer object: up to DG_GL_MAX_DIMENSION. */
    glBindFramebufferEXT(READ_FB, 1);
    reads = read(300, 230, 50, 3);
    assert(reads.size() == 1 && reads[0] == (std::array<ULONG, 4>{0, 300, 230, 50 | 3 << 16}));
    for (ULONG j = 0; j < 3; ++j)
        for (ULONG i = 0; i < 50; ++i)
            assert(out[j * 50 + i] == Color(300 + i, 230 + j));
    reads = read(4000, 4094, 400, 3);
    assert(reads.size() == 1 && reads[0] == (std::array<ULONG, 4>{0, 4000, 4094, 96 | 2 << 16}));
    for (ULONG j = 0; j < 3; ++j)
        for (ULONG i = 0; i < 400; ++i)
            assert(out[j * 400 + i] == (i < 96 && j < 2 ? Color(4000 + i, 4094 + j) : 0));
    reads = read(-10, -1, 20, 3);
    assert(reads.size() == 1 && reads[0] == (std::array<ULONG, 4>{0, 0, 0, 10 | 2 << 16}));
    assert(!out[0] && out[10 + 20] == Color(0, 0));
    assert(read(4096, 0, 8, 1).empty() && read(0, 5000, 8, 1).empty() && !out[0] && !out[7]);
    /* A full 4096-wide row splits at the readback capacity. */
    std::vector<ULONG> row(4096 * 5);
    glReadPixels(0, 4091, 4096, 5, GL_RGBA, GL_UNSIGNED_BYTE, row.data());
    reads = Reads();
    const ULONG tile = JglReadbackCapacity() / 4;
    assert(tile == 16384 && reads.size() == 2);
    assert(reads[0] == (std::array<ULONG, 4>{0, 0, 4091, 4096 | 4 << 16}));
    assert(reads[1] == (std::array<ULONG, 4>{0, 0, 4095, 4096 | 1 << 16}));
    assert(row[4 * 4096 + 4095] == Color(4095, 4095));
    /* Deleting the object returns reads to the window. */
    const GLuint one = 1;
    glDeleteFramebuffersEXT(1, &one);
    reads = read(300, 230, 50, 20);
    assert(reads.size() == 1 && reads[0] == (std::array<ULONG, 4>{0, 300, 230, 20 | 10 << 16}));
    Close(c);
}

/* DEPTH_STENCIL as UNSIGNED_INT_24_8: a DEPTH then a STENCIL read of each
 * tile, depth rounded to 24 bits above the stencil index's low byte. */
static ULONG Packed(ULONG x, ULONG y) {
    const double d = Depth(x, y);
    const ULONG z = !(d > 0.0) ? 0 : d >= 1.0 ? 0xffffff : (ULONG)(d * 16777215.0 + 0.5);
    return z << 8 | (Stencil(x, y) & 0xff);
}
static void DepthStencilReads(void) {
    JGL_CONTEXT *c = Open(FBO | IMAGES);
    /* Exact values: clamping, rounding and NaN. */
    assert(Packed(0, 0) == (0x000000U << 8 | 0x00)); /* 0.0, stencil 0x100 */
    assert(Packed(1, 0) == (0xffffffU << 8 | 0x03)); /* 1.0 */
    assert(Packed(2, 0) == (0x800000U << 8 | 0x06)); /* 0.5 */
    assert(Packed(3, 0) == (0x000000U << 8 | 0x09)); /* -0.25 clamps */
    assert(Packed(4, 0) == (0xffffffU << 8 | 0x0c)); /* 1.5 clamps */
    assert(Packed(0, 1) == (0x000000U << 8 | 0x05)); /* NaN */
    assert(Packed(1, 1) == (0x400000U << 8 | 0x08)); /* 0.25 */
    assert(Packed(2, 1) == (0x555555U << 8 | 0x0b)); /* 1/3 */
    std::vector<ULONG> out(100 * 100, 0xcccccccc);
    glBindFramebufferEXT(READ_FB, 6);
    glReadPixels(0, 0, 100, 100, DEPTH_STENCIL, UINT_24_8, out.data());
    assert(!c->Error);
    auto reads = Reads();
    /* Eight wire bytes per pixel: 81 rows of 100, then 19. */
    assert(reads.size() == 4);
    assert(reads[0] == (std::array<ULONG, 4>{DG_GL_READ_DEPTH, 0, 0, 100 | 81 << 16}));
    assert(reads[1] == (std::array<ULONG, 4>{DG_GL_READ_STENCIL, 0, 0, 100 | 81 << 16}));
    assert(reads[2] == (std::array<ULONG, 4>{DG_GL_READ_DEPTH, 0, 81, 100 | 19 << 16}));
    assert(reads[3] == (std::array<ULONG, 4>{DG_GL_READ_STENCIL, 0, 81, 100 | 19 << 16}));
    for (ULONG y = 0; y < 100; ++y)
        for (ULONG x = 0; x < 100; ++x)
            assert(out[y * 100 + x] == Packed(x, y));
    /* Pack state: swapped bytes as one 32-bit unit, row length, skips, and
     * zero outside the framebuffer's reach. */
    glPixelStorei(GL_PACK_SWAP_BYTES, 1);
    glPixelStorei(GL_PACK_ROW_LENGTH, 7);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 1);
    glPixelStorei(GL_PACK_SKIP_ROWS, 1);
    std::vector<BYTE> packed(7 * 4 * 4, 0xcc);
    glReadPixels(4094, 7, 4, 3, DEPTH_STENCIL, UINT_24_8, packed.data());
    assert(!c->Error);
    reads = Reads();
    assert(reads.size() == 2 &&
           reads[0] == (std::array<ULONG, 4>{DG_GL_READ_DEPTH, 4094, 7, 2 | 3 << 16}) &&
           reads[1] == (std::array<ULONG, 4>{DG_GL_READ_STENCIL, 4094, 7, 2 | 3 << 16}));
    for (ULONG row = 0; row < 4; ++row)
        for (ULONG column = 0; column < 7; ++column) {
            const BYTE *p = &packed[(row * 7 + column) * 4];
            if (row < 1 || column < 1 || column > 4) {
                assert(p[0] == 0xcc && p[1] == 0xcc && p[2] == 0xcc && p[3] == 0xcc);
                continue;
            }
            const ULONG x = 4094 + column - 1, y = 7 + row - 1;
            const ULONG expected = x < 4096 ? Packed(x, y) : 0;
            assert(p[0] == expected >> 24 && p[1] == ((expected >> 16) & 255) &&
                   p[2] == ((expected >> 8) & 255) && p[3] == (expected & 255));
        }
    glPixelStorei(GL_PACK_SWAP_BYTES, 0);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);
    glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_PACK_SKIP_ROWS, 0);
    /* The window too (its D24S8 buffers), within the drawable. */
    glBindFramebufferEXT(READ_FB, 0);
    glReadPixels(318, 0, 4, 1, DEPTH_STENCIL, UINT_24_8, out.data());
    reads = Reads();
    assert(reads.size() == 2 && reads[0][3] == (2 | 1 << 16) && out[0] == Packed(318, 0) &&
           out[1] == Packed(319, 0) && !out[2] && !out[3]);
    /* DEPTH and STENCIL alone keep their modes. */
    GLfloat depth = 0;
    GLuint stencil = 0;
    glReadPixels(2, 0, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
    glReadPixels(2, 0, 1, 1, GL_STENCIL_INDEX, GL_UNSIGNED_INT, &stencil);
    reads = Reads();
    assert(depth == 0.5f && stencil == Stencil(2, 0) && reads.size() == 2 &&
           reads[0][0] == DG_GL_READ_DEPTH && reads[1][0] == DG_GL_READ_STENCIL);
    /* Formats and types. */
    for (GLenum type :
         {(GLenum)GL_UNSIGNED_INT, (GLenum)GL_FLOAT, (GLenum)GL_UNSIGNED_BYTE, 0x1a00U, 0x8367U}) {
        glReadPixels(0, 0, 1, 1, DEPTH_STENCIL, type, out.data());
        Rejected(c, GL_INVALID_ENUM);
    }
    for (GLenum format : {(GLenum)GL_RGBA, (GLenum)GL_DEPTH_COMPONENT, (GLenum)GL_STENCIL_INDEX}) {
        glReadPixels(0, 0, 1, 1, format, UINT_24_8, out.data());
        Rejected(c, GL_INVALID_OPERATION);
    }
    /* Depth textures cannot be read (no ARB_depth_texture). */
    for (GLenum format : {(GLenum)GL_DEPTH_COMPONENT, DEPTH_STENCIL}) {
        glGetTexImage(GL_TEXTURE_2D, 0, format, format == DEPTH_STENCIL ? UINT_24_8 : GL_FLOAT,
                      out.data());
        Rejected(c, GL_INVALID_ENUM);
    }
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, UINT_24_8, out.data());
    Rejected(c, GL_INVALID_ENUM);
    assert(Queries.empty());
    Close(c);
    /* Typed reads need DG_CAP_GL_DEPTH_STENCIL_READBACK as well. */
    c = Open(FBO | DG_CAP_GL_BULK_READBACK);
    glReadPixels(0, 0, 1, 1, DEPTH_STENCIL, UINT_24_8, out.data());
    Rejected(c, GL_INVALID_OPERATION);
    Close(c);
}

/* Depth textures: borderless TEXTURE_2D images in depth formats with depth
 * pixels, gathered and swapped like scalar components (24_8 as one unit). */
static void DepthTextures(void) {
    JGL_CONTEXT *c = Open(FBO | IMAGES | DG_CAP_GL_TEXTURE_CUBE_MAP);
    BYTE source[256];
    for (ULONG i = 0; i < sizeof(source); ++i)
        source[i] = (BYTE)(i * 7 + 3);
    /* Every depth format, each depth pixel type, in one record. */
    for (GLenum internal :
         {(GLenum)GL_DEPTH_COMPONENT, DEPTH16, DEPTH24, DEPTH32, DEPTH_STENCIL, DEPTH24_STENCIL8}) {
        for (GLenum type : {(GLenum)GL_BYTE, (GLenum)GL_UNSIGNED_BYTE, (GLenum)GL_SHORT,
                            (GLenum)GL_UNSIGNED_SHORT, (GLenum)GL_INT, (GLenum)GL_UNSIGNED_INT,
                            (GLenum)GL_FLOAT}) {
            const ULONG unit = type <= GL_UNSIGNED_BYTE ? 1 : type <= GL_UNSIGNED_SHORT ? 2 : 4;
            glTexImage2D(GL_TEXTURE_2D, 1, (GLint)internal, 4, 2, 0, GL_DEPTH_COMPONENT, type,
                         source);
            const Records r = Sent(c);
            assert(r.size() == 1);
            ExpectData(r[0], FEnum_glTexImage2D,
                       {GL_TEXTURE_2D, 1, internal, 4, 2, 0, GL_DEPTH_COMPONENT, type}, source,
                       8 * unit);
        }
        glTexImage2D(GL_TEXTURE_2D, 0, (GLint)internal, 2, 2, 0, DEPTH_STENCIL, UINT_24_8, source);
        Records r = Sent(c);
        assert(r.size() == 1);
        ExpectData(r[0], FEnum_glTexImage2D,
                   {GL_TEXTURE_2D, 0, internal, 2, 2, 0, DEPTH_STENCIL, UINT_24_8}, source, 16);
        /* Zero-defined storage. */
        glTexImage2D(GL_TEXTURE_2D, 0, (GLint)internal, 64, 32, 0, GL_DEPTH_COMPONENT,
                     GL_UNSIGNED_INT, NULL);
        r = Sent(c);
        assert(r.size() == 1);
        ExpectData(r[0], FEnum_glTexImage2D,
                   {GL_TEXTURE_2D, 0, internal, 64, 32, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT},
                   NULL, 0);
    }
    /* Subimages. */
    glTexSubImage2D(GL_TEXTURE_2D, 0, 1, 2, 2, 1, DEPTH_STENCIL, UINT_24_8, source);
    Records r = Sent(c);
    assert(r.size() == 1);
    ExpectData(r[0], FEnum_glTexSubImage2D,
               {GL_TEXTURE_2D, 0, 1, 2, 2, 1, DEPTH_STENCIL, UINT_24_8}, source, 8);
    glTexSubImage2D(GL_TEXTURE_2D, 2, 0, 0, 3, 1, GL_DEPTH_COMPONENT, GL_FLOAT, source);
    r = Sent(c);
    assert(r.size() == 1);
    ExpectData(r[0], FEnum_glTexSubImage2D,
               {GL_TEXTURE_2D, 2, 0, 0, 3, 1, GL_DEPTH_COMPONENT, GL_FLOAT}, source, 12);
    /* Swapped bytes: components of their size, 24_8 as one 32-bit unit; padded
     * rows are gathered tight. */
    glPixelStorei(GL_UNPACK_SWAP_BYTES, 1);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 3);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 1);
    for (GLenum type : {(GLenum)GL_UNSIGNED_SHORT, (GLenum)GL_UNSIGNED_INT, UINT_24_8}) {
        const ULONG unit = type == GL_UNSIGNED_SHORT ? 2 : 4;
        const GLenum format = type == UINT_24_8 ? DEPTH_STENCIL : GL_DEPTH_COMPONENT;
        glTexImage2D(GL_TEXTURE_2D, 0, DEPTH24_STENCIL8, 2, 2, 0, format, type, source);
        r = Sent(c);
        std::vector<BYTE> expected;
        const ULONG stride = (3 * unit + 3) & ~3U;
        for (ULONG row = 0; row < 2; ++row)
            for (ULONG pixel = 0; pixel < 2; ++pixel)
                for (ULONG k = 0; k < unit; ++k)
                    expected.push_back(source[row * stride + (1 + pixel) * unit + unit - 1 - k]);
        assert(r.size() == 2);
        ExpectData(r[0], FEnum_glTexImage2D,
                   {GL_TEXTURE_2D, 0, DEPTH24_STENCIL8, 2, 2, 0, format, type}, NULL, 0);
        ExpectData(r[1], FEnum_glTexSubImage2D, {GL_TEXTURE_2D, 0, 0, 0, 2, 2, format, type},
                   expected.data(), (ULONG)expected.size());
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 2, 2, format, type, source);
        r = Sent(c);
        assert(r.size() == 1);
        ExpectData(r[0], FEnum_glTexSubImage2D, {GL_TEXTURE_2D, 0, 0, 0, 2, 2, format, type},
                   expected.data(), (ULONG)expected.size());
    }
    glPixelStorei(GL_UNPACK_SWAP_BYTES, 0);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);

    /* Borders, mismatched pixels, formats and types. */
    glTexImage2D(GL_TEXTURE_2D, 0, DEPTH24, 4, 4, 1, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
    Rejected(c, GL_INVALID_VALUE);
    glTexImage2D(GL_TEXTURE_2D, 0, DEPTH24, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    Rejected(c, GL_INVALID_OPERATION);
    glTexImage2D(GL_TEXTURE_2D, 0, RGBA8, 4, 4, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
    Rejected(c, GL_INVALID_OPERATION);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 4, 4, 0, DEPTH_STENCIL, UINT_24_8, NULL);
    Rejected(c, GL_INVALID_OPERATION);
    for (GLenum type : {(GLenum)GL_UNSIGNED_INT, (GLenum)GL_FLOAT, 0x1a00U, 0x8035U, 0x1234U}) {
        glTexImage2D(GL_TEXTURE_2D, 0, DEPTH24_STENCIL8, 4, 4, 0, DEPTH_STENCIL, type, NULL);
        Rejected(c, GL_INVALID_ENUM);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1, 1, DEPTH_STENCIL, type, source);
        Rejected(c, GL_INVALID_ENUM);
    }
    for (GLenum format : {(GLenum)GL_RGBA, (GLenum)GL_DEPTH_COMPONENT, (GLenum)GL_LUMINANCE}) {
        glTexImage2D(GL_TEXTURE_2D, 0, DEPTH24, 4, 4, 0, format, UINT_24_8, NULL);
        Rejected(c, GL_INVALID_OPERATION);
    }
    glTexImage2D(GL_TEXTURE_2D, 0, DEPTH24, 4, 4, 0, 0x1234, UINT_24_8, NULL);
    Rejected(c, GL_INVALID_ENUM);
    glTexImage2D(GL_TEXTURE_2D, 0, DEPTH24, 8, 1, 0, GL_DEPTH_COMPONENT, 0x1a00, NULL);
    Rejected(c, GL_INVALID_ENUM);
    glTexImage2D(GL_TEXTURE_2D, 0, DEPTH24, 4, 4, 0, GL_DEPTH_COMPONENT, PACKED_565, NULL);
    Rejected(c, GL_INVALID_OPERATION);
    /* Other targets keep their errors: 1D, cube faces and proxies. */
    glTexImage1D(GL_TEXTURE_1D, 0, DEPTH24, 4, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
    Rejected(c, GL_INVALID_ENUM);
    glTexImage1D(GL_TEXTURE_1D, 0, DEPTH24, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    Rejected(c, GL_INVALID_VALUE);
    glTexImage2D(CUBE_X, 0, DEPTH24, 4, 4, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
    Rejected(c, GL_INVALID_ENUM);
    glTexImage2D(CUBE_X, 0, DEPTH24, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    Rejected(c, GL_INVALID_VALUE);
    glTexSubImage2D(CUBE_X, 0, 0, 0, 1, 1, DEPTH_STENCIL, UINT_24_8, source);
    Rejected(c, GL_INVALID_ENUM);
    glTexImage2D(PROXY_2D, 0, DEPTH24, 4, 4, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
    Rejected(c, GL_INVALID_ENUM);
    glTexImage2D(PROXY_2D, 0, DEPTH24, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    Rejected(c, GL_INVALID_VALUE);
    /* Copies: borderless depth formats; native GL judges the rest. */
    glCopyTexImage2D(GL_TEXTURE_2D, 0, DEPTH24_STENCIL8, 0, 0, 4, 4, 1);
    Rejected(c, GL_INVALID_VALUE);
    glCopyTexImage2D(GL_TEXTURE_2D, 0, DEPTH24_STENCIL8, 0, 0, 4, 4, 0);
    Single(c, FEnum_glCopyTexImage2D, {GL_TEXTURE_2D, 0, DEPTH24_STENCIL8, 0, 0, 4, 4, 0});
    glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 1, 1, 0, 0, 2, 2);
    Single(c, FEnum_glCopyTexSubImage2D, {GL_TEXTURE_2D, 0, 1, 1, 0, 0, 2, 2});
    glCopyTexImage2D(CUBE_X, 0, DEPTH24, 0, 0, 4, 4, 0);
    Rejected(c, GL_INVALID_VALUE);
    /* Compiled uploads defer their errors as ever. */
    assert(JglListMode(GL_COMPILE));
    Clear(c);
    glTexImage2D(GL_TEXTURE_2D, 0, DEPTH24, 4, 4, 1, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
    glTexImage2D(GL_TEXTURE_2D, 0, DEPTH24, 1, 1, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, source);
    r = Sent(c);
    assert(r.size() == 2);
    ExpectDeferred(r[0], GL_INVALID_VALUE);
    ExpectData(r[1], FEnum_glTexImage2D,
               {GL_TEXTURE_2D, 0, DEPTH24, 1, 1, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT}, source,
               2);
    assert(JglListMode(0));
    Close(c);

    /* Beyond one record: a payload-free image, then block-aligned tiles. */
    c = Open(FBO | IMAGES, SMALL_PACKET);
    std::vector<GLfloat> depths(64 * 16);
    for (ULONG i = 0; i < depths.size(); ++i)
        depths[i] = (GLfloat)i / 1024.0f;
    glTexImage2D(GL_TEXTURE_2D, 0, DEPTH32, 64, 16, 0, GL_DEPTH_COMPONENT, GL_FLOAT, depths.data());
    r = Sent(c);
    assert(r.size() > 2);
    ExpectData(r[0], FEnum_glTexImage2D,
               {GL_TEXTURE_2D, 0, DEPTH32, 64, 16, 0, GL_DEPTH_COMPONENT, GL_FLOAT}, NULL, 0);
    std::vector<GLfloat> rebuilt(depths.size(), -1.0f);
    for (size_t i = 1; i < r.size(); ++i) {
        const std::vector<ULONG> &t = r[i];
        assert(t[0] == DG_GL_DATA_CALL && t[8] == FEnum_glTexSubImage2D &&
               t[16] == GL_DEPTH_COMPONENT && t[17] == GL_FLOAT && t[9] == t[14] * t[15] * 4 &&
               !(t[12] % 4) && !(t[13] % 4));
        for (ULONG y = 0; y < t[15]; ++y)
            memcpy(&rebuilt[(t[13] + y) * 64 + t[12]],
                   (const BYTE *)(t.data() + 18) + y * t[14] * 4, t[14] * 4);
    }
    assert(rebuilt == depths);
    Close(c);

    /* TEXTURE_IMAGES is still required for scalar types and new formats. */
    c = Open(FBO);
    glTexImage2D(GL_TEXTURE_2D, 0, DEPTH24, 4, 4, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, NULL);
    Rejected(c, GL_INVALID_OPERATION);
    glTexImage2D(GL_TEXTURE_2D, 0, DEPTH24, 4, 4, 0, DEPTH_STENCIL, UINT_24_8, NULL);
    Rejected(c, GL_INVALID_OPERATION);
    glTexImage2D(GL_TEXTURE_2D, 0, DEPTH24, 4, 4, 0, GL_DEPTH_COMPONENT, GL_UNSIGNED_BYTE, NULL);
    Rejected(c, GL_INVALID_OPERATION);
    Close(c);
}

/* DrawPixels takes DEPTH_STENCIL as four-byte UNSIGNED_INT_24_8 units;
 * CopyPixels takes the DEPTH_STENCIL type. */
static void DepthStencilPixels(void) {
    JGL_CONTEXT *c = Open(FBO);
    const BYTE pixels[8] = {1, 2, 3, 4, 5, 6, 7, 8}, swapped[8] = {4, 3, 2, 1, 8, 7, 6, 5};
    AliasDrawPixels(2, 1, DEPTH_STENCIL, UINT_24_8, pixels);
    Records r = Sent(c);
    assert(r.size() == 1);
    ExpectData(r[0], FEnum_glDrawPixels, {2, 1, DEPTH_STENCIL, UINT_24_8, 8, 0, 3, 1}, pixels, 8);
    glPixelStorei(GL_UNPACK_SWAP_BYTES, 1);
    AliasDrawPixels(2, 1, DEPTH_STENCIL, UINT_24_8, pixels);
    r = Sent(c);
    assert(r.size() == 1);
    ExpectData(r[0], FEnum_glDrawPixels, {2, 1, DEPTH_STENCIL, UINT_24_8, 8, 0, 3, 2}, swapped, 8);
    glPixelStorei(GL_UNPACK_SWAP_BYTES, 0);
    for (GLenum type :
         {(GLenum)GL_UNSIGNED_INT, (GLenum)GL_FLOAT, (GLenum)GL_UNSIGNED_BYTE, 0x1a00U}) {
        AliasDrawPixels(2, 1, DEPTH_STENCIL, type, pixels);
        Rejected(c, GL_INVALID_ENUM);
    }
    for (GLenum format : {(GLenum)GL_RGBA, (GLenum)GL_DEPTH_COMPONENT, (GLenum)GL_STENCIL_INDEX}) {
        AliasDrawPixels(2, 1, format, UINT_24_8, pixels);
        Rejected(c, GL_INVALID_OPERATION);
    }
    AliasCopyPixels(-1, 2, 3, 4, DEPTH_STENCIL);
    Single(c, FEnum_glCopyPixels, {0xffffffffU, 2, 3, 4, DEPTH_STENCIL});
    AliasCopyPixels(0, 0, 3, 4, DEPTH_STENCIL + 1);
    Rejected(c, GL_INVALID_ENUM);
    /* Compiled, their errors are deferred. */
    assert(JglListMode(GL_COMPILE));
    Clear(c);
    AliasDrawPixels(2, 1, GL_RGBA, UINT_24_8, pixels);
    r = Sent(c);
    assert(r.size() == 1);
    ExpectDeferred(r[0], GL_INVALID_OPERATION);
    assert(JglListMode(0));
    Close(c);
}

/* Makes a context current, forgetting the transport records that costs. */
static void Switch(HDC dc, HGLRC context) {
    assert(wglMakeCurrent(dc, context));
    Log.clear();
}
/* Renderbuffers belong to the share group, framebuffers to one context; the
 * group's last context frees the renderbuffer names. */
static void Sharing(void) {
    Reset();
    HostCaps = FBO;
    SubmittedRecord = Capture;
    AnswerQuery = Answer;
    HGLRC a = Create(), b = wglCreateContext((HDC)2), u = wglCreateContext((HDC)2);
    assert(b && u && wglShareLists(a, b));
    JGL_CONTEXT *ca = Lookup(a), *cb = Lookup(b);
    GLuint names[2] = {}, name = 0;
    glGenFramebuffersEXT(2, names);
    glGenRenderbuffersEXT(2, names);
    glBindFramebufferEXT(FRAMEBUFFER, 1);
    glBindRenderbufferEXT(RENDERBUFFER, 1);
    Submit(ca);
    Log.clear();
    /* b: the renderbuffers, not the framebuffers or bindings. */
    Switch((HDC)2, b);
    assert(glIsRenderbufferEXT(1) && !glIsRenderbufferEXT(2) && !glIsFramebufferEXT(1));
    assert(!Integer(FB_BINDING) && !Integer(RB_BINDING) && !cb->Framebuffer.Names);
    glGenRenderbuffersEXT(1, &name);
    assert(name == 3);
    glGenFramebuffersEXT(1, &name);
    assert(name == 1);
    glBindFramebufferEXT(FRAMEBUFFER, 1);
    glBindRenderbufferEXT(RENDERBUFFER, 1);
    Submit(cb);
    assert(Log.size() == 2 && Log[0][3] == cb->Id);
    Log.clear();
    /* Deletion in b resets b's renderbuffer binding only. */
    const GLuint one = 1;
    glDeleteRenderbuffersEXT(1, &one);
    assert(!Integer(RB_BINDING));
    Switch((HDC)1, a);
    assert(Integer(RB_BINDING) == 1 && !glIsRenderbufferEXT(1) && glIsFramebufferEXT(1));
    /* u: its own names. */
    Switch((HDC)2, u);
    assert(!glIsRenderbufferEXT(3));
    glGenRenderbuffersEXT(1, &name);
    assert(name == 1);
    glBindRenderbufferEXT(RENDERBUFFER, 1);
    /* The table outlives a; b's last release frees it. */
    Switch((HDC)1, a);
    glBindRenderbufferEXT(RENDERBUFFER, 3);
    assert(wglMakeCurrent(NULL, NULL) && wglDeleteContext(a));
    Switch((HDC)2, b);
    assert(glIsRenderbufferEXT(3) && !cb->Error);
    const ULONG held = Allocations;
    assert(wglMakeCurrent(NULL, NULL) && wglDeleteContext(b));
    assert(Allocations < held);
    Switch((HDC)2, u);
    assert(glIsRenderbufferEXT(1) && !glIsRenderbufferEXT(3));
    assert(wglMakeCurrent(NULL, NULL) && wglDeleteContext(u));
    assert(!Allocations);
    Log.clear();
}

int main() {
    Unnegotiated();
    Negotiation();
    Records_();
    Deletion();
    Validation();
    Buffers();
    FrontBuffer();
    Lists();
    ReadClipping();
    DepthStencilReads();
    DepthTextures();
    DepthStencilPixels();
    Sharing();
    puts("PASS framebuffer object frontend");
    return 0;
}
