/* SPDX-License-Identifier: GPL-2.0-or-later
 * DG_CAP_GL_FOG_COORD through the actual frontend on the strict transport
 * fixture: negotiation, exact FogCoordf records and bits, Begin/End and list
 * legality, the fog coordinate client array (validation, local queries, client
 * attribute stack), DrawArraysEXT slot 14 for array and element draws, the
 * ArrayElement record, FOG_COORDINATE_SOURCE validation, and unchanged records
 * without the array. Every draw record is also admitted by the NT validator. */
#include <utility>
#include <vector> /* before the Win32 fixture's min() macro */
#define main existing_frontend_suite_not_run
#include "frontend-harness.cpp"
#undef main
#include "arrays.cpp"
#include "query.cpp"
#include "fog.cpp"
using GLvoid = void;
#include "icd-client.inc"
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
#include "dg-gl-validate.h"

#define FOG_SOURCE 0x8450U
#define FOG_COORDINATE 0x8451U
#define FRAGMENT_DEPTH 0x8452U
#define CURRENT_FOG 0x8453U
#define FOG_ARRAY_TYPE 0x8454U
#define FOG_ARRAY_STRIDE 0x8455U
#define FOG_ARRAY_POINTER 0x8456U
#define FOG_ARRAY 0x8457U
#define TEXTURE0 0x84c0U
#define FLOAT1 (GL_FLOAT | (1U << 16))
#define FLOAT2 (GL_FLOAT | (2U << 16))
#define DOUBLE1 (GL_DOUBLE | (1U << 16))
#define POSITION_FOG (DG_GL_ARRAY_RAW | DG_GL_ARRAY_POSITION | DG_GL_ARRAY_FOG_COORD)

static const char *const Names[10] = {"glFogCoordf",         "glFogCoordfEXT",  "glFogCoordfv",
                                      "glFogCoordfvEXT",     "glFogCoordd",     "glFogCoorddEXT",
                                      "glFogCoorddv",        "glFogCoorddvEXT", "glFogCoordPointer",
                                      "glFogCoordPointerEXT"};
/* Positions and fog coordinates of five vertices; special float bit patterns
 * (signaling NaN, denormal, negative zero) must survive unchanged. */
static const GLfloat Positions[5][2] = {{0, 0}, {1, 0}, {0, 1}, {1, 1}, {.5f, .25f}};
static const ULONG FogBits[5] = {0x3f800000, 0x7f800123, 0x00000001, 0x80000000, 0x40490fdb};
static const struct {
    GLdouble Value, Padding;
} FogDoubles[5] = {{.5, 9}, {-1.25, 9}, {2, 9}, {1e300, 9}, {1e-50, 9}};

static ULONG Bits(GLfloat value) {
    ULONG bits;
    memcpy(&bits, &value, 4);
    return bits;
}
static GLfloat Float(ULONG bits) {
    GLfloat value;
    memcpy(&value, &bits, 4);
    return value;
}

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
static void ExpectCall(const std::vector<ULONG> &r, ULONG function,
                       std::initializer_list<ULONG> words) {
    assert(r[0] == DG_GL_CALL && r[8] == function && r[1] == (9 + words.size()) * 4);
    ULONG n = 9;
    for (ULONG word : words)
        assert(r[n++] == word);
}
static void ExpectData(const std::vector<ULONG> &r, ULONG function, ULONG pname, ULONG value) {
    assert(r[0] == DG_GL_DATA_CALL && r[8] == function && r[9] == 4 && r[1] == 48 &&
           r[10] == pname && r[11] == value);
}

/* The NT kernel admits each draw record exactly as captured. */
static ULONG KernelWords(void *, ULONG function) {
    return function == FEnum_glDrawArrays || function == FEnum_glDrawArraysEXT
               ? DG_GL_FUNCTION_INLINE_DATA | 4
               : (ULONG)-1;
}
static void Admitted(const std::vector<ULONG> &r) {
    std::vector<ULONG> copy(r);
    const DG_GL_LIMITS limits = {7, DG_ESCAPE_MAX_BYTES, DG_GL_MAX_RECORDS, 1};
    assert(DgValidateUserGl(copy.data(), copy.size() * 4, 42, 7, &limits, KernelWords, NULL));
}
/* Expected compact payload: descriptors, then each section at the shared
 * layout's offset with zero padding. */
