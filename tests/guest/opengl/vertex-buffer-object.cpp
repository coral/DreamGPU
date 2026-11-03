/* SPDX-License-Identifier: GPL-2.0-or-later
 * DG_CAP_GL_VERTEX_BUFFER_OBJECT through the actual frontend on the strict
 * transport fixture: negotiation (alone, after every other capability, and the
 * unchanged frontend without it), exact BufferData, BufferSubData and
 * DeleteBuffers records, local validation (nothing the host would reject as
 * malformed may leave the frontend), the mapping shadow, the buffer draw for
 * array, element and mixed client/buffer draws, its client-draw fallbacks
 * (display list compilation, offsets, strides and sizes the record cannot
 * express), ArrayElement, buffer bindings as client state and the share
 * group's table. Every DATA record is admitted by the NT validator. */
#include <array>
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
#include "buffer.cpp"
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

constexpr GLenum ARRAY_BUFFER = 0x8892, ELEMENT_BUFFER = 0x8893;
constexpr GLenum ARRAY_BINDING = 0x8894, ELEMENT_BINDING = 0x8895, VERTEX_BINDING = 0x8896,
                 NORMAL_BINDING = 0x8897, COLOR_BINDING = 0x8898, INDEX_BINDING = 0x8899,
                 TEXCOORD_BINDING = 0x889a, EDGE_BINDING = 0x889b, SECONDARY_BINDING = 0x889c,
                 FOG_BINDING = 0x889d, WEIGHT_BINDING = 0x889e, ATTRIB_BINDING = 0x889f;
constexpr GLenum STREAM_DRAW = 0x88e0, STATIC_DRAW = 0x88e4, DYNAMIC_DRAW = 0x88e8,
                 DYNAMIC_COPY = 0x88ea;
constexpr GLenum READ_ONLY = 0x88b8, WRITE_ONLY = 0x88b9, READ_WRITE = 0x88ba;
constexpr GLenum BUFFER_SIZE = 0x8764, BUFFER_USAGE = 0x8765, BUFFER_ACCESS = 0x88bb,
                 BUFFER_MAPPED = 0x88bc, MAP_POINTER = 0x88bd;
constexpr GLenum SECONDARY_ARRAY = 0x845e, FOG_ARRAY = 0x8457, TEXTURE0 = 0x84c0;
constexpr GLenum C4UB_V3F = 0x2a23;
constexpr GLbitfield CLIENT_VERTEX_ARRAY_BIT = 2, CLIENT_PIXEL_STORE_BIT = 1;
constexpr ULONG SMALL_PACKET = 512, HOST_ANSWER = 77;
constexpr ULONG VBO = DG_CAP_GL_VERTEX_BUFFER_OBJECT;
constexpr ULONG ALL_BUT_VBO =
    DG_CAP_GL_MULTITEXTURE | DG_CAP_GL_FIXED_STATE | DG_CAP_GL_TEXTURE_CONTROL |
    DG_CAP_GL_COMPRESSED_TEXTURES | DG_CAP_GL_FOG_COORD | DG_CAP_GL_TEXTURE_CUBE_MAP |
    DG_CAP_GL_FRAGMENT_PROGRAM | DG_CAP_GL_VERTEX_PROGRAM | DG_CAP_GL_TEXTURE_3D;
constexpr ULONG DRAWS =
    VBO | DG_CAP_GL_VERTEX_PROGRAM | DG_CAP_GL_MULTITEXTURE | DG_CAP_GL_FOG_COORD;
static const char *const Entries[] = {
    "glGenBuffersARB",  "glBindBufferARB",           "glDeleteBuffersARB",    "glIsBufferARB",
    "glBufferDataARB",  "glBufferSubDataARB",        "glGetBufferSubDataARB", "glMapBufferARB",
    "glUnmapBufferARB", "glGetBufferParameterivARB", "glGetBufferPointervARB"};

/* The NT kernel admits each DATA record exactly as captured. */
static ULONG KernelWords(void *, ULONG function) {
    switch (function) {
        case FEnum_glDrawArrays:
        case FEnum_glDrawArraysEXT:
            return DG_GL_FUNCTION_INLINE_DATA | 4;
        case FEnum_glVertexAttribPointerARB:
            return DG_GL_FUNCTION_INLINE_DATA | 5;
        case FEnum_glDrawRangeElements:
            return DG_GL_FUNCTION_INLINE_DATA | 10;
        case FEnum_glBufferDataARB:
            return DG_GL_FUNCTION_INLINE_DATA | 3;
        case FEnum_glBufferSubDataARB:
            return DG_GL_FUNCTION_INLINE_DATA | 2;
        case FEnum_glDeleteBuffersARB:
            return DG_GL_FUNCTION_INLINE_DATA | 1;
        default:
            return (ULONG)-1;
    }
}
static void Admitted(const std::vector<ULONG> &r) {
    std::vector<ULONG> copy(r);
    const DG_GL_LIMITS limits = {7, DG_ESCAPE_MAX_BYTES, DG_GL_MAX_RECORDS, 1};
    assert(DgValidateUserGl(copy.data(), copy.size() * 4, 42, 7, &limits, KernelWords, NULL));
}

