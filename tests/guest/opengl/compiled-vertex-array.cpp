/* SPDX-License-Identifier: GPL-2.0-or-later
 * Bulk indexed client-array draws and EXT_compiled_vertex_array through the
 * actual frontend on the strict transport fixture. With negotiated buffer
 * draws (DG_CAP_GL_VERTEX_BUFFER_OBJECT) an indexed client draw is one buffer
 * draw without buffer slots: each array's vertices low..high once and the
 * indices inline, whenever that is no larger than the per-index expansion.
 * It draws exactly the vertex sequence the expansion of an older host draws,
 * for every array kind, stride, index type and mode; sparse, oversized,
 * failing and compiled draws keep the expansion's records and errors; every
 * record is admitted by the NT validator and every inline index lies within
 * the client vertices, as the host requires. LockArraysEXT/UnlockArraysEXT are
 * client state only: gated, validated, queried, pushed and popped, never
 * recorded, and locked draws submit the records of unlocked ones. ArrayElement
 * keeps its exact records. With an argument "bench", prints records, bytes
 * and frontend time of a Quake III-like batch along each path instead. */
#include <array>
#include <chrono>
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
#include "extension-names.h"
#include "dg-gl-validate.h"

constexpr GLenum SECONDARY_ARRAY = 0x845e, FOG_ARRAY = 0x8457, TEXTURE0 = 0x84c0;
constexpr GLenum LOCK_FIRST = 0x81a8, LOCK_COUNT = 0x81a9;
constexpr GLbitfield CLIENT_VERTEX_ARRAY_BIT = 2;
constexpr ULONG HOST_ANSWER = 77;
constexpr ULONG VBO = DG_CAP_GL_VERTEX_BUFFER_OBJECT;
/* Every array kind of the client draws, without and with buffer draws. */
constexpr ULONG OLD = DG_CAP_GL_VERTEX_PROGRAM | DG_CAP_GL_MULTITEXTURE | DG_CAP_GL_FOG_COORD |
                      DG_CAP_GL_VERTEX_ARRAY_BGRA;
constexpr ULONG NEW = OLD | VBO;

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
        default:
            return (ULONG)-1;
    }
}
typedef std::vector<std::vector<ULONG>> Records;
static Records Log;
/* A buffer draw's every inline index lies within its client vertices (the
 * host's admission; a violation would fail the context). */
static void HostAdmits(const std::vector<ULONG> &r) {
    const ULONG *a = r.data() + 10;
    const BYTE *payload = (const BYTE *)(r.data() + 20);
    unsigned words[10], descriptors[DG_GL_ATTRIB_SLOTS], offsets[DG_GL_ATTRIB_SLOTS], at = 0,
                                                                                      length = 0;
    for (ULONG i = 0; i < 10; ++i)
        words[i] = a[i];
    memcpy(descriptors, payload, sizeof(descriptors));
    assert(DgRawBufferLayout(words, descriptors, (const unsigned *)(payload + 128), offsets, &at,
                             &length) &&
           length == r[9]);
    if (!a[7] || a[8] || !a[6])
        return;
    const ULONG size = DgBufferIndexBytes(a[7]);
    for (ULONG i = 0; i < a[2]; ++i) {
        ULONG v = 0;
        memcpy(&v, payload + at + i * size, size);
        assert(v >= a[1] && (unsigned long long)v < (unsigned long long)a[1] + a[6]);
    }
}
static void Capture(const ULONG *record) {
    Log.emplace_back(record, record + record[1] / 4);
    if (record[0] != DG_GL_DATA_CALL)
        return;
    std::vector<ULONG> copy(Log.back());
    const DG_GL_LIMITS limits = {7, DG_ESCAPE_MAX_BYTES, DG_GL_MAX_RECORDS, 1};
    assert(DgValidateUserGl(copy.data(), copy.size() * 4, 42, 7, &limits, KernelWords, NULL));
    if (record[8] == FEnum_glDrawRangeElements)
        HostAdmits(Log.back());
}
static BOOL Answer(const ULONG *r, DG_ESCAPE_REPLY *reply, ULONG) {
    if (r[8] != FEnum_glGetIntegerv)
        return FALSE;
    memcpy(reply + 1, &HOST_ANSWER, 4);
    reply->ResultBytes = 4;
    reply->ResultType = DG_GL_RESULT_INT;
    return TRUE;
}
static void Submit(JGL_CONTEXT *c) {
    assert(Flush(c) && !c->Failed);
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
    HostSecondaryWords = 3;
    SubmittedRecord = Capture;
    AnswerQuery = Answer;
    JGL_CONTEXT *c = Lookup(Create());
    Clear(c);
    return c;
}
static void Close(JGL_CONTEXT *c) {
    assert(wglMakeCurrent(NULL, NULL) && wglDeleteContext((HGLRC)(ULONG_PTR)c->Id));
    Log.clear();
}
/* The call raised exactly this error and nothing reached the host. */
static void Rejected(JGL_CONTEXT *c, GLenum error) {
    assert(c->Error == error && !c->Records && !c->Used);
    Submit(c);
    assert(Log.empty());
    c->Error = 0;
}
static void Quiet(JGL_CONTEXT *c) {
    assert(!c->Error && !c->Records && !c->Used);
}
/* The records of everything draw() submits, without a GL error. */
template <typename F> static Records Recorded(JGL_CONTEXT *c, F draw) {
    Log.clear();
    draw();
    assert(!c->Error);
    Submit(c);
    Records out;
    out.swap(Log);
    return out;
}
static GLint Integer(GLenum pname) {
    GLint value = 0x5a5a;
    glGetIntegerv(pname, &value);
    return value;
}