struct Section {
    ULONG Slot, Descriptor;
    std::vector<BYTE> Bytes;
};
static std::vector<BYTE> Payload(ULONG mask, ULONG count, ULONG slots,
                                 const std::vector<Section> &sections) {
    unsigned descriptors[DG_GL_ARRAY_EXT_SLOTS] = {}, offsets[DG_GL_ARRAY_EXT_SLOTS], length = 0;
    for (const Section &s : sections)
        descriptors[s.Slot] = s.Descriptor;
    assert(DgRawArrayLayoutSlots(mask, count, descriptors, offsets, &length, slots));
    std::vector<BYTE> out(length, 0);
    memcpy(out.data(), descriptors, slots * 4);
    for (const Section &s : sections) {
        assert(s.Bytes.size() ==
               count * (s.Descriptor >> 16) * DgArrayComponentBytes(s.Descriptor & 65535));
        memcpy(&out[offsets[s.Slot]], s.Bytes.data(), s.Bytes.size());
    }
    return out;
}
template <typename T>
static std::vector<BYTE> Gather(const T *source, std::initializer_list<ULONG> order) {
    std::vector<BYTE> out;
    for (ULONG i : order)
        out.insert(out.end(), (const BYTE *)&source[i], (const BYTE *)&source[i] + sizeof(T));
    return out;
}
static std::vector<BYTE> Doubles(std::initializer_list<ULONG> order) {
    std::vector<BYTE> out;
    for (ULONG i : order)
        out.insert(out.end(), (const BYTE *)&FogDoubles[i].Value,
                   (const BYTE *)&FogDoubles[i].Value + 8);
    return out;
}
static void ExpectDraw(const std::vector<ULONG> &r, ULONG function, ULONG mode, ULONG count,
                       ULONG mask, const std::vector<BYTE> &payload) {
    assert(r[0] == DG_GL_DATA_CALL && r[8] == function && r[9] == payload.size());
    assert(r[10] == mode && r[11] == 0 && r[12] == count && r[13] == mask);
    assert(r[1] == (8 + 2 + 4 + (payload.size() + 3) / 4) * 4);
    assert(!memcmp(r.data() + 14, payload.data(), payload.size()));
    Admitted(r);
}

static GLfloat HostFog;
static BOOL Answer(const ULONG *r, DG_ESCAPE_REPLY *reply, ULONG capacity) {
    if (r[8] != FEnum_glGetFloatv || r[9] != CURRENT_FOG)
        return FALSE;
    assert(capacity >= 4);
    memcpy(reply + 1, &HostFog, 4);
    reply->ResultBytes = 4;
    reply->ResultType = DG_GL_RESULT_FLOAT;
    return TRUE;
}
static JGL_CONTEXT *Open(ULONG caps) {
    Reset();
    HostCaps = caps;
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

/* Array and element draws plus an ArrayElement of the current client arrays. */
static std::vector<std::vector<ULONG>> Draws(JGL_CONTEXT *c) {
    static const GLushort indices[3] = {4, 0, 2};
    Clear(c);
    glDrawArrays(GL_TRIANGLES, 1, 3);
    glDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, indices);
    glBegin(GL_POINTS);
    AliasArrayElement(1);
    glEnd();
    assert(!c->Error);
    Submit(c);
    return Log;
}

