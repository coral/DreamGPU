/* SPDX-License-Identifier: GPL-2.0-or-later
 * DG_CAP_GL_VERTEX_PROGRAM through the actual frontend on the strict transport
 * fixture: negotiation (alone, after ARB_fragment_program, and the unchanged
 * frontend without it), the program functions for the vertex target, exact
 * VertexAttrib4fARB records and conversions inside and outside Begin/End and in
 * display lists, local validation (nothing the host would reject as malformed
 * may leave the frontend), generic attribute array state, queries and the
 * client attribute stack, the attribute draw for array, element and split
 * draws (every record admitted by the NT validator), ArrayElement with
 * attribute 0 as the position, and the host-answered current value. */
#include <string>
#include <utility>
#include <vector> /* before the Win32 fixture's min() macro */
#define main existing_frontend_suite_not_run
#include "frontend-harness.cpp"
#undef main
#include "arrays.cpp"
#include "query.cpp"
#include "program.cpp"
#include "attrib.cpp"
using GLvoid = void;
#include "icd-client.inc"
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

#define VP 0x8620U
#define FP 0x8804U
#define ASCII 0x8875U
#define POINT_SIZE 0x8642U
#define TWO_SIDE 0x8643U
#define MAX_ATTRIBS 0x8869U
#define ERROR_POSITION 0x864bU
#define ERROR_STRING 0x8874U
#define PROGRAM_LENGTH 0x8627U
#define PROGRAM_STRING 0x8628U
#define ADDRESS_REGISTERS 0x88b0U
#define ALU_INSTRUCTIONS 0x8805U
#define ENABLED 0x8622U
#define SIZE 0x8623U
#define STRIDE 0x8624U
#define TYPE 0x8625U
#define CURRENT 0x8626U
#define POINTER 0x8645U
#define NORMALIZED 0x886aU
#define CLIENT_VERTEX_ARRAY_BIT 2U
#define CLIENT_PIXEL_STORE_BIT 1U
#define SMALL_PACKET 512U
#define ALL_BUT_VERTEX                                                                             \
    (DG_CAP_GL_MULTITEXTURE | DG_CAP_GL_FIXED_STATE | DG_CAP_GL_TEXTURE_CONTROL |                  \
     DG_CAP_GL_COMPRESSED_TEXTURES | DG_CAP_GL_FOG_COORD | DG_CAP_GL_TEXTURE_CUBE_MAP |            \
     DG_CAP_GL_FRAGMENT_PROGRAM)

/* The generic attribute exports, ARB names only. */
static std::vector<std::string> AttributeEntries() {
    std::vector<std::string> names;
    for (const char *count : {"1", "2", "3", "4"})
        for (const char *kind : {"s", "f", "d"}) {
            names.push_back(std::string("glVertexAttrib") + count + kind + "ARB");
            if (strcmp(count, "4") || strcmp(kind, "s"))
                names.push_back(std::string("glVertexAttrib") + count + kind + "vARB");
        }
    for (const char *kind : {"b", "s", "i", "ub", "us", "ui"}) {
        names.push_back(std::string("glVertexAttrib4") + kind + "vARB");
        names.push_back(std::string("glVertexAttrib4N") + kind + "vARB");
    }
    for (const char *name :
         {"glVertexAttrib4NubARB", "glVertexAttribPointerARB", "glEnableVertexAttribArrayARB",
          "glDisableVertexAttribArrayARB", "glGetVertexAttribdvARB", "glGetVertexAttribfvARB",
          "glGetVertexAttribivARB", "glGetVertexAttribPointervARB"})
        names.push_back(name);
    assert(names.size() == 43);
    return names;
}

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
static ULONG Bits(GLfloat value) {
    ULONG bits;
    memcpy(&bits, &value, 4);
    return bits;
}
static void ExpectCall(const std::vector<ULONG> &r, ULONG function,
                       std::initializer_list<ULONG> words) {
    assert(r[0] == DG_GL_CALL && r[8] == function && r[1] == (9 + words.size()) * 4);
    ULONG n = 9;
    for (ULONG word : words)
        assert(r[n++] == word);
}
/* One VertexAttrib4fARB record with these float values. */
static void ExpectAttribute(const std::vector<ULONG> &r, ULONG index, GLfloat x, GLfloat y,
                            GLfloat z, GLfloat w) {
    ExpectCall(r, FEnum_glVertexAttrib4fARB, {index, Bits(x), Bits(y), Bits(z), Bits(w)});
}