typedef std::vector<std::vector<ULONG>> Records;
static Records Log;
static void Capture(const ULONG *record) {
    Log.emplace_back(record, record + record[1] / 4);
    if (record[0] == DG_GL_DATA_CALL)
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
/* One DATA record: its scalar words, then exactly these payload bytes. */
static void ExpectData(const std::vector<ULONG> &r, ULONG function,
                       std::initializer_list<ULONG> words, const void *payload, ULONG bytes) {
    assert(r[0] == DG_GL_DATA_CALL && r[8] == function && r[9] == bytes);
    assert(r[1] == (10 + words.size()) * 4 + ((bytes + 3) & ~3U));
    ULONG n = 10;
    for (ULONG word : words)
        assert(r[n++] == word);
    assert(!bytes || !memcmp(r.data() + n, payload, bytes));
}
/* BufferSubData slabs of at most `slab` bytes from Log[at] on reproduce
 * these bytes of buffer `name` from `offset`; returns the next index. */
static size_t ExpectSlabs(size_t at, GLuint name, ULONG offset, const BYTE *bytes, ULONG size,
                          ULONG slab) {
    for (ULONG done = 0; done < size;) {
        const ULONG n = size - done < slab ? size - done : slab;
        ExpectData(Log.at(at++), FEnum_glBufferSubDataARB, {name, offset + done}, bytes + done, n);
        done += n;
    }
    return at;
}

/* Host answers: every state query is HOST_ANSWER. */
static std::vector<std::vector<ULONG>> Queries;
static BOOL Answer(const ULONG *r, DG_ESCAPE_REPLY *reply, ULONG) {
    Queries.push_back({r[8], r[9], r[10], r[11]});
    auto put = [&](ULONG type, const void *data, ULONG bytes) {
        memcpy(reply + 1, data, bytes);
        reply->ResultBytes = bytes;
        reply->ResultType = type;
        return TRUE;
    };
    switch (r[8]) {
        case FEnum_glGetIntegerv:
            return put(DG_GL_RESULT_INT, &HOST_ANSWER, 4);
        case FEnum_glIsEnabled: {
            const BYTE value = 0;
            return put(DG_GL_RESULT_BOOL, &value, 1);
        }
        default:
            return FALSE;
    }
}
static JGL_CONTEXT *Open(ULONG caps, ULONG packet = DG_ESCAPE_MAX_BYTES, BOOL secondary = FALSE) {
    Reset();
    HostCaps = caps;
    HostMaxBytes = packet;
    HostSecondaryWords = secondary ? 3 : 0;
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
static GLint Integer(GLenum pname) {
    GLint value = 0x5a5a;
    glGetIntegerv(pname, &value);
    return value;
}
static GLint Parameter(GLenum target, GLenum pname) {
    GLint value = 0x5a5a;
    glGetBufferParameterivARB(target, pname, &value);
    return value;
}
static void *MapPointer(GLenum target) {
    void *pointer = (void *)16;
    glGetBufferPointervARB(target, MAP_POINTER, &pointer);
    return pointer;
}
static const void *Offset(ULONG_PTR offset) {
    return (const void *)offset;
}
/* Binds name to target and defines its contents; forgets the records. */
static void Upload(JGL_CONTEXT *c, GLenum target, GLuint name, const std::vector<BYTE> &bytes) {
    glBindBufferARB(target, name);
    glBufferDataARB(target, (JGL_SIZEIPTR)bytes.size(), bytes.data(), STATIC_DRAW);
    assert(!c->Error);
    Submit(c);
    Log.clear();
}
/* The records of everything draw() submits. */
template <typename F> static Records Recorded(JGL_CONTEXT *c, F draw) {
    Log.clear();
    draw();
    assert(!c->Error);
    Submit(c);
    Records out;
    out.swap(Log);
    return out;
}
/* A fallback: exactly the client draws of the same bytes in client memory. */
static void SameClientDraws(const Records &got, const Records &expected) {
    assert(!got.empty() && got == expected);
    for (const std::vector<ULONG> &r : got)
        assert(r[0] == DG_GL_DATA_CALL &&
               (r[8] == FEnum_glDrawArrays || r[8] == FEnum_glDrawArraysEXT ||
                r[8] == FEnum_glVertexAttribPointerARB));
}

static ULONG D(GLenum type, ULONG count) {
    return type | count << 16;
}
static const GLfloat Positions[8][3] = {{0, 0, 0},  {1, 0, 0}, {0, 1, 0},    {1, 1, 0},
                                        {.5f, .5f}, {-1, -1},  {2, 2, 2.5f}, {3, -3, 3}};
static const GLubyte Colors[8][4] = {{1, 2, 3, 4},     {5, 6, 7, 8},     {9, 10, 11, 12},
                                     {13, 14, 15, 16}, {17, 18, 19, 20}, {21, 22, 23, 24},
                                     {25, 26, 27, 28}, {255, 0, 128, 64}};
static const GLshort Normals[8][3] = {{-1, 2, -3},  {4, -5, 6},      {32767, -32768, 0}, {7, 8, 9},
                                      {-7, -8, -9}, {100, 200, 300}, {-100, 0, 100},     {1, 1, 1}};
static const GLshort Shorts[8][2] = {{-1, 2}, {3, -4},  {5, 6},   {-7, 8},
                                     {9, 10}, {11, 12}, {13, 14}, {15, 16}};
template <typename T, size_t N>
static std::vector<BYTE> Range(const T (&items)[N], ULONG first, ULONG count) {
    assert(first + count <= N);
    std::vector<BYTE> out;
    for (ULONG v = first; v < first + count; ++v)
        out.insert(out.end(), (const BYTE *)&items[v], (const BYTE *)&items[v] + sizeof(T));
    return out;
}
/* Client SHORT normals travel as GL 1.1 signed-normalized floats. */
static std::vector<BYTE> NormalFloats(ULONG first, ULONG count) {
    std::vector<BYTE> out;
    for (ULONG v = first; v < first + count; ++v)
        for (ULONG k = 0; k < 3; ++k) {
            const GLfloat value = (2.0f * Normals[v][k] + 1.0f) / 65535.0f;
            out.insert(out.end(), (const BYTE *)&value, (const BYTE *)&value + 4);
        }
    return out;
}
/* Buffer 1: sixteen bytes, then the eight positions (112 bytes). */
static std::vector<BYTE> VertexBytes(void) {
    std::vector<BYTE> out(16 + sizeof(Positions), 0xee);
    memcpy(&out[16], Positions, sizeof(Positions));
    return out;
}
/* Buffer 2: the colors from offset 4 with stride 8 (64 bytes). */
static std::vector<BYTE> ColorBytes(void) {
    std::vector<BYTE> out(64, 0xee);
    for (ULONG v = 0; v < 8; ++v)
        memcpy(&out[4 + 8 * v], Colors[v], 4);
    return out;
}
/* Buffer 3: UNSIGNED_BYTE indices at 1, UNSIGNED_SHORT at 8 and
 * UNSIGNED_INT at 16 (28 bytes); 200 lies beyond the vertex buffers. */
static const GLubyte ElementBytes8[3] = {5, 0, 7};
static const GLushort ElementBytes16[3] = {1, 6, 2};
static const GLuint ElementBytes32[3] = {3, 4, 200};
static std::vector<BYTE> ElementBytes(void) {
    std::vector<BYTE> out(28, 0xee);
    memcpy(&out[1], ElementBytes8, 3);
    memcpy(&out[8], ElementBytes16, 6);
    memcpy(&out[16], ElementBytes32, 12);
    return out;
}
/* Positions from buffer 1 at 16 and colors from buffer 2 at 4 (stride 8),
 * both enabled; buffer 3 is the element buffer; ARRAY_BUFFER is zero. */
static void BufferArrays(JGL_CONTEXT *c) {
    Upload(c, ARRAY_BUFFER, 1, VertexBytes());
    glVertexPointer(3, GL_FLOAT, 0, Offset(16));
    Upload(c, ARRAY_BUFFER, 2, ColorBytes());
    glColorPointer(4, GL_UNSIGNED_BYTE, 8, Offset(4));
    Upload(c, ELEMENT_BUFFER, 3, ElementBytes());
    glBindBufferARB(ARRAY_BUFFER, 0);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    Quiet(c);
}

/* The buffer draw's expected payload: descriptors, references in slot order,
 * client sections and inline indices at the shared layout's offsets. */
typedef std::array<ULONG, 10> Words;
struct Reference {
    ULONG Slot, Descriptor, Buffer, Offset, Stride;
};
struct Section {
    ULONG Slot, Descriptor;
    std::vector<BYTE> Bytes;
};
static std::vector<BYTE> BufferPayload(const Words &a, const std::vector<Reference> &refs,
                                       const std::vector<Section> &sections,
                                       const std::vector<BYTE> &indices = {}) {
    unsigned words[10], descriptors[DG_GL_ATTRIB_SLOTS] = {},
                        references[3 * DG_GL_ATTRIB_SLOTS] = {}, offsets[DG_GL_ATTRIB_SLOTS],
                        index_at = 0, length = 0, n = 0;
    for (ULONG i = 0; i < 10; ++i)
        words[i] = a[i];
    for (const Reference &r : refs) {
        assert(!n || r.Slot > refs[n / 3 - 1].Slot);
        descriptors[r.Slot] = r.Descriptor;
        references[n++] = r.Buffer;
        references[n++] = r.Offset;
        references[n++] = r.Stride;
    }
    for (const Section &s : sections)
        descriptors[s.Slot] = s.Descriptor;
    assert(DgRawBufferLayout(words, descriptors, references, offsets, &index_at, &length));
    std::vector<BYTE> out(length, 0);
    memcpy(out.data(), descriptors, sizeof(descriptors));
    memcpy(out.data() + sizeof(descriptors), references, n * 4);
    for (const Section &s : sections) {
        assert(offsets[s.Slot] &&
               s.Bytes.size() == a[6] * ((s.Descriptor >> 16) & 255) *
                                     DgArrayComponentBytes(s.Descriptor & 65535));
        memcpy(&out[offsets[s.Slot]], s.Bytes.data(), s.Bytes.size());
    }
    assert(!index_at == indices.empty());
    if (!indices.empty()) {
        assert(index_at + indices.size() <= length);
        memcpy(&out[index_at], indices.data(), indices.size());
    }
    return out;
}
static void ExpectBufferDraw(const std::vector<ULONG> &r, const Words &a,
                             const std::vector<BYTE> &payload) {
    assert(r[0] == DG_GL_DATA_CALL && r[8] == FEnum_glDrawRangeElements);
    assert(r[9] == payload.size() && r[1] == 20 * 4 + payload.size());
    for (ULONG i = 0; i < 10; ++i)
        assert(r[10 + i] == a[i]);
    assert(!memcmp(r.data() + 20, payload.data(), payload.size()));
}
template <typename T> static std::vector<BYTE> Bytes(const T &value) {
    return std::vector<BYTE>((const BYTE *)&value, (const BYTE *)&value + sizeof(value));
}

/* Client arrays of every draw form, without buffers: the seven-slot DrawArrays,
 * elements, DrawArraysEXT and the attribute draw. */
static Records ClientDraws(JGL_CONTEXT *c) {
    const GLubyte indices[4] = {7, 0, 3, 3};
    glVertexPointer(3, GL_FLOAT, 0, Positions);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, Colors);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    Records records = Recorded(c, [&] {
        glDrawArrays(GL_TRIANGLES, 2, 3);
        glDrawElements(GL_LINES, 4, GL_UNSIGNED_BYTE, indices);
        glClientActiveTextureARB(TEXTURE0 + 1);
        glTexCoordPointer(2, GL_FLOAT, 12, Positions);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glDrawArrays(GL_POINTS, 0, 4);
        glVertexAttribPointerARB(5, 2, GL_SHORT, GL_TRUE, 0, Shorts);
        glEnableVertexAttribArrayARB(5);
        glDrawElements(GL_POINTS, 4, GL_UNSIGNED_BYTE, indices);
    });
    assert(records.size() == 4 && records[0][8] == FEnum_glDrawArrays &&
           records[1][8] == FEnum_glDrawArrays && records[2][8] == FEnum_glDrawArraysEXT &&
           records[3][8] == FEnum_glVertexAttribPointerARB);
    /* The seven-slot record: descriptors, then each tight section. */
    std::vector<BYTE> payload(76, 0);
    const ULONG descriptors[7] = {D(GL_FLOAT, 3), D(GL_UNSIGNED_BYTE, 4)};
    memcpy(payload.data(), descriptors, 28);
    memcpy(&payload[28], Positions[2], 36);
    memcpy(&payload[64], Colors[2], 12);
    ExpectData(records[0], FEnum_glDrawArrays,
               {GL_TRIANGLES, 0, 3, DG_GL_ARRAY_RAW | DG_GL_ARRAY_POSITION | DG_GL_ARRAY_COLOR},
               payload.data(), 76);
    glDisableVertexAttribArrayARB(5);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glClientActiveTextureARB(TEXTURE0);
    Quiet(c);
    return records;
}

/* Without the capability nothing new exists: every entry point fails locally,
 * binding queries stay host queries (or the generic one GL_INVALID_ENUM) and
 * client draws are unchanged. */
static void Unnegotiated(void) {
    JGL_CONTEXT *c = Open(ALL_BUT_VBO, DG_ESCAPE_MAX_BYTES, TRUE);
    assert(!JglVertexBufferObjectAvailable() && JglVertexProgramAvailable());
    assert(!strstr((const char *)glGetString(GL_EXTENSIONS), "buffer_object"));
    for (const char *name : Entries)
        assert(!wglGetProcAddress(name));
    assert(wglGetProcAddress("glVertexAttribPointerARB") == Marker);
    GLuint names[2] = {7, 7};
    BYTE bytes[8] = {1, 2, 3, 4, 5, 6, 7, 8}, out[8];
    memset(out, 0xcc, sizeof(out));
    glGenBuffersARB(2, names);
    Rejected(c, GL_INVALID_OPERATION);
    assert(names[0] == 7 && names[1] == 7);
    for (GLuint name : {0U, 1U}) {
        glBindBufferARB(ARRAY_BUFFER, name);
        Rejected(c, GL_INVALID_OPERATION);
    }
    glBindBufferARB(0x1234, 1); /* the missing capability comes first */
    Rejected(c, GL_INVALID_OPERATION);
    glDeleteBuffersARB(1, names);
    Rejected(c, GL_INVALID_OPERATION);
    assert(!glIsBufferARB(1));
    Rejected(c, GL_INVALID_OPERATION);
    glBufferDataARB(ARRAY_BUFFER, 8, bytes, STATIC_DRAW);
    Rejected(c, GL_INVALID_OPERATION);
    glBufferSubDataARB(ARRAY_BUFFER, 0, 8, bytes);
    Rejected(c, GL_INVALID_OPERATION);
    glGetBufferSubDataARB(ARRAY_BUFFER, 0, 8, out);
    Rejected(c, GL_INVALID_OPERATION);
    assert(out[0] == 0xcc && out[7] == 0xcc);
    assert(!glMapBufferARB(ARRAY_BUFFER, WRITE_ONLY));
    Rejected(c, GL_INVALID_OPERATION);
    assert(!glUnmapBufferARB(ARRAY_BUFFER));
    Rejected(c, GL_INVALID_OPERATION);
    assert(Parameter(ARRAY_BUFFER, BUFFER_SIZE) == 0x5a5a);
    Rejected(c, GL_INVALID_OPERATION);
    assert(MapPointer(ARRAY_BUFFER) == (void *)16);
    Rejected(c, GL_INVALID_OPERATION);
    /* Binding queries reach the host as before; the generic one is unknown. */
    for (GLenum pname = ARRAY_BINDING; pname <= ATTRIB_BINDING; ++pname) {
        Queries.clear();
        assert(Integer(pname) == (GLint)HOST_ANSWER && Queries.size() == 1 &&
               Queries[0][0] == FEnum_glGetIntegerv && Queries[0][1] == pname);
    }
    Clear(c);
    GLint value = 0x5a5a;
    glGetVertexAttribivARB(1, ATTRIB_BINDING, &value);
    Rejected(c, GL_INVALID_ENUM);
    assert(value == 0x5a5a);
    /* Client draws are byte-identical with the capability negotiated. */
    const Records without = ClientDraws(c);
    Close(c);
    c = Open(ALL_BUT_VBO | VBO, DG_ESCAPE_MAX_BYTES, TRUE);
    assert(ClientDraws(c) == without);
    Close(c);
}

/* Negotiated: the extension is appended last, alone or after all others. */
static void Negotiation(void) {
    JGL_CONTEXT *c = Open(VBO);
    assert(JglVertexBufferObjectAvailable() && !JglVertexProgramAvailable());
    assert(!strcmp((const char *)glGetString(GL_EXTENSIONS), "GL_ARB_vertex_buffer_object"));
    for (const char *name : Entries)
        assert(wglGetProcAddress(name) == Marker);
    assert(!wglGetProcAddress("glVertexAttribPointerARB"));
    /* The generic binding needs ARB_vertex_program as well. */
    GLint value = 0x5a5a;
    glGetVertexAttribivARB(1, ATTRIB_BINDING, &value);
    Rejected(c, GL_INVALID_OPERATION);
    Close(c);
    Reset();
    HostCaps = ALL_BUT_VBO | VBO;
    HostSecondaryWords = 3;
    HGLRC context = Create();
    const char *all = (const char *)glGetString(GL_EXTENSIONS);
    const char *tail = "GL_EXT_texture3D GL_ARB_vertex_buffer_object";
    assert(strlen(all) > strlen(tail) && !strcmp(all + strlen(all) - strlen(tail), tail));
    for (const char *name : Entries)
        assert(wglGetProcAddress(name) == Marker);
    assert(wglMakeCurrent(NULL, NULL) && wglDeleteContext(context));
}

/* Exact records; nothing for names, bindings or queries. */
static void Records_(void) {
    JGL_CONTEXT *c = Open(VBO);
    GLuint names[4] = {}, name = 0;
    glGenBuffersARB(4, names);
    assert(names[0] == 1 && names[1] == 2 && names[2] == 3 && names[3] == 4);
    assert(!glIsBufferARB(1) && !glIsBufferARB(0)); /* generated, not yet objects */
    glBindBufferARB(ARRAY_BUFFER, 1);
    glBindBufferARB(ELEMENT_BUFFER, 9); /* a first bind creates any name */
    assert(glIsBufferARB(1) && glIsBufferARB(9) && !glIsBufferARB(2) && !glIsBufferARB(10));
    assert(Integer(ARRAY_BINDING) == 1 && Integer(ELEMENT_BINDING) == 9);
    assert(Parameter(ARRAY_BUFFER, BUFFER_SIZE) == 0 &&
           Parameter(ARRAY_BUFFER, BUFFER_USAGE) == (GLint)STATIC_DRAW &&
           Parameter(ARRAY_BUFFER, BUFFER_ACCESS) == (GLint)READ_WRITE &&
           Parameter(ARRAY_BUFFER, BUFFER_MAPPED) == 0 && !MapPointer(ARRAY_BUFFER));
    glGenBuffersARB(1, &name);
    assert(name == 5);
    glGenBuffersARB(0, &name);
    glGenBuffersARB(3, NULL);
    glDeleteBuffersARB(1, NULL);
    Quiet(c);
    assert(Queries.empty());

    /* Contents that fit travel in the BufferData record. */
    std::vector<BYTE> bytes(100);
    for (ULONG i = 0; i < bytes.size(); ++i)
        bytes[i] = (BYTE)(i * 13 + 5);
    glBufferDataARB(ARRAY_BUFFER, 100, bytes.data(), STREAM_DRAW);
    Submit(c);
    assert(Log.size() == 1);
    ExpectData(Log[0], FEnum_glBufferDataARB, {1, 100, STREAM_DRAW}, bytes.data(), 100);
    Log.clear();
    assert(Parameter(ARRAY_BUFFER, BUFFER_SIZE) == 100 &&
           Parameter(ARRAY_BUFFER, BUFFER_USAGE) == (GLint)STREAM_DRAW);
    std::vector<BYTE> out(100, 0xcc);
    glGetBufferSubDataARB(ARRAY_BUFFER, 0, 100, out.data());
    assert(out == bytes);
    /* NULL data defines zeroed storage: no payload. */
    glBufferDataARB(ELEMENT_BUFFER, 37, NULL, DYNAMIC_COPY);
    glBufferSubDataARB(ELEMENT_BUFFER, 30, 7, bytes.data()); /* padded to a word */
    Submit(c);
    assert(Log.size() == 2);
    ExpectData(Log[0], FEnum_glBufferDataARB, {9, 37, DYNAMIC_COPY}, NULL, 0);
    ExpectData(Log[1], FEnum_glBufferSubDataARB, {9, 30}, bytes.data(), 7);
    Log.clear();
    glGetBufferSubDataARB(ELEMENT_BUFFER, 28, 9, out.data());
    assert(!out[0] && !out[1] && !memcmp(&out[2], bytes.data(), 7));
    /* Empty updates send nothing. */
    glBufferSubDataARB(ARRAY_BUFFER, 100, 0, bytes.data());
    glBufferSubDataARB(ARRAY_BUFFER, 0, 4, NULL);
    glGetBufferSubDataARB(ARRAY_BUFFER, 100, 0, out.data());
    Quiet(c);
    glBufferDataARB(ARRAY_BUFFER, 0, bytes.data(), STATIC_DRAW);
    Submit(c);
    assert(Log.size() == 1);
    ExpectData(Log[0], FEnum_glBufferDataARB, {1, 0, STATIC_DRAW}, NULL, 0);
    Close(c);

    /* Beyond one packet BufferData defines zeroed storage and BufferSubData
     * slabs follow; BufferSubData splits the same way. */
    c = Open(VBO, SMALL_PACKET);
    const ULONG fits = JglMaxDataBytes(3), slab = JglMaxDataBytes(2) & ~3U;
    assert(fits == 460 && slab == 464);
    std::vector<BYTE> big(3 * slab + 5);
    for (ULONG i = 0; i < big.size(); ++i)
        big[i] = (BYTE)(i * 7 + i / 251);
    glBindBufferARB(ARRAY_BUFFER, 1);
    glBufferDataARB(ARRAY_BUFFER, fits, big.data(), STATIC_DRAW);
    Submit(c);
    assert(Log.size() == 1);
    ExpectData(Log[0], FEnum_glBufferDataARB, {1, fits, STATIC_DRAW}, big.data(), fits);
    Log.clear();
    glBufferDataARB(ARRAY_BUFFER, fits + 1, big.data(), STATIC_DRAW);
    Submit(c);
    assert(Log.size() == 2);
    ExpectData(Log[0], FEnum_glBufferDataARB, {1, fits + 1, STATIC_DRAW}, NULL, 0);
    assert(ExpectSlabs(1, 1, 0, big.data(), fits + 1, slab) == 2);
    Log.clear();
    glBufferDataARB(ARRAY_BUFFER, (JGL_SIZEIPTR)big.size(), big.data(), DYNAMIC_DRAW);
    Submit(c);
    assert(Log.size() == 5);
    ExpectData(Log[0], FEnum_glBufferDataARB, {1, (ULONG)big.size(), DYNAMIC_DRAW}, NULL, 0);
    assert(ExpectSlabs(1, 1, 0, big.data(), (ULONG)big.size(), slab) == 5);
    Log.clear();
    std::vector<BYTE> patch(2 * slab + 1);
    for (ULONG i = 0; i < patch.size(); ++i)
        patch[i] = (BYTE)~i;
    glBufferSubDataARB(ARRAY_BUFFER, 3, (JGL_SIZEIPTR)patch.size(), patch.data());
    Submit(c);
    assert(Log.size() == 3 && ExpectSlabs(0, 1, 3, patch.data(), (ULONG)patch.size(), slab) == 3);
    Log.clear();
    memcpy(&big[3], patch.data(), patch.size());
    out.assign(big.size(), 0xcc);
    glGetBufferSubDataARB(ARRAY_BUFFER, 0, (JGL_SIZEIPTR)out.size(), out.data());
    assert(out == big);
    glBufferDataARB(ARRAY_BUFFER, 5000, NULL, STATIC_DRAW);
    Submit(c);
    assert(Log.size() == 1);
    ExpectData(Log[0], FEnum_glBufferDataARB, {1, 5000, STATIC_DRAW}, NULL, 0);
    Close(c);

    /* DeleteBuffers names only objects that existed, in batches of a packet;
     * deleted, zero, unknown and generated-only names are forgotten locally. */
    for (ULONG packet : {SMALL_PACKET, (ULONG)DG_ESCAPE_MAX_BYTES}) {
        c = Open(VBO, packet);
        const ULONG batch = packet == SMALL_PACKET ? 117 : 256;
        assert(packet != SMALL_PACKET || JglMaxDataBytes(1) / 4 == batch);
        std::vector<GLuint> all(300);
        glGenBuffersARB(300, all.data());
        for (ULONG i = 0; i < 250; ++i)
            glBindBufferARB(ARRAY_BUFFER, all[i]);
        glBindBufferARB(ARRAY_BUFFER, 0);
        Quiet(c);
        std::vector<GLuint> doomed(all);
        doomed.insert(doomed.begin() + 10, {0U, 5000U, all[3]});
        glDeleteBuffersARB((GLsizei)doomed.size(), doomed.data());
        Submit(c);
        std::vector<GLuint> deleted;
        for (const std::vector<ULONG> &r : Log) {
            assert(r[0] == DG_GL_DATA_CALL && r[8] == FEnum_glDeleteBuffersARB);
            assert(r[10] && r[10] <= batch && r[9] == r[10] * 4);
            assert(&r == &Log.back() || r[10] == batch);
            deleted.insert(deleted.end(), r.begin() + 11, r.begin() + 11 + r[10]);
        }
        assert(Log.size() == (250 + batch - 1) / batch);
        assert(deleted == std::vector<GLuint>(all.begin(), all.begin() + 250));
        for (GLuint n : all)
            assert(!glIsBufferARB(n));
        glGenBuffersARB(1, &name);
        assert(name == 301);
        Close(c);
    }
}

/* Local validation: no record the host would reject leaves the frontend. */
static void Validation(void) {
    JGL_CONTEXT *c = Open(VBO);
    BYTE bytes[16], other[16], out[16];
    for (ULONG i = 0; i < 16; ++i) {
        bytes[i] = (BYTE)(i * 7 + 1);
        other[i] = (BYTE)(i * 3 + 2);
    }
    memset(out, 0xcc, sizeof(out));
    void *pointer = NULL;
    /* Unknown targets. */
    for (GLenum target : {0U, (GLenum)GL_TEXTURE_2D, 0x8891U, ARRAY_BINDING}) {
        glBindBufferARB(target, 1);
        Rejected(c, GL_INVALID_ENUM);
        glBufferDataARB(target, 4, bytes, STATIC_DRAW);
        Rejected(c, GL_INVALID_ENUM);
        glBufferSubDataARB(target, 0, 4, bytes);
        Rejected(c, GL_INVALID_ENUM);
        glGetBufferSubDataARB(target, 0, 4, out);
        Rejected(c, GL_INVALID_ENUM);
        assert(!glMapBufferARB(target, READ_ONLY));
        Rejected(c, GL_INVALID_ENUM);
        assert(!glUnmapBufferARB(target));
        Rejected(c, GL_INVALID_ENUM);
        assert(Parameter(target, BUFFER_SIZE) == 0x5a5a);
        Rejected(c, GL_INVALID_ENUM);
        assert(MapPointer(target) == (void *)16);
        Rejected(c, GL_INVALID_ENUM);
    }
    assert(!glIsBufferARB(1) && out[0] == 0xcc);
    /* Negative counts. */
    GLuint name = 9;
    glGenBuffersARB(-1, &name);
    Rejected(c, GL_INVALID_VALUE);
    glDeleteBuffersARB(-1, &name);
    Rejected(c, GL_INVALID_VALUE);
    assert(name == 9);
    /* The zero binding names no object. */
    for (GLenum target : {ARRAY_BUFFER, ELEMENT_BUFFER}) {
        glBufferDataARB(target, 4, bytes, STATIC_DRAW);
        Rejected(c, GL_INVALID_OPERATION);
        glBufferSubDataARB(target, 0, 4, bytes);
        Rejected(c, GL_INVALID_OPERATION);
        glGetBufferSubDataARB(target, 0, 4, out);
        Rejected(c, GL_INVALID_OPERATION);
        assert(!glMapBufferARB(target, READ_ONLY));
        Rejected(c, GL_INVALID_OPERATION);
        assert(!glUnmapBufferARB(target));
        Rejected(c, GL_INVALID_OPERATION);
        assert(Parameter(target, BUFFER_SIZE) == 0x5a5a);
        Rejected(c, GL_INVALID_OPERATION);
        assert(MapPointer(target) == (void *)16);
        Rejected(c, GL_INVALID_OPERATION);
    }
    glBindBufferARB(ARRAY_BUFFER, 1);
    glBufferDataARB(ARRAY_BUFFER, 16, bytes, STATIC_DRAW);
    Submit(c);
    Log.clear();
    /* Negative sizes and offsets, ranges beyond the store. */
    glBufferDataARB(ARRAY_BUFFER, -1, bytes, STATIC_DRAW);
    Rejected(c, GL_INVALID_VALUE);
    const JGL_SIZEIPTR huge = (JGL_SIZEIPTR)(~(size_t)0 >> 1);
    const JGL_SIZEIPTR ranges[][2] = {{-1, 4}, {0, -1}, {13, 4}, {17, 0}, {0, 17}, {huge, huge}};
    for (const auto &range : ranges) {
        glBufferSubDataARB(ARRAY_BUFFER, range[0], range[1], bytes);
        Rejected(c, GL_INVALID_VALUE);
        glGetBufferSubDataARB(ARRAY_BUFFER, range[0], range[1], out);
        Rejected(c, GL_INVALID_VALUE);
    }
    assert(out[0] == 0xcc);
    glBufferSubDataARB(ARRAY_BUFFER, 16, 0, bytes); /* empty, at the end */
    Quiet(c);
    /* Usage hints: the nine, and nothing else. */
    for (GLenum usage : {0U, 0x88e3U, 0x88e7U, 0x88ebU, 0x88dfU, (GLenum)GL_FLOAT}) {
        glBufferDataARB(ARRAY_BUFFER, 4, other, usage);
        Rejected(c, GL_INVALID_ENUM);
    }
    assert(Parameter(ARRAY_BUFFER, BUFFER_SIZE) == 16 &&
           Parameter(ARRAY_BUFFER, BUFFER_USAGE) == (GLint)STATIC_DRAW);
    for (GLenum usage = 0x88e0; usage <= 0x88ea; ++usage) {
        if ((usage & 3) == 3)
            continue;
        glBufferDataARB(ARRAY_BUFFER, 16, bytes, usage);
        assert(!c->Error && Parameter(ARRAY_BUFFER, BUFFER_USAGE) == (GLint)usage);
    }
    Submit(c);
    assert(Log.size() == 9);
    Log.clear();
    /* Map access and query names. */
    for (GLenum access : {0U, 0x88b7U, BUFFER_ACCESS, (GLenum)GL_FLOAT}) {
        assert(!glMapBufferARB(ARRAY_BUFFER, access));
        Rejected(c, GL_INVALID_ENUM);
    }
    assert(!Parameter(ARRAY_BUFFER, BUFFER_MAPPED));
    for (GLenum pname : {MAP_POINTER, 0x8766U, 0U}) {
        assert(Parameter(ARRAY_BUFFER, pname) == 0x5a5a);
        Rejected(c, GL_INVALID_ENUM);
    }
    pointer = (void *)16;
    glGetBufferPointervARB(ARRAY_BUFFER, BUFFER_SIZE, &pointer);
    Rejected(c, GL_INVALID_ENUM);
    assert(pointer == (void *)16);

    /* Storage beyond DG_GL_MAX_BUFFER_BYTES is GL_OUT_OF_MEMORY and leaves
     * the buffer empty, on the host as well: an empty BufferData. */
    glBufferDataARB(ARRAY_BUFFER, DG_GL_MAX_BUFFER_BYTES + 1, bytes, DYNAMIC_DRAW);
    assert(c->Error == GL_OUT_OF_MEMORY);
    c->Error = 0;
    Submit(c);
    assert(Log.size() == 1);
    ExpectData(Log[0], FEnum_glBufferDataARB, {1, 0, DYNAMIC_DRAW}, NULL, 0);
    Log.clear();
    assert(Parameter(ARRAY_BUFFER, BUFFER_SIZE) == 0 &&
           Parameter(ARRAY_BUFFER, BUFFER_USAGE) == (GLint)DYNAMIC_DRAW);
    /* So does a failed allocation; a same-size replacement needs none. */
    glBufferDataARB(ARRAY_BUFFER, 16, bytes, STATIC_DRAW);
    FailHeap = TRUE;
    glBufferDataARB(ARRAY_BUFFER, 16, other, STATIC_DRAW);
    assert(!c->Error);
    glBufferDataARB(ARRAY_BUFFER, 8, bytes, STREAM_DRAW);
    assert(c->Error == GL_OUT_OF_MEMORY);
    c->Error = 0;
    glBindBufferARB(ARRAY_BUFFER, 2); /* no memory for a new object */
    assert(c->Error == GL_OUT_OF_MEMORY && Integer(ARRAY_BINDING) == 1 && !glIsBufferARB(2));
    c->Error = 0;
    FailHeap = FALSE;
    Submit(c);
    assert(Log.size() == 3);
    ExpectData(Log[0], FEnum_glBufferDataARB, {1, 16, STATIC_DRAW}, bytes, 16);
    ExpectData(Log[1], FEnum_glBufferDataARB, {1, 16, STATIC_DRAW}, other, 16);
    ExpectData(Log[2], FEnum_glBufferDataARB, {1, 0, STREAM_DRAW}, NULL, 0);
    Log.clear();
    assert(!Parameter(ARRAY_BUFFER, BUFFER_SIZE));
    /* The largest buffer, and its last bytes. */
    glBufferDataARB(ARRAY_BUFFER, DG_GL_MAX_BUFFER_BYTES, NULL, STREAM_DRAW);
    glBufferSubDataARB(ARRAY_BUFFER, DG_GL_MAX_BUFFER_BYTES - 4, 4, bytes);
    glGetBufferSubDataARB(ARRAY_BUFFER, DG_GL_MAX_BUFFER_BYTES - 6, 6, out);
    assert(!c->Error && !out[0] && !out[1] && !memcmp(&out[2], bytes, 4));
    assert(Parameter(ARRAY_BUFFER, BUFFER_SIZE) == DG_GL_MAX_BUFFER_BYTES);
    glBufferDataARB(ARRAY_BUFFER, 0, NULL, STREAM_DRAW);
    Submit(c);
    assert(Log.size() == 3);
    ExpectData(Log[0], FEnum_glBufferDataARB, {1, DG_GL_MAX_BUFFER_BYTES, STREAM_DRAW}, NULL, 0);
    ExpectData(Log[1], FEnum_glBufferSubDataARB, {1, DG_GL_MAX_BUFFER_BYTES - 4}, bytes, 4);
    Log.clear();

    /* Inside Begin/End every entry point and binding query is an error. */
    glBufferDataARB(ARRAY_BUFFER, 16, bytes, STATIC_DRAW);
    Submit(c);
    Log.clear();
    c->InBegin = TRUE;
    auto inside = [&](auto call) {
        call();
        assert(c->Error == GL_INVALID_OPERATION && !c->Records && !c->Used);
        c->Error = 0;
    };
    inside([&] { glGenBuffersARB(1, &name); });
    inside([&] { glBindBufferARB(ARRAY_BUFFER, 3); });
    inside([&] { glDeleteBuffersARB(1, &name); });
    inside([&] { assert(!glIsBufferARB(1)); });
    inside([&] { glBufferDataARB(ARRAY_BUFFER, 4, bytes, STATIC_DRAW); });
    inside([&] { glBufferSubDataARB(ARRAY_BUFFER, 0, 4, bytes); });
    inside([&] { glGetBufferSubDataARB(ARRAY_BUFFER, 0, 4, out); });
    inside([&] { assert(!glMapBufferARB(ARRAY_BUFFER, READ_ONLY)); });
    inside([&] { assert(!glUnmapBufferARB(ARRAY_BUFFER)); });
    inside([&] { assert(Parameter(ARRAY_BUFFER, BUFFER_SIZE) == 0x5a5a); });
    inside([&] { assert(MapPointer(ARRAY_BUFFER) == (void *)16); });
    inside([&] { assert(Integer(ARRAY_BINDING) == 0x5a5a); });
    c->InBegin = FALSE;
    assert(name == 9 && Integer(ARRAY_BINDING) == 1 && !Parameter(ARRAY_BUFFER, BUFFER_MAPPED));
    /* Buffer commands are not compiled: errors are immediate, data executes. */
    assert(JglListMode(GL_COMPILE));
    Clear(c);
    glBufferDataARB(ARRAY_BUFFER, 4, bytes, 0);
    Rejected(c, GL_INVALID_ENUM);
    glBufferSubDataARB(ARRAY_BUFFER, 0, 4, other);
    Submit(c);
    assert(Log.size() == 1);
    ExpectData(Log[0], FEnum_glBufferSubDataARB, {1, 0}, other, 4);
    assert(JglListMode(0));
    Close(c);

    /* The share group holds at most DG_GL_MAX_BUFFERS names. */
    c = Open(VBO);
    std::vector<GLuint> all(DG_GL_MAX_BUFFERS);
    glGenBuffersARB(DG_GL_MAX_BUFFERS, all.data());
    for (ULONG i = 0; i < all.size(); ++i)
        assert(all[i] == i + 1);
    GLuint extra[2] = {5, 5};
    glGenBuffersARB(1, extra);
    Rejected(c, GL_OUT_OF_MEMORY);
    glBindBufferARB(ARRAY_BUFFER, DG_GL_MAX_BUFFERS + 1);
    Rejected(c, GL_OUT_OF_MEMORY);
    assert(!Integer(ARRAY_BINDING) && !glIsBufferARB(DG_GL_MAX_BUFFERS + 1));
    glBindBufferARB(ARRAY_BUFFER, all[7]); /* a generated name has room */
    assert(Integer(ARRAY_BINDING) == (GLint)all[7] && glIsBufferARB(all[7]));
    glDeleteBuffersARB(1, &all[0]); /* never an object: nothing to send */
    Quiet(c);
    glGenBuffersARB(2, extra); /* all or nothing */
    Rejected(c, GL_OUT_OF_MEMORY);
    assert(extra[0] == 5 && extra[1] == 5);
    glGenBuffersARB(1, extra);
    assert(extra[0] == DG_GL_MAX_BUFFERS + 1);
    all.push_back(extra[0]);
    glDeleteBuffersARB((GLsizei)all.size(), all.data());
    Submit(c);
    assert(Log.size() == 1);
    ExpectData(Log[0], FEnum_glDeleteBuffersARB, {1}, &all[7], 4);
    assert(!Integer(ARRAY_BINDING));
    Close(c);
}

/* MapBuffer returns the frontend copy; UnmapBuffer sends it all back unless
 * the map was READ_ONLY. */
static void Mapping(void) {
    JGL_CONTEXT *c = Open(VBO, SMALL_PACKET);
    const ULONG slab = JglMaxDataBytes(2) & ~3U;
    std::vector<BYTE> bytes(1000);
    for (ULONG i = 0; i < bytes.size(); ++i)
        bytes[i] = (BYTE)(i * 11 + 3);
    BYTE out[4];
    glBindBufferARB(ARRAY_BUFFER, 1);
    glBufferDataARB(ARRAY_BUFFER, 1000, bytes.data(), DYNAMIC_DRAW);
    Submit(c);
    Log.clear();
    BYTE *p = (BYTE *)glMapBufferARB(ARRAY_BUFFER, WRITE_ONLY);
    assert(p && !((ULONG_PTR)p % 16) && !memcmp(p, bytes.data(), 1000));
    Quiet(c);
    assert(Parameter(ARRAY_BUFFER, BUFFER_MAPPED) == 1 &&
           Parameter(ARRAY_BUFFER, BUFFER_ACCESS) == (GLint)WRITE_ONLY &&
           Parameter(ARRAY_BUFFER, BUFFER_SIZE) == 1000 &&
           Parameter(ARRAY_BUFFER, BUFFER_USAGE) == (GLint)DYNAMIC_DRAW &&
           MapPointer(ARRAY_BUFFER) == p);
    /* One map at a time; the mapped store is out of reach of the API. */
    assert(!glMapBufferARB(ARRAY_BUFFER, READ_ONLY));
    Rejected(c, GL_INVALID_OPERATION);
    assert(Parameter(ARRAY_BUFFER, BUFFER_ACCESS) == (GLint)WRITE_ONLY);
    glGetBufferSubDataARB(ARRAY_BUFFER, 0, 4, out);
    Rejected(c, GL_INVALID_OPERATION);
    glBufferSubDataARB(ARRAY_BUFFER, 0, 4, bytes.data());
    Rejected(c, GL_INVALID_OPERATION);
    /* A written map sends the whole store, exactly. */
    p[0] ^= 0xff;
    p[999] = 0x42;
    bytes[0] ^= 0xff;
    bytes[999] = 0x42;
    assert(glUnmapBufferARB(ARRAY_BUFFER) == 1 && !c->Error);
    Submit(c);
    assert(Log.size() == 3 && ExpectSlabs(0, 1, 0, bytes.data(), 1000, slab) == 3);
    Log.clear();
    assert(!Parameter(ARRAY_BUFFER, BUFFER_MAPPED) && !MapPointer(ARRAY_BUFFER) &&
           Parameter(ARRAY_BUFFER, BUFFER_ACCESS) == (GLint)WRITE_ONLY);
    assert(!glUnmapBufferARB(ARRAY_BUFFER));
    Rejected(c, GL_INVALID_OPERATION);
    /* A READ_ONLY map sends nothing back. */
    p = (BYTE *)glMapBufferARB(ARRAY_BUFFER, READ_ONLY);
    assert(p && !memcmp(p, bytes.data(), 1000) &&
           Parameter(ARRAY_BUFFER, BUFFER_ACCESS) == (GLint)READ_ONLY);
    assert(glUnmapBufferARB(ARRAY_BUFFER) == 1);
    Quiet(c);
    /* READ_WRITE sends it, through either target of the object. */
    glBindBufferARB(ELEMENT_BUFFER, 1);
    p = (BYTE *)glMapBufferARB(ELEMENT_BUFFER, READ_WRITE);
    assert(p && Parameter(ARRAY_BUFFER, BUFFER_MAPPED) == 1 && MapPointer(ARRAY_BUFFER) == p);
    p[500] = 7;
    bytes[500] = 7;
    assert(glUnmapBufferARB(ARRAY_BUFFER) == 1);
    Submit(c);
    assert(Log.size() == 3 && ExpectSlabs(0, 1, 0, bytes.data(), 1000, slab) == 3);
    Log.clear();
    glBindBufferARB(ELEMENT_BUFFER, 0);
    /* BufferData replaces a mapped store and unmaps it without an error. */
    assert(glMapBufferARB(ARRAY_BUFFER, WRITE_ONLY));
    const BYTE small[3] = {9, 8, 7};
    glBufferDataARB(ARRAY_BUFFER, 3, small, STREAM_DRAW);
    assert(!c->Error && !Parameter(ARRAY_BUFFER, BUFFER_MAPPED) && !MapPointer(ARRAY_BUFFER) &&
           Parameter(ARRAY_BUFFER, BUFFER_ACCESS) == (GLint)READ_WRITE &&
           Parameter(ARRAY_BUFFER, BUFFER_SIZE) == 3 &&
           Parameter(ARRAY_BUFFER, BUFFER_USAGE) == (GLint)STREAM_DRAW);
    Submit(c);
    assert(Log.size() == 1);
    ExpectData(Log[0], FEnum_glBufferDataARB, {1, 3, STREAM_DRAW}, small, 3);
    Log.clear();
    assert(!glUnmapBufferARB(ARRAY_BUFFER));
    Rejected(c, GL_INVALID_OPERATION);
    /* An empty store maps too; there is nothing to send. */
    glBufferDataARB(ARRAY_BUFFER, 0, NULL, STATIC_DRAW);
    Submit(c);
    Log.clear();
    p = (BYTE *)glMapBufferARB(ARRAY_BUFFER, WRITE_ONLY);
    assert(p && !((ULONG_PTR)p % 16) && glUnmapBufferARB(ARRAY_BUFFER) == 1);
    Quiet(c);

    /* Draws and ArrayElement using a mapped buffer are errors. */
    glBufferDataARB(ARRAY_BUFFER, 1000, bytes.data(), STATIC_DRAW);
    glVertexPointer(2, GL_FLOAT, 0, Offset(0));
    glEnableClientState(GL_VERTEX_ARRAY);
    glBindBufferARB(ELEMENT_BUFFER, 1);
    Submit(c);
    Log.clear();
    assert(glMapBufferARB(ARRAY_BUFFER, READ_ONLY));
    glDrawArrays(GL_POINTS, 0, 1);
    Rejected(c, GL_INVALID_OPERATION);
    glDrawElements(GL_POINTS, 1, GL_UNSIGNED_BYTE, Offset(0));
    Rejected(c, GL_INVALID_OPERATION);
    glBindBufferARB(ELEMENT_BUFFER, 0);
    const GLubyte index = 0;
    glDrawElements(GL_POINTS, 1, GL_UNSIGNED_BYTE, &index);
    Rejected(c, GL_INVALID_OPERATION);
    glBegin(GL_POINTS);
    Submit(c);
    Log.clear();
    AliasArrayElement(0);
    assert(c->Error == GL_INVALID_OPERATION && !c->Records && !c->Used);
    c->Error = 0;
    glEnd();
    Clear(c);
    /* A mapped element buffer with client arrays. */
    assert(glUnmapBufferARB(ARRAY_BUFFER));
    glBindBufferARB(ELEMENT_BUFFER, 2);
    glBufferDataARB(ELEMENT_BUFFER, 1, &index, STATIC_DRAW);
    glBindBufferARB(ARRAY_BUFFER, 0);
    glVertexPointer(3, GL_FLOAT, 0, Positions);
    Submit(c);
    Log.clear();
    assert(glMapBufferARB(ELEMENT_BUFFER, READ_ONLY));
    glDrawElements(GL_POINTS, 1, GL_UNSIGNED_BYTE, Offset(0));
    Rejected(c, GL_INVALID_OPERATION);
    /* In a display list the error is compiled. */
    assert(JglListMode(GL_COMPILE));
    Clear(c);
    glDrawElements(GL_POINTS, 1, GL_UNSIGNED_BYTE, Offset(0));
    assert(!c->Error && c->Records == 1);
    Submit(c);
    assert(Log.size() == 1);
    ExpectCall(Log[0], DG_GL_RECORD_ERROR, {GL_INVALID_OPERATION});
    assert(JglListMode(0));
    Clear(c);
    /* Unmapped, the same draw is a buffer draw. */
    assert(glUnmapBufferARB(ELEMENT_BUFFER) == 1);
    glDrawElements(GL_POINTS, 1, GL_UNSIGNED_BYTE, Offset(0));
    Submit(c);
    assert(Log.size() == 1 && Log[0][8] == FEnum_glDrawRangeElements);
    Log.clear();
    /* Deleting a mapped buffer ends the mapping; the store is not sent. */
    glBindBufferARB(ARRAY_BUFFER, 1);
    p = (BYTE *)glMapBufferARB(ARRAY_BUFFER, WRITE_ONLY);
    p[0] = 1;
    const GLuint one = 1;
    glDeleteBuffersARB(1, &one);
    Submit(c);
    assert(Log.size() == 1);
    ExpectData(Log[0], FEnum_glDeleteBuffersARB, {1}, &one, 4);
    Log.clear();
    assert(!glIsBufferARB(1) && !Integer(ARRAY_BINDING) && Integer(ELEMENT_BINDING) == 2);
    assert(!glUnmapBufferARB(ARRAY_BUFFER));
    Rejected(c, GL_INVALID_OPERATION);
    Close(c);
}

/* The buffer draw: references for buffer arrays, client sections for the
 * rest, every record exact and admitted. */
static void Draws(void) {
    JGL_CONTEXT *c = Open(DRAWS, DG_ESCAPE_MAX_BYTES, TRUE);
    BufferArrays(c);
    const ULONG pc = DG_GL_ARRAY_RAW | DG_GL_ARRAY_POSITION | DG_GL_ARRAY_COLOR;
    const ULONG float3 = D(GL_FLOAT, 3), ubyte4 = D(GL_UNSIGNED_BYTE, 4);
    const std::vector<Reference> refs = {{0, float3, 1, 16, 0}, {1, ubyte4, 2, 4, 8}};
    /* Array draws: base is first; nothing but references travels. */
    glDrawArrays(GL_TRIANGLES, 2, 3);
    glDrawArrays(GL_POINTS, 0, 8); /* both buffers exactly */
    Submit(c);
    assert(Log.size() == 2);
    Words a = {GL_TRIANGLES, 2, 3, pc, 0, 3, 0, 0, 0, 0};
    std::vector<BYTE> payload = BufferPayload(a, refs, {});
    assert(payload.size() == 128 + 24);
    ExpectBufferDraw(Log[0], a, payload);
    a = {GL_POINTS, 0, 8, pc, 0, 3, 0, 0, 0, 0};
    ExpectBufferDraw(Log[1], a, BufferPayload(a, refs, {}));
    Log.clear();
    /* One vertex beyond either buffer is a local error. */
    glDrawArrays(GL_POINTS, 1, 8);
    Rejected(c, GL_INVALID_OPERATION);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDrawArrays(GL_POINTS, 0, 1); /* nothing provokes a vertex */
    Quiet(c);
    glEnableClientState(GL_VERTEX_ARRAY);

    /* The element buffer at an offset, each index type. The host bounds the
     * indices of buffer-only draws (200 lies beyond the vertex buffers). */
    glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_BYTE, Offset(1));
    glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, Offset(8));
    glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, Offset(16));
    glDrawElements(GL_POINTS, 28, GL_UNSIGNED_BYTE, Offset(0)); /* the whole buffer */
    Submit(c);
    assert(Log.size() == 4);
    const ULONG types[3][2] = {
        {GL_UNSIGNED_BYTE, 1}, {GL_UNSIGNED_SHORT, 8}, {GL_UNSIGNED_INT, 16}};
    for (ULONG i = 0; i < 3; ++i) {
        a = {GL_TRIANGLES, 0, 3, pc, 0, 3, 0, types[i][0], 3, types[i][1]};
        ExpectBufferDraw(Log[i], a, BufferPayload(a, refs, {}));
    }
    a = {GL_POINTS, 0, 28, pc, 0, 3, 0, GL_UNSIGNED_BYTE, 3, 0};
    ExpectBufferDraw(Log[3], a, BufferPayload(a, refs, {}));
    Log.clear();
    /* Element ranges beyond the buffer are local errors. */
    glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, Offset(20));
    Rejected(c, GL_INVALID_OPERATION);
    glDrawElements(GL_POINTS, 28, GL_UNSIGNED_BYTE, Offset(1));
    Rejected(c, GL_INVALID_OPERATION);
    if (sizeof(void *) > 4) { /* an offset is never truncated to 32 bits */
        glDrawElements(GL_POINTS, 1, GL_UNSIGNED_BYTE, Offset((ULONG_PTR)1 << 32));
        Rejected(c, GL_INVALID_OPERATION);
    }
    glDrawElements(GL_POINTS, 1, GL_UNSIGNED_BYTE, Offset(~(ULONG_PTR)0));
    Rejected(c, GL_INVALID_OPERATION);

    /* Inline client indices with buffer arrays travel after the references
     * at their size alignment; the host bounds them too. */
    glBindBufferARB(ELEMENT_BUFFER, 0);
    const GLubyte inline8[3] = {2, 9, 1};
    const GLushort inline16[5] = {4, 0, 7, 1, 2};
    const GLuint inline32[3] = {7, 6, 5};
    glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_BYTE, inline8);
    glDrawElements(GL_TRIANGLE_STRIP, 5, GL_UNSIGNED_SHORT, inline16);
    glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, inline32);
    Submit(c);
    assert(Log.size() == 3);
    a = {GL_TRIANGLES, 0, 3, pc, 0, 3, 0, GL_UNSIGNED_BYTE, 0, 0};
    payload = BufferPayload(a, refs, {}, Bytes(inline8));
    assert(payload.size() == 156 && !memcmp(&payload[152], inline8, 3) && !payload[155]);
    ExpectBufferDraw(Log[0], a, payload);
    a = {GL_TRIANGLE_STRIP, 0, 5, pc, 0, 3, 0, GL_UNSIGNED_SHORT, 0, 0};
    payload = BufferPayload(a, refs, {}, Bytes(inline16));
    assert(payload.size() == 164 && !memcmp(&payload[152], inline16, 10));
    ExpectBufferDraw(Log[1], a, payload);
    a = {GL_TRIANGLES, 0, 3, pc, 0, 3, 0, GL_UNSIGNED_INT, 0, 0};
    ExpectBufferDraw(Log[2], a, BufferPayload(a, refs, {}, Bytes(inline32)));
    Log.clear();
    /* Missing inline indices are GL_INVALID_VALUE, as in a client draw. */
    glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_BYTE, NULL);
    Rejected(c, GL_INVALID_VALUE);

    /* Mixed: buffer positions, client colors and SHORT normals (converted
     * like any client normal). Client sections hold exactly the vertices
     * min..max of the indices. */
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, Colors);
    glNormalPointer(GL_SHORT, 0, Normals);
    glEnableClientState(GL_NORMAL_ARRAY);
    const ULONG pcn = pc | DG_GL_ARRAY_NORMAL;
    const std::vector<Reference> position = {{0, float3, 1, 16, 0}};
    const GLubyte lines[4] = {7, 3, 5, 3};
    glDrawElements(GL_LINES, 4, GL_UNSIGNED_BYTE, lines);
    Submit(c);
    assert(Log.size() == 1);
    a = {GL_LINES, 3, 4, pcn, 0, 1, 5, GL_UNSIGNED_BYTE, 0, 0};
    payload = BufferPayload(
        a, position, {{1, ubyte4, Range(Colors, 3, 5)}, {2, D(GL_FLOAT, 3), NormalFloats(3, 5)}},
        Bytes(lines));
    assert(payload.size() == 224 && !memcmp(&payload[140], Colors[3], 20) &&
           !memcmp(&payload[220], lines, 4));
    ExpectBufferDraw(Log[0], a, payload);
    Log.clear();
    /* With the element buffer: the frontend copy bounds the client sections. */
    std::vector<BYTE> shorts(8, 0xee);
    const GLushort element[3] = {6, 2, 4};
    memcpy(&shorts[2], element, 6);
    Upload(c, ELEMENT_BUFFER, 4, shorts);
    glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, Offset(2));
    Submit(c);
    assert(Log.size() == 1);
    a = {GL_TRIANGLES, 2, 3, pcn, 0, 1, 5, GL_UNSIGNED_SHORT, 4, 2};
    ExpectBufferDraw(
        Log[0], a,
        BufferPayload(a, position,
                      {{1, ubyte4, Range(Colors, 2, 5)}, {2, D(GL_FLOAT, 3), NormalFloats(2, 5)}}));
    Log.clear();
    /* With client arrays the indices are scanned: one beyond a buffer is a
     * local error, inline or in the element buffer. */
    const GLubyte beyond[2] = {1, 8};
    glBindBufferARB(ELEMENT_BUFFER, 0);
    glDrawElements(GL_LINES, 2, GL_UNSIGNED_BYTE, beyond);
    Rejected(c, GL_INVALID_OPERATION);
    shorts[6] = 8;
    Upload(c, ELEMENT_BUFFER, 4, shorts);
    glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, Offset(2));
    Rejected(c, GL_INVALID_OPERATION);
    glBindBufferARB(ELEMENT_BUFFER, 0);
    /* Mixed array draw: sections of first..first+count-1. */
    glDisableClientState(GL_NORMAL_ARRAY);
    glDrawArrays(GL_POINTS, 5, 3);
    Submit(c);
    assert(Log.size() == 1);
    a = {GL_POINTS, 5, 3, pc, 0, 1, 3, 0, 0, 0};
    ExpectBufferDraw(Log[0], a, BufferPayload(a, position, {{1, ubyte4, Range(Colors, 5, 3)}}));
    Log.clear();
    /* A client array without memory is GL_INVALID_VALUE, as in a client draw. */
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, NULL);
    glDrawArrays(GL_POINTS, 0, 1);
    Rejected(c, GL_INVALID_VALUE);
    glDrawElements(GL_POINTS, 3, GL_UNSIGNED_BYTE, inline8);
    Rejected(c, GL_INVALID_VALUE);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);

    /* Generic attributes: buffers 1, 2 and 5 (a FLOAT marked normalized:
     * ignored) and client SHORT pairs. */
    std::vector<BYTE> floats(40, 0xee);
    for (ULONG v = 0; v < 8; ++v) {
        const GLfloat value = 0.25f * v - 1;
        memcpy(&floats[8 + 4 * v], &value, 4);
    }
    Upload(c, ARRAY_BUFFER, 5, floats);
    glVertexAttribPointerARB(9, 1, GL_FLOAT, GL_TRUE, 0, Offset(8));
    glBindBufferARB(ARRAY_BUFFER, 1);
    glVertexAttribPointerARB(0, 3, GL_FLOAT, GL_FALSE, 0, Offset(16));
    glBindBufferARB(ARRAY_BUFFER, 2);
    glVertexAttribPointerARB(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, 8, Offset(4));
    glBindBufferARB(ARRAY_BUFFER, 0);
    glVertexAttribPointerARB(6, 2, GL_SHORT, GL_FALSE, 0, Shorts);
    for (GLuint index : {0U, 3U, 6U, 9U})
        glEnableVertexAttribArrayARB(index);
    glDrawArrays(GL_TRIANGLES, 1, 3);
    Submit(c);
    assert(Log.size() == 1);
    const ULONG generic = 1 | 1 << 3 | 1 << 6 | 1 << 9;
    const ULONG buffered = 1U << 16 | 1U << 19 | 1U << 25;
    const std::vector<Reference> attributes = {{16, float3, 1, 16, 0},
                                               {19, ubyte4 | DG_GL_ATTRIB_NORMALIZED, 2, 4, 8},
                                               {25, D(GL_FLOAT, 1), 5, 8, 0}};
    a = {GL_TRIANGLES, 1, 3, DG_GL_ARRAY_RAW, generic, buffered, 3, 0, 0, 0};
    payload = BufferPayload(a, attributes, {{22, D(GL_SHORT, 2), Range(Shorts, 1, 3)}});
    assert(payload.size() == 176 && !memcmp(&payload[164], Shorts[1], 12));
    ExpectBufferDraw(Log[0], a, payload);
    Log.clear();
    /* Buffer-only generics through the element buffer. */
    glDisableVertexAttribArrayARB(6);
    glBindBufferARB(ELEMENT_BUFFER, 3);
    glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_BYTE, Offset(1));
    Submit(c);
    assert(Log.size() == 1);
    a = {GL_TRIANGLES,     0, 3, DG_GL_ARRAY_RAW, generic & ~(1U << 6), buffered, 0,
         GL_UNSIGNED_BYTE, 3, 1};
    ExpectBufferDraw(Log[0], a, BufferPayload(a, attributes, {}));
    Log.clear();
    for (GLuint index : {0U, 3U, 9U})
        glDisableVertexAttribArrayARB(index);
    glBindBufferARB(ELEMENT_BUFFER, 0);

    /* Secondary color, color index, edge flag, unit 2 coordinates and the
     * fog coordinate from buffer 6, the position from buffer 1. */
    std::vector<BYTE> mixed(152, 0xee);
    Upload(c, ARRAY_BUFFER, 6, mixed);
    glClientActiveTextureARB(TEXTURE0 + 2);
    glTexCoordPointer(2, GL_FLOAT, 0, Offset(0));
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glClientActiveTextureARB(TEXTURE0);
    glFogCoordPointerEXT(GL_FLOAT, 0, Offset(64));
    glEnableClientState(FOG_ARRAY);
    glSecondaryColorPointerEXT(3, GL_UNSIGNED_BYTE, 4, Offset(96));
    glEnableClientState(SECONDARY_ARRAY);
    glIndexPointer(GL_SHORT, 0, Offset(128));
    glEnableClientState(GL_INDEX_ARRAY);
    glEdgeFlagPointer(0, Offset(144));
    glEnableClientState(GL_EDGE_FLAG_ARRAY);
    glBindBufferARB(ARRAY_BUFFER, 1);
    glVertexPointer(3, GL_FLOAT, 0, Offset(16));
    glEnableClientState(GL_VERTEX_ARRAY);
    glBindBufferARB(ARRAY_BUFFER, 0);
    assert(!c->Error);
    glDrawArrays(GL_POINTS, 2, 4);
    Submit(c);
    assert(Log.size() == 1);
    const ULONG slots = 1 | 1 << 4 | 1 << 5 | 1 << 6 | 1 << 8 | 1 << 14;
    a = {GL_POINTS, 2, 4, DG_GL_ARRAY_RAW | slots, 0, slots, 0, 0, 0, 0};
    payload = BufferPayload(a,
                            {{0, float3, 1, 16, 0},
                             {4, D(GL_UNSIGNED_BYTE, 3), 6, 96, 4},
                             {5, D(GL_SHORT, 1), 6, 128, 0},
                             {6, D(GL_UNSIGNED_BYTE, 1), 6, 144, 0},
                             {8, D(GL_FLOAT, 2), 6, 0, 0},
                             {14, D(GL_FLOAT, 1), 6, 64, 0}},
                            {});
    assert(payload.size() == 128 + 6 * 12);
    ExpectBufferDraw(Log[0], a, payload);
    Close(c);
}