/* Without the capability nothing new leaves the frontend. */
static void Unnegotiated(void) {
    JGL_CONTEXT *c = Open(DG_CAP_GL_MULTITEXTURE | DG_CAP_GL_FIXED_STATE |
                          DG_CAP_GL_TEXTURE_CONTROL | DG_CAP_GL_COMPRESSED_TEXTURES);
    const GLfloat f = .5f;
    const GLdouble d = .5;
    const GLint source = (GLint)FOG_COORDINATE;
    assert(!JglFogCoordAvailable());
    assert(!strstr((const char *)glGetString(GL_EXTENSIONS), "fog_coord"));
    for (const char *name : Names)
        assert(!wglGetProcAddress(name));
    assert(wglGetProcAddress("glFogf") == Marker && wglGetProcAddress("glFogi") == Marker &&
           wglGetProcAddress("glActiveTextureARB") == Marker);
    glFogCoordf(f);
    Rejected(c, GL_INVALID_OPERATION);
    glFogCoordfEXT(f);
    Rejected(c, GL_INVALID_OPERATION);
    glFogCoordfv(&f);
    Rejected(c, GL_INVALID_OPERATION);
    glFogCoordfvEXT(&f);
    Rejected(c, GL_INVALID_OPERATION);
    glFogCoordd(d);
    Rejected(c, GL_INVALID_OPERATION);
    glFogCoorddEXT(d);
    Rejected(c, GL_INVALID_OPERATION);
    glFogCoorddv(&d);
    Rejected(c, GL_INVALID_OPERATION);
    glFogCoorddvEXT(&d);
    Rejected(c, GL_INVALID_OPERATION);
    // The array does not exist: pointer, enable and pointer query fail locally.
    glFogCoordPointer(GL_FLOAT, 0, FogBits);
    Rejected(c, GL_INVALID_OPERATION);
    glFogCoordPointerEXT(GL_FLOAT, 0, FogBits);
    Rejected(c, GL_INVALID_OPERATION);
    assert(!c->Arrays.Attribute[JGL_FOG_SLOT].Pointer);
    glEnableClientState(FOG_ARRAY);
    Rejected(c, GL_INVALID_ENUM);
    glDisableClientState(FOG_ARRAY);
    Rejected(c, GL_INVALID_ENUM);
    assert(!c->Arrays.Attribute[JGL_FOG_SLOT].Enabled);
    void *pointer = (void *)&f;
    glGetPointerv(FOG_ARRAY_POINTER, &pointer);
    Rejected(c, GL_INVALID_ENUM);
    assert(pointer == (void *)&f);
    {
        // Array state names are not local here: the host answers as before.
        const ULONG queries = QueryCalls;
        GLint value = 0;
        HostInteger = 77;
        glGetIntegerv(FOG_ARRAY_TYPE, &value);
        assert(value == 77 && QueryCalls == queries + 1 && LastQueryArgument == FOG_ARRAY_TYPE);
        assert(!glIsEnabled(FOG_ARRAY) && QueryCalls == queries + 2 &&
               LastQueryArgument == FOG_ARRAY);
        Log.clear();
    }
    // FOG_COORDINATE_SOURCE is not a fog name; scalar fog names are validated.
    glFogf(FOG_SOURCE, 33873.0f);
    Rejected(c, GL_INVALID_ENUM);
    glFogi(FOG_SOURCE, (GLint)FOG_COORDINATE);
    Rejected(c, GL_INVALID_ENUM);
    glFogiv(FOG_SOURCE, &source);
    Rejected(c, GL_INVALID_ENUM);
    glFogfv(FOG_SOURCE, &f);
    Rejected(c, GL_INVALID_ENUM);
    glFogf(GL_FOG_COLOR, 1); /* a vector-only name */
    Rejected(c, GL_INVALID_ENUM);
    glFogf(0x1234, 1);
    Rejected(c, GL_INVALID_ENUM);
    glFogf(GL_FOG_MODE, 9729.0f);
    assert(c->Records == 1 && !c->Error);
    Submit(c);
    ExpectCall(Log[0], FEnum_glFogf, {GL_FOG_MODE, Bits(9729.0f)});
    // A display list defers command errors; client state stays immediate.
    assert(JglListMode(GL_COMPILE));
    Clear(c);
    glFogCoordf(f);
    glFogCoorddvEXT(&d);
    glFogf(FOG_SOURCE, 33873.0f);
    glFogi(FOG_SOURCE, (GLint)FOG_COORDINATE);
    assert(!c->Error && c->Records == 4);
    glFogCoordPointer(GL_FLOAT, 0, FogBits);
    assert(c->Error == GL_INVALID_OPERATION && c->Records == 4);
    c->Error = 0;
    Submit(c);
    ExpectCall(Log[0], DG_GL_RECORD_ERROR, {GL_INVALID_OPERATION});
    ExpectCall(Log[1], DG_GL_RECORD_ERROR, {GL_INVALID_OPERATION});
    ExpectCall(Log[2], DG_GL_RECORD_ERROR, {GL_INVALID_ENUM});
    ExpectCall(Log[3], DG_GL_RECORD_ERROR, {GL_INVALID_ENUM});
    assert(JglListMode(0));
    Close(c);
}

