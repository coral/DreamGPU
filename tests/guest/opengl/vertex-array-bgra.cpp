/* SPDX-License-Identifier: GPL-2.0-or-later
 * DG_CAP_GL_VERTEX_ARRAY_BGRA through the actual frontend on the strict
 * transport fixture: negotiation (alone, after every other capability, and the
 * unchanged frontend without it), the ARB_vertex_array_bgra errors (nothing a
 * rejected pointer call changes or sends), BGRA in every size query and the
 * client attribute stack, client arrays reordered into exactly the RGBA records
 * of the same data in RGBA order (DrawArrays, elements, DrawArraysEXT, the
 * attribute draw, a mixed buffer draw, display lists, strides), buffer slots in
 * the BGRA buffer draw (colour, secondary colour and generic attributes, with
 * RGBA slots beside them, array and element draws) and its client-draw
 * fallbacks, and ArrayElement from client memory and from buffers. Every DATA
 * record is admitted by the NT validator, which keeps refusing BGRA
 * descriptors in the buffer draw. */
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
#include "extension-names.h"
#include "dg-gl-validate.h"

constexpr GLenum ARRAY_BUFFER = 0x8892, ELEMENT_BUFFER = 0x8893, STATIC_DRAW = 0x88e4;
constexpr GLenum SECONDARY_ARRAY = 0x845e, SECONDARY_SIZE = 0x845a, SECONDARY_TYPE = 0x845b;
constexpr GLenum ATTRIB_SIZE = 0x8623, ATTRIB_TYPE = 0x8625, ATTRIB_NORMALIZED = 0x886a;
constexpr GLenum C4UB_V3F = 0x2a23, TEXTURE0 = 0x84c0;
constexpr GLbitfield CLIENT_VERTEX_ARRAY_BIT = 2;
constexpr ULONG BGRA = DG_CAP_GL_VERTEX_ARRAY_BGRA, VBO = DG_CAP_GL_VERTEX_BUFFER_OBJECT;
constexpr ULONG ALL_BUT_BGRA = DG_CAP_GL_MULTITEXTURE | DG_CAP_GL_FIXED_STATE |
                               DG_CAP_GL_TEXTURE_CONTROL | DG_CAP_GL_COMPRESSED_TEXTURES |
                               DG_CAP_GL_FOG_COORD | DG_CAP_GL_TEXTURE_CUBE_MAP |
                               DG_CAP_GL_FRAGMENT_PROGRAM | DG_CAP_GL_VERTEX_PROGRAM |
                               DG_CAP_GL_TEXTURE_3D | VBO | DG_CAP_GL_FRAMEBUFFER_OBJECT;
constexpr ULONG DRAWS =
    BGRA | VBO | DG_CAP_GL_VERTEX_PROGRAM | DG_CAP_GL_MULTITEXTURE | DG_CAP_GL_FOG_COORD;

/* The NT kernel admits each DATA record exactly as captured. */
static ULONG KernelWords(void *, ULONG function) {
    switch (function) {
        case FEnum_glDrawArrays:
        case FEnum_glDrawArraysEXT:
            return DG_GL_FUNCTION_INLINE_DATA | 4;
        case FEnum_glVertexAttribPointerARB:
            return DG_GL_FUNCTION_INLINE_DATA | 5;
        case FEnum_glDrawRangeElements:
        case FEnum_glDrawRangeElementsEXT:
            return DG_GL_FUNCTION_INLINE_DATA | 10;
        case FEnum_glBufferDataARB:
            return DG_GL_FUNCTION_INLINE_DATA | 3;
        case FEnum_glBufferSubDataARB:
            return DG_GL_FUNCTION_INLINE_DATA | 2;
        default:
            return (ULONG)-1;
    }
}
static BOOL Admits(const std::vector<ULONG> &r) {
    std::vector<ULONG> copy(r);
    const DG_GL_LIMITS limits = {7, DG_ESCAPE_MAX_BYTES, DG_GL_MAX_RECORDS, 1};
    return DgValidateUserGl(copy.data(), copy.size() * 4, 42, 7, &limits, KernelWords, NULL);
}