/* Client draws where the buffer draw cannot be used: compiled display lists,
 * offsets, strides and sizes the record cannot express. Each is exactly the
 * client draw of the same bytes in client memory. */
static void Fallbacks(void) {
    JGL_CONTEXT *c = Open(DRAWS, DG_ESCAPE_MAX_BYTES, TRUE);
    BufferArrays(c);
    const std::vector<BYTE> vertex = VertexBytes(), color = ColorBytes(), element = ElementBytes();
    auto buffers = [&](ULONG_PTR offset, GLsizei stride) {
        glBindBufferARB(ARRAY_BUFFER, 1);
        glVertexPointer(3, GL_FLOAT, stride, Offset(offset));
        glBindBufferARB(ARRAY_BUFFER, 2);
        glColorPointer(4, GL_UNSIGNED_BYTE, 8, Offset(4));
        glBindBufferARB(ARRAY_BUFFER, 0);
        glBindBufferARB(ELEMENT_BUFFER, 3);
    };
    auto clients = [&](ULONG_PTR offset, GLsizei stride) {
        glBindBufferARB(ARRAY_BUFFER, 0);
        glBindBufferARB(ELEMENT_BUFFER, 0);
        glVertexPointer(3, GL_FLOAT, stride, vertex.data() + offset);
        glColorPointer(4, GL_UNSIGNED_BYTE, 8, color.data() + 4);
    };
    /* Display list compilation dereferences every buffer. */
    for (ULONG mode : {GL_COMPILE, GL_COMPILE_AND_EXECUTE}) {
        assert(JglListMode(mode));
        Clear(c);
        buffers(16, 0);
        const Records compiled = Recorded(c, [&] {
            glDrawArrays(GL_TRIANGLES, 2, 3);
            glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, Offset(8));
            glDrawElements(GL_POINTS, 3, GL_UNSIGNED_INT, Offset(16)); /* 200: beyond */
        });
        assert(compiled.size() == 3);
        ExpectCall(compiled[2], DG_GL_RECORD_ERROR, {GL_INVALID_OPERATION});
        clients(16, 0);
        const Records reference = Recorded(c, [&] {
            glDrawArrays(GL_TRIANGLES, 2, 3);
            glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, element.data() + 8);
        });
        SameClientDraws(Records(compiled.begin(), compiled.begin() + 2), reference);
        assert(JglListMode(0));
    }
    /* Offsets and strides that are not component multiples, and strides
     * beyond DG_GL_MAX_BUFFER_STRIDE. */
    const std::pair<ULONG_PTR, GLsizei> layouts[] = {{18, 0}, {16, 14}, {17, 12}};
    for (const auto &layout : layouts) {
        buffers(layout.first, layout.second);
        const Records got = Recorded(c, [&] { glDrawArrays(GL_TRIANGLES, 0, 3); });
        clients(layout.first, layout.second);
        SameClientDraws(got, Recorded(c, [&] { glDrawArrays(GL_TRIANGLES, 0, 3); }));
    }
    std::vector<BYTE> strided(16 + 4096 * 2 + 12);
    for (ULONG i = 0; i < strided.size(); ++i)
        strided[i] = (BYTE)(i * 5);
    Upload(c, ARRAY_BUFFER, 7, strided);
    glDisableClientState(GL_COLOR_ARRAY);
    for (GLsizei stride : {DG_GL_MAX_BUFFER_STRIDE, DG_GL_MAX_BUFFER_STRIDE + 4, 4096}) {
        glBindBufferARB(ARRAY_BUFFER, 7);
        glVertexPointer(3, GL_FLOAT, stride, Offset(16));
        glBindBufferARB(ARRAY_BUFFER, 0);
        const Records got = Recorded(c, [&] { glDrawArrays(GL_TRIANGLES, 0, 3); });
        if (stride == DG_GL_MAX_BUFFER_STRIDE) { /* the largest a reference holds */
            const Words a = {
                GL_TRIANGLES, 0, 3, DG_GL_ARRAY_RAW | DG_GL_ARRAY_POSITION, 0, 1, 0, 0, 0, 0};
            assert(got.size() == 1);
            ExpectBufferDraw(got[0], a, BufferPayload(a, {{0, D(GL_FLOAT, 3), 7, 16, 2048}}, {}));
            continue;
        }
        glVertexPointer(3, GL_FLOAT, stride, strided.data() + 16);
        SameClientDraws(got, Recorded(c, [&] { glDrawArrays(GL_TRIANGLES, 0, 3); }));
    }
    glEnableClientState(GL_COLOR_ARRAY);
    /* An element offset that is not an index multiple. */
    buffers(16, 0);
    std::vector<BYTE> odd(7, 0);
    memcpy(&odd[1], ElementBytes16, 6);
    Upload(c, ELEMENT_BUFFER, 8, odd);
    Records got =
        Recorded(c, [&] { glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, Offset(1)); });
    clients(16, 0);
    SameClientDraws(got, Recorded(c, [&] {
                        glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, odd.data() + 1);
                    }));

    /* Beyond DG_GL_MAX_VERTICES: a buffer-only array draw stays one record;
     * with a client array, or an index span that large, it is client draws. */
    const ULONG many = DG_GL_MAX_VERTICES + 4464;
    std::vector<BYTE> pairs(8 * (many + 1)), tints(4 * (many + 1));
    for (ULONG i = 0; i < pairs.size(); ++i)
        pairs[i] = (BYTE)(i * 3 + i / 4096);
    for (ULONG i = 0; i < tints.size(); ++i)
        tints[i] = (BYTE)(i * 7);
    Upload(c, ARRAY_BUFFER, 9, pairs);
    glVertexPointer(2, GL_FLOAT, 0, Offset(0));
    glBindBufferARB(ARRAY_BUFFER, 0);
    glBindBufferARB(ELEMENT_BUFFER, 0);
    glDisableClientState(GL_COLOR_ARRAY);
    got = Recorded(c, [&] { glDrawArrays(GL_POINTS, 0, (GLsizei)many); });
    Words a = {GL_POINTS, 0, many, DG_GL_ARRAY_RAW | DG_GL_ARRAY_POSITION, 0, 1, 0, 0, 0, 0};
    assert(got.size() == 1);
    ExpectBufferDraw(got[0], a, BufferPayload(a, {{0, D(GL_FLOAT, 2), 9, 0, 0}}, {}));
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, tints.data());
    glEnableClientState(GL_COLOR_ARRAY);
    const GLuint span[2] = {0, many};
    got = Recorded(c, [&] {
        glDrawArrays(GL_POINTS, 0, (GLsizei)many);
        glDrawElements(GL_LINES, 2, GL_UNSIGNED_INT, span);
    });
    glVertexPointer(2, GL_FLOAT, 0, pairs.data());
    const Records reference = Recorded(c, [&] {
        glDrawArrays(GL_POINTS, 0, (GLsizei)many);
        glDrawElements(GL_LINES, 2, GL_UNSIGNED_INT, span);
    });
    assert(reference.size() > 2);
    SameClientDraws(got, reference);
    Close(c);

    /* Records beyond one packet: inline indices and client sections. */
    c = Open(DRAWS, SMALL_PACKET, TRUE);
    assert(JglMaxDataBytes(10) == 432);
    BufferArrays(c);
    glBindBufferARB(ELEMENT_BUFFER, 0);
    std::vector<GLubyte> indices(281);
    for (ULONG i = 0; i < indices.size(); ++i)
        indices[i] = (GLubyte)(i * 5 % 8);
    got = Recorded(c, [&] {
        glDrawElements(GL_POINTS, 280, GL_UNSIGNED_BYTE, indices.data()); /* 152 + 280 */
        glDrawElements(GL_POINTS, 281, GL_UNSIGNED_BYTE, indices.data());
    });
    assert(got[0][8] == FEnum_glDrawRangeElements && got[0][9] == 432);
    got.erase(got.begin());
    clients(16, 0);
    SameClientDraws(got, Recorded(c, [&] {
                        glDrawElements(GL_POINTS, 281, GL_UNSIGNED_BYTE, indices.data());
                    }));
    /* Buffer positions with client colors. */
    std::vector<GLubyte> colors(4 * 74);
    for (ULONG i = 0; i < colors.size(); ++i)
        colors[i] = (GLubyte)(i * 9);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, colors.data());
    std::vector<GLfloat> points(3 * 74);
    for (ULONG i = 0; i < points.size(); ++i)
        points[i] = (GLfloat)i;
    Upload(c, ARRAY_BUFFER, 10,
           std::vector<BYTE>((const BYTE *)points.data(),
                             (const BYTE *)(points.data() + points.size())));
    glVertexPointer(3, GL_FLOAT, 0, Offset(0));
    glBindBufferARB(ARRAY_BUFFER, 0);
    got = Recorded(c, [&] {
        glDrawArrays(GL_POINTS, 0, 73); /* 128 + 12 + 73 * 4 */
        glDrawArrays(GL_POINTS, 0, 74);
    });
    assert(got[0][8] == FEnum_glDrawRangeElements && got[0][9] == 432);
    got.erase(got.begin());
    glVertexPointer(3, GL_FLOAT, 0, points.data());
    SameClientDraws(got, Recorded(c, [&] { glDrawArrays(GL_POINTS, 0, 74); }));
    assert(got.size() > 1);
    Close(c);
}