/* The NT kernel admits each draw record exactly as captured. */
static ULONG KernelWords(void *, ULONG function) {
    return function == FEnum_glDrawArrays || function == FEnum_glDrawArraysEXT
               ? DG_GL_FUNCTION_INLINE_DATA | 4
           : function == FEnum_glVertexAttribPointerARB ? DG_GL_FUNCTION_INLINE_DATA | 5
                                                        : (ULONG)-1;
}
static void Admitted(const std::vector<ULONG> &r) {
    std::vector<ULONG> copy(r);
    const DG_GL_LIMITS limits = {7, DG_ESCAPE_MAX_BYTES, DG_GL_MAX_RECORDS, 1};
    assert(DgValidateUserGl(copy.data(), copy.size() * 4, 42, 7, &limits, KernelWords, NULL));
}
struct Section {
    ULONG Slot, Descriptor;
    std::vector<BYTE> Bytes;
};
/* The attribute draw payload: thirty-two descriptors, then each section at
 * the shared layout's offset with zero padding. */
static std::vector<BYTE> Payload(ULONG mask, ULONG generic, ULONG count,
                                 const std::vector<Section> &sections) {
    unsigned descriptors[DG_GL_ATTRIB_SLOTS] = {}, offsets[DG_GL_ATTRIB_SLOTS], length = 0;
    for (const Section &s : sections)
        descriptors[s.Slot] = s.Descriptor;
    assert(DgRawAttribLayout(mask, generic, count, descriptors, offsets, &length));
    std::vector<BYTE> out(length, 0);
    memcpy(out.data(), descriptors, sizeof(descriptors));
    for (const Section &s : sections) {
        assert(s.Bytes.size() ==
               count * ((s.Descriptor >> 16) & 255) * DgArrayComponentBytes(s.Descriptor & 65535));
        memcpy(&out[offsets[s.Slot]], s.Bytes.data(), s.Bytes.size());
    }
    return out;
}
static void ExpectDraw(const std::vector<ULONG> &r, ULONG mode, ULONG count, ULONG mask,
                       ULONG generic, const std::vector<BYTE> &payload) {
    assert(r[0] == DG_GL_DATA_CALL && r[8] == FEnum_glVertexAttribPointerARB);
    assert(r[9] == payload.size() && r[10] == mode && r[11] == 0 && r[12] == count &&
           r[13] == mask && r[14] == generic);
    assert(!memcmp(r.data() + 15, payload.data(), payload.size()));
    Admitted(r);
}
template <typename T>
static std::vector<BYTE> Gather(const T *source, std::initializer_list<ULONG> order) {
    std::vector<BYTE> out;
    for (ULONG i : order)
        out.insert(out.end(), (const BYTE *)&source[i], (const BYTE *)&source[i] + sizeof(T));
    return out;
}