typedef std::vector<std::vector<ULONG>> Records;
static Records Log;
static void Capture(const ULONG *record) {
    Log.emplace_back(record, record + record[1] / 4);
    if (record[0] == DG_GL_DATA_CALL) {
        assert(Admits(Log.back()));
        /* Older kernels' buffer draw never admits a BGRA descriptor. */
        if (record[8] == FEnum_glDrawRangeElementsEXT) {
            std::vector<ULONG> older(Log.back());
            older[8] = FEnum_glDrawRangeElements;
            assert(!Admits(older));
        }
    }
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
static JGL_CONTEXT *Open(ULONG caps) {
    Reset();
    HostCaps = caps;
    HostMaxBytes = DG_ESCAPE_MAX_BYTES;
    HostSecondaryWords = 3;
    SubmittedRecord = Capture;
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
static GLint Attrib(GLuint index, GLenum pname) {
    GLint value = 0x5a5a;
    glGetVertexAttribivARB(index, pname, &value);
    return value;
}
static const void *Offset(ULONG_PTR offset) {
    return (const void *)offset;
}
template <typename F> static Records Recorded(JGL_CONTEXT *c, F draw) {
    Log.clear();
    draw();
    assert(!c->Error);
    Submit(c);
    Records out;
    out.swap(Log);
    return out;
}
static ULONG D(GLenum type, ULONG count) {
    return type | count << 16;
}

/* Interleaved vertices: a position, a BGRA colour, a BGRA specular and a pad
 * word, so each array has stride 20. Every byte differs; red and blue never
 * match. */
struct Vertex {
    GLfloat x, y;
    GLubyte color[4], specular[4], pad[4];
};
static_assert(sizeof(Vertex) == 20);
static const Vertex Vertices[6] = {
    {0, 0, {0x10, 0x20, 0x30, 0x40}, {0x11, 0x21, 0x31, 0x41}, {1, 1, 1, 1}},
    {1, 0, {0x50, 0x60, 0x70, 0x80}, {0x51, 0x61, 0x71, 0x81}, {2, 2, 2, 2}},
    {0, 1, {0x90, 0xa0, 0xb0, 0xc0}, {0x91, 0xa1, 0xb1, 0xc1}, {3, 3, 3, 3}},
    {1, 1, {0xd0, 0xe0, 0xf0, 0x05}, {0xd1, 0xe1, 0xf1, 0x06}, {4, 4, 4, 4}},
    {2, 2, {0x01, 0x02, 0x03, 0x04}, {0x07, 0x08, 0x09, 0x0a}, {5, 5, 5, 5}},
    {3, 3, {0xff, 0x00, 0x7f, 0x80}, {0x00, 0xff, 0x80, 0x7f}, {6, 6, 6, 6}}};
/* The same colours in RGBA order, and the specular colours as RGB. */
struct Reordered {
    GLubyte color[6][4], specular[6][3];
};
static Reordered Rgba(void) {
    Reordered r;
    for (ULONG v = 0; v < 6; ++v) {
        const GLubyte *c = Vertices[v].color, *s = Vertices[v].specular;
        const GLubyte color[4] = {c[2], c[1], c[0], c[3]}, specular[3] = {s[2], s[1], s[0]};
        memcpy(r.color[v], color, 4);
        memcpy(r.specular[v], specular, 3);
    }
    return r;
}
static const Reordered RGBA = Rgba();
static const BYTE *Field(ULONG offset) {
    return (const BYTE *)Vertices + offset;
}

/* Without the capability BGRA stays an invalid size and nothing changes. */
static void Unnegotiated(void) {
    JGL_CONTEXT *c = Open(ALL_BUT_BGRA);
    assert(!JglVertexArrayBgraAvailable());
    assert(!strstr((const char *)glGetString(GL_EXTENSIONS), "bgra"));
    glColorPointer(4, GL_UNSIGNED_BYTE, 20, Field(8));
    glSecondaryColorPointerEXT(3, GL_UNSIGNED_BYTE, 20, Field(12));
    glVertexAttribPointerARB(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, 20, Field(8));
    Quiet(c);
    glColorPointer(JGL_BGRA, GL_UNSIGNED_BYTE, 20, Field(8));
    Rejected(c, GL_INVALID_VALUE);
    glSecondaryColorPointerEXT(JGL_BGRA, GL_UNSIGNED_BYTE, 20, Field(12));
    Rejected(c, GL_INVALID_VALUE);
    glVertexAttribPointerARB(3, JGL_BGRA, GL_UNSIGNED_BYTE, GL_TRUE, 20, Field(8));
    Rejected(c, GL_INVALID_VALUE);
    assert(Integer(GL_COLOR_ARRAY_SIZE) == 4 && Integer(SECONDARY_SIZE) == 3 &&
           Attrib(3, ATTRIB_SIZE) == 4);
    Close(c);
}

static void Negotiation(void) {
    JGL_CONTEXT *c = Open(BGRA);
    assert(JglVertexArrayBgraAvailable() && !JglVertexBufferObjectAvailable());
    assert(!strcmp((const char *)glGetString(GL_EXTENSIONS),
                   "GL_EXT_secondary_color GL_EXT_separate_specular_color "
                   "GL_ARB_vertex_array_bgra"));
    /* Colours and secondary colours from client memory need nothing else. */
    glColorPointer(JGL_BGRA, GL_UNSIGNED_BYTE, 20, Field(8));
    glSecondaryColorPointerEXT(JGL_BGRA, GL_UNSIGNED_BYTE, 20, Field(12));
    Quiet(c);
    /* Generic attributes need ARB_vertex_program. */
    glVertexAttribPointerARB(3, JGL_BGRA, GL_UNSIGNED_BYTE, GL_TRUE, 20, Field(8));
    Rejected(c, GL_INVALID_OPERATION);
    Close(c);
    Reset();
    HostCaps = ALL_BUT_BGRA | BGRA;
    HostSecondaryWords = 3;
    HGLRC context = Create();
    const char *all = (const char *)glGetString(GL_EXTENSIONS);
    /* Every name in its place and under its gate (extension-names.h). */
    assert(!strcmp(all, ExpectedExtensions(HostCaps, TRUE)));
    assert(wglMakeCurrent(NULL, NULL) && wglDeleteContext(context));
}

/* ARB_vertex_array_bgra's errors; a rejected call keeps the previous array. */
static void Errors(void) {
    JGL_CONTEXT *c = Open(DRAWS);
    glColorPointer(3, GL_FLOAT, 12, Field(0));
    glSecondaryColorPointerEXT(3, GL_SHORT, 6, Field(0));
    glVertexAttribPointerARB(2, 2, GL_SHORT, GL_FALSE, 4, Field(0));
    Quiet(c);
    /* Other types of a colour array: INVALID_OPERATION; no type: INVALID_ENUM. */
    for (GLenum type :
         {GL_FLOAT, GL_SHORT, GL_BYTE, GL_UNSIGNED_SHORT, GL_INT, GL_UNSIGNED_INT, GL_DOUBLE}) {
        glColorPointer(JGL_BGRA, type, 20, Field(8));
        Rejected(c, GL_INVALID_OPERATION);
        glSecondaryColorPointerEXT(JGL_BGRA, type, 20, Field(12));
        Rejected(c, GL_INVALID_OPERATION);
        glVertexAttribPointerARB(2, JGL_BGRA, type, GL_TRUE, 20, Field(8));
        Rejected(c, GL_INVALID_OPERATION);
    }
    glColorPointer(JGL_BGRA, 0x1234, 20, Field(8));
    Rejected(c, GL_INVALID_ENUM);
    glVertexAttribPointerARB(2, JGL_BGRA, 0x1234, GL_TRUE, 20, Field(8));
    Rejected(c, GL_INVALID_ENUM);
    /* A generic BGRA array must be normalized. */
    glVertexAttribPointerARB(2, JGL_BGRA, GL_UNSIGNED_BYTE, GL_FALSE, 20, Field(8));
    Rejected(c, GL_INVALID_OPERATION);
    /* Every other array kind keeps BGRA an invalid size, as is a negative stride. */
    glVertexPointer(JGL_BGRA, GL_FLOAT, 0, Field(0));
    Rejected(c, GL_INVALID_VALUE);
    glTexCoordPointer(JGL_BGRA, GL_FLOAT, 0, Field(0));
    Rejected(c, GL_INVALID_VALUE);
    glClientActiveTextureARB(TEXTURE0 + 2);
    glTexCoordPointer(JGL_BGRA, GL_FLOAT, 0, Field(0));
    Rejected(c, GL_INVALID_VALUE);
    glClientActiveTextureARB(TEXTURE0);
    glColorPointer(JGL_BGRA, GL_UNSIGNED_BYTE, -1, Field(8));
    Rejected(c, GL_INVALID_VALUE);
    glVertexAttribPointerARB(2, JGL_BGRA, GL_UNSIGNED_BYTE, GL_TRUE, -4, Field(8));
    Rejected(c, GL_INVALID_VALUE);
    glVertexAttribPointerARB(DG_GL_MAX_VERTEX_ATTRIBS, JGL_BGRA, GL_UNSIGNED_BYTE, GL_TRUE, 0,
                             Field(8));
    Rejected(c, GL_INVALID_VALUE);
    assert(Integer(GL_COLOR_ARRAY_SIZE) == 3 && Integer(GL_COLOR_ARRAY_TYPE) == GL_FLOAT &&
           Integer(GL_COLOR_ARRAY_STRIDE) == 12);
    assert(Integer(SECONDARY_SIZE) == 3 && Integer(SECONDARY_TYPE) == GL_SHORT);
    assert(Attrib(2, ATTRIB_SIZE) == 2 && Attrib(2, ATTRIB_TYPE) == GL_SHORT &&
           !Attrib(2, ATTRIB_NORMALIZED));
    Quiet(c);
    Close(c);
}

/* Queries answer BGRA as the size; the client stack and InterleavedArrays. */
static void Queries(void) {
    JGL_CONTEXT *c = Open(DRAWS);
    glColorPointer(JGL_BGRA, GL_UNSIGNED_BYTE, 20, Field(8));
    glSecondaryColorPointerEXT(JGL_BGRA, GL_UNSIGNED_BYTE, 20, Field(12));
    glVertexAttribPointerARB(6, JGL_BGRA, GL_UNSIGNED_BYTE, GL_TRUE, 20, Field(8));
    Quiet(c);
    assert(Integer(GL_COLOR_ARRAY_SIZE) == JGL_BGRA &&
           Integer(GL_COLOR_ARRAY_TYPE) == GL_UNSIGNED_BYTE &&
           Integer(GL_COLOR_ARRAY_STRIDE) == 20);
    assert(Integer(SECONDARY_SIZE) == JGL_BGRA && Integer(SECONDARY_TYPE) == GL_UNSIGNED_BYTE);
    assert(Attrib(6, ATTRIB_SIZE) == JGL_BGRA && Attrib(6, ATTRIB_TYPE) == GL_UNSIGNED_BYTE &&
           Attrib(6, ATTRIB_NORMALIZED) == 1);
    GLfloat f = 0;
    GLdouble d = 0;
    GLfloat fv[4] = {};
    glGetFloatv(GL_COLOR_ARRAY_SIZE, &f);
    glGetDoublev(SECONDARY_SIZE, &d);
    glGetVertexAttribfvARB(6, ATTRIB_SIZE, fv);
    assert(f == (GLfloat)JGL_BGRA && d == JGL_BGRA && fv[0] == (GLfloat)JGL_BGRA);
    void *pointer = NULL;
    glGetPointerv(GL_COLOR_ARRAY_POINTER, &pointer);
    assert(pointer == Field(8));
    glGetVertexAttribPointervARB(6, 0x8645, &pointer);
    assert(pointer == Field(8));
    /* CLIENT_VERTEX_ARRAY_BIT saves BGRA; a later pointer call clears it. */
    AliasPushClientAttrib(CLIENT_VERTEX_ARRAY_BIT);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, Field(8));
    glSecondaryColorPointerEXT(3, GL_UNSIGNED_BYTE, 0, Field(12));
    glVertexAttribPointerARB(6, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, Field(8));
    assert(Integer(GL_COLOR_ARRAY_SIZE) == 4 && Integer(SECONDARY_SIZE) == 3 &&
           Attrib(6, ATTRIB_SIZE) == 4);
    AliasPopClientAttrib();
    assert(Integer(GL_COLOR_ARRAY_SIZE) == JGL_BGRA && Integer(SECONDARY_SIZE) == JGL_BGRA &&
           Attrib(6, ATTRIB_SIZE) == JGL_BGRA);
    /* InterleavedArrays defines an ordinary colour array. */
    AliasInterleavedArrays(C4UB_V3F, 0, Field(0));
    assert(Integer(GL_COLOR_ARRAY_SIZE) == 4 && Integer(SECONDARY_SIZE) == JGL_BGRA);
    Quiet(c);
    Close(c);
}

/* Client memory: every record equals the one the RGBA data gives. */
static void ClientArrays(void) {
    JGL_CONTEXT *c = Open(DRAWS);
    const GLubyte indices[5] = {5, 0, 3, 3, 1};
    const GLushort fan[6] = {0, 1, 2, 3, 4, 5};
    glVertexPointer(2, GL_FLOAT, 20, Field(0));
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(SECONDARY_ARRAY);
    auto draws = [&] {
        glDrawArrays(GL_TRIANGLES, 2, 3);
        glDrawElements(GL_LINES, 4, GL_UNSIGNED_BYTE, indices);
        glDrawElements(GL_TRIANGLE_FAN, 6, GL_UNSIGNED_SHORT, fan);
        glClientActiveTextureARB(TEXTURE0 + 1);
        glTexCoordPointer(2, GL_FLOAT, 20, Field(0));
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glDrawArrays(GL_POINTS, 0, 6);
        glEnableVertexAttribArrayARB(5);
        glDrawElements(GL_POINTS, 5, GL_UNSIGNED_BYTE, indices);
        glDisableVertexAttribArrayARB(5);
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        glClientActiveTextureARB(TEXTURE0);
    };
    glColorPointer(JGL_BGRA, GL_UNSIGNED_BYTE, 20, Field(8));
    glSecondaryColorPointerEXT(JGL_BGRA, GL_UNSIGNED_BYTE, 20, Field(12));
    glVertexAttribPointerARB(5, JGL_BGRA, GL_UNSIGNED_BYTE, GL_TRUE, 20, Field(12));
    const Records bgra = Recorded(c, draws);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, RGBA.color);
    glSecondaryColorPointerEXT(3, GL_UNSIGNED_BYTE, 0, RGBA.specular);
    glVertexAttribPointerARB(5, 3, GL_UNSIGNED_BYTE, GL_TRUE, 3, RGBA.specular);
    const Records rgba = Recorded(c, [&] {
        /* The generic BGRA array keeps its alpha: compare it as four bytes. */
        GLubyte generic[6][4];
        for (ULONG v = 0; v < 6; ++v) {
            const GLubyte *s = Vertices[v].specular;
            const GLubyte value[4] = {s[2], s[1], s[0], s[3]};
            memcpy(generic[v], value, 4);
        }
        glVertexAttribPointerARB(5, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, generic);
        draws();
    });
    assert(bgra.size() == 5 && bgra == rgba);
    assert(bgra[0][8] == FEnum_glDrawArrays && bgra[3][8] == FEnum_glDrawArraysEXT &&
           bgra[4][8] == FEnum_glVertexAttribPointerARB);
    /* The seven-slot record: RGBA colour and RGB secondary colour sections. */
    const std::vector<ULONG> &r = bgra[0];
    const BYTE *payload = (const BYTE *)(r.data() + 14);
    assert(r[13] ==
           (DG_GL_ARRAY_RAW | DG_GL_ARRAY_POSITION | DG_GL_ARRAY_COLOR | DG_GL_ARRAY_SECONDARY));
    ULONG descriptors[7];
    memcpy(descriptors, payload, 28);
    assert(descriptors[1] == D(GL_UNSIGNED_BYTE, 4) && descriptors[4] == D(GL_UNSIGNED_BYTE, 3));
    assert(!memcmp(payload + 28 + 24, RGBA.color[2], 12));
    assert(!memcmp(payload + 28 + 24 + 12, RGBA.specular[2], 9));
    /* The attribute draw's generic descriptor is an ordinary normalized one. */
    const BYTE *attribute = (const BYTE *)(bgra[4].data() + 15);
    ULONG generic;
    memcpy(&generic, attribute + 4 * (16 + 5), 4);
    assert(generic == (D(GL_UNSIGNED_BYTE, 4) | DG_GL_ATTRIB_NORMALIZED));

    /* Compiled into a display list, the same reordered records. */
    glColorPointer(JGL_BGRA, GL_UNSIGNED_BYTE, 20, Field(8));
    glSecondaryColorPointerEXT(JGL_BGRA, GL_UNSIGNED_BYTE, 20, Field(12));
    glVertexAttribPointerARB(5, JGL_BGRA, GL_UNSIGNED_BYTE, GL_TRUE, 20, Field(12));
    assert(JglListMode(GL_COMPILE));
    Clear(c);
    const Records compiled = Recorded(c, draws);
    assert(JglListMode(0));
    assert(compiled == bgra);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(SECONDARY_ARRAY);
    Close(c);
}