/* What a draw record draws: its mode and, per drawn vertex, every enabled
 * slot's number, descriptor and wire bytes. */
struct Drawn {
    ULONG Mode = ~0u;
    std::vector<std::vector<BYTE>> Vertices;
    bool operator==(const Drawn &) const = default;
};
static std::vector<BYTE> Vertex(const unsigned *descriptors, const unsigned *offsets,
                                const BYTE *payload, ULONG v) {
    std::vector<BYTE> out;
    for (ULONG slot = 0; slot < DG_GL_ATTRIB_SLOTS; ++slot) {
        const unsigned d = descriptors[slot];
        if (!d)
            continue;
        const ULONG element = ((d >> 16) & 255) * DgArrayComponentBytes(d & 65535);
        out.push_back((BYTE)slot);
        out.insert(out.end(), (const BYTE *)&d, (const BYTE *)&d + 4);
        out.insert(out.end(), payload + offsets[slot] + v * element,
                   payload + offsets[slot] + (v + 1) * element);
    }
    return out;
}
static void Decode(const std::vector<ULONG> &r, Drawn &out) {
    assert(r[0] == DG_GL_DATA_CALL);
    const ULONG function = r[8], *a = r.data() + 10;
    unsigned descriptors[DG_GL_ATTRIB_SLOTS] = {}, offsets[DG_GL_ATTRIB_SLOTS] = {}, length = 0;
    assert(out.Mode == ~0u || out.Mode == a[0]);
    out.Mode = a[0];
    if (function == FEnum_glDrawRangeElements) {
        const BYTE *payload = (const BYTE *)(r.data() + 20);
        unsigned words[10], at = 0;
        for (ULONG i = 0; i < 10; ++i)
            words[i] = a[i];
        memcpy(descriptors, payload, sizeof(descriptors));
        assert(!a[5] && !a[8] && a[7]); /* client sections, inline indices */
        assert(DgRawBufferLayout(words, descriptors, NULL, offsets, &at, &length));
        const ULONG size = DgBufferIndexBytes(a[7]);
        for (ULONG i = 0; i < a[2]; ++i) {
            ULONG index = 0;
            memcpy(&index, payload + at + i * size, size);
            out.Vertices.push_back(Vertex(descriptors, offsets, payload, index - a[1]));
        }
        return;
    }
    const ULONG slots = function == FEnum_glDrawArrays      ? 7
                        : function == FEnum_glDrawArraysEXT ? DG_GL_ARRAY_EXT_SLOTS
                                                            : DG_GL_ATTRIB_SLOTS;
    const BYTE *payload = (const BYTE *)(r.data() + 10 + (slots == DG_GL_ATTRIB_SLOTS ? 5 : 4));
    assert(slots != DG_GL_ATTRIB_SLOTS || function == FEnum_glVertexAttribPointerARB);
    memcpy(descriptors, payload, slots * 4);
    assert(slots == DG_GL_ATTRIB_SLOTS
               ? DgRawAttribLayout(a[3], a[4], a[2], descriptors, offsets, &length)
               : DgRawArrayLayoutSlots(a[3], a[2], descriptors, offsets, &length, slots));
    for (ULONG v = 0; v < a[2]; ++v)
        out.Vertices.push_back(Vertex(descriptors, offsets, payload, v));
}
static Drawn Draws(const Records &records) {
    Drawn out;
    for (const std::vector<ULONG> &r : records)
        Decode(r, out);
    return out;
}