/* Host answers: current attribute values encode their index. */
static std::vector<std::vector<ULONG>> Queries;
static BOOL Answer(const ULONG *r, DG_ESCAPE_REPLY *reply, ULONG capacity) {
    Queries.push_back({r[8], r[9], r[10], r[11]});
    auto put = [&](ULONG type, const void *data, ULONG bytes) {
        memcpy(reply + 1, data, bytes);
        reply->ResultBytes = bytes;
        reply->ResultType = type;
        return TRUE;
    };
    switch (r[8]) {
        case FEnum_glGetVertexAttribfvARB: {
            assert(r[10] == CURRENT && !r[11] && capacity == 16);
            const GLfloat values[4] = {(GLfloat)r[9], 1.5f, -1.5f, 0.49f};
            return put(DG_GL_RESULT_FLOAT, values, 16);
        }
        case FEnum_glGetIntegerv:
        case FEnum_glGetProgramivARB: {
            const ULONG value = r[8] == FEnum_glGetIntegerv ? 16 : 42;
            return put(DG_GL_RESULT_INT, &value, 4);
        }
        case FEnum_glIsEnabled:
        case FEnum_glGetBooleanv: {
            const BYTE value = 1;
            return put(DG_GL_RESULT_BOOL, &value, 1);
        }
        case FEnum_glGetString: {
            static const char text[] = "DreamGPU: generic vertex attribute";
            assert(r[9] == ERROR_STRING && capacity == DG_GL_MAX_RESULT_BYTES);
            return put(DG_GL_RESULT_STRING, text, sizeof(text));
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
    JGL_CONTEXT *c = Lookup(Create());
    Clear(c);
    return c;
}
static void Close(JGL_CONTEXT *c) {
    assert(wglMakeCurrent(NULL, NULL) && wglDeleteContext((HGLRC)(ULONG_PTR)c->Id));
    Log.clear();
}

/* Without the capability nothing new leaves the frontend: generic attributes,
 * the vertex target and the vertex program modes keep their local errors. */
static void Unnegotiated(void) {
    JGL_CONTEXT *c = Open(ALL_BUT_VERTEX);
    assert(!JglVertexProgramAvailable() && JglProgramsAvailable());
    assert(!strstr((const char *)glGetString(GL_EXTENSIONS), "vertex_program"));
    for (const std::string &name : AttributeEntries())
        assert(!wglGetProcAddress(name.c_str()));
    assert(wglGetProcAddress("glBindProgramARB") == Marker);
    const GLfloat v[4] = {1, 2, 3, 4};
    GLfloat fv[4] = {9, 9, 9, 9};
    GLint iv = 0x5a5a;
    void *pointer = (void *)16;
    glVertexAttrib4fARB(1, 1, 2, 3, 4);
    Rejected(c, GL_INVALID_OPERATION);
    glVertexAttrib4fvARB(1, v);
    Rejected(c, GL_INVALID_OPERATION);
    glVertexAttrib1sARB(1, 2);
    Rejected(c, GL_INVALID_OPERATION);
    glVertexAttrib4NubARB(1, 1, 2, 3, 4);
    Rejected(c, GL_INVALID_OPERATION);
    c->InBegin = TRUE;
    glVertexAttrib2fARB(0, 1, 2);
    assert(c->Error == GL_INVALID_OPERATION && !c->Records);
    c->Error = 0;
    c->InBegin = FALSE;
    glVertexAttribPointerARB(1, 4, GL_FLOAT, GL_FALSE, 0, v);
    Rejected(c, GL_INVALID_OPERATION);
    glEnableVertexAttribArrayARB(1);
    Rejected(c, GL_INVALID_OPERATION);
    glDisableVertexAttribArrayARB(1);
    Rejected(c, GL_INVALID_OPERATION);
    glGetVertexAttribfvARB(1, CURRENT, fv);
    Rejected(c, GL_INVALID_OPERATION);
    glGetVertexAttribivARB(1, SIZE, &iv);
    Rejected(c, GL_INVALID_OPERATION);
    glGetVertexAttribPointervARB(1, POINTER, &pointer);
    Rejected(c, GL_INVALID_OPERATION);
    assert(fv[0] == 9 && iv == 0x5a5a && pointer == (void *)16);
    /* The vertex target is unknown; the fragment target still works. */
    glBindProgramARB(VP, 1);
    Rejected(c, GL_INVALID_ENUM);
    glProgramStringARB(VP, ASCII, 3, "END");
    Rejected(c, GL_INVALID_ENUM);
    glProgramEnvParameter4fvARB(VP, 0, v);
    Rejected(c, GL_INVALID_ENUM);
    glGetProgramivARB(VP, PROGRAM_LENGTH, &iv);
    Rejected(c, GL_INVALID_ENUM);
    /* The modes stay GL_INVALID_ENUM without a record. */
    for (GLenum cap : {VP, POINT_SIZE, TWO_SIDE}) {
        glEnable(cap);
        Rejected(c, GL_INVALID_ENUM);
        glDisable(cap);
        Rejected(c, GL_INVALID_ENUM);
        assert(!glIsEnabled(cap));
        Rejected(c, GL_INVALID_ENUM);
    }
    glGetIntegerv(MAX_ATTRIBS, &iv);
    Rejected(c, GL_INVALID_ENUM);
    /* Shared program state stays available through ARB_fragment_program. */
    glGetIntegerv(ERROR_POSITION, &iv);
    assert(!c->Error && iv == 16);
    assert(Queries.size() == 1 && Queries[0][0] == FEnum_glGetIntegerv);
    glEnable(FP);
    assert(c->Records == 1 && !c->Error);
    Close(c);
    /* Neither program capability: the shared names are local errors too. */
    c = Open(DG_CAP_GL_MULTITEXTURE);
    glGetIntegerv(ERROR_POSITION, &iv);
    Rejected(c, GL_INVALID_ENUM);
    assert(!glGetString(ERROR_STRING));
    Rejected(c, GL_INVALID_ENUM);
    Close(c);
}

/* Negotiated: the extension is appended last, alone or after all others. */
static void Negotiation(void) {
    JGL_CONTEXT *c = Open(DG_CAP_GL_VERTEX_PROGRAM);
    assert(JglVertexProgramAvailable() && !JglFragmentProgramAvailable());
    assert(!strcmp((const char *)glGetString(GL_EXTENSIONS), "GL_ARB_vertex_program"));
    for (const std::string &name : AttributeEntries())
        assert(wglGetProcAddress(name.c_str()) == Marker);
    for (const char *name : {"glGenProgramsARB", "glBindProgramARB", "glProgramStringARB",
                             "glProgramEnvParameter4fARB", "glGetProgramivARB"})
        assert(wglGetProcAddress(name) == Marker);
    /* Only the negotiated target exists. */
    glBindProgramARB(FP, 1);
    Rejected(c, GL_INVALID_ENUM);
    glBindProgramARB(VP, 1);
    assert(!c->Error && c->Records == 1);
    assert(glGetString(ERROR_STRING) &&
           !strcmp((const char *)glGetString(ERROR_STRING), "DreamGPU: generic vertex attribute"));
    Close(c);
    Reset();
    HostCaps = ALL_BUT_VERTEX | DG_CAP_GL_VERTEX_PROGRAM;
    HostSecondaryWords = 3;
    HGLRC context = Create();
    const char *all = (const char *)glGetString(GL_EXTENSIONS);
    const char *tail = "GL_ARB_fragment_program GL_ARB_vertex_program";
    assert(strlen(all) > strlen(tail) && !strcmp(all + strlen(all) - strlen(tail), tail));
    assert(wglMakeCurrent(NULL, NULL) && wglDeleteContext(context));
}

/* Exact records and conversions; local validation. */
static void Records(void) {
    JGL_CONTEXT *c = Open(DG_CAP_GL_VERTEX_PROGRAM | DG_CAP_GL_FRAGMENT_PROGRAM);
    static const char source[] = "!!ARBvp1.0\nMOV result.position, vertex.attrib[0];\nEND";
    static const ULONG patterns[4] = {0x7f800123, 0x80000000, 0x00000001, 0x7fc055aa};
    GLfloat p[4];
    memcpy(p, patterns, sizeof(p));
    glBindProgramARB(VP, 3);
    glProgramStringARB(VP, ASCII, (GLsizei)(sizeof(source) - 1), source);
    glProgramEnvParameter4fARB(VP, 255, p[0], p[1], p[2], p[3]);
    glProgramLocalParameter4dARB(VP, 7, 0.5, -2, 1e40, 3);
    glEnable(VP);
    glEnable(POINT_SIZE);
    glDisable(TWO_SIDE);
    assert(!c->Error);
    Submit(c);
    assert(Log.size() == 7);
    ExpectCall(Log[0], FEnum_glBindProgramARB, {VP, 3});
    assert(Log[1][8] == FEnum_glProgramStringARB && Log[1][10] == VP &&
           Log[1][12] == sizeof(source) - 1);
    ExpectCall(Log[2], FEnum_glProgramEnvParameter4fARB,
               {VP, 255, patterns[0], patterns[1], patterns[2], patterns[3]});
    ExpectCall(Log[3], FEnum_glProgramLocalParameter4fARB,
               {VP, 7, Bits(0.5f), Bits(-2.0f), Bits((GLfloat)1e40), Bits(3.0f)});
    ExpectCall(Log[4], FEnum_glEnable, {VP});
    ExpectCall(Log[5], FEnum_glEnable, {POINT_SIZE});
    ExpectCall(Log[6], FEnum_glDisable, {TWO_SIDE});
    Log.clear();

    /* Float bits survive; absent components are (0, 0, 0, 1). */
    glVertexAttrib1fARB(1, p[0]);
    glVertexAttrib2fARB(2, p[0], p[1]);
    glVertexAttrib3fARB(3, p[0], p[1], p[2]);
    glVertexAttrib4fARB(15, p[0], p[1], p[2], p[3]);
    glVertexAttrib1fvARB(4, p);
    glVertexAttrib2fvARB(5, p);
    glVertexAttrib3fvARB(6, p);
    glVertexAttrib4fvARB(7, p);
    glVertexAttrib4fvARB(7, NULL); /* submits nothing */
    Submit(c);
    assert(Log.size() == 8);
    const ULONG one = 0x3f800000;
    ExpectCall(Log[0], FEnum_glVertexAttrib4fARB, {1, patterns[0], 0, 0, one});
    ExpectCall(Log[1], FEnum_glVertexAttrib4fARB, {2, patterns[0], patterns[1], 0, one});
    ExpectCall(Log[2], FEnum_glVertexAttrib4fARB, {3, patterns[0], patterns[1], patterns[2], one});
    ExpectCall(Log[3], FEnum_glVertexAttrib4fARB,
               {15, patterns[0], patterns[1], patterns[2], patterns[3]});
    ExpectCall(Log[4], FEnum_glVertexAttrib4fARB, {4, patterns[0], 0, 0, one});
    ExpectCall(Log[5], FEnum_glVertexAttrib4fARB, {5, patterns[0], patterns[1], 0, one});
    ExpectCall(Log[6], FEnum_glVertexAttrib4fARB, {6, patterns[0], patterns[1], patterns[2], one});
    ExpectCall(Log[7], FEnum_glVertexAttrib4fARB,
               {7, patterns[0], patterns[1], patterns[2], patterns[3]});
    Log.clear();

    /* Integer and double forms convert; N forms normalize (Table 2.6). */
    const GLshort s[4] = {-7, 3, 32767, -32768};
    const GLdouble d[4] = {0.1, -2.5, 1e40, 3};
    const signed char b[4] = {-128, 127, 0, -1};
    const GLubyte ub[4] = {0, 255, 51, 128};
    const GLushort us[4] = {0, 65535, 1, 2};
    const GLint i[4] = {-1, 2147483647, 0, 5};
    const GLuint ui[4] = {0, 4294967295u, 7, 1};
    glVertexAttrib1sARB(1, -7);
    glVertexAttrib2svARB(2, s);
    glVertexAttrib3dARB(3, 0.1, -2.5, 1e40);
    glVertexAttrib4dvARB(4, d);
    glVertexAttrib4bvARB(5, b);
    glVertexAttrib4NbvARB(5, b);
    glVertexAttrib4NubvARB(6, ub);
    glVertexAttrib4NusvARB(7, us);
    glVertexAttrib4NivARB(8, i);
    glVertexAttrib4NuivARB(9, ui);
    glVertexAttrib4ivARB(10, i);
    glVertexAttrib4uivARB(11, ui);
    glVertexAttrib4NsvARB(12, s);
    glVertexAttrib4NubARB(13, 255, 0, 51, 255);
    glVertexAttrib4ubvARB(14, ub);
    glVertexAttrib3svARB(14, NULL); /* submits nothing */
    Submit(c);
    assert(Log.size() == 15);
    ExpectAttribute(Log[0], 1, -7, 0, 0, 1);
    ExpectAttribute(Log[1], 2, -7, 3, 0, 1);
    ExpectAttribute(Log[2], 3, (GLfloat)0.1, -2.5f, (GLfloat)1e40, 1);
    ExpectAttribute(Log[3], 4, (GLfloat)0.1, -2.5f, (GLfloat)1e40, 3);
    ExpectAttribute(Log[4], 5, -128, 127, 0, -1);
    ExpectAttribute(Log[5], 5, -1, 1, (2.0f * 0 + 1.0f) / 255.0f, (2.0f * -1 + 1.0f) / 255.0f);
    ExpectAttribute(Log[6], 6, 0, 1, 51 / 255.0f, 128 / 255.0f);
    ExpectAttribute(Log[7], 7, 0, 1, 1 / 65535.0f, 2 / 65535.0f);
    ExpectAttribute(Log[8], 8, (GLfloat)((2.0 * -1 + 1.0) / 4294967295.0), 1,
                    (GLfloat)(1.0 / 4294967295.0), (GLfloat)((2.0 * 5 + 1.0) / 4294967295.0));
    ExpectAttribute(Log[9], 9, 0, 1, (GLfloat)(7 / 4294967295.0), (GLfloat)(1 / 4294967295.0));
    ExpectAttribute(Log[10], 10, -1, 2147483647.0f, 0, 5);
    ExpectAttribute(Log[11], 11, 0, 4294967295.0f, 7, 1);
    ExpectAttribute(Log[12], 12, (2.0f * -7 + 1.0f) / 65535.0f, (2.0f * 3 + 1.0f) / 65535.0f, 1,
                    -1);
    ExpectAttribute(Log[13], 13, 1, 0, 51 / 255.0f, 1);
    ExpectAttribute(Log[14], 14, 0, 255, 51, 128);
    Log.clear();

    /* Legal inside Begin/End, where attribute 0 provokes the vertex. */
    glBegin(GL_POINTS);
    glVertexAttrib4fARB(3, 1, 0, 0, 1);
    glVertexAttrib2fARB(0, 0.5f, 0.25f);
    glEnd();
    Submit(c);
    assert(Log.size() == 4);
    ExpectCall(Log[0], FEnum_glBegin, {GL_POINTS});
    ExpectAttribute(Log[1], 3, 1, 0, 0, 1);
    ExpectAttribute(Log[2], 0, 0.5f, 0.25f, 0, 1);
    ExpectCall(Log[3], FEnum_glEnd, {});
    Log.clear();
    /* An index the protocol does not have never reaches the host. */
    glVertexAttrib4fARB(16, 1, 2, 3, 4);
    Rejected(c, GL_INVALID_VALUE);
    glVertexAttrib4NubvARB(0xffffffffu, ub);
    Rejected(c, GL_INVALID_VALUE);
    /* Compiled into display lists; errors are deferred into the list. */
    assert(JglListMode(GL_COMPILE));
    Clear(c);
    glVertexAttrib4fARB(2, 1, 2, 3, 4);
    glVertexAttrib1dARB(16, 1);
    glBindProgramARB(VP, 4);
    assert(!c->Error && c->Records == 3);
    Submit(c);
    ExpectAttribute(Log[0], 2, 1, 2, 3, 4);
    ExpectCall(Log[1], DG_GL_RECORD_ERROR, {GL_INVALID_VALUE});
    ExpectCall(Log[2], FEnum_glBindProgramARB, {VP, 4});
    assert(JglListMode(0));
    Clear(c);
    /* GetProgramiv names follow the target. */
    GLint value = 0x5a5a;
    glGetProgramivARB(VP, ALU_INSTRUCTIONS, &value);
    Rejected(c, GL_INVALID_ENUM);
    glGetProgramivARB(FP, ADDRESS_REGISTERS, &value);
    Rejected(c, GL_INVALID_ENUM);
    glGetProgramivARB(VP, ADDRESS_REGISTERS, &value);
    assert(value == 42 && Queries.back()[1] == VP && Queries.back()[2] == ADDRESS_REGISTERS);
    Close(c);
}

/* Generic array state, the attribute draw and the client attribute stack. */
static void Arrays(void) {
    JGL_CONTEXT *c = Open(DG_CAP_GL_VERTEX_PROGRAM);
    GLint value = 0x5a5a;
    GLfloat fv[4] = {9, 9, 9, 9};
    GLdouble dv[4] = {9, 9, 9, 9};
    void *pointer = (void *)16;
    /* Initial state: four unnormalized floats, disabled, no pointer. */
    glGetVertexAttribivARB(5, SIZE, &value);
    assert(value == 4);
    glGetVertexAttribfvARB(5, TYPE, fv);
    assert(fv[0] == GL_FLOAT && fv[1] == 9);
    glGetVertexAttribdvARB(5, ENABLED, dv);
    assert(dv[0] == 0);
    glGetVertexAttribivARB(5, NORMALIZED, &value);
    assert(!value);
    glGetVertexAttribPointervARB(5, POINTER, &pointer);
    assert(!pointer && Queries.empty() && !c->Error);
    /* Validation is local. */
    const GLfloat positions[6][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}, {.5f, .5f}, {-1, -1}};
    glVertexAttribPointerARB(16, 2, GL_FLOAT, GL_FALSE, 0, positions);
    Rejected(c, GL_INVALID_VALUE);
    for (GLint size : {0, 5}) {
        glVertexAttribPointerARB(0, size, GL_FLOAT, GL_FALSE, 0, positions);
        Rejected(c, GL_INVALID_VALUE);
    }
    glVertexAttribPointerARB(0, 2, GL_FLOAT, GL_FALSE, -4, positions);
    Rejected(c, GL_INVALID_VALUE);
    for (GLenum type : {0x1407U, 0x140bU, 0x8368U}) {
        glVertexAttribPointerARB(0, 2, type, GL_FALSE, 0, positions);
        Rejected(c, GL_INVALID_ENUM);
    }
    glEnableVertexAttribArrayARB(16);
    Rejected(c, GL_INVALID_VALUE);
    glGetVertexAttribfvARB(16, SIZE, fv);
    Rejected(c, GL_INVALID_VALUE);
    glGetVertexAttribivARB(1, 0x1234, &value);
    Rejected(c, GL_INVALID_ENUM);
    glGetVertexAttribPointervARB(1, SIZE, &pointer);
    Rejected(c, GL_INVALID_ENUM);
    c->InBegin = TRUE;
    glVertexAttribPointerARB(0, 2, GL_FLOAT, GL_FALSE, 0, positions);
    assert(c->Error == GL_INVALID_OPERATION && !c->Records);
    c->Error = 0;
    c->InBegin = FALSE;

    /* Generic 0 (FLOAT 2) without a vertex array, generic 3 UNSIGNED_BYTE 4
     * normalized, generic 7 FLOAT 3 marked normalized (ignored for floats),
     * generic 15 SHORT 1 and the unit 0 coordinates as FLOAT 2 at a stride. */
    const GLubyte colors[6][4] = {{1, 2, 3, 4},     {5, 6, 7, 8},     {9, 10, 11, 12},
                                  {13, 14, 15, 16}, {17, 18, 19, 20}, {21, 22, 23, 24}};
    const GLfloat normals[6][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1},
                                   {1, 1, 0}, {0, 1, 1}, {1, 0, 1}};
    const GLshort shorts[6] = {-1, 2, -3, 4, -5, 6};
    const struct {
        GLfloat uv[2], pad;
    } coordinates[6] = {{{.1f, .2f}, 7}, {{.3f, .4f}, 7}, {{.5f, .6f}, 7},
                        {{.7f, .8f}, 7}, {{.9f, 1}, 7},   {{0, 0}, 7}};
    glVertexAttribPointerARB(0, 2, GL_FLOAT, GL_FALSE, 0, positions);
    glVertexAttribPointerARB(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, colors);
    glVertexAttribPointerARB(7, 3, GL_FLOAT, GL_TRUE, 12, normals);
    glVertexAttribPointerARB(15, 1, GL_SHORT, GL_FALSE, 0, shorts);
    for (GLuint index : {0U, 3U, 7U, 15U})
        glEnableVertexAttribArrayARB(index);
    glTexCoordPointer(2, GL_FLOAT, sizeof(coordinates[0]), coordinates);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glGetVertexAttribivARB(7, NORMALIZED, &value);
    assert(value == 1);
    glGetVertexAttribivARB(7, STRIDE, &value);
    assert(value == 12);
    glGetVertexAttribPointervARB(3, POINTER, &pointer);
    assert(pointer == colors && !c->Error && !c->Records);
    const ULONG mask = DG_GL_ARRAY_RAW | DG_GL_ARRAY_TEXCOORD;
    const ULONG generic = 1 | 1 << 3 | 1 << 7 | 1 << 15;
    const ULONG float2 = GL_FLOAT | (2U << 16), float3 = GL_FLOAT | (3U << 16);
    const ULONG ubyte4n = GL_UNSIGNED_BYTE | (4U << 16) | DG_GL_ATTRIB_NORMALIZED;
    const ULONG short1 = GL_SHORT | (1U << 16);
    glDrawArrays(GL_TRIANGLES, 1, 3);
    Submit(c);
    assert(Log.size() == 1);
    std::vector<BYTE> uv;
    for (ULONG v : {1U, 2U, 3U})
        uv.insert(uv.end(), (const BYTE *)coordinates[v].uv, (const BYTE *)coordinates[v].uv + 8);
    ExpectDraw(Log[0], GL_TRIANGLES, 3, mask, generic,
               Payload(mask, generic, 3,
                       {{3, float2, uv},
                        {16, float2, Gather(positions, {1, 2, 3})},
                        {19, ubyte4n, Gather(colors, {1, 2, 3})},
                        {23, float3, Gather(normals, {1, 2, 3})},
                        {31, short1, Gather(shorts, {1, 2, 3})}}));
    Log.clear();
    /* Elements gather the same packed vertices. */
    const GLubyte indices[3] = {5, 0, 4};
    glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_BYTE, indices);
    Submit(c);
    uv.clear();
    for (ULONG v : {5U, 0U, 4U})
        uv.insert(uv.end(), (const BYTE *)coordinates[v].uv, (const BYTE *)coordinates[v].uv + 8);
    ExpectDraw(Log[0], GL_TRIANGLES, 3, mask, generic,
               Payload(mask, generic, 3,
                       {{3, float2, uv},
                        {16, float2, Gather(positions, {5, 0, 4})},
                        {19, ubyte4n, Gather(colors, {5, 0, 4})},
                        {23, float3, Gather(normals, {5, 0, 4})},
                        {31, short1, Gather(shorts, {5, 0, 4})}}));
    Log.clear();
    /* An array range beyond the address space fails before any record. */
    glVertexAttribPointerARB(15, 1, GL_SHORT, GL_FALSE, 0, (const void *)~(ULONG_PTR)3);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    Rejected(c, GL_INVALID_VALUE);
    glVertexAttribPointerARB(15, 1, GL_SHORT, GL_FALSE, 0, shorts);
    /* Neither the vertex array nor generic attribute 0: nothing is drawn. */
    glDisableVertexAttribArrayARB(0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    assert(!c->Error && !c->Records);
    /* The vertex array with generic attributes only above zero. */
    glVertexPointer(2, GL_FLOAT, 0, positions);
    glEnableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    for (GLuint index : {3U, 7U})
        glDisableVertexAttribArrayARB(index);
    glDrawArrays(GL_POINTS, 0, 2);
    Submit(c);
    const ULONG position = DG_GL_ARRAY_RAW | DG_GL_ARRAY_POSITION;
    ExpectDraw(
        Log[0], GL_POINTS, 2, position, 1U << 15,
        Payload(position, 1U << 15, 2,
                {{0, float2, Gather(positions, {0, 1})}, {31, short1, Gather(shorts, {0, 1})}}));
    Log.clear();
    /* No generic array: the unchanged seven-slot DrawArrays. */
    glDisableVertexAttribArrayARB(15);
    glDrawArrays(GL_POINTS, 0, 2);
    Submit(c);
    assert(Log.size() == 1 && Log[0][8] == FEnum_glDrawArrays &&
           Log[0][1] == (10 + 4) * 4 + 28 + 16);
    Admitted(Log[0]);
    Log.clear();

    /* ArrayElement: conventional attributes, generic 1-15, then generic 0
     * instead of the vertex array. */
    const GLshort pairs[6][2] = {{32767, -32768}, {0, 1}, {2, 3}, {4, 5}, {6, 7}, {8, 9}};
    glVertexAttribPointerARB(2, 2, GL_SHORT, GL_TRUE, 0, pairs);
    glEnableVertexAttribArrayARB(2);
    glEnableVertexAttribArrayARB(0);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, colors);
    glEnableClientState(GL_COLOR_ARRAY);
    glBegin(GL_POINTS);
    AliasArrayElement(1);
    AliasArrayElement(0);
    glEnd();
    Submit(c);
    assert(Log.size() == 8);
    ExpectCall(Log[0], FEnum_glBegin, {GL_POINTS});
    ExpectCall(Log[1], FEnum_glColor4f,
               {Bits(5 / 255.0f), Bits(6 / 255.0f), Bits(7 / 255.0f), Bits(8 / 255.0f)});
    ExpectAttribute(Log[2], 2, (2.0f * 0 + 1.0f) / 65535.0f, (2.0f * 1 + 1.0f) / 65535.0f, 0, 1);
    ExpectAttribute(Log[3], 0, 1, 0, 0, 1);
    ExpectAttribute(Log[5], 2, 1, -1, 0, 1);
    ExpectAttribute(Log[6], 0, 0, 0, 0, 1);
    ExpectCall(Log[7], FEnum_glEnd, {});
    Log.clear();

    /* CLIENT_VERTEX_ARRAY_BIT saves every generic array; InterleavedArrays
     * leaves them alone. */
    AliasPushClientAttrib(CLIENT_VERTEX_ARRAY_BIT);
    glVertexAttribPointerARB(2, 3, GL_INT, GL_FALSE, 8, shorts);
    glDisableVertexAttribArrayARB(2);
    AliasInterleavedArrays(0x2a20 /* V2F */, 0, positions);
    glGetVertexAttribivARB(2, SIZE, &value);
    assert(value == 3);
    AliasPopClientAttrib();
    glGetVertexAttribivARB(2, SIZE, &value);
    assert(value == 2);
    glGetVertexAttribivARB(2, ENABLED, &value);
    assert(value == 1);
    glGetVertexAttribPointervARB(2, POINTER, &pointer);
    assert(pointer == pairs);
    AliasPushClientAttrib(CLIENT_PIXEL_STORE_BIT);
    glDisableVertexAttribArrayARB(2);
    AliasPopClientAttrib();
    glGetVertexAttribivARB(2, ENABLED, &value);
    assert(!value && !c->Error && !c->Records);
    Close(c);

    /* Draws beyond one packet split at whole primitives; every piece is an
     * admitted attribute draw. */
    c = Open(DG_CAP_GL_VERTEX_PROGRAM, SMALL_PACKET);
    std::vector<GLfloat> strip(2 * 60);
    for (ULONG v = 0; v < strip.size(); ++v)
        strip[v] = (GLfloat)v;
    glVertexAttribPointerARB(0, 2, GL_FLOAT, GL_FALSE, 0, strip.data());
    glEnableVertexAttribArrayARB(0);
    glDrawArrays(GL_TRIANGLES, 0, 60);
    Submit(c);
    ULONG drawn = 0;
    assert(Log.size() > 1);
    for (const std::vector<ULONG> &r : Log) {
        assert(r[8] == FEnum_glVertexAttribPointerARB && r[12] % 3 == 0 && r[14] == 1);
        Admitted(r);
        unsigned descriptors[DG_GL_ATTRIB_SLOTS], offsets[DG_GL_ATTRIB_SLOTS], length;
        memcpy(descriptors, r.data() + 15, sizeof(descriptors));
        assert(DgRawAttribLayout(r[13], r[14], r[12], descriptors, offsets, &length));
        assert(!memcmp((const BYTE *)(r.data() + 15) + offsets[16], &strip[drawn * 2], r[12] * 8));
        drawn += r[12];
    }
    assert(drawn == 60);
    Close(c);
}