/* The BGRA buffer draw: descriptors, references, client sections in order. */
typedef std::array<ULONG, 10> Words;
struct Reference {
    ULONG Slot, Descriptor, Buffer, Offset, Stride;
};
struct Section {
    ULONG Slot, Descriptor;
    std::vector<BYTE> Bytes;
};
static std::vector<BYTE> BufferPayload(const Words &a, const std::vector<Reference> &refs,
                                       const std::vector<Section> &sections, int bgra) {
    unsigned words[10], descriptors[DG_GL_ATTRIB_SLOTS] = {},
                        references[3 * DG_GL_ATTRIB_SLOTS] = {}, offsets[DG_GL_ATTRIB_SLOTS],
                        index_at = 0, length = 0, n = 0;
    for (ULONG i = 0; i < 10; ++i)
        words[i] = a[i];
    for (const Reference &r : refs) {
        descriptors[r.Slot] = r.Descriptor;
        references[n++] = r.Buffer;
        references[n++] = r.Offset;
        references[n++] = r.Stride;
    }
    for (const Section &s : sections)
        descriptors[s.Slot] = s.Descriptor;
    /* A layout with a BGRA slot is never a buffer draw. */
    assert(!bgra ||
           !DgRawBufferLayout(words, descriptors, references, offsets, &index_at, &length));
    assert(DgRawBufferLayoutOf(words, descriptors, references, offsets, &index_at, &length, bgra));
    std::vector<BYTE> out(length, 0);
    memcpy(out.data(), descriptors, sizeof(descriptors));
    memcpy(out.data() + sizeof(descriptors), references, n * 4);
    for (const Section &s : sections) {
        assert(offsets[s.Slot] &&
               s.Bytes.size() == a[6] * ((s.Descriptor >> 16) & 255) *
                                     DgArrayComponentBytes(s.Descriptor & 65535));
        memcpy(&out[offsets[s.Slot]], s.Bytes.data(), s.Bytes.size());
    }
    return out;
}
static void ExpectBufferDraw(const std::vector<ULONG> &r, ULONG function, const Words &a,
                             const std::vector<BYTE> &payload) {
    assert(r[0] == DG_GL_DATA_CALL && r[8] == function);
    assert(r[9] == payload.size() && r[1] == 20 * 4 + payload.size());
    for (ULONG i = 0; i < 10; ++i)
        assert(r[10 + i] == a[i]);
    assert(!memcmp(r.data() + 20, payload.data(), payload.size()));
}
static void Upload(JGL_CONTEXT *c, GLenum target, GLuint name, const void *bytes, ULONG size) {
    glBindBufferARB(target, name);
    glBufferDataARB(target, (JGL_SIZEIPTR)size, bytes, STATIC_DRAW);
    assert(!c->Error);
    Submit(c);
    Log.clear();
}