/* Deterministic client memory: a little-endian LCG byte stream. */
static ULONG Seed = 1;
static BYTE Next(void) {
    Seed = Seed * 1103515245u + 12345u;
    return (BYTE)(Seed >> 16);
}
static std::vector<BYTE> Noise(size_t bytes) {
    std::vector<BYTE> out(bytes);
    for (BYTE &b : out)
        b = Next();
    return out;
}
static std::vector<BYTE> Floats(size_t count) {
    std::vector<BYTE> out;
    for (size_t i = 0; i < count; ++i) {
        const GLfloat value = (GLfloat)((int)Next() - 128) / 16.0f;
        out.insert(out.end(), (const BYTE *)&value, (const BYTE *)&value + 4);
    }
    return out;
}

/* A Quake III-like batch: FLOAT 3 positions padded to 16 bytes, unit 0 and
 * unit 1 FLOAT 2 coordinates (unit 1 interleaved, stride 12), UNSIGNED_BYTE 4
 * colours, and UNSIGNED_INT triangle indices of a vertices-wide grid. */
struct Batch {
    std::vector<BYTE> Positions, Texcoords, Lightmaps, Colors;
    std::vector<GLuint> Indices;
};
static Batch Quake(ULONG columns, ULONG rows) {
    Batch b;
    const ULONG vertices = columns * rows;
    b.Positions = Floats(vertices * 4);
    b.Texcoords = Floats(vertices * 2);
    b.Lightmaps = Floats(vertices * 3);
    b.Colors = Noise(vertices * 4);
    for (ULONG y = 0; y + 1 < rows; ++y)
        for (ULONG x = 0; x + 1 < columns; ++x) {
            const GLuint v = y * columns + x;
            for (GLuint i : {v, v + columns, v + 1, v + 1, v + columns, v + columns + 1})
                b.Indices.push_back(i);
        }
    return b;
}
static void QuakeArrays(const Batch &b) {
    glVertexPointer(3, GL_FLOAT, 16, b.Positions.data());
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, b.Colors.data());
    glClientActiveTextureARB(TEXTURE0);
    glTexCoordPointer(2, GL_FLOAT, 0, b.Texcoords.data());
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glClientActiveTextureARB(TEXTURE0 + 1);
    glTexCoordPointer(2, GL_FLOAT, 12, b.Lightmaps.data());
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glClientActiveTextureARB(TEXTURE0);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
}
/* Quake III's R_DrawStripElements: ArrayElement strips of a triangle list. */
static void Strips(const std::vector<GLuint> &indices) {
    GLuint last[3];
    bool even = false;
    glBegin(GL_TRIANGLE_STRIP);
    for (ULONG i = 0; i < 3; ++i)
        AliasArrayElement((GLint)(last[i] = indices[i]));
    for (ULONG i = 3; i < indices.size(); i += 3) {
        if (!even ? indices[i] == last[2] && indices[i + 1] == last[1]
                  : last[2] == indices[i + 1] && last[0] == indices[i]) {
            AliasArrayElement((GLint)indices[i + 2]);
            even = !even;
        } else {
            glEnd();
            glBegin(GL_TRIANGLE_STRIP);
            for (ULONG k = 0; k < 3; ++k)
                AliasArrayElement((GLint)indices[i + k]);
            even = false;
        }
        last[0] = indices[i];
        last[1] = indices[i + 1];
        last[2] = indices[i + 2];
    }
    glEnd();
}
static size_t Bytes(const Records &records) {
    size_t bytes = 0;
    for (const std::vector<ULONG> &r : records)
        bytes += r.size() * 4;
    return bytes;
}
static void ExpectRanged(const Records &records, ULONG low, ULONG vertices) {
    assert(records.size() == 1 && records[0][8] == FEnum_glDrawRangeElements);
    const ULONG *a = records[0].data() + 10;
    assert(a[1] == low && a[6] == vertices && !a[5] && !a[8] && !a[9]);
}

/* One indexed draw of the current arrays: the old host's records (the
 * per-index expansion) and the new host's, drawing the same vertices. */
template <typename Arrays>
static std::pair<Records, Records> Both(Arrays arrays, GLenum mode, GLsizei count, GLenum type,
                                        const void *indices) {
    std::pair<Records, Records> out;
    for (ULONG caps : {OLD, NEW}) {
        JGL_CONTEXT *c = Open(caps);
        arrays();
        Quiet(c);
        (caps == OLD ? out.first : out.second) =
            Recorded(c, [&] { glDrawElements(mode, count, type, indices); });
        Close(c);
    }
    for (const std::vector<ULONG> &r : out.first)
        assert(r[8] != FEnum_glDrawRangeElements);
    assert(Draws(out.first) == Draws(out.second));
    return out;
}