/* Objects a latch no longer finds, ranges, mapping and ArrayElement. */
static void Elements(void) {
    JGL_CONTEXT *c = Open(DRAWS, DG_ESCAPE_MAX_BYTES, TRUE);
    BufferArrays(c);
    /* Generic 2: normalized SHORT pairs from buffer 5 at 2. */
    std::vector<BYTE> pairs(2 + sizeof(Shorts), 0xee);
    memcpy(&pairs[2], Shorts, sizeof(Shorts));
    Upload(c, ARRAY_BUFFER, 5, pairs);
    glVertexAttribPointerARB(2, 2, GL_SHORT, GL_TRUE, 0, Offset(2));
    glEnableVertexAttribArrayARB(2);
    glBindBufferARB(ARRAY_BUFFER, 0);
    glBegin(GL_POINTS);
    AliasArrayElement(7);
    AliasArrayElement(0);
    glEnd();
    Submit(c);
    assert(Log.size() == 8);
    ExpectCall(Log[0], FEnum_glBegin, {GL_POINTS});
    for (ULONG n = 0; n < 2; ++n) {
        const ULONG v = n ? 0 : 7;
        const std::vector<ULONG> *r = &Log[1 + 3 * n];
        ExpectCall(r[0], FEnum_glColor4f,
                   {Bits(Colors[v][0] / 255.0f), Bits(Colors[v][1] / 255.0f),
                    Bits(Colors[v][2] / 255.0f), Bits(Colors[v][3] / 255.0f)});
        ExpectCall(r[1], FEnum_glVertexAttrib4fARB,
                   {2, Bits((2.0f * Shorts[v][0] + 1.0f) / 65535.0f),
                    Bits((2.0f * Shorts[v][1] + 1.0f) / 65535.0f), 0, Bits(1.0f)});
        ExpectCall(
            r[2], FEnum_glVertex4f,
            {Bits(Positions[v][0]), Bits(Positions[v][1]), Bits(Positions[v][2]), Bits(1.0f)});
    }
    ExpectCall(Log[7], FEnum_glEnd, {});
    Log.clear();
    /* Inside Begin/End the errors record nothing. */
    auto element = [&](GLint index, GLenum error) {
        glBegin(GL_POINTS);
        Submit(c);
        Log.clear();
        AliasArrayElement(index);
        assert(c->Error == error && !c->Records && !c->Used);
        c->Error = 0;
        glEnd();
        Clear(c);
    };
    element(8, GL_INVALID_OPERATION); /* beyond buffer 1 */
    glBindBufferARB(ARRAY_BUFFER, 5);
    assert(glMapBufferARB(ARRAY_BUFFER, READ_ONLY));
    element(0, GL_INVALID_OPERATION);
    assert(glUnmapBufferARB(ARRAY_BUFFER));
    /* An offset is never added before it is known to be in the object. */
    glVertexAttribPointerARB(2, 2, GL_SHORT, GL_TRUE, 0, Offset(~(ULONG_PTR)3));
    element(0, GL_INVALID_OPERATION);
    glDrawArrays(GL_POINTS, 0, 1);
    Rejected(c, GL_INVALID_OPERATION);
    glVertexAttribPointerARB(2, 2, GL_SHORT, GL_TRUE, 0, Offset(2));
    glBindBufferARB(ARRAY_BUFFER, 0);
    /* Compiled into a list, the values come from the copy. */
    assert(JglListMode(GL_COMPILE));
    Clear(c);
    AliasArrayElement(3);
    Submit(c);
    assert(Log.size() == 3 && Log[2][8] == FEnum_glVertex4f && Log[2][9] == Bits(Positions[3][0]));
    assert(JglListMode(0));
    Clear(c);

    /* A latch whose object is gone (restored by PopClientAttrib) or whose
     * name now names another object finds nothing: GL_INVALID_OPERATION for
     * every draw, indexed buffer-only draws included. */
    const GLuint one = 1;
    AliasPushClientAttrib(CLIENT_VERTEX_ARRAY_BIT);
    glDeleteBuffersARB(1, &one);
    assert(!Integer(VERTEX_BINDING));
    AliasPopClientAttrib();
    assert(Integer(VERTEX_BINDING) == 1 && !c->Error);
    Submit(c);
    Log.clear();
    for (int pass = 0; pass < 2; ++pass) {
        glDrawArrays(GL_POINTS, 0, 1);
        Rejected(c, GL_INVALID_OPERATION);
        glDrawElements(GL_POINTS, 3, GL_UNSIGNED_BYTE, Offset(1));
        Rejected(c, GL_INVALID_OPERATION);
        element(0, GL_INVALID_OPERATION);
        Upload(c, ARRAY_BUFFER, 1, VertexBytes()); /* a new object named 1 */
        glBindBufferARB(ARRAY_BUFFER, 0);
    }
    /* An object without contents: local for array draws, the host's for
     * indexed buffer-only draws. */
    glBindBufferARB(ARRAY_BUFFER, 11);
    glVertexPointer(3, GL_FLOAT, 0, Offset(0));
    glBindBufferARB(ARRAY_BUFFER, 0);
    glDrawArrays(GL_POINTS, 0, 1);
    Rejected(c, GL_INVALID_OPERATION);
    glDrawElements(GL_POINTS, 3, GL_UNSIGNED_BYTE, Offset(1));
    Submit(c);
    assert(Log.size() == 1 && Log[0][8] == FEnum_glDrawRangeElements);
    Close(c);
}