static void Buffers(void) {
    JGL_CONTEXT *c = Open(DRAWS);
    const ULONG float2 = D(GL_FLOAT, 2), bgra = D(GL_UNSIGNED_BYTE, 4) | DG_GL_ARRAY_BGRA;
    const ULONG pcs =
        DG_GL_ARRAY_RAW | DG_GL_ARRAY_POSITION | DG_GL_ARRAY_COLOR | DG_GL_ARRAY_SECONDARY;
    Upload(c, ARRAY_BUFFER, 1, Vertices, sizeof(Vertices));
    glVertexPointer(2, GL_FLOAT, 20, Offset(0));
    glColorPointer(JGL_BGRA, GL_UNSIGNED_BYTE, 20, Offset(8));
    glSecondaryColorPointerEXT(JGL_BGRA, GL_UNSIGNED_BYTE, 20, Offset(12));
    glBindBufferARB(ARRAY_BUFFER, 0);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(SECONDARY_ARRAY);
    Quiet(c);
    /* Buffer slots carry BGRA in the BGRA buffer draw, secondary colour with
     * four components; nothing but references travels. */
    const std::vector<Reference> refs = {
        {0, float2, 1, 0, 20}, {1, bgra, 1, 8, 20}, {4, bgra, 1, 12, 20}};
    glDrawArrays(GL_TRIANGLES, 1, 3);
    glDrawArrays(GL_POINTS, 0, 6); /* the buffer exactly */
    Submit(c);
    assert(Log.size() == 2);
    Words a = {GL_TRIANGLES, 1, 3, pcs, 0, 0x13, 0, 0, 0, 0};
    ExpectBufferDraw(Log[0], FEnum_glDrawRangeElementsEXT, a, BufferPayload(a, refs, {}, 1));
    a = {GL_POINTS, 0, 6, pcs, 0, 0x13, 0, 0, 0, 0};
    ExpectBufferDraw(Log[1], FEnum_glDrawRangeElementsEXT, a, BufferPayload(a, refs, {}, 1));
    Log.clear();
    /* One vertex beyond the buffer: no slot is read, nothing provokes a
     * vertex, nothing is drawn and there is no error. */
    glDrawArrays(GL_POINTS, 1, 6);
    Quiet(c);
    /* A BGRA colour buffer one byte short of vertex 5's colour is never read
     * (its current value applies); the BGRA secondary colour keeps the BGRA
     * buffer draw. Both short, no BGRA slot is left: the plain buffer draw. */
    Upload(c, ARRAY_BUFFER, 3, Vertices, 5 * 20 + 11);
    glColorPointer(JGL_BGRA, GL_UNSIGNED_BYTE, 20, Offset(8));
    glDrawArrays(GL_TRIANGLES, 1, 5);
    glSecondaryColorPointerEXT(JGL_BGRA, GL_UNSIGNED_BYTE, 20, Offset(12));
    glDrawArrays(GL_TRIANGLES, 1, 5);
    glDrawArrays(GL_TRIANGLES, 1, 3); /* both colours read */
    Submit(c);
    assert(!c->Error && Log.size() == 3);
    a = {GL_TRIANGLES, 1, 5, pcs & ~DG_GL_ARRAY_COLOR, 0, 0x11, 0, 0, 0, 0};
    ExpectBufferDraw(Log[0], FEnum_glDrawRangeElementsEXT, a,
                     BufferPayload(a, {{0, float2, 1, 0, 20}, {4, bgra, 1, 12, 20}}, {}, 1));
    a = {GL_TRIANGLES, 1, 5, DG_GL_ARRAY_RAW | DG_GL_ARRAY_POSITION, 0, 0x1, 0, 0, 0, 0};
    ExpectBufferDraw(Log[1], FEnum_glDrawRangeElements, a,
                     BufferPayload(a, {{0, float2, 1, 0, 20}}, {}, 0));
    a = {GL_TRIANGLES, 1, 3, pcs, 0, 0x13, 0, 0, 0, 0};
    ExpectBufferDraw(
        Log[2], FEnum_glDrawRangeElementsEXT, a,
        BufferPayload(a, {{0, float2, 1, 0, 20}, {1, bgra, 3, 8, 20}, {4, bgra, 3, 12, 20}}, {},
                      1));
    Log.clear();
    glBindBufferARB(ARRAY_BUFFER, 1);
    glColorPointer(JGL_BGRA, GL_UNSIGNED_BYTE, 20, Offset(8));
    glSecondaryColorPointerEXT(JGL_BGRA, GL_UNSIGNED_BYTE, 20, Offset(12));
    glBindBufferARB(ARRAY_BUFFER, 0);
    /* An element buffer, indices beyond the buffer bounded by the host. */
    const GLushort elements[4] = {0xeeee, 5, 0, 2};
    Upload(c, ELEMENT_BUFFER, 2, elements, sizeof(elements));
    glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, Offset(2));
    Submit(c);
    assert(Log.size() == 1);
    a = {GL_TRIANGLES, 0, 3, pcs, 0, 0x13, 0, GL_UNSIGNED_SHORT, 2, 2};
    ExpectBufferDraw(Log[0], FEnum_glDrawRangeElementsEXT, a, BufferPayload(a, refs, {}, 1));
    Log.clear();
    glBindBufferARB(ELEMENT_BUFFER, 0);

    /* RGBA buffer colours stay the buffer draw beside a BGRA client
     * secondary colour, which is reordered into its RGB section. */
    glBindBufferARB(ARRAY_BUFFER, 1);
    glColorPointer(4, GL_UNSIGNED_BYTE, 20, Offset(8));
    glBindBufferARB(ARRAY_BUFFER, 0);
    glSecondaryColorPointerEXT(JGL_BGRA, GL_UNSIGNED_BYTE, 20, Field(12));
    glDrawArrays(GL_TRIANGLES, 2, 3);
    Submit(c);
    assert(Log.size() == 1);
    a = {GL_TRIANGLES, 2, 3, pcs, 0, 0x3, 3, 0, 0, 0};
    const std::vector<BYTE> rgb((const BYTE *)RGBA.specular[2], (const BYTE *)RGBA.specular[5]);
    ExpectBufferDraw(Log[0], FEnum_glDrawRangeElements, a,
                     BufferPayload(a,
                                   {{0, float2, 1, 0, 20}, {1, D(GL_UNSIGNED_BYTE, 4), 1, 8, 20}},
                                   {{4, D(GL_UNSIGNED_BYTE, 3), rgb}}, 0));
    Log.clear();
    /* The other way round: a BGRA buffer colour beside a BGRA client
     * secondary colour; only the buffer slot is BGRA on the wire. */
    glBindBufferARB(ARRAY_BUFFER, 1);
    glColorPointer(JGL_BGRA, GL_UNSIGNED_BYTE, 20, Offset(8));
    glBindBufferARB(ARRAY_BUFFER, 0);
    glDrawArrays(GL_TRIANGLES, 2, 3);
    Submit(c);
    assert(Log.size() == 1);
    ExpectBufferDraw(Log[0], FEnum_glDrawRangeElementsEXT, a,
                     BufferPayload(a, {{0, float2, 1, 0, 20}, {1, bgra, 1, 8, 20}},
                                   {{4, D(GL_UNSIGNED_BYTE, 3), rgb}}, 1));
    Log.clear();

    /* Generic attributes: BGRA (normalized) from the buffer beside an RGBA
     * buffer attribute and a BGRA client one. */
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(SECONDARY_ARRAY);
    glBindBufferARB(ARRAY_BUFFER, 1);
    glVertexAttribPointerARB(2, JGL_BGRA, GL_UNSIGNED_BYTE, GL_TRUE, 20, Offset(12));
    glVertexAttribPointerARB(7, 4, GL_UNSIGNED_BYTE, GL_TRUE, 20, Offset(8));
    glBindBufferARB(ARRAY_BUFFER, 0);
    glVertexAttribPointerARB(9, JGL_BGRA, GL_UNSIGNED_BYTE, GL_TRUE, 20, Field(8));
    glEnableVertexAttribArrayARB(2);
    glEnableVertexAttribArrayARB(7);
    glEnableVertexAttribArrayARB(9);
    glDrawArrays(GL_POINTS, 3, 2);
    Submit(c);
    assert(Log.size() == 1);
    const ULONG normalized = D(GL_UNSIGNED_BYTE, 4) | DG_GL_ATTRIB_NORMALIZED;
    a = {GL_POINTS,
         3,
         2,
         DG_GL_ARRAY_RAW | DG_GL_ARRAY_POSITION,
         1 << 2 | 1 << 7 | 1 << 9,
         1 | 1 << 18 | 1 << 23,
         2,
         0,
         0,
         0};
    const std::vector<BYTE> colors((const BYTE *)RGBA.color[3], (const BYTE *)RGBA.color[5]);
    ExpectBufferDraw(Log[0], FEnum_glDrawRangeElementsEXT, a,
                     BufferPayload(a,
                                   {{0, float2, 1, 0, 20},
                                    {18, normalized | DG_GL_ARRAY_BGRA, 1, 12, 20},
                                    {23, normalized, 1, 8, 20}},
                                   {{25, normalized, colors}}, 1));
    Log.clear();
    glDisableVertexAttribArrayARB(2);
    glDisableVertexAttribArrayARB(7);
    glDisableVertexAttribArrayARB(9);
    Quiet(c);
    Close(c);
}