/* Without the capability nothing new exists and indexed draws expand. */
static void Unnegotiated(void) {
    JGL_CONTEXT *c = Open(OLD);
    assert(!JglCompiledVertexArrayAvailable());
    assert(!strstr((const char *)glGetString(GL_EXTENSIONS), "compiled_vertex_array"));
    assert(!wglGetProcAddress("glLockArraysEXT") && !wglGetProcAddress("glUnlockArraysEXT"));
    glLockArraysEXT(0, 4);
    Rejected(c, GL_INVALID_OPERATION);
    glUnlockArraysEXT();
    Rejected(c, GL_INVALID_OPERATION);
    /* The lock queries stay the host's. */
    assert(Integer(LOCK_FIRST) == (GLint)HOST_ANSWER);
    Close(c);
}

/* The extension, entry points, errors, queries and client attribute stack. */
static void LockState(void) {
    JGL_CONTEXT *c = Open(NEW);
    const char *names = (const char *)glGetString(GL_EXTENSIONS);
    /* Second, right after GL_ARB_multitexture (extension-names.h). */
    assert(!strncmp(names, "GL_ARB_multitexture GL_EXT_compiled_vertex_array ", 49));
    assert(!strcmp(names, ExpectedExtensions(NEW, TRUE)));
    assert(wglGetProcAddress("glLockArraysEXT") == Marker &&
           wglGetProcAddress("glUnlockArraysEXT") == Marker);
    Log.clear();
    assert(Integer(LOCK_FIRST) == 0 && Integer(LOCK_COUNT) == 0);
    glUnlockArraysEXT();
    Rejected(c, GL_INVALID_OPERATION);
    for (std::pair<GLint, GLsizei> bad : {std::pair{-1, 4}, std::pair{0, 0}, std::pair{2, -1}}) {
        glLockArraysEXT(bad.first, bad.second);
        Rejected(c, GL_INVALID_VALUE);
    }
    glLockArraysEXT(3, 9);
    Quiet(c);
    assert(Integer(LOCK_FIRST) == 3 && Integer(LOCK_COUNT) == 9);
    GLboolean locked = GL_FALSE;
    glGetBooleanv(LOCK_COUNT, &locked);
    assert(locked == GL_TRUE);
    glLockArraysEXT(0, 1); /* already locked */
    Rejected(c, GL_INVALID_OPERATION);
    assert(Integer(LOCK_FIRST) == 3 && Integer(LOCK_COUNT) == 9);
    /* CLIENT_VERTEX_ARRAY_BIT saves the lock; the pixel store bit does not. */
    AliasPushClientAttrib(CLIENT_VERTEX_ARRAY_BIT);
    glUnlockArraysEXT();
    glLockArraysEXT(5, 6);
    AliasPopClientAttrib();
    assert(Integer(LOCK_FIRST) == 3 && Integer(LOCK_COUNT) == 9);
    AliasPushClientAttrib(1);
    glUnlockArraysEXT();
    AliasPopClientAttrib();
    assert(Integer(LOCK_COUNT) == 0);
    Quiet(c);
    /* Not between Begin and End, and never compiled into a list. */
    glBegin(GL_POINTS);
    Log.clear();
    c->Used = c->Records = 0;
    glLockArraysEXT(0, 2);
    assert(c->Error == GL_INVALID_OPERATION && !c->Records);
    c->Error = 0;
    glEnd();
    Clear(c);
    assert(JglListMode(GL_COMPILE));
    glLockArraysEXT(1, 2);
    Quiet(c);
    assert(Integer(LOCK_COUNT) == 2);
    glUnlockArraysEXT();
    Quiet(c);
    assert(JglListMode(0));
    assert(Integer(LOCK_COUNT) == 0);
    Close(c);
}

/* Games of 1997-2001 search a fixed-size copy of GL_EXTENSIONS (Quake III
 * 1.11: Q_strncpyz into 1024 bytes, 1023 characters). With every contract
 * negotiated, the names they probe lie wholly within that prefix. */
