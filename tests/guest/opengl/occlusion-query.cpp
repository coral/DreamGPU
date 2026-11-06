/* SPDX-License-Identifier: GPL-2.0-or-later
 * DG_CAP_GL_OCCLUSION_QUERY (ARB_occlusion_query, NV_fence) through the actual
 * frontend on the strict transport fixture: negotiation (the extension
 * string and wglGetProcAddress, alone and beside every other capability, and
 * the unchanged frontend without it), exact CALL, DATA and QUERY records
 * within the host's shapes and admitted by the NT validator, local validation
 * (nothing the host would reject as malformed leaves the frontend), name
 * batches, display lists (Begin/End/SetFence compile with deferred errors,
 * everything else executes at once), and the answers the frontend keeps: one
 * round trip for availability then the result, reported results of other
 * queries, signalled fences, and their invalidation by Begin, SetFence,
 * deletion, display lists and context. */
#include <utility>
#include <vector> /* before the Win32 fixture's min() macro */
#define main existing_frontend_suite_not_run
#include "frontend-harness.cpp"
#undef main
#include "arrays.cpp"
#include "query.cpp"
#include "program.cpp"
#include "attrib.cpp"
#include "buffer.cpp"
#include "occlusion.cpp"
typedef void (*PROC)(void);
#define GL_FOG_INDEX 0x0b61
#define GL_FOG_DENSITY 0x0b62
#define GL_FOG_START 0x0b63
#define GL_FOG_END 0x0b64
#define GL_FOG_MODE 0x0b65
#define GL_FOG_COLOR 0x0b66
#define GL_TRUE 1
static void Marker(void) {}
extern "C" PROC GetProcAddress(HINSTANCE, LPCSTR name) {
    return name[0] == 'g' && name[1] == 'l' ? Marker : nullptr;
}
#include "compatibility.cpp"
#include "dg-gl-validate.h"

constexpr GLenum SAMPLES = 0x8914, BITS = 0x8864, CURRENT = 0x8865, RESULT = 0x8866,
                 AVAILABLE = 0x8867, ALL_COMPLETED = 0x84f2, STATUS = 0x84f3, CONDITION = 0x84f4;
constexpr ULONG QUERIES = DG_CAP_GL_OCCLUSION_QUERY;
constexpr ULONG ALL_BUT_QUERIES =
    DG_CAP_GL_MULTITEXTURE | DG_CAP_GL_FIXED_STATE | DG_CAP_GL_TEXTURE_CONTROL |
    DG_CAP_GL_COMPRESSED_TEXTURES | DG_CAP_GL_FOG_COORD | DG_CAP_GL_TEXTURE_CUBE_MAP |
    DG_CAP_GL_FRAGMENT_PROGRAM | DG_CAP_GL_VERTEX_PROGRAM | DG_CAP_GL_TEXTURE_3D |
    DG_CAP_GL_VERTEX_BUFFER_OBJECT | DG_CAP_GL_FRAMEBUFFER_OBJECT | DG_CAP_GL_VERTEX_ARRAY_BGRA;
static const char *const Entries[] = {
    "glGenQueriesARB", "glDeleteQueriesARB", "glIsQueryARB",          "glBeginQueryARB",
    "glEndQueryARB",   "glGetQueryivARB",    "glGetQueryObjectivARB", "glGetQueryObjectuivARB",
    "glGenFencesNV",   "glDeleteFencesNV",   "glSetFenceNV",          "glTestFenceNV",
    "glFinishFenceNV", "glIsFenceNV",        "glGetFenceivNV"};

/* The host's words for these records (gl.h), as the NT kernel learns them. */
static ULONG KernelWords(void *, ULONG function) {
    switch (function) {
        case FEnum_glBeginQuery:
        case FEnum_glSetFenceNV:
            return 2;
        case FEnum_glEndQuery:
        case DG_GL_RECORD_ERROR:
        case FEnum_glCallList:
            return 1;
        case FEnum_glDeleteQueries:
        case FEnum_glDeleteFencesNV:
            return DG_GL_FUNCTION_INLINE_DATA | 1;
        case FEnum_glGenQueries:
        case FEnum_glIsQuery:
        case FEnum_glGenFencesNV:
        case FEnum_glIsFenceNV:
        case FEnum_glTestFenceNV:
        case FEnum_glFinishFenceNV:
            return DG_GL_FUNCTION_QUERY | 1;
        case FEnum_glGetQueryiv:
        case FEnum_glGetFenceivNV:
            return DG_GL_FUNCTION_QUERY | 2;
        case FEnum_glGetQueryObjectuiv:
            return DG_GL_FUNCTION_QUERY | 3;
        default:
            return (ULONG)-1;
    }
}
/* The host's record shapes: anything else is a batch fault that fails the
 * context, so none may leave the frontend. */