/* Buffer arrays the record cannot carry resolve into the frontend's copy and
 * draw as reordered client arrays: lists, strides beyond the limit. */
static void Fallbacks(void) {
    JGL_CONTEXT *c = Open(DRAWS);
    Upload(c, ARRAY_BUFFER, 1, Vertices, sizeof(Vertices));
    auto buffers = [&](GLsizei stride) {
        glBindBufferARB(ARRAY_BUFFER, 1);
        glVertexPointer(2, GL_FLOAT, stride, Offset(0));
        glColorPointer(JGL_BGRA, GL_UNSIGNED_BYTE, stride, Offset(8));
        glSecondaryColorPointerEXT(JGL_BGRA, GL_UNSIGNED_BYTE, stride, Offset(12));
        glVertexAttribPointerARB(4, JGL_BGRA, GL_UNSIGNED_BYTE, GL_TRUE, stride, Offset(8));
        glBindBufferARB(ARRAY_BUFFER, 0);
    };
    auto rgba = [&](const void *positions, GLsizei stride) {
        glVertexPointer(2, GL_FLOAT, stride, positions);
        glColorPointer(4, GL_UNSIGNED_BYTE, 0, RGBA.color);
        glSecondaryColorPointerEXT(3, GL_UNSIGNED_BYTE, 0, RGBA.specular);
        glVertexAttribPointerARB(4, 4, GL_UNSIGNED_BYTE, GL_TRUE, 0, RGBA.color);
    };
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(SECONDARY_ARRAY);
    glEnableVertexAttribArrayARB(4);
    const GLubyte indices[4] = {4, 1, 5, 0};
    for (ULONG mode : {GL_COMPILE, GL_COMPILE_AND_EXECUTE}) {
        buffers(20);
        assert(JglListMode(mode));
        Clear(c);
        const Records compiled = Recorded(c, [&] {
            glDrawArrays(GL_TRIANGLES, 1, 3);
            glDrawElements(GL_LINES, 4, GL_UNSIGNED_BYTE, indices);
        });
        assert(JglListMode(0));
        rgba(Field(0), 20);
        const Records reference = Recorded(c, [&] {
            glDrawArrays(GL_TRIANGLES, 1, 3);
            glDrawElements(GL_LINES, 4, GL_UNSIGNED_BYTE, indices);
        });
        assert(compiled.size() == 2 && compiled == reference &&
               compiled[0][8] == FEnum_glVertexAttribPointerARB);
    }
    /* A stride beyond DG_GL_MAX_BUFFER_STRIDE: one vertex per stride. */
    const GLsizei wide = DG_GL_MAX_BUFFER_STRIDE + 20;
    std::vector<BYTE> spread(wide * 5 + 20, 0xee);
    for (ULONG v = 0; v < 6; ++v)
        memcpy(&spread[v * wide], &Vertices[v], 20);
    Upload(c, ARRAY_BUFFER, 3, spread.data(), (ULONG)spread.size());
    glVertexPointer(2, GL_FLOAT, wide, Offset(0));
    glColorPointer(JGL_BGRA, GL_UNSIGNED_BYTE, wide, Offset(8));
    glSecondaryColorPointerEXT(JGL_BGRA, GL_UNSIGNED_BYTE, wide, Offset(12));
    glVertexAttribPointerARB(4, JGL_BGRA, GL_UNSIGNED_BYTE, GL_TRUE, wide, Offset(8));
    glBindBufferARB(ARRAY_BUFFER, 0);
    const Records got = Recorded(c, [&] { glDrawArrays(GL_TRIANGLE_STRIP, 0, 6); });
    rgba(spread.data(), wide);
    assert(got == Recorded(c, [&] { glDrawArrays(GL_TRIANGLE_STRIP, 0, 6); }));
    glDisableVertexAttribArrayARB(4);
    Quiet(c);
    Close(c);
}