static void LegacyPrefix(void) {
    constexpr ULONG ALL =
        DG_CAP_GL_MULTITEXTURE | DG_CAP_GL_FIXED_STATE | DG_CAP_GL_TEXTURE_CONTROL |
        DG_CAP_GL_COMPRESSED_TEXTURES | DG_CAP_GL_FOG_COORD | DG_CAP_GL_TEXTURE_CUBE_MAP |
        DG_CAP_GL_FRAGMENT_PROGRAM | DG_CAP_GL_VERTEX_PROGRAM | DG_CAP_GL_TEXTURE_3D | VBO |
        DG_CAP_GL_FRAMEBUFFER_OBJECT | DG_CAP_GL_VERTEX_ARRAY_BGRA | DG_CAP_GL_OCCLUSION_QUERY |
        DG_CAP_GL_TEXTURE_RECTANGLE | DG_CAP_GL_TEXTURE_IMAGES | DG_CAP_GL_TEXTURE_BORDERS |
        DG_CAP_GL_BULK_READBACK | DG_CAP_GL_DEPTH_STENCIL_READBACK;
    static const char *const legacy[] = {"GL_ARB_multitexture",
                                         "GL_EXT_compiled_vertex_array",
                                         "GL_ARB_texture_env_add",
                                         "GL_EXT_texture_env_add",
                                         "GL_ARB_texture_env_combine",
                                         "GL_EXT_texture_env_combine",
                                         "GL_EXT_texture_filter_anisotropic",
                                         "GL_EXT_texture_edge_clamp",
                                         "GL_SGIS_texture_edge_clamp",
                                         "GL_ARB_point_parameters",
                                         "GL_EXT_point_parameters",
                                         "GL_ARB_texture_compression",
                                         "GL_EXT_texture_compression_s3tc",
                                         "GL_EXT_secondary_color",
                                         "GL_EXT_fog_coord",
                                         "GL_ARB_texture_cube_map"};
    JGL_CONTEXT *c = Open(ALL);
    const char *names = (const char *)glGetString(GL_EXTENSIONS);
    assert(!strcmp(names, ExpectedExtensions(ALL, TRUE)));
    /* Every model name exactly once, as a whole word. */
    for (const JGL_TEST_EXTENSION &e : ExtensionNames) {
        const char *at = strstr(names, e.Name);
        const size_t length = strlen(e.Name);
        assert(at && (at == names || at[-1] == ' ') && (at[length] == ' ' || !at[length]));
        assert(!strstr(at + length, e.Name));
    }
    char copy[1024];
    strncpy(copy, names, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = 0;
    for (const char *name : legacy) {
        const char *at = strstr(copy, name);
        const size_t length = strlen(name);
        assert(at && (at == copy || at[-1] == ' ') && (at[length] == ' ' || !at[length]));
        printf("  %-34s at %4zu..%4zu\n", name, (size_t)(at - copy), (size_t)(at - copy) + length);
    }
    printf("  all contracts: %zu characters; the legacy names end by %zu\n", strlen(names),
           (size_t)(strstr(names, "GL_ARB_texture_cube_map") - names) +
               strlen("GL_ARB_texture_cube_map"));
    Close(c);
}

/* A locked draw submits the unlocked draw's records; the arrays are read as
 * they are at each draw (the extension permits, never requires, caching). */
static void LockedDraws(void) {
    const Batch b = Quake(15, 20);
    const GLsizei count = (GLsizei)b.Indices.size();
    JGL_CONTEXT *c = Open(NEW);
    QuakeArrays(b);
    const Records unlocked = Recorded(
        c, [&] { glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, b.Indices.data()); });
    ExpectRanged(unlocked, 0, 300);
    std::vector<BYTE> colors(b.Colors.size(), 0x5a);
    const Records locked = Recorded(c, [&] {
        glLockArraysEXT(0, 300);
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, b.Indices.data());
        /* A later pass of the same lock with new colours. */
        glColorPointer(4, GL_UNSIGNED_BYTE, 0, colors.data());
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, b.Indices.data());
        glUnlockArraysEXT();
        glColorPointer(4, GL_UNSIGNED_BYTE, 0, b.Colors.data());
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, b.Indices.data());
    });
    assert(locked.size() == 3 && locked[0] == unlocked[0] && locked[2] == unlocked[0]);
    /* The second pass read the new colours, everything else unchanged. */
    const Drawn first = Draws({locked[0]}), second = Draws({locked[1]});
    assert(first.Vertices.size() == second.Vertices.size() && !(first == second));
    const unsigned *d = (const unsigned *)(locked[1].data() + 20);
    assert(d[1] == (GL_UNSIGNED_BYTE | 4u << 16));
    unsigned words[10], offsets[DG_GL_ATTRIB_SLOTS], at, length;
    for (ULONG i = 0; i < 10; ++i)
        words[i] = locked[1][10 + i];
    assert(DgRawBufferLayout(words, d, NULL, offsets, &at, &length));
    assert(!memcmp((const BYTE *)(locked[1].data() + 20) + offsets[1], colors.data(), 300 * 4));
    /* Data changed in place while locked is read again too. */
    std::vector<BYTE> positions = b.Positions;
    glLockArraysEXT(0, 300);
    glVertexPointer(3, GL_FLOAT, 16, positions.data());
    const Records before = Recorded(
        c, [&] { glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, b.Indices.data()); });
    positions[0] ^= 0x80;
    const Records after = Recorded(
        c, [&] { glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, b.Indices.data()); });
    assert(before == unlocked && !(after == before));
    glUnlockArraysEXT();
    Quiet(c);
    Close(c);
}