/* Bindings and latches are vertex-array client state. */
static void ClientState(void) {
    JGL_CONTEXT *c = Open(DRAWS, DG_ESCAPE_MAX_BYTES, TRUE);
    GLuint names[13];
    glGenBuffersARB(13, names);
    for (GLuint name : names)
        glBindBufferARB(ARRAY_BUFFER, name);
    glBindBufferARB(ARRAY_BUFFER, 5);
    glVertexPointer(3, GL_FLOAT, 0, Offset(8));
    glBindBufferARB(ARRAY_BUFFER, 6); /* the vertex array keeps buffer 5 */
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, Offset(0));
    assert(Integer(VERTEX_BINDING) == 5 && Integer(COLOR_BINDING) == 6 &&
           Integer(ARRAY_BINDING) == 6);
    glBindBufferARB(ARRAY_BUFFER, 0);
    glNormalPointer(GL_FLOAT, 0, Positions);
    glBindBufferARB(ARRAY_BUFFER, 7);
    glIndexPointer(GL_SHORT, 0, Offset(0));
    glBindBufferARB(ARRAY_BUFFER, 8);
    glTexCoordPointer(2, GL_FLOAT, 0, Offset(0));
    glClientActiveTextureARB(TEXTURE0 + 3);
    glBindBufferARB(ARRAY_BUFFER, 9);
    glTexCoordPointer(2, GL_FLOAT, 0, Offset(0));
    glBindBufferARB(ARRAY_BUFFER, 10);
    glEdgeFlagPointer(0, Offset(0));
    glBindBufferARB(ARRAY_BUFFER, 11);
    glSecondaryColorPointerEXT(3, GL_UNSIGNED_BYTE, 0, Offset(0));
    glBindBufferARB(ARRAY_BUFFER, 12);
    glFogCoordPointerEXT(GL_FLOAT, 0, Offset(0));
    glBindBufferARB(ARRAY_BUFFER, 3);
    glVertexAttribPointerARB(4, 4, GL_FLOAT, GL_FALSE, 0, Offset(0));
    glBindBufferARB(ELEMENT_BUFFER, 4);
    Quiet(c);
    assert(Integer(ARRAY_BINDING) == 3 && Integer(ELEMENT_BINDING) == 4 &&
           Integer(VERTEX_BINDING) == 5 && Integer(NORMAL_BINDING) == 0 &&
           Integer(COLOR_BINDING) == 6 && Integer(INDEX_BINDING) == 7 &&
           Integer(TEXCOORD_BINDING) == 9 && Integer(EDGE_BINDING) == 10 &&
           Integer(SECONDARY_BINDING) == 11 && Integer(FOG_BINDING) == 12);
    glClientActiveTextureARB(TEXTURE0); /* the client-active unit's array */
    assert(Integer(TEXCOORD_BINDING) == 8);
    GLboolean flag = 0;
    GLfloat f = 0;
    GLdouble d = 0;
    glGetBooleanv(VERTEX_BINDING, &flag);
    glGetFloatv(VERTEX_BINDING, &f);
    glGetDoublev(INDEX_BINDING, &d);
    assert(flag == 1 && f == 5 && d == 7);
    void *pointer = NULL;
    glGetPointerv(GL_VERTEX_ARRAY_POINTER, &pointer);
    assert(pointer == (void *)8);
    GLint value = 0x5a5a;
    GLfloat fv[4] = {9, 9, 9, 9};
    glGetVertexAttribivARB(4, ATTRIB_BINDING, &value);
    glGetVertexAttribfvARB(4, ATTRIB_BINDING, fv);
    assert(value == 3 && fv[0] == 3 && fv[1] == 9);
    glGetVertexAttribivARB(5, ATTRIB_BINDING, &value);
    assert(!value);
    glGetVertexAttribPointervARB(4, 0x8645, &pointer);
    assert(!pointer);
    Quiet(c);
    assert(Queries.empty());
    /* WEIGHT_ARRAY has no array here: the host's. */
    assert(Integer(WEIGHT_BINDING) == (GLint)HOST_ANSWER && Queries.size() == 1);
    Clear(c);

    /* CLIENT_VERTEX_ARRAY_BIT saves both bindings and every latch. */
    AliasPushClientAttrib(CLIENT_VERTEX_ARRAY_BIT);
    glBindBufferARB(ARRAY_BUFFER, 2);
    glBindBufferARB(ELEMENT_BUFFER, 1);
    glVertexPointer(2, GL_FLOAT, 0, Offset(4));
    glVertexAttribPointerARB(4, 4, GL_FLOAT, GL_FALSE, 0, Offset(0));
    assert(Integer(VERTEX_BINDING) == 2 && Integer(ARRAY_BINDING) == 2);
    AliasPopClientAttrib();
    glGetPointerv(GL_VERTEX_ARRAY_POINTER, &pointer);
    glGetVertexAttribivARB(4, ATTRIB_BINDING, &value);
    assert(Integer(ARRAY_BINDING) == 3 && Integer(ELEMENT_BINDING) == 4 &&
           Integer(VERTEX_BINDING) == 5 && pointer == (void *)8 && value == 3);
    AliasPushClientAttrib(CLIENT_PIXEL_STORE_BIT);
    glBindBufferARB(ARRAY_BUFFER, 2);
    glBindBufferARB(ELEMENT_BUFFER, 1);
    AliasPopClientAttrib();
    assert(Integer(ARRAY_BINDING) == 2 && Integer(ELEMENT_BINDING) == 1);
    Quiet(c);

    /* InterleavedArrays latches ARRAY_BUFFER for the arrays it specifies. */
    std::vector<BYTE> interleaved(32 + 16 * 3, 0);
    for (ULONG v = 0; v < 3; ++v) {
        memcpy(&interleaved[32 + 16 * v], Colors[v], 4);
        memcpy(&interleaved[36 + 16 * v], Positions[v], 12);
    }
    Upload(c, ARRAY_BUFFER, 13, interleaved);
    AliasInterleavedArrays(C4UB_V3F, 0, Offset(32));
    glBindBufferARB(ARRAY_BUFFER, 0);
    glGetPointerv(GL_VERTEX_ARRAY_POINTER, &pointer);
    assert(pointer == (void *)36 && Integer(VERTEX_BINDING) == 13 && Integer(COLOR_BINDING) == 13 &&
           Integer(NORMAL_BINDING) == 0 && Integer(TEXCOORD_BINDING) == 8 &&
           Integer(INDEX_BINDING) == 7 && Integer(EDGE_BINDING) == 10 && !c->Error);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    Submit(c);
    assert(Log.size() == 1);
    const Words a = {GL_TRIANGLES,
                     0,
                     3,
                     DG_GL_ARRAY_RAW | DG_GL_ARRAY_POSITION | DG_GL_ARRAY_COLOR,
                     0,
                     3,
                     0,
                     0,
                     0,
                     0};
    ExpectBufferDraw(
        Log[0], a,
        BufferPayload(a, {{0, D(GL_FLOAT, 3), 13, 36, 16}, {1, D(GL_UNSIGNED_BYTE, 4), 13, 32, 16}},
                      {}));
    Log.clear();

    /* Deletion resets this context's bindings and latches of the object. */
    glBindBufferARB(ARRAY_BUFFER, 13);
    glBindBufferARB(ELEMENT_BUFFER, 13);
    glBindBufferARB(ARRAY_BUFFER, 3);
    glVertexAttribPointerARB(6, 4, GL_FLOAT, GL_FALSE, 0, Offset(0));
    const GLuint doomed[2] = {13, 3};
    glDeleteBuffersARB(2, doomed);
    Submit(c);
    assert(Log.size() == 1);
    ExpectData(Log[0], FEnum_glDeleteBuffersARB, {2}, doomed, 8);
    Log.clear();
    glGetVertexAttribivARB(4, ATTRIB_BINDING, &value);
    assert(!value);
    glGetVertexAttribivARB(6, ATTRIB_BINDING, &value);
    assert(!value && !Integer(ARRAY_BINDING) && !Integer(ELEMENT_BINDING) &&
           !Integer(VERTEX_BINDING) && !Integer(COLOR_BINDING) && Integer(INDEX_BINDING) == 7 &&
           Integer(TEXCOORD_BINDING) == 8 && Integer(EDGE_BINDING) == 10 &&
           Integer(SECONDARY_BINDING) == 11 && Integer(FOG_BINDING) == 12);
    Close(c);

    /* Without secondary color and fog coordinates their bindings are the
     * host's; the others stay local. */
    c = Open(VBO);
    Queries.clear();
    assert(Integer(SECONDARY_BINDING) == (GLint)HOST_ANSWER &&
           Integer(FOG_BINDING) == (GLint)HOST_ANSWER && Queries.size() == 2);
    assert(!Integer(VERTEX_BINDING) && !Integer(TEXCOORD_BINDING) && Queries.size() == 2);
    Close(c);
}

