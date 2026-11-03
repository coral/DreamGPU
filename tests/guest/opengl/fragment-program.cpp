/* SPDX-License-Identifier: GPL-2.0-or-later
 * DG_CAP_GL_FRAGMENT_PROGRAM through the actual frontend on the strict
 * transport fixture: negotiation and the unchanged frontend without it, local
 * validation (nothing the host would reject as malformed may leave the
 * frontend), exact records and float bits, chunked names, deletions and
 * strings, untransferred program strings, host-answered queries and display
 * list deferral. */
#include <utility>
#include <vector> /* before the Win32 fixture's min() macro */
#define main existing_frontend_suite_not_run
#include "frontend-harness.cpp"
#undef main
#include "arrays.cpp"
#include "query.cpp"
#include "program.cpp"
#include "dg-gl-result.h"
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

#define FP 0x8804U
#define VP 0x8620U
#define ASCII 0x8875U
#define PROGRAM_LENGTH 0x8627U
#define PROGRAM_STRING 0x8628U
#define PROGRAM_BINDING 0x8677U
#define UNDER_NATIVE 0x88b6U
#define ADDRESS_REGISTERS 0x88b0U
#define ERROR_POSITION 0x864bU
#define ERROR_STRING 0x8874U
#define CURRENT_MATRIX 0x8641U
#define MATRIX0 0x88c0U
#define MAX_COORDS 0x8871U
#define MAX_IMAGE_UNITS 0x8872U
#define PROGRAM_NAMES "GL_ARB_fragment_program"
#define SMALL_PACKET 256U /* 64 words: 204 payload bytes per program string */

static const char *const Entries[] = {
    "glGenProgramsARB",
    "glDeleteProgramsARB",
    "glBindProgramARB",
    "glIsProgramARB",
    "glProgramStringARB",
    "glProgramEnvParameter4fARB",
    "glProgramEnvParameter4fvARB",
    "glProgramEnvParameter4dARB",
    "glProgramEnvParameter4dvARB",
    "glProgramLocalParameter4fARB",
    "glProgramLocalParameter4fvARB",
    "glProgramLocalParameter4dARB",
    "glProgramLocalParameter4dvARB",
    "glGetProgramEnvParameterfvARB",
    "glGetProgramEnvParameterdvARB",
    "glGetProgramLocalParameterfvARB",
    "glGetProgramLocalParameterdvARB",
    "glGetProgramivARB",
    "glGetProgramStringARB",
};

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
    for (ULONG i = bytes; i < (bytes + 3) / 4 * 4; ++i)
        assert(!((const BYTE *)(r.data() + n))[i]);
}
static void ExpectCall(const std::vector<ULONG> &r, ULONG function,
                       std::initializer_list<ULONG> words) {
    assert(r[0] == DG_GL_CALL && r[8] == function && r[1] == (9 + words.size()) * 4);
    ULONG n = 9;
    for (ULONG word : words)
        assert(r[n++] == word);
}
static ULONG Bits(GLfloat value) {
    ULONG bits;
    memcpy(&bits, &value, 4);
    return bits;
}

/* Host answers. Generated names start at NextName; the bound program's
 * string is Program; parameters encode their index. */