/* The current value is the host's; the rest of the state is local. */
static void Queries_(void) {
    JGL_CONTEXT *c = Open(DG_CAP_GL_VERTEX_PROGRAM);
    GLfloat fv[4];
    GLdouble dv[4];
    GLint iv[4];
    glGetVertexAttribfvARB(2, CURRENT, fv);
    assert(Queries.size() == 1 &&
           Queries[0] == std::vector<ULONG>({FEnum_glGetVertexAttribfvARB, 2, CURRENT, 0}));
    assert(fv[0] == 2 && fv[1] == 1.5f && fv[2] == -1.5f && fv[3] == 0.49f);
    glGetVertexAttribdvARB(15, CURRENT, dv);
    assert(dv[0] == 15 && dv[3] == (GLdouble)0.49f);
    glGetVertexAttribivARB(3, CURRENT, iv);
    assert(iv[0] == 3 && iv[1] == 2 && iv[2] == -2 && iv[3] == 0);
    /* Attribute 0 is the position and has no current value. */
    Queries.clear();
    Log.clear();
    glGetVertexAttribfvARB(0, CURRENT, fv);
    Rejected(c, GL_INVALID_OPERATION);
    assert(Queries.empty());
    /* The limit, the modes and the shared program state reach the host. */
    glGetIntegerv(MAX_ATTRIBS, iv);
    assert(iv[0] == 16 && Queries.back()[1] == MAX_ATTRIBS);
    assert(glIsEnabled(VP) && Queries.back()[0] == FEnum_glIsEnabled);
    GLboolean flag = 0;
    glGetBooleanv(TWO_SIDE, &flag);
    assert(flag);
    glGetIntegerv(ERROR_POSITION, iv);
    assert(!c->Error && !c->Failed);
    Close(c);
}

int main() {
    Unnegotiated();
    Negotiation();
    Records();
    Arrays();
    Queries_();
    puts("PASS vertex program frontend");
    return 0;
}
