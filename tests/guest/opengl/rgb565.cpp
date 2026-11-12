/* SPDX-License-Identifier: GPL-2.0-or-later
 * DG_CAP2_GL_RGB565 (exact R5G6B5 storage) through the actual frontend on the
 * strict transport fixture: negotiation from the CAPABILITIES reply's
 * DeviceError and the unchanged frontend without it (an older host, or an
 * older driver that leaves DeviceError zero); no extension name either way;
 * exact records of RGB565 1D, 2D, rectangle, cube face and 3D images, proxies,
 * copies and renderbuffer storage, within the host's rules and admitted by the
 * NT validator; the local errors RGB565 keeps without the capability; and
 * display lists that capture the records. */
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
                 CUBE_X = 0x8515, CUBE_NZ = 0x851a, PROXY_1D = 0x8063, PROXY_2D = 0x8064,
                 PROXY_RECT = 0x84f7, PROXY_CUBE = 0x851b, PROXY_3D = 0x8070, RENDERBUFFER = 0x8d41,
                 RGB5 = 0x8050, RGB8 = 0x8051, PACKED_565 = 0x8363;
constexpr ULONG RGB565 = DG_GL_RGB565;
constexpr ULONG OTHERS =
    DG_CAP_GL_MULTITEXTURE | DG_CAP_GL_FIXED_STATE | DG_CAP_GL_TEXTURE_CONTROL |
    DG_CAP_GL_COMPRESSED_TEXTURES | DG_CAP_GL_FOG_COORD | DG_CAP_GL_TEXTURE_CUBE_MAP |
    DG_CAP_GL_VERTEX_PROGRAM | DG_CAP_GL_TEXTURE_3D | DG_CAP_GL_VERTEX_BUFFER_OBJECT |
    DG_CAP_GL_FRAMEBUFFER_OBJECT | DG_CAP_GL_VERTEX_ARRAY_BGRA | DG_CAP_GL_OCCLUSION_QUERY |
    DG_CAP_GL_TEXTURE_RECTANGLE | DG_CAP_GL_TEXTURE_IMAGES | DG_CAP_GL_TEXTURE_BORDERS |
    DG_CAP_GL_BULK_READBACK | DG_CAP_GL_FRAGMENT_PROGRAM;
constexpr ULONG EXACT = DG_CAP2_GL_RGB565, DEPTH_TEXTURE = DG_CAP2_GL_DEPTH_TEXTURE;

/* The host's colour internal formats (gl_validation.rs): GL 1.1's sized and
 * base formats, and RGB565. Anything else in an image or copy record is a
 * batch fault that fails the context. */
static BOOL HostColourFormat(ULONG f) {
    return (f >= 1 && f <= 4) || (f >= 0x1906 && f <= 0x190a) || f == 0x8049 || f == 0x2a10 ||
           (f >= 0x803b && f <= 0x8048) || (f >= 0x804a && f <= 0x804d) ||
           (f >= 0x804f && f <= 0x805b) || f == RGB565;
}
static void HostShape(const std::vector<ULONG> &r) {
    if (r[0] == DG_GL_DATA_CALL &&
        (r[8] == FEnum_glTexImage1D || r[8] == FEnum_glTexImage2D || r[8] == FEnum_glTexImage3D))
        assert(HostColourFormat(r[12]));
    if (r[0] == DG_GL_CALL && (r[8] == FEnum_glCopyTexImage2D || r[8] == FEnum_glCopyTexImage1D))
        assert(HostColourFormat(r[11]));
    if (r[0] == DG_GL_CALL && r[8] == FEnum_glRenderbufferStorage)
        assert(r[10] == RGB565 || r[10] == RGB5 || r[10] == RGB8);
}
/* Argument words of the records this suite submits, as the host reports them
 * to the NT kernel. */
