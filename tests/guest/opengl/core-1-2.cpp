/* SPDX-License-Identifier: GPL-2.0-or-later
 * DG_CAP2_GL_CORE_1_2 (the GL 1.2 core names) through the actual frontend on
 * the strict transport fixture: GL_VERSION is 1.2 only with the bit, 3D
 * textures, fixed state and secondary colour, and 1.1 with any of them
 * missing; TEXTURE_MIN_LOD/MAX_LOD are TexParameter records and
 * GetTexParameter queries with the bit and a local GL_INVALID_ENUM without
 * it; DrawRangeElements is found by lookup with GL 1.2 only, submits exactly
 * the records of the same DrawElements and rejects a reversed range locally;
 * no extension name changes. */
#include <array>
#include <string>
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

constexpr GLenum TEX_1D = 0x0de0, TEX_2D = 0x0de1, TEX_3D = 0x806f, TEX_CUBE = 0x8513,
                 PROXY_2D = 0x8064, MIN_LOD = 0x813a, MAX_LOD = 0x813b;
constexpr ULONG REQUIRED = DG_CAP_GL_TEXTURE_3D | DG_CAP_GL_FIXED_STATE;
constexpr ULONG OTHERS = REQUIRED | DG_CAP_GL_MULTITEXTURE | DG_CAP_GL_TEXTURE_CUBE_MAP |
                         DG_CAP_GL_TEXTURE_CONTROL | DG_CAP_GL_COMPRESSED_TEXTURES;
constexpr ULONG CORE = DG_CAP2_GL_CORE_1_2;

typedef std::vector<std::vector<ULONG>> Records;
static Records Log;
static void Capture(const ULONG *record) {
    Log.emplace_back(record, record + record[1] / 4);
}
static JGL_CONTEXT *Open(ULONG caps, ULONG caps2, ULONG secondary) {
    Reset();
    HostCaps = caps;
    HostCaps2 = caps2;
    HostSecondaryWords = secondary;
    SubmittedRecord = Capture;
    JGL_CONTEXT *c = Lookup(Create());
    c->Used = c->Records = 0;
    c->Error = 0;
    Log.clear();
    return c;
}
static void Close(JGL_CONTEXT *c) {
    assert(wglMakeCurrent(NULL, NULL) && wglDeleteContext((HGLRC)(ULONG_PTR)c->Id));
    Log.clear();
}
static Records Sent(JGL_CONTEXT *c) {
    assert(!c->Error);
    assert(Flush(c) && !c->Failed);
    Records out;
    out.swap(Log);
    return out;
}
/* The call raised exactly this error and nothing reached the host. */
static void Rejected(JGL_CONTEXT *c, GLenum error) {
    assert(c->Error == error && !c->Records && !c->Used);
    assert(Flush(c) && !c->Failed && Log.empty());
    c->Error = 0;
}

/* GL 1.2 only with every 1.2 feature: the bit alone, or any missing feature,
 * keeps 1.1, as does a failed CAPABILITIES reply. The extension string never
 * changes with the bit. */
static void Version(void) {
    JGL_CONTEXT *c = Open(OTHERS, CORE, 3);
    assert(JglCore12Available() && JglCore12());
    assert(!strcmp((const char *)glGetString(GL_VERSION), "1.2 DreamGPU"));
    const std::string with((const char *)glGetString(GL_EXTENSIONS));
    assert(with == ExpectedExtensions(OTHERS, 1, CORE));
    Close(c);
    c = Open(OTHERS, 0, 3);
    assert(!JglCore12Available() && !JglCore12());
    assert(!strcmp((const char *)glGetString(GL_VERSION), "1.1 DreamGPU"));
    assert(with == (const char *)glGetString(GL_EXTENSIONS));
    Close(c);
    const struct {
        ULONG caps, caps2, secondary;
    } partial[] = {
        {OTHERS & ~DG_CAP_GL_TEXTURE_3D, CORE, 3},
        {OTHERS & ~DG_CAP_GL_FIXED_STATE, CORE, 3},
        {OTHERS, CORE, 0},
        {OTHERS, DG_CAP2_GL_RGB565 | DG_CAP2_GL_BULK_READ | DG_CAP2_GL_DEPTH_TEXTURE, 3},
    };
    for (const auto &p : partial) {
        c = Open(p.caps, p.caps2, p.secondary);
        assert(!JglCore12());
        assert(!strcmp((const char *)glGetString(GL_VERSION), "1.1 DreamGPU"));
        Close(c);
    }
    for (int failure : {1, 2, 3, 4, 5, 6, 7}) {
        Reset();
        HostCaps = OTHERS;
        HostCaps2 = CORE;
        HostSecondaryWords = 3;
        CapabilityFailure = failure;
        c = Lookup(Create());
        assert(!JglCore12Available());
        assert(!strcmp((const char *)glGetString(GL_VERSION), "1.1 DreamGPU"));
        Close(c);
    }
    /* No GL_VERSION without a context, as before. */
    Reset();
    assert(!glGetString(GL_VERSION));
}