/* Makes a context current, forgetting the transport records that costs. */
static void Switch(HDC dc, HGLRC context) {
    assert(wglMakeCurrent(dc, context));
    Log.clear();
}
/* wglShareLists contexts share objects and names; others do not; the group's
 * last context frees its table. */
static void Sharing(void) {
    Reset();
    HostCaps = VBO;
    SubmittedRecord = Capture;
    AnswerQuery = Answer;
    HGLRC a = Create(), b = wglCreateContext((HDC)2), u = wglCreateContext((HDC)2);
    assert(b && u && wglShareLists(a, b));
    JGL_CONTEXT *ca = Lookup(a), *cb = Lookup(b);
    GLuint names[2] = {}, name = 0;
    const BYTE bytes[8] = {1, 2, 3, 4, 5, 6, 7, 8}, patch[2] = {0xaa, 0xbb};
    BYTE out[8];
    glGenBuffersARB(2, names);
    glBindBufferARB(ARRAY_BUFFER, 1);
    glBufferDataARB(ARRAY_BUFFER, 8, bytes, STREAM_DRAW);
    Submit(ca);
    Log.clear();
    /* b: the same objects and names. */
    Switch((HDC)2, b);
    assert(glIsBufferARB(1) && !glIsBufferARB(2) && !Integer(ARRAY_BINDING));
    glGenBuffersARB(1, &name);
    assert(name == 3);
    glBindBufferARB(ARRAY_BUFFER, 1);
    glGetBufferSubDataARB(ARRAY_BUFFER, 0, 8, out);
    assert(!memcmp(out, bytes, 8) && Parameter(ARRAY_BUFFER, BUFFER_SIZE) == 8 &&
           Parameter(ARRAY_BUFFER, BUFFER_USAGE) == (GLint)STREAM_DRAW);
    glBufferSubDataARB(ARRAY_BUFFER, 0, 2, patch);
    BYTE *p = (BYTE *)glMapBufferARB(ARRAY_BUFFER, WRITE_ONLY);
    assert(p && !cb->Error);
    Submit(cb);
    assert(Log.size() == 1 && Log[0][3] == cb->Id);
    ExpectData(Log[0], FEnum_glBufferSubDataARB, {1, 0}, patch, 2);
    Log.clear();
    /* a: the mapping belongs to the object; a unmaps and sends it. */
    Switch((HDC)1, a);
    assert(Parameter(ARRAY_BUFFER, BUFFER_MAPPED) == 1 && MapPointer(ARRAY_BUFFER) == p);
    p[7] = 0x77;
    assert(glUnmapBufferARB(ARRAY_BUFFER) == 1);
    Submit(ca);
    const BYTE expected[8] = {0xaa, 0xbb, 3, 4, 5, 6, 7, 0x77};
    assert(Log.size() == 1 && Log[0][3] == ca->Id);
    ExpectData(Log[0], FEnum_glBufferSubDataARB, {1, 0}, expected, 8);
    Log.clear();
    /* u: its own namespace. */
    Switch((HDC)2, u);
    assert(!glIsBufferARB(1));
    glGenBuffersARB(1, &name);
    assert(name == 1);
    glBindBufferARB(ARRAY_BUFFER, 1);
    glBufferDataARB(ARRAY_BUFFER, 4, bytes, STATIC_DRAW);
    assert(Parameter(ARRAY_BUFFER, BUFFER_SIZE) == 4);
    Switch((HDC)1, a);
    assert(Parameter(ARRAY_BUFFER, BUFFER_SIZE) == 8);
    /* Deletion in b resets b's bindings only; a's binding names nothing. */
    Switch((HDC)2, b);
    const GLuint one = 1;
    glDeleteBuffersARB(1, &one);
    assert(!Integer(ARRAY_BINDING));
    Switch((HDC)1, a);
    assert(Integer(ARRAY_BINDING) == 1 && !glIsBufferARB(1));
    glBufferDataARB(ARRAY_BUFFER, 8, bytes, STATIC_DRAW);
    assert(ca->Error == GL_INVALID_OPERATION);
    ca->Error = 0;
    /* The table outlives a; b's last release frees it. */
    glBindBufferARB(ARRAY_BUFFER, 3);
    glBufferDataARB(ARRAY_BUFFER, 8, bytes, STATIC_DRAW);
    assert(wglMakeCurrent(NULL, NULL) && wglDeleteContext(a));
    Switch((HDC)2, b);
    glBindBufferARB(ARRAY_BUFFER, 3);
    glGetBufferSubDataARB(ARRAY_BUFFER, 0, 8, out);
    assert(glIsBufferARB(3) && !memcmp(out, bytes, 8) && !cb->Error);
    const ULONG held = Allocations;
    assert(wglMakeCurrent(NULL, NULL) && wglDeleteContext(b));
    assert(Allocations < held);
    Switch((HDC)2, u);
    assert(glIsBufferARB(1) && Parameter(ARRAY_BUFFER, BUFFER_SIZE) == 4);
    assert(wglMakeCurrent(NULL, NULL) && wglDeleteContext(u));
    assert(!Allocations);
    Log.clear();
}

int main() {
    Unnegotiated();
    Negotiation();
    Records_();
    Validation();
    Mapping();
    Draws();
    Fallbacks();
    Elements();
    ClientState();
    Sharing();
    puts("PASS vertex buffer object frontend");
    return 0;
}