/* ArrayElement reads B, G, R, A from client memory and buffer copies. */
static void Elements(void) {
    JGL_CONTEXT *c = Open(DRAWS);
    glVertexPointer(2, GL_FLOAT, 20, Field(0));
    glColorPointer(JGL_BGRA, GL_UNSIGNED_BYTE, 20, Field(8));
    glSecondaryColorPointerEXT(JGL_BGRA, GL_UNSIGNED_BYTE, 20, Field(12));
    glVertexAttribPointerARB(3, JGL_BGRA, GL_UNSIGNED_BYTE, GL_TRUE, 20, Field(12));
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(SECONDARY_ARRAY);
    glEnableVertexAttribArrayARB(3);
    auto expect = [&](ULONG v) {
        Submit(c);
        assert(Log.size() == 6);
        ExpectCall(Log[0], FEnum_glBegin, {GL_POINTS});
        const GLubyte *rgba = RGBA.color[v], *rgb = RGBA.specular[v];
        ExpectCall(Log[1], FEnum_glColor4f,
                   {Bits(rgba[0] / 255.0f), Bits(rgba[1] / 255.0f), Bits(rgba[2] / 255.0f),
                    Bits(rgba[3] / 255.0f)});
        ExpectCall(Log[2], FEnum_glSecondaryColor3f,
                   {Bits(rgb[0] / 255.0f), Bits(rgb[1] / 255.0f), Bits(rgb[2] / 255.0f)});
        ExpectCall(Log[3], FEnum_glVertexAttrib4fARB,
                   {3, Bits(rgb[0] / 255.0f), Bits(rgb[1] / 255.0f), Bits(rgb[2] / 255.0f),
                    Bits(Vertices[v].specular[3] / 255.0f)});
        ExpectCall(Log[4], FEnum_glVertex4f,
                   {Bits(Vertices[v].x), Bits(Vertices[v].y), 0, Bits(1.0f)});
        ExpectCall(Log[5], FEnum_glEnd, {});
        Log.clear();
    };
    glBegin(GL_POINTS);
    AliasArrayElement(3);
    glEnd();
    expect(3);
    Upload(c, ARRAY_BUFFER, 1, Vertices, sizeof(Vertices));
    glColorPointer(JGL_BGRA, GL_UNSIGNED_BYTE, 20, Offset(8));
    glSecondaryColorPointerEXT(JGL_BGRA, GL_UNSIGNED_BYTE, 20, Offset(12));
    glVertexAttribPointerARB(3, JGL_BGRA, GL_UNSIGNED_BYTE, GL_TRUE, 20, Offset(12));
    glBindBufferARB(ARRAY_BUFFER, 0);
    glBegin(GL_POINTS);
    AliasArrayElement(5);
    glEnd();
    expect(5);
    /* A BGRA colour copy one byte short of element 3's colour is never read
     * for it (its current colour applies); element 2's is. */
    Upload(c, ARRAY_BUFFER, 2, Vertices, 3 * 20 + 11);
    glColorPointer(JGL_BGRA, GL_UNSIGNED_BYTE, 20, Offset(8));
    glBindBufferARB(ARRAY_BUFFER, 0);
    glBegin(GL_POINTS);
    AliasArrayElement(3);
    AliasArrayElement(2);
    glEnd();
    Submit(c);
    assert(!c->Error && Log.size() == 9);
    const GLubyte *rgb = RGBA.specular[3], *rgba = RGBA.color[2];
    ExpectCall(Log[1], FEnum_glSecondaryColor3f,
               {Bits(rgb[0] / 255.0f), Bits(rgb[1] / 255.0f), Bits(rgb[2] / 255.0f)});
    assert(Log[2][8] == FEnum_glVertexAttrib4fARB && Log[3][8] == FEnum_glVertex4f);
    ExpectCall(Log[4], FEnum_glColor4f,
               {Bits(rgba[0] / 255.0f), Bits(rgba[1] / 255.0f), Bits(rgba[2] / 255.0f),
                Bits(rgba[3] / 255.0f)});
    Log.clear();
    glDisableVertexAttribArrayARB(3);
    Quiet(c);
    Close(c);
}

int main() {
    Unnegotiated();
    Negotiation();
    Errors();
    Queries();
    ClientArrays();
    Buffers();
    Fallbacks();
    Elements();
    puts("PASS vertex array bgra frontend");
    return 0;
}