/* Negotiated lookup and the extension string. */
static void Negotiation(void) {
    JGL_CONTEXT *c = Open(DG_CAP_GL_FOG_COORD);
    assert(JglFogCoordAvailable());
    assert(!strcmp((const char *)glGetString(GL_EXTENSIONS), "GL_EXT_fog_coord"));
    for (const char *name : Names)
        assert(wglGetProcAddress(name) == Marker);
    assert(!wglGetProcAddress("glActiveTextureARB") &&
           !wglGetProcAddress("glCompressedTexImage2DARB"));
    Close(c);
    Reset();
    HostCaps = DG_CAP_GL_MULTITEXTURE | DG_CAP_GL_FIXED_STATE | DG_CAP_GL_TEXTURE_CONTROL |
               DG_CAP_GL_COMPRESSED_TEXTURES | DG_CAP_GL_FOG_COORD;
    HostSecondaryWords = 3;
    HGLRC context = Create();
    assert(!strcmp(
        (const char *)glGetString(GL_EXTENSIONS),
        "GL_ARB_multitexture GL_ARB_texture_env_combine GL_EXT_texture_env_combine "
        "GL_ARB_texture_env_dot3 GL_EXT_secondary_color GL_EXT_separate_specular_color "
        "GL_EXT_blend_color GL_EXT_blend_minmax GL_EXT_blend_subtract GL_EXT_blend_func_separate "
        "GL_EXT_blend_equation_separate GL_EXT_stencil_wrap GL_ATI_separate_stencil "
        "GL_ARB_texture_border_clamp GL_ARB_texture_mirrored_repeat GL_EXT_texture_edge_clamp "
        "GL_SGIS_texture_edge_clamp GL_EXT_texture_filter_anisotropic GL_EXT_texture_lod_bias "
        "GL_ARB_point_parameters GL_EXT_point_parameters GL_ARB_point_sprite "
        "GL_ARB_texture_env_add GL_EXT_texture_env_add GL_ATI_texture_env_combine3 "
        "GL_ATI_texture_mirror_once GL_SGIS_generate_mipmap GL_ARB_texture_compression "
        "GL_EXT_texture_compression_s3tc GL_EXT_fog_coord"));
    assert(wglMakeCurrent(NULL, NULL) && wglDeleteContext(context));
}