static void HostShape(const std::vector<ULONG> &r) {
    const ULONG *a = r.data() + 9;
    switch (r[8]) {
        case FEnum_glBeginQuery:
        case FEnum_glEndQuery:
            assert(r[0] == DG_GL_CALL && a[0] == SAMPLES);
            break;
        case FEnum_glSetFenceNV:
            assert(r[0] == DG_GL_CALL && a[1] == ALL_COMPLETED);
            break;
        case FEnum_glDeleteQueries:
        case FEnum_glDeleteFencesNV:
            assert(r[0] == DG_GL_DATA_CALL && r[10] && r[10] <= DG_GL_MAX_QUERIES &&
                   r[9] == r[10] * 4 && r.size() == 11 + r[10]);
            for (ULONG i = 0; i < r[10]; ++i)
                assert(r[11 + i]);
            break;
        case FEnum_glGenQueries:
        case FEnum_glGenFencesNV:
            assert(r[0] == DG_GL_QUERY && r[9] && r[9] <= DG_GL_QUERY_NAME_BATCH && !r[10] &&
                   !r[11]);
            break;
        case FEnum_glGetQueryiv:
            assert(r[0] == DG_GL_QUERY && r[9] == SAMPLES && (r[10] == CURRENT || r[10] == BITS));
            break;
        case FEnum_glGetQueryObjectuiv:
            assert(r[0] == DG_GL_QUERY && (r[10] == RESULT || r[10] == AVAILABLE) &&
                   r[11] == DG_GL_QUERY_REPORTS);
            break;
        case FEnum_glGetFenceivNV:
            assert(r[0] == DG_GL_QUERY && (r[10] == STATUS || r[10] == CONDITION) && !r[11]);
            break;
        default:
            break;
    }
}
typedef std::vector<std::vector<ULONG>> Records;
static Records Log;
static void Capture(const ULONG *record) {
    Log.emplace_back(record, record + record[1] / 4);
    const std::vector<ULONG> &r = Log.back();
    if ((r[0] != DG_GL_CALL && r[0] != DG_GL_DATA_CALL && r[0] != DG_GL_QUERY) ||
        KernelWords(NULL, r[8]) == (ULONG)-1)
        return;
    HostShape(r);
    std::vector<ULONG> copy(r);
    const DG_GL_LIMITS limits = {7, DG_ESCAPE_MAX_BYTES, DG_GL_MAX_RECORDS, 1};
    assert(DgValidateUserGl(copy.data(), copy.size() * 4, 42, 7, &limits, KernelWords, NULL));
}

/* Host answers: queued replies for GetQueryObjectuiv and the fence records
 * (words), names for Gen (counting up) and fixed booleans and integers. */