/* The Quake III batch and every array kind, stride, index type and mode
 * draw the old host's vertices from one buffer draw. */
static void Equivalence(void) {
    const Batch b = Quake(15, 20);
    const auto quake = Both([&] { QuakeArrays(b); }, GL_TRIANGLES, (GLsizei)b.Indices.size(),
                            GL_UNSIGNED_INT, b.Indices.data());
    ExpectRanged(quake.second, 0, 300);
    assert(quake.first.size() == 1 && quake.first[0][8] == FEnum_glDrawArraysEXT);
    /* 300 vertices of 32 bytes and 1596 indices instead of 1596 vertices. */
    assert(quake.second[0][9] == 128 + 300 * 32 + b.Indices.size() * 4);
    assert(quake.first[0][9] == 64 + b.Indices.size() * 32);

    /* Every conventional kind that converts or reorders (SHORT normals,
     * BGRA colours and secondary colours), DOUBLE coordinates, the fog
     * coordinate, colour index and edge flags, with odd strides, from a
     * nonzero first vertex, by UNSIGNED_BYTE and UNSIGNED_SHORT indices. */
    const std::vector<BYTE> memory = Noise(64 * 40);
    std::vector<BYTE> doubles;
    for (ULONG i = 0; i < 64 * 4; ++i) {
        const GLdouble value = (GLdouble)((int)Next() - 100) / 8.0;
        doubles.insert(doubles.end(), (const BYTE *)&value, (const BYTE *)&value + 8);
    }
    const std::vector<BYTE> positions = Floats(64 * 3);
    auto every = [&] {
        glVertexPointer(2, GL_FLOAT, 12, positions.data());
        glColorPointer(JGL_BGRA, GL_UNSIGNED_BYTE, 6, memory.data() + 1);
        glNormalPointer(GL_SHORT, 10, memory.data() + 3);
        glSecondaryColorPointerEXT(JGL_BGRA, GL_UNSIGNED_BYTE, 0, memory.data() + 7);
        glIndexPointer(GL_SHORT, 0, memory.data() + 11);
        glEdgeFlagPointer(3, memory.data() + 13);
        glFogCoordPointerEXT(GL_DOUBLE, 0, doubles.data() + 8);
        glTexCoordPointer(3, GL_DOUBLE, 32, doubles.data());
        glClientActiveTextureARB(TEXTURE0 + 3);
        glTexCoordPointer(4, GL_SHORT, 0, memory.data() + 17);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glClientActiveTextureARB(TEXTURE0);
        for (GLenum cap : std::initializer_list<GLenum>{
                 GL_VERTEX_ARRAY, GL_COLOR_ARRAY, GL_NORMAL_ARRAY, SECONDARY_ARRAY, GL_INDEX_ARRAY,
                 GL_EDGE_FLAG_ARRAY, FOG_ARRAY, GL_TEXTURE_COORD_ARRAY})
            glEnableClientState(cap);
    };
    std::vector<GLubyte> bytes;
    std::vector<GLushort> shorts;
    for (ULONG i = 0; i < 90; ++i) {
        bytes.push_back((GLubyte)(5 + (i * 7) % 11));
        shorts.push_back((GLushort)(9 + (i * 13) % 17));
    }
    for (GLenum mode : {GL_TRIANGLES, GL_TRIANGLE_STRIP, GL_TRIANGLE_FAN, GL_LINE_LOOP, GL_POLYGON,
                        GL_QUADS, GL_LINES, GL_POINTS}) {
        ExpectRanged(Both(every, mode, 90, GL_UNSIGNED_BYTE, bytes.data()).second, 5, 11);
        ExpectRanged(Both(every, mode, 90, GL_UNSIGNED_SHORT, shorts.data()).second, 9, 17);
    }
    /* Generic attributes (normalized, BGRA) with and without the position
     * array; generic attribute 0 provokes. */
    auto generics = [&](bool position) {
        if (position) {
            glVertexPointer(4, GL_FLOAT, 0, positions.data());
            glEnableClientState(GL_VERTEX_ARRAY);
        } else {
            glVertexAttribPointerARB(0, 3, GL_FLOAT, GL_FALSE, 20, positions.data());
            glEnableVertexAttribArrayARB(0);
        }
        glVertexAttribPointerARB(3, 2, GL_SHORT, GL_TRUE, 6, memory.data() + 2);
        glVertexAttribPointerARB(7, JGL_BGRA, GL_UNSIGNED_BYTE, GL_TRUE, 0, memory.data() + 5);
        glVertexAttribPointerARB(15, 1, GL_DOUBLE, GL_FALSE, 0, doubles.data());
        for (GLuint n : {3u, 7u, 15u})
            glEnableVertexAttribArrayARB(n);
    };
    std::vector<GLuint> ints(shorts.begin(), shorts.end());
    for (bool position : {true, false})
        ExpectRanged(
            Both([&] { generics(position); }, GL_TRIANGLES, 90, GL_UNSIGNED_INT, ints.data())
                .second,
            9, 17);
}