/* Every entry point submits one FogCoordf word; floats keep their bits. */
static void Records(void) {
    JGL_CONTEXT *c = Open(DG_CAP_GL_FOG_COORD);
    for (ULONG bits : FogBits) {
        const GLfloat value = Float(bits);
        glFogCoordf(value);
        glFogCoordfEXT(value);
        glFogCoordfv(&value);
        glFogCoordfvEXT(&value);
    }
    const struct {
        GLdouble Value;
        ULONG Bits;
    } doubles[] = {{.5, 0x3f000000}, {-2.25, 0xc0100000}, {1e300, 0x7f800000}, {1e-50, 0}};
    for (const auto &item : doubles) {
        glFogCoordd(item.Value);
        glFogCoorddEXT(item.Value);
        glFogCoorddv(&item.Value);
        glFogCoorddvEXT(&item.Value);
    }
    glFogCoordfv(NULL);
    glFogCoordfvEXT(NULL);
    glFogCoorddv(NULL);
    glFogCoorddvEXT(NULL);
    assert(!c->Error && c->Records == 36);
    Submit(c);
    for (ULONG n = 0; n < 20; ++n)
        ExpectCall(Log[n], FEnum_glFogCoordf, {FogBits[n / 4]});
    for (ULONG n = 0; n < 16; ++n)
        ExpectCall(Log[20 + n], FEnum_glFogCoordf, {doubles[n / 4].Bits});

    // Legal between Begin and End; FOG_COORDINATE_SOURCE (a Fog call) is not.
    Clear(c);
    const GLdouble half = .5;
    glBegin(GL_TRIANGLES);
    glFogCoordf(.25f);
    glFogCoorddvEXT(&half);
    glVertex3f(1, 2, 3);
    glFogi(FOG_SOURCE, (GLint)FOG_COORDINATE);
    assert(c->Error == GL_INVALID_OPERATION);
    c->Error = 0;
    glEnd();
    assert(!c->Error && c->Records == 5);
    Submit(c);
    ExpectCall(Log[0], FEnum_glBegin, {GL_TRIANGLES});
    ExpectCall(Log[1], FEnum_glFogCoordf, {0x3e800000});
    ExpectCall(Log[2], FEnum_glFogCoordf, {0x3f000000});
    assert(Log[3][8] == FEnum_glVertex3f);
    ExpectCall(Log[4], FEnum_glEnd, {});

    // Display lists record the coordinate and the source like any command.
    assert(JglListMode(GL_COMPILE));
    Clear(c);
    glFogCoordf(Float(0x7f800123));
    glFogCoordd(-2.25);
    glFogi(FOG_SOURCE, (GLint)FOG_COORDINATE);
    assert(!c->Error && c->Records == 3);
    Submit(c);
    ExpectCall(Log[0], FEnum_glFogCoordf, {0x7f800123});
    ExpectCall(Log[1], FEnum_glFogCoordf, {0xc0100000});
    ExpectCall(Log[2], FEnum_glFogf, {FOG_SOURCE, Bits(33873.0f)});
    assert(JglListMode(0));

    // FOG_COORDINATE_SOURCE: one value through each Fog form; the value itself
    // is native GL's to judge. CURRENT_FOG_COORDINATE is no parameter.
    Clear(c);
    const GLfloat coordinate = 33873.0f;
    const GLint depth = (GLint)FRAGMENT_DEPTH, unknown = 0x1234;
    glFogi(FOG_SOURCE, (GLint)FRAGMENT_DEPTH);
    glFogf(FOG_SOURCE, coordinate);
    glFogfv(FOG_SOURCE, &coordinate);
    glFogiv(FOG_SOURCE, &depth);
    glFogiv(FOG_SOURCE, &unknown);
    assert(!c->Error && c->Records == 5);
    glFogi(CURRENT_FOG, 0);
    assert(c->Error == GL_INVALID_ENUM && c->Records == 5);
    c->Error = 0;
    glFogfv(CURRENT_FOG, &coordinate);
    assert(c->Error == GL_INVALID_ENUM && c->Records == 5);
    c->Error = 0;
    Submit(c);
    ExpectCall(Log[0], FEnum_glFogf, {FOG_SOURCE, Bits(33874.0f)});
    ExpectCall(Log[1], FEnum_glFogf, {FOG_SOURCE, Bits(33873.0f)});
    ExpectData(Log[2], FEnum_glFogfv, FOG_SOURCE, Bits(33873.0f));
    ExpectData(Log[3], FEnum_glFogfv, FOG_SOURCE, Bits(33874.0f));
    ExpectData(Log[4], FEnum_glFogfv, FOG_SOURCE, Bits(4660.0f));

    // The source and the current coordinate are host-answered.
    {
        const ULONG queries = QueryCalls;
        GLint value = 0;
        GLfloat current = 0;
        HostInteger = FOG_COORDINATE;
        glGetIntegerv(FOG_SOURCE, &value);
        assert(value == (GLint)FOG_COORDINATE && LastQueryArgument == FOG_SOURCE);
        HostFog = .75f;
        glGetFloatv(CURRENT_FOG, &current);
        assert(current == .75f && LastQueryFunction == FEnum_glGetFloatv &&
               QueryCalls == queries + 2);
    }
    Close(c);
}