static std::vector<std::vector<ULONG>> Replies;
static ULONG NextName, HostCurrent;
static BYTE HostIs;
static BOOL Answer(const ULONG *r, DG_ESCAPE_REPLY *reply, ULONG capacity) {
    auto put = [&](ULONG type, const void *data, ULONG bytes) {
        assert(bytes && bytes <= capacity);
        memcpy(reply + 1, data, bytes);
        reply->ResultBytes = bytes;
        reply->ResultType = type;
        return TRUE;
    };
    switch (r[8]) {
        case FEnum_glGenQueries:
        case FEnum_glGenFencesNV: {
            assert(capacity == r[9] * 4);
            std::vector<ULONG> names(r[9]);
            for (ULONG &n : names)
                n = NextName ? NextName++ : 0;
            return put(DG_GL_RESULT_INT, names.data(), r[9] * 4);
        }
        case FEnum_glIsQuery:
        case FEnum_glIsFenceNV:
            assert(capacity == 1);
            return put(DG_GL_RESULT_BOOL, &HostIs, 1);
        case FEnum_glGetQueryiv: {
            const ULONG value = r[10] == BITS ? 32 : HostCurrent;
            return put(DG_GL_RESULT_INT, &value, 4);
        }
        case FEnum_glGetQueryObjectuiv:
            assert(capacity == (2 + 2 * DG_GL_QUERY_REPORTS) * 4);
            [[fallthrough]];
        case FEnum_glTestFenceNV:
        case FEnum_glFinishFenceNV:
        case FEnum_glGetFenceivNV: {
            if (r[8] != FEnum_glGetQueryObjectuiv)
                assert(capacity == 8);
            assert(!Replies.empty());
            const std::vector<ULONG> words = Replies.front();
            Replies.erase(Replies.begin());
            return put(DG_GL_RESULT_INT, words.data(), words.size() * 4);
        }
        default:
            return FALSE;
    }
}
static void Clear(JGL_CONTEXT *c) {
    assert(!c->InBegin);
    c->Used = c->Records = 0;
    c->Error = 0;
    Log.clear();
}
static JGL_CONTEXT *Open(ULONG caps) {
    Reset();
    HostCaps = caps;
    HostMaxBytes = DG_ESCAPE_MAX_BYTES;
    /* Executing a display list may draw into the window's front buffer. */
    BindCapabilities = DG_WINDOW_CAP_FRONT_ONLY;
    SubmittedRecord = Capture;
    AnswerQuery = Answer;
    Replies.clear();
    NextName = 10;
    HostIs = 0;
    HostCurrent = 0;
    JGL_CONTEXT *c = Lookup(Create());
    Clear(c);
    return c;
}
static void Close(JGL_CONTEXT *c) {
    assert(Replies.empty());
    assert(wglMakeCurrent(NULL, NULL) && wglDeleteContext((HGLRC)(ULONG_PTR)c->Id));
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
static void ExpectQuery(const std::vector<ULONG> &r, ULONG function, ULONG a, ULONG b, ULONG c) {
    assert(r[0] == DG_GL_QUERY && r[1] == DG_GL_QUERY_BYTES && r[8] == function && r[9] == a &&
           r[10] == b && r[11] == c);
}
static void ExpectDeferred(const std::vector<ULONG> &r, GLenum error) {
    ExpectCall(r, DG_GL_RECORD_ERROR, {error});
}
static GLuint Object(GLuint id, GLenum pname) {
    GLuint value = 0x5a5a5a5a;
    glGetQueryObjectuivARB(id, pname, &value);
    return value;
}

/* Every entry point fails locally with INVALID_OPERATION, sending nothing. */
static void Unnegotiated(void) {
    JGL_CONTEXT *c = Open(ALL_BUT_QUERIES);
    assert(!JglOcclusionQueryAvailable());
    const char *extensions = (const char *)glGetString(GL_EXTENSIONS);
    assert(!strstr(extensions, "occlusion_query") && !strstr(extensions, "fence"));
    for (const char *name : Entries)
        assert(!wglGetProcAddress(name));
    assert(wglGetProcAddress("glBindFramebufferEXT") == Marker);
    GLuint names[2] = {7, 7};
    GLint value = 0x5a5a;
    glGenQueriesARB(2, names);
    Rejected(c, GL_INVALID_OPERATION);
    assert(names[0] == 7);
    glDeleteQueriesARB(2, names);
    Rejected(c, GL_INVALID_OPERATION);
    assert(!glIsQueryARB(7));
    Rejected(c, GL_INVALID_OPERATION);
    glBeginQueryARB(SAMPLES, 7);
    Rejected(c, GL_INVALID_OPERATION);
    glEndQueryARB(SAMPLES);
    Rejected(c, GL_INVALID_OPERATION);
    glGetQueryivARB(SAMPLES, BITS, &value);
    Rejected(c, GL_INVALID_OPERATION);
    glGetQueryObjectivARB(7, RESULT, &value);
    Rejected(c, GL_INVALID_OPERATION);
    assert(Object(7, AVAILABLE) == 0x5a5a5a5a);
    Rejected(c, GL_INVALID_OPERATION);
    glGenFencesNV(2, names);
    Rejected(c, GL_INVALID_OPERATION);
    glDeleteFencesNV(2, names);
    Rejected(c, GL_INVALID_OPERATION);
    glSetFenceNV(7, ALL_COMPLETED);
    Rejected(c, GL_INVALID_OPERATION);
    assert(glTestFenceNV(7));
    Rejected(c, GL_INVALID_OPERATION);
    glFinishFenceNV(7);
    Rejected(c, GL_INVALID_OPERATION);
    assert(!glIsFenceNV(7));
    Rejected(c, GL_INVALID_OPERATION);
    glGetFenceivNV(7, STATUS, &value);
    Rejected(c, GL_INVALID_OPERATION);
    assert(value == 0x5a5a && !QueryCalls);
    Close(c);
}
/* The extension names and entry points, alone and after every other group. */
static void Negotiation(void) {
    for (ULONG caps : {QUERIES, ALL_BUT_QUERIES | QUERIES}) {
        JGL_CONTEXT *c = Open(caps);
        const char *extensions = (const char *)glGetString(GL_EXTENSIONS);
        const char *found = strstr(extensions, "GL_ARB_occlusion_query GL_NV_fence");
        assert(found && !strcmp(found, "GL_ARB_occlusion_query GL_NV_fence"));
        assert(caps == QUERIES ? found == extensions : found > extensions && found[-1] == ' ');
        for (const char *name : Entries)
            assert(wglGetProcAddress(name) == Marker);
        Close(c);
    }
}
/* Exact records within the host's shapes. */
static void Records_(void) {
    JGL_CONTEXT *c = Open(QUERIES);
    GLuint names[300] = {};
    GLint value = 0;
    glGenQueriesARB(3, names);
    assert(names[0] == 10 && names[1] == 11 && names[2] == 12 && !names[3]);
    Records r = Sent(c);
    assert(r.size() == 1);
    ExpectQuery(r[0], FEnum_glGenQueries, 3, 0, 0);
    /* Batches of at most DG_GL_QUERY_NAME_BATCH; a failed batch stops. */
    glGenQueriesARB(300, names);
    r = Sent(c);
    assert(r.size() == 3);
    ExpectQuery(r[0], FEnum_glGenQueries, 128, 0, 0);
    ExpectQuery(r[1], FEnum_glGenQueries, 128, 0, 0);
    ExpectQuery(r[2], FEnum_glGenQueries, 44, 0, 0);
    assert(names[0] == 13 && names[299] == 312);
    NextName = 0;
    glGenQueriesARB(300, names);
    assert(Sent(c).size() == 1 && names[0] == 13 && names[299] == 312);
    glGenQueriesARB(0, names);
    glGenQueriesARB(5, NULL);
    assert(Sent(c).empty());
    glBeginQueryARB(SAMPLES, 10);
    glEndQueryARB(SAMPLES);
    glSetFenceNV(4, ALL_COMPLETED);
    r = Sent(c);
    assert(r.size() == 3);
    ExpectCall(r[0], FEnum_glBeginQuery, {SAMPLES, 10});
    ExpectCall(r[1], FEnum_glEndQuery, {SAMPLES});
    ExpectCall(r[2], FEnum_glSetFenceNV, {4, ALL_COMPLETED});
    /* Zero names are dropped; batches fit the packet and 256 names. */
    const GLuint some[4] = {10, 0, 11, 12};
    glDeleteQueriesARB(4, some);
    r = Sent(c);
    assert(r.size() == 1 && r[0][0] == DG_GL_DATA_CALL && r[0][8] == FEnum_glDeleteQueries &&
           r[0][10] == 3 && r[0][11] == 10 && r[0][12] == 11 && r[0][13] == 12);
    for (ULONG i = 0; i < 300; ++i)
        names[i] = i + 1;
    names[256] = 0;
    glDeleteFencesNV(300, names);
    r = Sent(c);
    assert(r.size() == 2 && r[0][8] == FEnum_glDeleteFencesNV && r[0][10] == 256 &&
           r[1][10] == 43 && r[1][11] == 258);
    const GLuint zeros[2] = {0, 0};
    glDeleteQueriesARB(2, zeros);
    glDeleteQueriesARB(1, NULL);
    assert(Sent(c).empty());
    /* State queries answer from the host. */
    HostCurrent = 10;
    glGetQueryivARB(SAMPLES, CURRENT, &value);
    assert(value == 10);
    glGetQueryivARB(SAMPLES, BITS, &value);
    assert(value == 32);
    HostIs = 1;
    assert(glIsQueryARB(12) && glIsFenceNV(3));
    assert(!glIsQueryARB(0) && !glIsFenceNV(0));
    r = Sent(c);
    assert(r.size() == 4);
    ExpectQuery(r[0], FEnum_glGetQueryiv, SAMPLES, CURRENT, 0);
    ExpectQuery(r[1], FEnum_glGetQueryiv, SAMPLES, BITS, 0);
    ExpectQuery(r[2], FEnum_glIsQuery, 12, 0, 0);
    ExpectQuery(r[3], FEnum_glIsFenceNV, 3, 0, 0);
    Close(c);
}
/* Everything the host would reject as malformed is a local error. */
static void Validation(void) {
    JGL_CONTEXT *c = Open(QUERIES);
    GLint value = 0x5a5a;
    GLuint names[2] = {7, 7};
    glBeginQueryARB(0x88bf /* TIME_ELAPSED */, 1);
    Rejected(c, GL_INVALID_ENUM);
    glEndQueryARB(0x8c2f /* ANY_SAMPLES_PASSED */);
    Rejected(c, GL_INVALID_ENUM);
    glSetFenceNV(1, STATUS);
    Rejected(c, GL_INVALID_ENUM);
    glGetQueryivARB(0x88bf, CURRENT, &value);
    Rejected(c, GL_INVALID_ENUM);
    glGetQueryivARB(SAMPLES, RESULT, &value);
    Rejected(c, GL_INVALID_ENUM);
    glGetQueryObjectuivARB(1, CURRENT, (GLuint *)&value);
    Rejected(c, GL_INVALID_ENUM);
    glGetQueryObjectivARB(1, BITS, &value);
    Rejected(c, GL_INVALID_ENUM);
    glGetFenceivNV(1, ALL_COMPLETED, &value);
    Rejected(c, GL_INVALID_ENUM);
    assert(value == 0x5a5a);
    glGenQueriesARB(-1, names);
    Rejected(c, GL_INVALID_VALUE);
    glDeleteQueriesARB(-1, names);
    Rejected(c, GL_INVALID_VALUE);
    glGenFencesNV(-1, names);
    Rejected(c, GL_INVALID_VALUE);
    glDeleteFencesNV(-1, names);
    Rejected(c, GL_INVALID_VALUE);
    /* Inside Begin/End: INVALID_OPERATION, and TestFenceNV answers TRUE. */
    glBegin(GL_TRIANGLES);
    Log.clear();
    c->Used = c->Records = 0;
    glBeginQueryARB(SAMPLES, 1);
    assert(c->Error == GL_INVALID_OPERATION && !c->Records);
    c->Error = 0;
    glEndQueryARB(SAMPLES);
    assert(c->Error == GL_INVALID_OPERATION && !c->Records);
    c->Error = 0;
    assert(glTestFenceNV(1) && c->Error == GL_INVALID_OPERATION && !c->Records);
    c->Error = 0;
    assert(!glIsQueryARB(1) && c->Error == GL_INVALID_OPERATION && !c->Records);
    c->Error = 0;
    glGenQueriesARB(2, names);
    assert(c->Error == GL_INVALID_OPERATION && !c->Records && !QueryCalls);
    c->Error = 0;
    glEnd();
    Clear(c);
    Close(c);
}
/* One round trip answers availability and result, with reports. */
static void Polling(void) {
    JGL_CONTEXT *c = Open(QUERIES);
    GLint signed_value = 0;
    /* Pending: FALSE, nothing kept; the result was not asked for. */
    Replies = {{DG_GL_QUERY_PENDING, 0}};
    assert(Object(10, AVAILABLE) == GL_FALSE);
    Records r = Sent(c);
    assert(r.size() == 1);
    ExpectQuery(r[0], FEnum_glGetQueryObjectuiv, 10, AVAILABLE, DG_GL_QUERY_REPORTS);
    assert(QueryCalls == 1);
    /* Available with two reports: everything after answers locally. */
    Replies = {{DG_GL_QUERY_AVAILABLE, 64, 11, 32, 12, 0}};
    assert(Object(10, AVAILABLE) == 1);
    assert(QueryCalls == 2);
    assert(Object(10, RESULT) == 64 && Object(10, AVAILABLE) == 1);
    assert(Object(11, AVAILABLE) == 1 && Object(11, RESULT) == 32);
    assert(Object(12, RESULT) == 0);
    glGetQueryObjectivARB(11, RESULT, &signed_value);
    assert(signed_value == 32);
    assert(glIsQueryARB(12));
    assert(QueryCalls == 2 && Sent(c).size() == 1);
    /* Begin forgets that name only. */
    glBeginQueryARB(SAMPLES, 11);
    glEndQueryARB(SAMPLES);
    Replies = {{DG_GL_QUERY_AVAILABLE, 16}};
    assert(Object(11, RESULT) == 16);
    assert(Object(10, RESULT) == 64 && QueryCalls == 3);
    r = Sent(c);
    assert(r.size() == 3);
    ExpectQuery(r[2], FEnum_glGetQueryObjectuiv, 11, RESULT, DG_GL_QUERY_REPORTS);
    /* Counts beyond INT_MAX clamp for the signed query only. */
    Replies = {{DG_GL_QUERY_AVAILABLE, 0xfffffff0U}};
    glGetQueryObjectivARB(13, RESULT, &signed_value);
    assert(signed_value == 0x7fffffff && Object(13, RESULT) == 0xfffffff0U);
    /* A host error (one zero word) leaves params alone and keeps nothing. */
    Replies = {{0}, {0}};
    assert(Object(14, RESULT) == 0x5a5a5a5a && Object(14, AVAILABLE) == 0x5a5a5a5a);
    assert(QueryCalls == 6);
    /* Deletion forgets; so does executing a display list. */
    const GLuint ten = 10;
    glDeleteQueriesARB(1, &ten);
    Replies = {{0}};
    assert(Object(10, RESULT) == 0x5a5a5a5a);
    assert(Object(12, RESULT) == 0 && QueryCalls == 7);
    JglCallList(1);
    Replies = {{DG_GL_QUERY_AVAILABLE, 5}};
    assert(Object(12, RESULT) == 5 && QueryCalls == 8);
    /* Compiling one does not execute it. */
    assert(JglListMode(GL_COMPILE));
    JglCallList(1);
    assert(Object(12, RESULT) == 5 && QueryCalls == 8);
    assert(JglListMode(0));
    /* Answers belong to their context. */
    HGLRC other = wglCreateContext((HDC)2), mine = (HGLRC)(ULONG_PTR)c->Id;
    assert(other && wglMakeCurrent((HDC)2, other));
    Replies = {{DG_GL_QUERY_PENDING, 0}};
    assert(Object(12, AVAILABLE) == GL_FALSE && QueryCalls == 9);
    assert(wglMakeCurrent((HDC)1, mine));
    assert(Object(12, AVAILABLE) == 1 && QueryCalls == 9);
    assert(wglMakeCurrent(NULL, NULL) && wglDeleteContext(other));
    assert(wglMakeCurrent((HDC)1, mine));
    /* A full table replaces its oldest entries; answers stay exact. */
    std::vector<ULONG> many = {DG_GL_QUERY_AVAILABLE, 1000};
    for (ULONG n = 0; n < 63; ++n) {
        many.push_back(100 + n);
        many.push_back(2000 + n);
    }
    Replies = {many};
    assert(Object(99, RESULT) == 1000);
    assert(c->Queries.Results.Count == JGL_SETTLED_SLOTS);
    ULONG calls = QueryCalls;
    for (ULONG n = 0; n < 63; ++n)
        assert(Object(100 + n, RESULT) == 2000 + n);
    assert(QueryCalls == calls);
    /* The newest are kept; an older entry was replaced and asks again. */
    assert(Object(99, RESULT) == 1000 && QueryCalls == calls);
    Replies = {{DG_GL_QUERY_AVAILABLE, 5}};
    assert(Object(12, RESULT) == 5 && QueryCalls == calls + 1);
    Log.clear();
    Close(c);
}
/* Fence status: TRUE is kept until SetFenceNV, deletion or a list. */
static void Fences(void) {
    JGL_CONTEXT *c = Open(QUERIES);
    GLint value = 0x5a5a;
    Replies = {{1, 0}};
    assert(!glTestFenceNV(3));
    Replies = {{1, 1}};
    assert(glTestFenceNV(3));
    assert(QueryCalls == 2);
    assert(glTestFenceNV(3) && glIsFenceNV(3));
    glFinishFenceNV(3);
    glGetFenceivNV(3, STATUS, &value);
    assert(value == 1);
    glGetFenceivNV(3, CONDITION, &value);
    assert(value == (GLint)ALL_COMPLETED && QueryCalls == 2);
    Records r = Sent(c);
    assert(r.size() == 2);
    ExpectQuery(r[0], FEnum_glTestFenceNV, 3, 0, 0);
    /* SetFenceNV resets it. */
    glSetFenceNV(3, ALL_COMPLETED);
    Replies = {{1, 0}, {1, ALL_COMPLETED}, {1, 1}};
    glGetFenceivNV(3, STATUS, &value);
    assert(value == 0);
    glGetFenceivNV(3, CONDITION, &value);
    assert(value == (GLint)ALL_COMPLETED);
    glFinishFenceNV(3);
    assert(glTestFenceNV(3) && QueryCalls == 5);
    r = Sent(c);
    assert(r.size() == 4);
    ExpectCall(r[0], FEnum_glSetFenceNV, {3, ALL_COMPLETED});
    ExpectQuery(r[1], FEnum_glGetFenceivNV, 3, STATUS, 0);
    ExpectQuery(r[2], FEnum_glGetFenceivNV, 3, CONDITION, 0);
    ExpectQuery(r[3], FEnum_glFinishFenceNV, 3, 0, 0);
    /* Not a fence: TRUE, nothing kept, params untouched. */
    Replies = {{0}, {0}, {0}};
    assert(glTestFenceNV(8));
    value = 0x5a5a;
    glGetFenceivNV(8, STATUS, &value);
    assert(value == 0x5a5a);
    glFinishFenceNV(8);
    HostIs = 0;
    assert(!glIsFenceNV(8) && QueryCalls == 9);
    /* Deletion and lists forget signalled fences. */
    const GLuint three = 3;
    glDeleteFencesNV(1, &three);
    assert(!glIsFenceNV(3) && QueryCalls == 10);
    Replies = {{1, 1}};
    glFinishFenceNV(4);
    assert(glTestFenceNV(4) && QueryCalls == 11);
    JglCallList(2);
    Replies = {{1, 1}};
    assert(glTestFenceNV(4) && QueryCalls == 12);
    Log.clear();
    Close(c);
}
/* Begin, End and SetFence compile with deferred errors; the rest executes. */
static void Lists(void) {
    JGL_CONTEXT *c = Open(QUERIES);
    GLuint names[2] = {};
    assert(JglListMode(GL_COMPILE));
    Clear(c);
    glBeginQueryARB(SAMPLES, 10);
    glBeginQueryARB(0x88bf, 10);
    glEndQueryARB(SAMPLES);
    glSetFenceNV(2, 0);
    glSetFenceNV(2, ALL_COMPLETED);
    assert(!c->Error);
    Records r = Sent(c);
    assert(r.size() == 5);
    ExpectCall(r[0], FEnum_glBeginQuery, {SAMPLES, 10});
    ExpectDeferred(r[1], GL_INVALID_ENUM);
    ExpectCall(r[2], FEnum_glEndQuery, {SAMPLES});
    ExpectDeferred(r[3], GL_INVALID_ENUM);
    ExpectCall(r[4], FEnum_glSetFenceNV, {2, ALL_COMPLETED});
    /* Immediate errors, names and answers. */
    glGetQueryivARB(SAMPLES, RESULT, NULL);
    Rejected(c, GL_INVALID_ENUM);
    glGenQueriesARB(-1, names);
    Rejected(c, GL_INVALID_VALUE);
    glGenQueriesARB(2, names);
    glDeleteQueriesARB(2, names);
    Replies = {{DG_GL_QUERY_AVAILABLE, 9}};
    assert(Object(names[0], RESULT) == 9);
    r = Sent(c);
    assert(r.size() == 3 && r[0][0] == DG_GL_QUERY && r[1][0] == DG_GL_DATA_CALL &&
           r[2][0] == DG_GL_QUERY);
    assert(JglListMode(0));
    Close(c);
}

int main() {
    Unnegotiated();
    Negotiation();
    Records_();
    Validation();
    Polling();
    Fences();
    Lists();
    puts("PASS occlusion query frontend");
    return 0;
}