/* What the buffer draw cannot carry, or carries larger, keeps the old
 * records exactly, and so do every error and display list compilation. */
static void Fallbacks(void) {
    const std::vector<BYTE> positions = Floats(70001 * 2);
    auto arrays = [&] {
        glVertexPointer(2, GL_FLOAT, 0, positions.data());
        glEnableClientState(GL_VERTEX_ARRAY);
    };
    /* Sparse indices: the expansion is smaller. Indices beyond
     * DG_GL_MAX_VERTICES apart: the client section cannot hold them. */
    const GLushort sparse[4] = {0, 7, 900, 3};
    const GLuint far[3] = {0, 70000, 1};
    for (const auto &both : {Both(arrays, GL_LINE_STRIP, 4, GL_UNSIGNED_SHORT, sparse),
                             Both(arrays, GL_TRIANGLES, 3, GL_UNSIGNED_INT, far)})
        assert(both.first == both.second);
    /* More indices than one record carries. */
    std::vector<GLubyte> many(DG_GL_MAX_INDICES + 1);
    for (size_t i = 0; i < many.size(); ++i)
        many[i] = (GLubyte)(i % 3);
    {
        const auto both =
            Both(arrays, GL_TRIANGLES, (GLsizei)many.size(), GL_UNSIGNED_BYTE, many.data());
        assert(both.first == both.second && both.first.size() > 1);
    }
    /* Errors stay the frontend's, before anything is submitted. */
    JGL_CONTEXT *c = Open(NEW);
    arrays();
    const GLuint indices[3] = {0, 1, 2};
    glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, NULL);
    Rejected(c, GL_INVALID_VALUE);
    glVertexPointer(2, GL_FLOAT, 0, (const void *)(~(ULONG_PTR)0 - 15));
    glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, indices); /* beyond the address space */
    Rejected(c, GL_INVALID_VALUE);
    arrays();
    glDrawElements(GL_TRIANGLES, 3, GL_FLOAT, indices);
    Rejected(c, GL_INVALID_ENUM);
    glDrawElements(GL_POLYGON + 1, 3, GL_UNSIGNED_INT, indices);
    Rejected(c, GL_INVALID_ENUM);
    glDrawElements(GL_TRIANGLES, -1, GL_UNSIGNED_INT, indices);
    Rejected(c, GL_INVALID_VALUE);
    glBegin(GL_TRIANGLES);
    Log.clear();
    c->Used = c->Records = 0;
    glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_INT, indices);
    assert(c->Error == GL_INVALID_OPERATION && !c->Records);
    c->Error = 0;
    glEnd();
    Clear(c);
    Close(c);
    /* Display lists capture the expansion, compiled or compiled and run. */
    const Batch b = Quake(4, 3);
    for (ULONG mode : {GL_COMPILE, GL_COMPILE_AND_EXECUTE}) {
        Records compiled[2];
        for (ULONG caps : {OLD, NEW}) {
            JGL_CONTEXT *context = Open(caps);
            QuakeArrays(b);
            assert(JglListMode(mode));
            compiled[caps == NEW] = Recorded(context, [&] {
                glDrawElements(GL_TRIANGLES, (GLsizei)b.Indices.size(), GL_UNSIGNED_INT,
                               b.Indices.data());
            });
            assert(JglListMode(0));
            Close(context);
        }
        assert(compiled[0] == compiled[1] && compiled[1].size() == 1 &&
               compiled[1][0][8] == FEnum_glDrawArraysEXT);
    }
}

/* ArrayElement's records: attributes in their established order, the vertex
 * last, FLOAT bits kept and UNSIGNED_BYTE colours normalized. */