/* The client array: validation, local state queries and the attribute stack. */
static void Pointers(void) {
    JGL_CONTEXT *c = Open(DG_CAP_GL_FOG_COORD);
    JGL_ARRAY *fog = &c->Arrays.Attribute[JGL_FOG_SLOT];
    const ULONG queries = QueryCalls;
    GLint value = -1;
    GLboolean enabled = 2;
    void *pointer = (void *)1;
    assert(fog->Type == GL_FLOAT && fog->Size == 1 && !fog->Stride && !fog->Pointer &&
           !fog->Enabled);
    glGetIntegerv(FOG_ARRAY_TYPE, &value);
    assert(value == GL_FLOAT);
    glGetIntegerv(FOG_ARRAY_STRIDE, &value);
    assert(value == 0);
    glGetIntegerv(FOG_ARRAY, &value);
    assert(value == 0 && !glIsEnabled(FOG_ARRAY));
    glGetPointerv(FOG_ARRAY_POINTER, &pointer);
    assert(!pointer);
    for (GLenum type :
         {(GLenum)GL_BYTE, (GLenum)GL_UNSIGNED_BYTE, (GLenum)GL_SHORT, (GLenum)GL_UNSIGNED_SHORT,
          (GLenum)GL_INT, (GLenum)GL_UNSIGNED_INT, (GLenum)0x1234}) {
        glFogCoordPointer(type, 0, FogBits);
        assert(c->Error == GL_INVALID_ENUM);
        c->Error = 0;
        glFogCoordPointerEXT(type, 8, FogBits);
        assert(c->Error == GL_INVALID_ENUM);
        c->Error = 0;
    }
    glFogCoordPointer(GL_FLOAT, -4, FogBits);
    assert(c->Error == GL_INVALID_VALUE);
    c->Error = 0;
    assert(fog->Type == GL_FLOAT && !fog->Stride && !fog->Pointer);
    glFogCoordPointerEXT(GL_DOUBLE, 16, FogDoubles);
    assert(!c->Error && fog->Type == GL_DOUBLE && fog->Stride == 16 && fog->Pointer == FogDoubles &&
           fog->Size == 1);
    glGetIntegerv(FOG_ARRAY_TYPE, &value);
    assert(value == GL_DOUBLE);
    glGetIntegerv(FOG_ARRAY_STRIDE, &value);
    assert(value == 16);
    glGetPointerv(FOG_ARRAY_POINTER, &pointer);
    assert(pointer == FogDoubles);
    glEnableClientState(FOG_ARRAY);
    glGetBooleanv(FOG_ARRAY, &enabled);
    glGetIntegerv(FOG_ARRAY, &value);
    assert(fog->Enabled && glIsEnabled(FOG_ARRAY) && enabled == 1 && value == 1);
    glDisableClientState(FOG_ARRAY);
    assert(!fog->Enabled && !glIsEnabled(FOG_ARRAY));
    // All of it is client state: no query and no record.
    assert(QueryCalls == queries && !c->Records && !c->Error);

    // CLIENT_VERTEX_ARRAY_BIT saves and restores the array; the pixel-store
    // bit alone does not.
    glFogCoordPointer(GL_FLOAT, 0, FogBits);
    glEnableClientState(FOG_ARRAY);
    AliasPushClientAttrib(2);
    glFogCoordPointer(GL_DOUBLE, 16, FogDoubles);
    glDisableClientState(FOG_ARRAY);
    AliasPopClientAttrib();
    assert(fog->Type == GL_FLOAT && !fog->Stride && fog->Pointer == FogBits && fog->Enabled);
    AliasPushClientAttrib(1);
    glFogCoordPointer(GL_DOUBLE, 16, FogDoubles);
    glDisableClientState(FOG_ARRAY);
    AliasPopClientAttrib();
    assert(fog->Type == GL_DOUBLE && fog->Pointer == FogDoubles && !fog->Enabled);
    assert(!c->Error && !c->Records);
    Close(c);
}