static ULONG KernelWords(void *, ULONG function) {
    switch (function) {
        case FEnum_glBindRenderbuffer:
            return 2;
        case FEnum_glRenderbufferStorage:
            return 4;
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
        case FEnum_glTexImage3D:
            return DG_GL_FUNCTION_INLINE_DATA | 9;
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
static JGL_CONTEXT *Open(ULONG caps, ULONG caps2) {
    Reset();
    HostCaps = caps;
    HostCaps2 = caps2;
    SubmittedRecord = Capture;
    JGL_CONTEXT *c = Lookup(Create());
    Clear(c);
    return c;
}
static void Close(JGL_CONTEXT *c) {
    assert(wglMakeCurrent(NULL, NULL) && wglDeleteContext((HGLRC)(ULONG_PTR)c->Id));
    Log.clear();
}

/* Every 5/6/5 word once: 32 reds x 64 greens x 32 blues would be 64 KiB; a
 * 64x32 image holds every green with every red, blue running with x + y. */
static unsigned short Words[64 * 32];
static void Fill(void) {
    for (ULONG y = 0; y < 32; ++y)
        for (ULONG x = 0; x < 64; ++x)
            Words[y * 64 + x] = (unsigned short)(y << 11 | x << 5 | ((x + y) & 31));
}

/* Without the bit RGB565 is no internal format: every record keeps the local
 * error any unknown format has, and nothing reaches the host. A failed or
 * malformed CAPABILITIES reply negotiates neither word. */
static void Unnegotiated(void) {
    for (int failure : {1, 2, 3, 4, 5, 6, 7}) {
        Reset();
        HostCaps = OTHERS;
        HostCaps2 = EXACT | DEPTH_TEXTURE;
        CapabilityFailure = failure;
        JGL_CONTEXT *c = Lookup(Create());
        assert(!JglRgb565Available());
        Close(c);
    }
    for (ULONG caps2 :
         std::initializer_list<ULONG>{0, DEPTH_TEXTURE, DEPTH_TEXTURE | 0x80000000u}) {
        JGL_CONTEXT *c = Open(OTHERS, caps2);
        assert(!JglRgb565Available());
        glTexImage1D(TEX_1D, 0, RGB565, 64, 0, GL_RGB, PACKED_565, Words);
        Rejected(c, GL_INVALID_VALUE);
        glTexImage1D(PROXY_1D, 0, RGB565, 64, 0, GL_RGB, PACKED_565, NULL);
        Rejected(c, GL_INVALID_VALUE);
        for (GLenum target : {TEX_2D, TEX_RECT, CUBE_X}) {
            glTexImage2D(target, 0, RGB565, 32, 32, 0, GL_RGB, PACKED_565, Words);
            Rejected(c, GL_INVALID_VALUE);
            glCopyTexImage2D(target, 0, RGB565, 0, 0, 8, 8, 0);
            Rejected(c, GL_INVALID_VALUE);
        }
        for (GLenum target : {PROXY_2D, PROXY_RECT, PROXY_CUBE}) {
            glTexImage2D(target, 0, RGB565, 32, 32, 0, GL_RGB, PACKED_565, NULL);
            Rejected(c, GL_INVALID_VALUE);
        }
        glTexImage3DEXT(TEX_3D, 0, RGB565, 4, 4, 4, 0, GL_RGB, PACKED_565, Words);
        Rejected(c, GL_INVALID_VALUE);
        glRenderbufferStorageEXT(RENDERBUFFER, RGB565, 16, 16);
        Rejected(c, GL_INVALID_ENUM);
        /* RGB5 is unchanged: native GL stores it as it always did. */
        glTexImage2D(TEX_2D, 0, RGB5, 64, 32, 0, GL_RGB, PACKED_565, Words);
        Records r = Sent(c);
        assert(r.size() == 1);
        ExpectData(r[0], FEnum_glTexImage2D, {TEX_2D, 0, RGB5, 64, 32, 0, GL_RGB, PACKED_565},
                   Words, sizeof(Words));
        Close(c);
    }
}

/* No extension name: the string is the one the other bits give, byte for
 * byte (test_rgb565.py checks that no ARB_ES2_compatibility entry point is
 * exported). */
static void Negotiation(void) {
    for (ULONG caps : std::initializer_list<ULONG>{0, OTHERS}) {
        for (ULONG caps2 : std::initializer_list<ULONG>{0, DEPTH_TEXTURE}) {
            JGL_CONTEXT *c = Open(caps, caps2 | EXACT);
            assert(JglRgb565Available());
            const char *names = (const char *)glGetString(GL_EXTENSIONS);
            assert(!strcmp(names, ExpectedExtensions(caps, FALSE, caps2)));
            assert(!strstr(names, "ES2") && !strstr(names, "565"));
            Close(c);
        }
    }
}

/* With the bit every colour record takes RGB565 unchanged: 1D, 2D, rectangle,
 * cube face and 3D images, their proxies, copies and renderbuffers. */
static void Records565(void) {
    JGL_CONTEXT *c = Open(OTHERS, EXACT);
    glTexImage1D(TEX_1D, 0, RGB565, 64, 0, GL_RGB, PACKED_565, Words);
    glTexImage1D(PROXY_1D, 0, RGB565, 2048, 0, GL_RGB, PACKED_565, NULL);
    glTexImage2D(TEX_2D, 0, RGB565, 64, 32, 0, GL_RGB, PACKED_565, Words);
    glTexImage2D(TEX_RECT, 0, RGB565, 64, 32, 0, GL_RGB, PACKED_565, Words);
    glTexImage2D(CUBE_NZ, 1, RGB565, 32, 32, 0, GL_RGB, PACKED_565, Words);
    glTexImage2D(PROXY_2D, 0, RGB565, 64, 32, 0, GL_RGB, PACKED_565, NULL);
    glTexImage2D(PROXY_RECT, 0, RGB565, 640, 480, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexImage2D(PROXY_CUBE, 0, RGB565, 32, 32, 0, GL_RGB, PACKED_565, NULL);
    glTexImage3DEXT(TEX_3D, 0, RGB565, 8, 8, 4, 0, GL_RGB, PACKED_565, Words);
    glCopyTexImage2D(TEX_2D, 0, RGB565, 0, 0, 64, 32, 0);
    glCopyTexImage2D(TEX_RECT, 0, RGB565, 0, 0, 640, 480, 0);
    glCopyTexImage2D(CUBE_X, 0, RGB565, 0, 0, 16, 16, 0);
    glBindRenderbufferEXT(RENDERBUFFER, 3);
    glRenderbufferStorageEXT(RENDERBUFFER, RGB565, 400, 300);
    Records r = Sent(c);
    assert(r.size() == 14);
    ExpectData(r[0], FEnum_glTexImage1D, {TEX_1D, 0, RGB565, 64, 1, 0, GL_RGB, PACKED_565}, Words,
               128);
    ExpectData(r[1], FEnum_glTexImage1D, {PROXY_1D, 0, RGB565, 2048, 1, 0, GL_RGB, PACKED_565},
               NULL, 0);
    ExpectData(r[2], FEnum_glTexImage2D, {TEX_2D, 0, RGB565, 64, 32, 0, GL_RGB, PACKED_565}, Words,
               sizeof(Words));
    ExpectData(r[3], FEnum_glTexImage2D, {TEX_RECT, 0, RGB565, 64, 32, 0, GL_RGB, PACKED_565},
               Words, sizeof(Words));
    ExpectData(r[4], FEnum_glTexImage2D, {CUBE_NZ, 1, RGB565, 32, 32, 0, GL_RGB, PACKED_565}, Words,
               32 * 32 * 2);
    ExpectData(r[5], FEnum_glTexImage2D, {PROXY_2D, 0, RGB565, 64, 32, 0, GL_RGB, PACKED_565}, NULL,
               0);
    ExpectData(r[6], FEnum_glTexImage2D,
               {PROXY_RECT, 0, RGB565, 640, 480, 0, GL_RGBA, GL_UNSIGNED_BYTE}, NULL, 0);
    ExpectData(r[7], FEnum_glTexImage2D, {PROXY_CUBE, 0, RGB565, 32, 32, 0, GL_RGB, PACKED_565},
               NULL, 0);
    ExpectData(r[8], FEnum_glTexImage3D, {TEX_3D, 0, RGB565, 8, 8, 4, 0, GL_RGB, PACKED_565}, Words,
               8 * 8 * 4 * 2);
    ExpectCall(r[9], FEnum_glCopyTexImage2D, {TEX_2D, 0, RGB565, 0, 0, 64, 32, 0});
    ExpectCall(r[10], FEnum_glCopyTexImage2D, {TEX_RECT, 0, RGB565, 0, 0, 640, 480, 0});
    ExpectCall(r[11], FEnum_glCopyTexImage2D, {CUBE_X, 0, RGB565, 0, 0, 16, 16, 0});
    ExpectCall(r[12], FEnum_glBindRenderbuffer, {RENDERBUFFER, 3});
    ExpectCall(r[13], FEnum_glRenderbufferStorage, {RENDERBUFFER, RGB565, 400, 300});
    /* The other rules are unchanged: a non-square face, a rectangle level and
     * the neighbouring ES tokens keep their errors. */
    glTexImage2D(CUBE_X, 0, RGB565, 32, 16, 0, GL_RGB, PACKED_565, Words);
    Rejected(c, GL_INVALID_VALUE);
    glTexImage2D(TEX_RECT, 1, RGB565, 4, 4, 0, GL_RGB, PACKED_565, Words);
    Rejected(c, GL_INVALID_VALUE);
    glCopyTexImage2D(CUBE_X, 0, RGB565, 0, 0, 16, 8, 0);
    Rejected(c, GL_INVALID_VALUE);
    for (GLint other : {(GLint)RGB565 - 1, (GLint)RGB565 + 1}) {
        glTexImage2D(TEX_2D, 0, other, 4, 4, 0, GL_RGB, PACKED_565, Words);
        Rejected(c, GL_INVALID_VALUE);
        glCopyTexImage2D(TEX_RECT, 0, (GLenum)other, 0, 0, 4, 4, 0);
        Rejected(c, GL_INVALID_VALUE);
        glRenderbufferStorageEXT(RENDERBUFFER, (GLenum)other, 4, 4);
        Rejected(c, GL_INVALID_ENUM);
    }
    /* Formats that were already images are unchanged. */
    glTexImage2D(TEX_2D, 0, RGB8, 64, 32, 0, GL_RGB, PACKED_565, Words);
    r = Sent(c);
    ExpectData(r[0], FEnum_glTexImage2D, {TEX_2D, 0, RGB8, 64, 32, 0, GL_RGB, PACKED_565}, Words,
               sizeof(Words));
    Close(c);
}

/* Display lists capture the records; without the bit they capture the error. */
static void Lists(void) {
    for (ULONG caps2 : std::initializer_list<ULONG>{EXACT, 0}) {
        JGL_CONTEXT *c = Open(OTHERS, caps2);
        assert(JglListMode(GL_COMPILE));
        Clear(c);
        glTexImage2D(TEX_2D, 0, RGB565, 64, 32, 0, GL_RGB, PACKED_565, Words);
        glCopyTexImage2D(TEX_2D, 0, RGB565, 0, 0, 8, 8, 0);
        assert(!c->Error);
        Submit(c);
        assert(Log.size() == 2);
        if (caps2) {
            ExpectData(Log[0], FEnum_glTexImage2D,
                       {TEX_2D, 0, RGB565, 64, 32, 0, GL_RGB, PACKED_565}, Words, sizeof(Words));
            ExpectCall(Log[1], FEnum_glCopyTexImage2D, {TEX_2D, 0, RGB565, 0, 0, 8, 8, 0});
        } else {
            ExpectCall(Log[0], DG_GL_RECORD_ERROR, {GL_INVALID_VALUE});
            ExpectCall(Log[1], DG_GL_RECORD_ERROR, {GL_INVALID_VALUE});
        }
        assert(JglListMode(0));
        Close(c);
    }
}

int main() {
    Fill();
    Unnegotiated();
    Negotiation();
    Records565();
    Lists();
    puts("PASS RGB565 frontend");
    return 0;
}