static ULONG Bits(GLfloat value) {
    ULONG bits;
    memcpy(&bits, &value, 4);
    return bits;
}
static void Elements(void) {
    const Batch b = Quake(4, 3);
    JGL_CONTEXT *c = Open(NEW);
    QuakeArrays(b);
    const Records records = Recorded(c, [&] {
        glBegin(GL_TRIANGLES);
        AliasArrayElement(5);
        glEnd();
    });
    assert(records.size() == 6);
    const BYTE *color = b.Colors.data() + 20;
    const GLfloat *st = (const GLfloat *)(b.Texcoords.data() + 40);
    const GLfloat *lightmap = (const GLfloat *)(b.Lightmaps.data() + 60);
    const GLfloat *xyz = (const GLfloat *)(b.Positions.data() + 80);
    auto call = [&](const std::vector<ULONG> &r, ULONG function, std::vector<ULONG> words) {
        assert(r[0] == DG_GL_CALL && r[8] == function && r.size() == 9 + words.size());
        for (ULONG i = 0; i < words.size(); ++i)
            assert(r[9 + i] == words[i]);
    };
    call(records[1], FEnum_glColor4f,
         {Bits(color[0] / 255.0f), Bits(color[1] / 255.0f), Bits(color[2] / 255.0f),
          Bits(color[3] / 255.0f)});
    call(records[2], FEnum_glTexCoord4f, {Bits(st[0]), Bits(st[1]), 0, Bits(1.0f)});
    call(records[3], FEnum_glMultiTexCoord4f,
         {TEXTURE0 + 1, Bits(lightmap[0]), Bits(lightmap[1]), 0, Bits(1.0f)});
    call(records[4], FEnum_glVertex4f, {Bits(xyz[0]), Bits(xyz[1]), Bits(xyz[2]), Bits(1.0f)});
    Close(c);
}

/* Inline indices are copied as scanned: an index the application changes
 * between the scan and the copy is held within the scanned range, so the host
 * never receives one outside the client vertices. */
static void Clamped(void) {
    const GLubyte bytes[4] = {4, 9, 2, 7};
    const GLushort shorts[3] = {65535, 5, 0};
    const GLuint ints[3] = {0xffffffffu, 6, 3};
    BYTE out[16] = {};
    CopyIndices(out, bytes, 1, 4, 3, 8);
    assert(!memcmp(out, "\x04\x08\x03\x07", 4));
    GLushort s[3];
    CopyIndices(out, (const BYTE *)shorts, 2, 3, 4, 6);
    memcpy(s, out, 6);
    assert(s[0] == 6 && s[1] == 5 && s[2] == 4);
    GLuint i[3];
    CopyIndices(out, (const BYTE *)ints, 4, 3, 3, 70000);
    memcpy(i, out, 12);
    assert(i[0] == 70000 && i[1] == 6 && i[2] == 3);
}

/* Records, bytes and frontend time per Quake III-like batch along the old
 * paths (ArrayElement strips, the per-index expansion) and the buffer draw. */
static void Bench(void) {
    const Batch b = Quake(15, 20);
    const GLsizei count = (GLsizei)b.Indices.size();
    struct Path {
        const char *Name;
        ULONG Caps;
        bool Strips;
    } paths[] = {{"ArrayElement strips (today's Quake III)", NEW, true},
                 {"DrawElements, old host (expansion)", OLD, false},
                 {"LockArrays + DrawElements (buffer draw)", NEW, false}};
    printf("Quake III-like batch: 300 vertices (XYZ stride 16, ST, lightmap ST, RGBA8), %d "
           "UNSIGNED_INT indices\n",
           count);
    for (const Path &p : paths) {
        JGL_CONTEXT *c = Open(p.Caps);
        QuakeArrays(b);
        auto draw = [&] {
            if (p.Strips) {
                Strips(b.Indices);
            } else if (p.Caps & VBO) {
                glLockArraysEXT(0, 300);
                glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, b.Indices.data());
                glUnlockArraysEXT();
            } else {
                glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, b.Indices.data());
            }
        };
        const Records records = Recorded(c, draw);
        c->Error = 0;
        SubmittedRecord = NULL;
        const ULONG rounds = 2000;
        const auto start = std::chrono::steady_clock::now();
        for (ULONG i = 0; i < rounds; ++i) {
            draw();
            c->Used = c->Records = 0; /* the packet, not the transport */
        }
        const double us =
            std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start)
                .count() /
            rounds;
        c->Error = 0;
        printf("  %-42s %5zu records %7zu bytes %8.1f us/batch (host CPU)\n", p.Name,
               records.size(), Bytes(records), us);
        Close(c);
    }
}

int main(int argc, char **argv) {
    (void)AliasInterleavedArrays;
    if (argc > 1 && !strcmp(argv[1], "bench")) {
        Bench();
        return 0;
    }
    Unnegotiated();
    LockState();
    LegacyPrefix();
    LockedDraws();
    Equivalence();
    Fallbacks();
    Elements();
    Clamped();
    puts("PASS compiled vertex array frontend");
    return 0;
}