/* DrawArraysEXT slot 14 for array and element draws; ArrayElement's record. */
static void DrawRecords(void) {
    const ULONG mode = GL_TRIANGLES;
    JGL_CONTEXT *c = Open(DG_CAP_GL_FOG_COORD);
    glVertexPointer(2, GL_FLOAT, 0, Positions);
    glEnableClientState(GL_VERTEX_ARRAY);
    glFogCoordPointer(GL_FLOAT, 0, FogBits);
    glEnableClientState(FOG_ARRAY);
    std::vector<std::vector<ULONG>> log = Draws(c);
    assert(log.size() == 6);
    // Three float2 positions at 64, three FLOAT fog coordinates at 88.
    ExpectDraw(log[0], FEnum_glDrawArraysEXT, mode, 3, POSITION_FOG,
               Payload(POSITION_FOG, 3, 16,
                       {{0, FLOAT2, Gather(Positions, {1, 2, 3})},
                        {JGL_FOG_SLOT, FLOAT1, Gather(FogBits, {1, 2, 3})}}));
    assert(log[0][9] == 100 && log[0][14 + 14] == FLOAT1 && log[0][14 + 15] == 0 &&
           log[0][14 + 22] == 0x7f800123 && log[0][14 + 24] == 0x80000000);
    // Elements expand to the indexed vertices, fog in the same order.
    ExpectDraw(log[1], FEnum_glDrawArraysEXT, mode, 3, POSITION_FOG,
               Payload(POSITION_FOG, 3, 16,
                       {{0, FLOAT2, Gather(Positions, {4, 0, 2})},
                        {JGL_FOG_SLOT, FLOAT1, Gather(FogBits, {4, 0, 2})}}));
    // ArrayElement sets the coordinate bits before the vertex.
    ExpectCall(log[2], FEnum_glBegin, {GL_POINTS});
    ExpectCall(log[3], FEnum_glFogCoordf, {0x7f800123});
    ExpectCall(log[4], FEnum_glVertex4f, {Bits(1), 0, 0, Bits(1)});
    ExpectCall(log[5], FEnum_glEnd, {});

    // DOUBLE coordinates with a stride travel as doubles; ArrayElement converts.
    glFogCoordPointerEXT(GL_DOUBLE, sizeof(FogDoubles[0]), FogDoubles);
    log = Draws(c);
    assert(log.size() == 6);
    ExpectDraw(log[0], FEnum_glDrawArraysEXT, mode, 3, POSITION_FOG,
               Payload(POSITION_FOG, 3, 16,
                       {{0, FLOAT2, Gather(Positions, {1, 2, 3})},
                        {JGL_FOG_SLOT, DOUBLE1, Doubles({1, 2, 3})}}));
    assert(log[0][9] == 112);
    ExpectDraw(log[1], FEnum_glDrawArraysEXT, mode, 3, POSITION_FOG,
               Payload(POSITION_FOG, 3, 16,
                       {{0, FLOAT2, Gather(Positions, {4, 0, 2})},
                        {JGL_FOG_SLOT, DOUBLE1, Doubles({4, 0, 2})}}));
    ExpectCall(log[3], FEnum_glFogCoordf, {Bits(-1.25f)});

    // Interleaved arrays leave the EXT array enabled; it joins the draw.
    AliasInterleavedArrays(0x2a20 /* V2F */, 0, Positions);
    assert(c->Arrays.Attribute[JGL_FOG_SLOT].Enabled && c->Arrays.Attribute[0].Enabled);
    glFogCoordPointer(GL_FLOAT, 0, FogBits);
    Clear(c);
    glDrawArrays(GL_POINTS, 0, 2);
    Submit(c);
    ExpectDraw(Log[0], FEnum_glDrawArraysEXT, GL_POINTS, 2, POSITION_FOG,
               Payload(POSITION_FOG, 2, 16,
                       {{0, FLOAT2, Gather(Positions, {0, 1})},
                        {JGL_FOG_SLOT, FLOAT1, Gather(FogBits, {0, 1})}}));

    // A display list captures the same record.
    assert(JglListMode(GL_COMPILE));
    Clear(c);
    glDrawArrays(GL_TRIANGLES, 1, 3);
    assert(!c->Error);
    Submit(c);
    ExpectDraw(Log[0], FEnum_glDrawArraysEXT, mode, 3, POSITION_FOG,
               Payload(POSITION_FOG, 3, 16,
                       {{0, FLOAT2, Gather(Positions, {1, 2, 3})},
                        {JGL_FOG_SLOT, FLOAT1, Gather(FogBits, {1, 2, 3})}}));
    assert(JglListMode(0));
    Close(c);

    // With texture units: unit 0 (slot 3) and unit 1 (slot 7) precede slot 14.
    c = Open(DG_CAP_GL_MULTITEXTURE | DG_CAP_GL_FOG_COORD);
    static const GLfloat units[5][2] = {{.1f, .2f}, {.3f, .4f}, {.5f, .6f}, {.7f, .8f}, {.9f, 1}};
    static const GLfloat second[5][2] = {{2, 3}, {4, 5}, {6, 7}, {8, 9}, {10, 11}};
    glVertexPointer(2, GL_FLOAT, 0, Positions);
    glEnableClientState(GL_VERTEX_ARRAY);
    glTexCoordPointer(2, GL_FLOAT, 0, units);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glClientActiveTextureARB(TEXTURE0 + 1);
    glTexCoordPointer(2, GL_FLOAT, 0, second);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glClientActiveTextureARB(TEXTURE0);
    glFogCoordPointer(GL_FLOAT, 0, FogBits);
    glEnableClientState(FOG_ARRAY);
    const ULONG mask = POSITION_FOG | DG_GL_ARRAY_TEXCOORD | DG_GL_ARRAY_TEXCOORD1;
    log = Draws(c);
    assert(log.size() == 8);
    ExpectDraw(log[0], FEnum_glDrawArraysEXT, mode, 3, mask,
               Payload(mask, 3, 16,
                       {{0, FLOAT2, Gather(Positions, {1, 2, 3})},
                        {3, FLOAT2, Gather(units, {1, 2, 3})},
                        {7, FLOAT2, Gather(second, {1, 2, 3})},
                        {JGL_FOG_SLOT, FLOAT1, Gather(FogBits, {1, 2, 3})}}));
    assert(log[0][9] == 64 + 24 * 3 + 12);
    ExpectDraw(log[1], FEnum_glDrawArraysEXT, mode, 3, mask,
               Payload(mask, 3, 16,
                       {{0, FLOAT2, Gather(Positions, {4, 0, 2})},
                        {3, FLOAT2, Gather(units, {4, 0, 2})},
                        {7, FLOAT2, Gather(second, {4, 0, 2})},
                        {JGL_FOG_SLOT, FLOAT1, Gather(FogBits, {4, 0, 2})}}));
    ExpectCall(log[3], FEnum_glTexCoord4f, {Bits(.3f), Bits(.4f), 0, Bits(1)});
    ExpectCall(log[4], FEnum_glMultiTexCoord4f, {TEXTURE0 + 1, Bits(4), Bits(5), 0, Bits(1)});
    ExpectCall(log[5], FEnum_glFogCoordf, {0x7f800123});
    ExpectCall(log[6], FEnum_glVertex4f, {Bits(1), 0, 0, Bits(1)});
    ExpectCall(log[7], FEnum_glEnd, {});
    Close(c);
}