/* The LOD clamps: scalar and vector records of every binding target with the
 * bit (it alone suffices); a local GL_INVALID_ENUM without it, never a record
 * an older host rejects as malformed. Faces and proxies are no parameter
 * targets either way. */
static void Lod(void) {
    for (ULONG negotiated = 0; negotiated < 2; ++negotiated) {
        JGL_CONTEXT *c = Open(OTHERS, negotiated ? CORE : 0, 0);
        assert(JglCore12Available() == (BOOL)negotiated);
        for (GLenum target : {TEX_1D, TEX_2D, TEX_3D, TEX_CUBE}) {
            glTexParameterf(target, MIN_LOD, -2.0f);
            glTexParameteri(target, MAX_LOD, 3);
            const GLfloat bias[4] = {0.5f, 0, 0, 0};
            glTexParameterfv(target, MAX_LOD, bias);
            if (!negotiated) {
                assert(c->Error == GL_INVALID_ENUM);
                c->Error = 0;
                assert(!c->Records && !c->Used);
                continue;
            }
            Records r = Sent(c);
            assert(r.size() == 3);
            const GLfloat minimum = -2.0f;
            ULONG bits;
            memcpy(&bits, &minimum, 4);
            assert(r[0][0] == DG_GL_CALL && r[0][8] == FEnum_glTexParameterf &&
                   r[0][9] == target && r[0][10] == MIN_LOD && r[0][11] == bits);
            assert(r[1][0] == DG_GL_CALL && r[1][8] == FEnum_glTexParameteri &&
                   r[1][9] == target && r[1][10] == MAX_LOD && r[1][11] == 3);
            assert(r[2][8] == FEnum_glTexParameterfv && r[2][10] == target &&
                   r[2][11] == MAX_LOD);
        }
        glTexParameteri(0x8515 /* POSITIVE_X */, MIN_LOD, 0);
        Rejected(c, GL_INVALID_ENUM);
        glTexParameteri(PROXY_2D, MAX_LOD, 0);
        Rejected(c, GL_INVALID_ENUM);
        /* GetTexParameter asks the host only with the bit. */
        const ULONG queries = QueryCalls;
        GLint value = 7;
        AnswerQuery = [](const ULONG *r, DG_ESCAPE_REPLY *reply, ULONG capacity) -> BOOL {
            if (r[8] != FEnum_glGetTexParameteriv)
                return FALSE;
            assert(r[9] == TEX_2D && r[10] == MAX_LOD && capacity >= 4);
            const GLint lod = 1000;
            memcpy(reply + 1, &lod, 4);
            reply->ResultBytes = 4;
            reply->ResultType = DG_GL_RESULT_INT;
            return TRUE;
        };
        glGetTexParameteriv(TEX_2D, MAX_LOD, &value);
        if (negotiated) {
            assert(!c->Error && value == 1000 && QueryCalls == queries + 1);
        } else {
            assert(c->Error == GL_INVALID_ENUM && value == 7 && QueryCalls == queries);
            c->Error = 0;
        }
        AnswerQuery = NULL;
        Close(c);
    }
}

/* DrawRangeElements: looked up only with GL 1.2; the records of the same
 * DrawElements; a reversed range is a local GL_INVALID_VALUE. */
static void RangeElements(void) {
    static const GLfloat vertices[] = {0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 0};
    static const GLushort indices[] = {0, 1, 2, 2, 1, 3};
    JGL_CONTEXT *c = Open(OTHERS, 0, 3);
    assert(!wglGetProcAddress("glDrawRangeElements"));
    Close(c);
    c = Open(OTHERS, CORE, 3);
    assert(wglGetProcAddress("glDrawRangeElements") == (PROC)Marker);
    glVertexPointer(3, GL_FLOAT, 0, vertices);
    glEnableClientState(GL_VERTEX_ARRAY);
    (void)Sent(c);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);
    const Records elements = Sent(c);
    assert(!elements.empty());
    glDrawRangeElements(GL_TRIANGLES, 0, 3, 6, GL_UNSIGNED_SHORT, indices);
    const Records range = Sent(c);
    assert(range == elements);
    /* The promise is only checked for order; equal bounds are a range. */
    glDrawRangeElements(GL_TRIANGLES, 3, 0, 6, GL_UNSIGNED_SHORT, indices);
    Rejected(c, GL_INVALID_VALUE);
    glDrawRangeElements(GL_TRIANGLES, 2, 2, 1, GL_UNSIGNED_SHORT, indices + 2);
    assert(!Sent(c).empty());
    /* Errors DrawElements raises stay its own. */
    glDrawRangeElements(GL_TRIANGLES, 0, 3, -1, GL_UNSIGNED_SHORT, indices);
    Rejected(c, GL_INVALID_VALUE);
    glDrawRangeElements(GL_TRIANGLES, 0, 3, 6, GL_FLOAT, indices);
    Rejected(c, GL_INVALID_ENUM);
    Close(c);
}

int main() {
    Version();
    Lod();
    RangeElements();
    puts("PASS GL 1.2 core frontend");
    return 0;
}