static std::vector<std::vector<ULONG>> Queries; /* function, three arguments */
static ULONG NextName, ProgramLength;
static BOOL FailGenerate;
static BYTE Program[1000];
static void Reply(DG_ESCAPE_REPLY *reply, ULONG type, const void *data, ULONG bytes) {
    memcpy(reply + 1, data, bytes);
    reply->ResultBytes = bytes;
    reply->ResultType = type;
}
static BOOL Answer(const ULONG *r, DG_ESCAPE_REPLY *reply, ULONG capacity) {
    Queries.push_back({r[8], r[9], r[10], r[11]});
    switch (r[8]) {
        case FEnum_glGenProgramsARB: {
            std::vector<ULONG> names(r[9]);
            assert(r[9] >= 1 && r[9] <= DG_GL_PROGRAM_NAME_BATCH && capacity == r[9] * 4);
            for (ULONG &name : names)
                name = FailGenerate ? 0 : NextName++;
            Reply(reply, DG_GL_RESULT_INT, names.data(), r[9] * 4);
            return TRUE;
        }
        case FEnum_glIsProgramARB: {
            const BYTE value = r[9] == 5;
            Reply(reply, DG_GL_RESULT_BOOL, &value, 1);
            return TRUE;
        }
        case FEnum_glGetProgramivARB: {
            const ULONG value = r[10] == PROGRAM_LENGTH ? ProgramLength : 42;
            Reply(reply, DG_GL_RESULT_INT, &value, 4);
            return TRUE;
        }
        case FEnum_glGetProgramEnvParameterfvARB:
        case FEnum_glGetProgramLocalParameterfvARB: {
            const GLfloat sign = r[8] == FEnum_glGetProgramEnvParameterfvARB ? 1.0f : -1.0f;
            const GLfloat values[4] = {sign * (GLfloat)r[10], 0.5f, 0.25f, 1.0f / 3.0f};
            Reply(reply, DG_GL_RESULT_FLOAT, values, 16);
            return TRUE;
        }
        case FEnum_glGetProgramStringARB: {
            /* target, words, first word: zero-padded past the end. */
            BYTE window[DG_ESCAPE_MAX_RESULT_BYTES] = {};
            assert(r[9] == FP && r[10] && capacity == r[10] * 4);
            for (ULONG i = 0; i < r[10] * 4; ++i)
                if (r[11] * 4 + i < ProgramLength)
                    window[i] = Program[r[11] * 4 + i];
            Reply(reply, DG_GL_RESULT_INT, window, r[10] * 4);
            return TRUE;
        }
        case FEnum_glGetString: {
            /* The host's form: printable text ending at its terminator, the
             * only STRING the guest drivers pass on (dg-gl-result.h). */
            static const char text[] = "line 3: unexpected BOGUS";
            assert(r[9] == ERROR_STRING && capacity == DG_GL_MAX_RESULT_BYTES);
            assert(DgValidateGlResult(DG_GL_RESULT_STRING, sizeof(text), capacity,
                                      (const unsigned char *)text));
            Reply(reply, DG_GL_RESULT_STRING, text, sizeof(text));
            return TRUE;
        }
        case FEnum_glGetIntegerv: {
            const ULONG value = 8;
            Reply(reply, DG_GL_RESULT_INT, &value, 4);
            return TRUE;
        }
        case FEnum_glGetFloatv: {
            GLfloat matrix[16] = {};
            for (ULONG i = 0; i < 16; ++i)
                matrix[i] = (GLfloat)i;
            Reply(reply, DG_GL_RESULT_FLOAT, matrix, capacity < 64 ? 4 : 64);
            return TRUE;
        }
        case FEnum_glGetBooleanv:
        case FEnum_glIsEnabled: {
            const BYTE value = 1;
            Reply(reply, DG_GL_RESULT_BOOL, &value, 1);
            return TRUE;
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
    Queries.clear();
    NextName = 1;
    FailGenerate = FALSE;
    ProgramLength = 0;
    JGL_CONTEXT *c = Lookup(Create());
    Clear(c);
    return c;
}
static void Close(JGL_CONTEXT *c) {
    assert(wglMakeCurrent(NULL, NULL) && wglDeleteContext((HGLRC)(ULONG_PTR)c->Id));
    Log.clear();
}

/* Without the capability nothing new leaves the frontend: the entry points are
 * hidden and fail locally, and every new state name keeps its local error. */
static void Unnegotiated(void) {
    JGL_CONTEXT *c =
        Open(DG_CAP_GL_MULTITEXTURE | DG_CAP_GL_FIXED_STATE | DG_CAP_GL_TEXTURE_CONTROL |
             DG_CAP_GL_COMPRESSED_TEXTURES | DG_CAP_GL_FOG_COORD | DG_CAP_GL_TEXTURE_CUBE_MAP);
    assert(!JglFragmentProgramAvailable());
    assert(!strstr((const char *)glGetString(GL_EXTENSIONS), "program"));
    for (const char *name : Entries)
        assert(!wglGetProcAddress(name));
    assert(wglGetProcAddress("glActiveTextureARB") == Marker);
    const GLfloat v[4] = {1, 2, 3, 4};
    const GLdouble dv[4] = {1, 2, 3, 4};
    GLuint names[2] = {0x5a5a, 0x5a5a};
    GLfloat fv[4] = {9, 9, 9, 9};
    GLdouble out[4] = {9, 9, 9, 9};
    GLint value = 0x5a5a;
    GLboolean flag = 0x5a;
    char text[4] = "abc";
    glGenProgramsARB(2, names);
    Rejected(c, GL_INVALID_OPERATION);
    glDeleteProgramsARB(2, names);
    Rejected(c, GL_INVALID_OPERATION);
    glBindProgramARB(FP, 1);
    Rejected(c, GL_INVALID_OPERATION);
    assert(!glIsProgramARB(1));
    Rejected(c, GL_INVALID_OPERATION);
    glProgramStringARB(FP, ASCII, 3, "END");
    Rejected(c, GL_INVALID_OPERATION);
    glProgramEnvParameter4fARB(FP, 0, 1, 2, 3, 4);
    Rejected(c, GL_INVALID_OPERATION);
    glProgramEnvParameter4fvARB(FP, 0, v);
    Rejected(c, GL_INVALID_OPERATION);
    glProgramEnvParameter4dARB(FP, 0, 1, 2, 3, 4);
    Rejected(c, GL_INVALID_OPERATION);
    glProgramEnvParameter4dvARB(FP, 0, dv);
    Rejected(c, GL_INVALID_OPERATION);
    glProgramLocalParameter4fARB(FP, 0, 1, 2, 3, 4);
    Rejected(c, GL_INVALID_OPERATION);
    glProgramLocalParameter4fvARB(FP, 0, v);
    Rejected(c, GL_INVALID_OPERATION);
    glProgramLocalParameter4dARB(FP, 0, 1, 2, 3, 4);
    Rejected(c, GL_INVALID_OPERATION);
    glProgramLocalParameter4dvARB(FP, 0, dv);
    Rejected(c, GL_INVALID_OPERATION);
    glGetProgramEnvParameterfvARB(FP, 0, fv);
    Rejected(c, GL_INVALID_OPERATION);
    glGetProgramEnvParameterdvARB(FP, 0, out);
    Rejected(c, GL_INVALID_OPERATION);
    glGetProgramLocalParameterfvARB(FP, 0, fv);
    Rejected(c, GL_INVALID_OPERATION);
    glGetProgramLocalParameterdvARB(FP, 0, out);
    Rejected(c, GL_INVALID_OPERATION);
    glGetProgramivARB(FP, PROGRAM_LENGTH, &value);
    Rejected(c, GL_INVALID_OPERATION);
    glGetProgramStringARB(FP, PROGRAM_STRING, text);
    Rejected(c, GL_INVALID_OPERATION);
    /* State names stay the local GL_INVALID_ENUM the host reported before. */
    for (GLenum name : {FP, ERROR_POSITION, CURRENT_MATRIX, 0x8640U, 0x88b7U, 0x862fU, 0x862eU,
                        MAX_COORDS, MAX_IMAGE_UNITS}) {
        glGetIntegerv(name, &value);
        Rejected(c, GL_INVALID_ENUM);
        glGetBooleanv(name, &flag);
        Rejected(c, GL_INVALID_ENUM);
        glGetFloatv(name, fv);
        Rejected(c, GL_INVALID_ENUM);
        glGetDoublev(name, out);
        Rejected(c, GL_INVALID_ENUM);
    }
    assert(!glIsEnabled(FP));
    Rejected(c, GL_INVALID_ENUM);
    assert(!glGetString(ERROR_STRING));
    Rejected(c, GL_INVALID_ENUM);
    assert(Queries.empty() && names[0] == 0x5a5a && fv[0] == 9 && out[0] == 9 && value == 0x5a5a &&
           flag == 0x5a && !strcmp(text, "abc"));
    /* Enable and matrix modes keep passing through, as before. */
    glEnable(FP);
    glMatrixMode(MATRIX0);
    assert(c->Records == 2 && !c->Error);
    Submit(c);
    ExpectCall(Log[0], FEnum_glEnable, {FP});
    ExpectCall(Log[1], FEnum_glMatrixMode, {MATRIX0});
    Close(c);
}

/* Negotiated: the extension is appended last, alone or after all others, and
 * exactly its entry points become visible. */
static void Negotiation(void) {
    JGL_CONTEXT *c = Open(DG_CAP_GL_FRAGMENT_PROGRAM);
    assert(JglFragmentProgramAvailable());
    assert(!strcmp((const char *)glGetString(GL_EXTENSIONS), PROGRAM_NAMES));
    for (const char *name : Entries)
        assert(wglGetProcAddress(name) == Marker);
    assert(!wglGetProcAddress("glActiveTextureARB") && !wglGetProcAddress("glFogCoordf"));
    Close(c);
    Reset();
    HostCaps = DG_CAP_GL_MULTITEXTURE | DG_CAP_GL_FIXED_STATE | DG_CAP_GL_TEXTURE_CONTROL |
               DG_CAP_GL_COMPRESSED_TEXTURES | DG_CAP_GL_FOG_COORD | DG_CAP_GL_TEXTURE_CUBE_MAP |
               DG_CAP_GL_FRAGMENT_PROGRAM;
    HostSecondaryWords = 3;
    HGLRC context = Create();
    const char *all = (const char *)glGetString(GL_EXTENSIONS);
    const char *tail =
        "GL_EXT_fog_coord GL_ARB_texture_cube_map GL_EXT_texture_cube_map " PROGRAM_NAMES;
    assert(strlen(all) > strlen(tail) && !strcmp(all + strlen(all) - strlen(tail), tail));
    assert(!strncmp(all, "GL_ARB_multitexture ", 20));
    assert(wglMakeCurrent(NULL, NULL) && wglDeleteContext(context));
}

/* Exact records and bits for every command the host executes. */
static void Records(void) {
    JGL_CONTEXT *c = Open(DG_CAP_GL_FRAGMENT_PROGRAM);
    static const char source[] = "!!ARBfp1.0\nMOV result.color, fragment.color;\nEND";
    const ULONG length = sizeof(source) - 1; /* not a multiple of four */
    static const ULONG patterns[4] = {0x7f800123, 0x80000000, 0x00000001, 0x7fc055aa};
    GLfloat v[4];
    memcpy(v, patterns, sizeof(v));
    const GLuint names[3] = {0, 7, 9};
    glBindProgramARB(FP, 7);
    glProgramStringARB(FP, ASCII, (GLsizei)length, source);
    glProgramStringARB(FP, ASCII, 0, NULL);
    glProgramEnvParameter4fARB(FP, 27, v[0], v[1], v[2], v[3]);
    glProgramEnvParameter4fvARB(FP, 3, v);
    glProgramEnvParameter4fvARB(FP, 3, NULL); /* submits nothing */
    glProgramLocalParameter4fARB(FP, 4000, v[0], v[1], v[2], v[3]);
    glProgramLocalParameter4fvARB(FP, 1, v);
    glProgramEnvParameter4dARB(FP, 2, 0.1, -2.5, 1e40, 3.0);
    const GLdouble dv[4] = {0.2, 0.5, -0.0, 1e-50};
    glProgramLocalParameter4dvARB(FP, 6, dv);
    glProgramLocalParameter4dvARB(FP, 6, NULL);
    glDeleteProgramsARB(3, names);
    glDeleteProgramsARB(0, NULL);
    glBindProgramARB(FP, 0);
    assert(!c->Error);
    Submit(c);
    assert(Log.size() == 11);
    ExpectCall(Log[0], FEnum_glBindProgramARB, {FP, 7});
    ExpectData(Log[1], FEnum_glProgramStringARB, {FP, ASCII, length}, source, length);
    ExpectData(Log[2], FEnum_glProgramStringARB, {FP, ASCII, 0}, NULL, 0);
    ExpectCall(Log[3], FEnum_glProgramEnvParameter4fARB,
               {FP, 27, patterns[0], patterns[1], patterns[2], patterns[3]});
    ExpectCall(Log[4], FEnum_glProgramEnvParameter4fARB,
               {FP, 3, patterns[0], patterns[1], patterns[2], patterns[3]});
    ExpectCall(Log[5], FEnum_glProgramLocalParameter4fARB,
               {FP, 4000, patterns[0], patterns[1], patterns[2], patterns[3]});
    ExpectCall(Log[6], FEnum_glProgramLocalParameter4fARB,
               {FP, 1, patterns[0], patterns[1], patterns[2], patterns[3]});
    /* Doubles convert to the float record, as native GL does. */
    ExpectCall(Log[7], FEnum_glProgramEnvParameter4fARB,
               {FP, 2, Bits((GLfloat)0.1), Bits(-2.5f), Bits((GLfloat)1e40), Bits(3.0f)});
    ExpectCall(Log[8], FEnum_glProgramLocalParameter4fARB,
               {FP, 6, Bits((GLfloat)0.2), Bits(0.5f), Bits(-0.0f), Bits((GLfloat)1e-50)});
    ExpectData(Log[9], FEnum_glDeleteProgramsARB, {3}, names, sizeof(names));
    ExpectCall(Log[10], FEnum_glBindProgramARB, {FP, 0});
    Close(c);

    /* Deletion splits at the packet's capacity; a string beyond the packet (or
     * DG_GL_MAX_PROGRAM_BYTES) travels without bytes, which are never read. */
    c = Open(DG_CAP_GL_FRAGMENT_PROGRAM, SMALL_PACKET);
    const ULONG capacity = JglMaxDataBytes(1) / 4;
    std::vector<GLuint> many(capacity * 2 + 3);
    for (ULONG i = 0; i < many.size(); ++i)
        many[i] = i + 100;
    glDeleteProgramsARB((GLsizei)many.size(), many.data());
    Submit(c);
    assert(Log.size() == 3);
    ExpectData(Log[0], FEnum_glDeleteProgramsARB, {capacity}, many.data(), capacity * 4);
    ExpectData(Log[1], FEnum_glDeleteProgramsARB, {capacity}, many.data() + capacity, capacity * 4);
    ExpectData(Log[2], FEnum_glDeleteProgramsARB, {3}, many.data() + 2 * capacity, 12);
    Log.clear();
    const ULONG fits = JglMaxDataBytes(3);
    std::vector<char> text(fits + 1, 'x');
    glProgramStringARB(FP, ASCII, (GLsizei)fits, text.data());
    glProgramStringARB(FP, ASCII, (GLsizei)fits + 1, text.data());
    Submit(c);
    ExpectData(Log[0], FEnum_glProgramStringARB, {FP, ASCII, fits}, text.data(), fits);
    ExpectData(Log[1], FEnum_glProgramStringARB, {FP, ASCII, fits + 1}, NULL, 0);
    Close(c);
    c = Open(DG_CAP_GL_FRAGMENT_PROGRAM);
    glProgramStringARB(FP, ASCII, DG_GL_MAX_PROGRAM_BYTES + 1, (const void *)16);
    glProgramStringARB(FP, ASCII, 0x7fffffff, (const void *)16);
    Submit(c);
    ExpectData(Log[0], FEnum_glProgramStringARB, {FP, ASCII, DG_GL_MAX_PROGRAM_BYTES + 1}, NULL, 0);
    ExpectData(Log[1], FEnum_glProgramStringARB, {FP, ASCII, 0x7fffffff}, NULL, 0);
    Close(c);
}

/* Everything the record shows to be invalid is a local error. */
static void Validation(void) {
    JGL_CONTEXT *c = Open(DG_CAP_GL_FRAGMENT_PROGRAM);
    GLuint names[2] = {0x5a5a, 0x5a5a};
    GLfloat fv[4] = {9, 9, 9, 9};
    GLint value = 0x5a5a;
    const GLfloat v[4] = {1, 2, 3, 4};
    char text[4] = "abc";
    /* Vertex programs (which WineD3D's ARB backend still drives) and every
     * other target are unknown. */
    for (GLenum target : {VP, 0U, 0x0de1U}) {
        glBindProgramARB(target, 1);
        Rejected(c, GL_INVALID_ENUM);
        glProgramStringARB(target, ASCII, 3, "END");
        Rejected(c, GL_INVALID_ENUM);
        glProgramEnvParameter4fvARB(target, 0, v);
        Rejected(c, GL_INVALID_ENUM);
        glProgramLocalParameter4fARB(target, 0, 1, 2, 3, 4);
        Rejected(c, GL_INVALID_ENUM);
        glProgramLocalParameter4dARB(target, 0, 1, 2, 3, 4);
        Rejected(c, GL_INVALID_ENUM);
        glGetProgramEnvParameterfvARB(target, 0, fv);
        Rejected(c, GL_INVALID_ENUM);
        glGetProgramivARB(target, PROGRAM_LENGTH, &value);
        Rejected(c, GL_INVALID_ENUM);
        glGetProgramStringARB(target, PROGRAM_STRING, text);
        Rejected(c, GL_INVALID_ENUM);
    }
    glProgramStringARB(FP, 0, 3, "END");
    Rejected(c, GL_INVALID_ENUM);
    glProgramStringARB(FP, ASCII, -1, "END");
    Rejected(c, GL_INVALID_VALUE);
    glProgramStringARB(FP, ASCII, 3, NULL);
    Rejected(c, GL_INVALID_VALUE);
    glGenProgramsARB(-1, names);
    Rejected(c, GL_INVALID_VALUE);
    glGenProgramsARB(1, NULL);
    Rejected(c, GL_INVALID_VALUE);
    glDeleteProgramsARB(-1, names);
    Rejected(c, GL_INVALID_VALUE);
    glDeleteProgramsARB(1, NULL);
    Rejected(c, GL_INVALID_VALUE);
    for (GLenum pname : {ADDRESS_REGISTERS, 0x88b3U, PROGRAM_STRING, 0x8804U, 0U}) {
        glGetProgramivARB(FP, pname, &value);
        Rejected(c, GL_INVALID_ENUM);
    }
    glGetProgramStringARB(FP, PROGRAM_LENGTH, text);
    Rejected(c, GL_INVALID_ENUM);
    assert(Queries.empty() && names[0] == 0x5a5a && fv[0] == 9 && value == 0x5a5a);
    /* Inside Begin/End nothing but vertex specification is legal. */
    c->InBegin = TRUE;
    glBindProgramARB(FP, 1);
    assert(c->Error == GL_INVALID_OPERATION && !c->Records);
    c->Error = 0;
    glProgramEnvParameter4fARB(FP, 0, 1, 2, 3, 4);
    assert(c->Error == GL_INVALID_OPERATION && !c->Records);
    c->Error = 0;
    glGenProgramsARB(1, names);
    assert(c->Error == GL_INVALID_OPERATION && Queries.empty());
    c->Error = 0;
    c->InBegin = FALSE;
    Clear(c);
    /* Compiled commands defer their errors into the list; the immediate ones
     * (Gen, Delete, Is and queries) raise them at once. */
    assert(JglListMode(GL_COMPILE));
    Clear(c);
    glBindProgramARB(VP, 1);
    glProgramStringARB(FP, 0, 3, "END");
    glProgramLocalParameter4fvARB(FP, 0, NULL);
    glBindProgramARB(FP, 3);
    assert(!c->Error && c->Records == 3);
    glGenProgramsARB(-1, names);
    assert(c->Error == GL_INVALID_VALUE && c->Records == 3);
    c->Error = 0;
    Submit(c);
    ExpectCall(Log[0], DG_GL_RECORD_ERROR, {GL_INVALID_ENUM});
    ExpectCall(Log[1], DG_GL_RECORD_ERROR, {GL_INVALID_ENUM});
    ExpectCall(Log[2], FEnum_glBindProgramARB, {FP, 3});
    assert(JglListMode(0));
    Close(c);
}

/* Host-answered queries: chunked names, parameters, strings and state. */
static void Queries_(void) {
    JGL_CONTEXT *c = Open(DG_CAP_GL_FRAGMENT_PROGRAM);
    std::vector<GLuint> names(DG_GL_PROGRAM_NAME_BATCH * 2 + 5, 0x5a5a);
    glGenProgramsARB((GLsizei)names.size(), names.data());
    assert(Queries.size() == 3 && !c->Error);
    assert(Queries[0][1] == DG_GL_PROGRAM_NAME_BATCH && Queries[2][1] == 5);
    for (ULONG i = 0; i < names.size(); ++i)
        assert(names[i] == i + 1);
    /* A failed reservation (GL_OUT_OF_MEMORY is the host's) writes nothing. */
    FailGenerate = TRUE;
    GLuint failed[2] = {0x5a5a, 0x5a5a};
    glGenProgramsARB(2, failed);
    assert(failed[0] == 0x5a5a && failed[1] == 0x5a5a && !c->Error);
    glGenProgramsARB(0, NULL);
    assert(glIsProgramARB(5) && !glIsProgramARB(6) && !glIsProgramARB(0));
    Queries.clear();
    GLint value = 0;
    glGetProgramivARB(FP, UNDER_NATIVE, &value);
    assert(value == 42);
    glGetProgramivARB(FP, PROGRAM_BINDING, &value);
    assert(Queries.size() == 2 && Queries[1][0] == FEnum_glGetProgramivARB && Queries[1][1] == FP &&
           Queries[1][2] == PROGRAM_BINDING && !Queries[1][3]);
    GLfloat fv[4];
    GLdouble dv[4];
    glGetProgramEnvParameterfvARB(FP, 27, fv);
    assert(fv[0] == 27 && fv[1] == 0.5f && fv[3] == 1.0f / 3.0f);
    glGetProgramLocalParameterdvARB(FP, 4000, dv);
    assert(dv[0] == -4000.0 && dv[2] == 0.25 && dv[3] == (GLdouble)(1.0f / 3.0f));
    glGetProgramEnvParameterdvARB(FP, 1, dv);
    glGetProgramLocalParameterfvARB(FP, 1, fv);
    assert(dv[0] == 1.0 && fv[0] == -1.0f);
    assert(Queries.back()[0] == FEnum_glGetProgramLocalParameterfvARB && Queries.back()[2] == 1);
    /* The string arrives in windows of at most one result buffer and fills
     * exactly PROGRAM_LENGTH bytes. */
    for (ULONG i = 0; i < sizeof(Program); ++i)
        Program[i] = (BYTE)('A' + i % 26);
    ProgramLength = 999;
    HostMaxResultBytes = DG_ESCAPE_LEGACY_RESULT_BYTES;
    MaxResultBytes = DG_ESCAPE_LEGACY_RESULT_BYTES;
    std::vector<BYTE> out(1100, 0xcc);
    Queries.clear();
    glGetProgramStringARB(FP, PROGRAM_STRING, out.data());
    assert(!memcmp(out.data(), Program, 999) && out[999] == 0xcc);
    assert(Queries.size() == 3 && Queries[1][0] == FEnum_glGetProgramStringARB &&
           Queries[1][2] == 128 && Queries[1][3] == 0 && Queries[2][2] == 122 &&
           Queries[2][3] == 128);
    /* An empty program writes nothing at all. */
    ProgramLength = 0;
    Queries.clear();
    glGetProgramStringARB(FP, PROGRAM_STRING, out.data() + 1050);
    assert(Queries.size() == 1 && out[1050] == 0xcc);
    /* The error string is host text in stable per-context storage. */
    const GLubyte *first = glGetString(ERROR_STRING);
    assert(first && !strcmp((const char *)first, "line 3: unexpected BOGUS"));
    assert(glGetString(ERROR_STRING) == first);
    /* State names reach the host once negotiated. */
    GLboolean flag = 0;
    GLfloat matrix[16];
    glGetIntegerv(ERROR_POSITION, &value);
    glGetIntegerv(MAX_IMAGE_UNITS, &value);
    assert(value == 8);
    glGetFloatv(CURRENT_MATRIX, matrix);
    assert(matrix[15] == 15.0f);
    glGetBooleanv(FP, &flag);
    assert(glIsEnabled(FP));
    assert(!c->Error && !c->Failed);
    Close(c);
}

int main() {
    Unnegotiated();
    Negotiation();
    Records();
    Validation();
    Queries_();
    puts("PASS fragment program frontend");
    return 0;
}