/* Without the array the records are exactly those of an unnegotiated host. */
static void UnchangedRecords(void) {
    std::vector<std::vector<ULONG>> before, after;
    for (ULONG negotiated = 0; negotiated < 2; ++negotiated) {
        JGL_CONTEXT *c = Open(negotiated ? DG_CAP_GL_FOG_COORD : 0);
        glVertexPointer(2, GL_FLOAT, 0, Positions);
        glEnableClientState(GL_VERTEX_ARRAY);
        if (negotiated) {
            // Configured but disabled: no effect on any record.
            glFogCoordPointer(GL_FLOAT, 0, FogBits);
            glEnableClientState(FOG_ARRAY);
            glDisableClientState(FOG_ARRAY);
        }
        (negotiated ? after : before) = Draws(c);
        Close(c);
    }
    assert(before.size() == 5 && before == after);
    const ULONG mask = DG_GL_ARRAY_RAW | DG_GL_ARRAY_POSITION;
    ExpectDraw(before[0], FEnum_glDrawArrays, GL_TRIANGLES, 3, mask,
               Payload(mask, 3, 7, {{0, FLOAT2, Gather(Positions, {1, 2, 3})}}));
    ExpectDraw(before[1], FEnum_glDrawArrays, GL_TRIANGLES, 3, mask,
               Payload(mask, 3, 7, {{0, FLOAT2, Gather(Positions, {4, 0, 2})}}));
    assert(before[3][8] == FEnum_glVertex4f);
}

int main() {
    Unnegotiated();
    Negotiation();
    Records();
    Pointers();
    DrawRecords();
    UnchangedRecords();
    Reset();
    assert(!Allocations);
    puts("PASS fog coordinates actual frontend: negotiation and hidden names; exact FogCoordf "
         "records and bits for every entry point; Begin/End and lists; pointer validation, "
         "local queries and client attribute stack; DrawArraysEXT slot 14 (FLOAT/DOUBLE, "
         "elements, texture units, kernel admission); ArrayElement; FOG_COORDINATE_SOURCE; "
         "unchanged records without the array");
    return 0;
}
