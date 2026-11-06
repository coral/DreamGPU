#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Patch 0053 on actual pinned Wine source: fixed-function vertex tweening
(D3DRS_VERTEXBLEND D3DVBF_TWEENING, D3DRS_TWEENFACTOR) on the CPU.

Direct3D defines POSITION = POSITION1 * (1 - TWEENFACTOR) + POSITION2 *
TWEENFACTOR, normals likewise, before lighting and clipping, with the second
position and normal as declaration elements of usage index 1 (D3D9
POSITION1/NORMAL1, D3D8 D3DVSDE_POSITION2/NORMAL2). WineD3D had no slot for
them, printed a FIXME and drew the first position; a GeForce FX claimed
D3DVTXPCAPS_TWEENING (D3D9 VertexProcessingCaps 0x17b), Wine did not (0x13b).

The prepared caps function claims TWEENING with and without
ARB_vertex_blend; the D3D9 mask keeps it (0x17b), D3D8 copies it (0x17f) and
D3D7's mask drops it (no tweening there). Both vertex blend state handlers
are quiet for tweening, and fixed_get_input declines usage index 1 positions
and normals without a FIXME. D3D8's declaration conversion maps POSITION2 and
NORMAL2 to usage index 1.

The prepared helpers (0052's dg_vertex_blend_* with 0053's dg_vertex_tween_*),
0041's index scan and 0048's span check are compiled unchanged under
ASan/UBSan against a recording GL. Every case tweens against a
double-precision reference of the documented formula: the second elements
interleaved in the first stream (D3D8 style) or in a second stream with its
own offset and stride (D3D9), FLOAT3/FLOAT4 positions, position-only,
normal-only and both, tween factors 0, 1/2, 1, fractions and extrapolation;
factor 0 gives the first position and normal exactly, 1 the second, 1/2 the
exact midpoint of integer data. Array draws are read back as GL reads client
arrays (DrawArrays from a start vertex, DrawElements with 16- and 32-bit
indices, load base vertices, ARB_draw_elements_base_vertex, buffer-object and
system-memory first elements, local and heap storage): the modelview is never
touched and Wine's own position and normal pointers are restored exactly.
drawStridedSlow's per-vertex path sends the same values. Every fallback
(blending modes, DISABLE, no second element, no declaration, vertex shaders,
pretransformed positions, generic attributes, unsupported formats,
per-instance data, second streams missing, too short or of stride zero,
indices beyond them) answers FALSE before any GL call, so the draw is exactly
the untweened one; blending with a second position bound still blends. The
source placement in drawStridedSlow is checked. Fails without the patch.
"""
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('patches', ROOT / 'tests/guest/support/patches.py')
patches = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patches)
MANIFEST = ROOT / 'support/guest/d3d/patches/base/manifest.json'

PREAMBLE = r'''
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned int UINT, DWORD, GLuint, GLenum;
typedef int BOOL, INT, GLint, GLsizei;
typedef long LONG;
typedef unsigned short WORD;
typedef unsigned char BYTE;
typedef float GLfloat;
typedef uintptr_t ULONG_PTR;
#define TRUE 1
#define FALSE 0
#define MAX_ATTRIBS 16
#define min(a, b) ((a) < (b) ? (a) : (b))
#define GL_EXTCALL(f) (gl_info->gl_ops.ext.p_##f)
#define checkGLcall(text) do {} while (0)
#define TRACE(...) do {} while (0)
#define WARN(...) do {} while (0)
#define FIXME(...) do { ++fixmes; } while (0)
static unsigned int fixmes;
static const char *debug_d3dformat(DWORD id) { (void)id; return ""; }
#define GL_FLOAT 0x1406
#define GL_MODELVIEW 0x1700
#define GL_ARRAY_BUFFER 0x8892
@ENUMS@
static const char *debug_d3ddeclusage(enum wined3d_decl_usage usage) { (void)usage; return ""; }
enum { ARB_VERTEX_BLEND, ARB_VERTEX_BUFFER_OBJECT, ARB_DRAW_ELEMENTS_BASE_VERTEX, NV_FOG_DISTANCE, EXT_COUNT };
enum { WINED3DFMT_UNKNOWN, WINED3DFMT_R32_FLOAT, WINED3DFMT_R32G32_FLOAT, WINED3DFMT_R32G32B32_FLOAT,
       WINED3DFMT_R32G32B32A32_FLOAT, WINED3DFMT_B8G8R8A8_UNORM, WINED3DFMT_R16G16B16A16_FLOAT,
       WINED3DFMT_R16G16B16A16_SINT, WINED3DFMT_R8G8B8A8_UINT };
struct wined3d_format
{
    UINT id;
    UINT byte_count;
    GLint component_count;
    GLenum gl_vtx_type;
    GLint gl_vtx_format;
};
struct wined3d_bo_address { GLuint buffer_object; BYTE *addr; };
struct wined3d_stream_info_element
{
    const struct wined3d_format *format;
    struct wined3d_bo_address data;
    UINT offset;
    GLsizei stride;
    unsigned int stream_idx;
    unsigned int divisor;
};
struct wined3d_stream_info
{
    struct wined3d_stream_info_element elements[MAX_ATTRIBS];
    DWORD position_transformed : 1;
    DWORD all_vbo : 1;
    WORD swizzle_map, use_map;
};
struct wined3d_resource { UINT size; void *heap_memory; };
struct wined3d_buffer { struct wined3d_resource resource; GLuint buffer_object; };
struct wined3d_stream_state { struct wined3d_buffer *buffer; UINT offset, stride, frequency, flags; };
@DECLARATION@
struct wined3d_state
{
    const struct wined3d_vertex_declaration *vertex_declaration;
    DWORD render_states[WINEHIGHEST_RENDER_STATE + 1];
    struct wined3d_matrix transforms[WINED3D_TS_WORLD_LAST + 1];
    struct wined3d_stream_state streams[MAX_STREAMS + 1];
    struct wined3d_buffer *index_buffer;
    INT base_vertex_index, load_base_vertex_index;
    BOOL vertex_shader;
};
static BOOL use_vs(const struct wined3d_state *state) { return state->vertex_shader; }
struct wined3d_gl_info
{
    BOOL supported[EXT_COUNT];
    struct { UINT lights, blends, clipplanes; } limits;
    struct
    {
        struct
        {
            void (*p_glMatrixMode)(GLenum);
            void (*p_glPushMatrix)(void);
            void (*p_glPopMatrix)(void);
            void (*p_glLoadMatrixf)(const GLfloat *);
            void (*p_glVertexPointer)(GLint, GLenum, GLsizei, const void *);
            void (*p_glNormalPointer)(GLenum, GLsizei, const void *);
            void (*p_glNormal3fv)(const GLfloat *);
            void (*p_glVertex4fv)(const GLfloat *);
        } gl;
        struct
        {
            void (*p_glBindBuffer)(GLenum, GLuint);
        } ext;
    } gl_ops;
};
struct wined3d_d3d_info { BOOL ffp_generic_attributes; };
struct wined3d_context
{
    const struct wined3d_gl_info *gl_info;
    const struct wined3d_d3d_info *d3d_info;
};
static unsigned int sysmem_calls;
static BYTE *buffer_get_sysmem(struct wined3d_buffer *buffer, struct wined3d_context *context)
{
    (void)context;
    ++sysmem_calls;
    return buffer->resource.heap_memory;
}
'''

TEST = r'''
/* The recording GL: every call in order, with its arguments. */
enum { CALL_MATRIX_MODE, CALL_PUSH, CALL_POP, CALL_LOAD, CALL_VERTEX_POINTER, CALL_NORMAL_POINTER,
       CALL_NORMAL, CALL_VERTEX, CALL_BIND };
struct call { int op; GLenum a; GLint size; GLsizei stride; const void *pointer; float v[16]; };
static struct call calls[4096];
static unsigned int call_count;
static struct call *record(int op)
{
    assert(call_count < sizeof(calls) / sizeof(*calls));
    memset(&calls[call_count], 0, sizeof(*calls));
    calls[call_count].op = op;
    return &calls[call_count++];
}
static void gl_matrix_mode(GLenum mode) { record(CALL_MATRIX_MODE)->a = mode; }
static void gl_push(void) { record(CALL_PUSH); }
static void gl_pop(void) { record(CALL_POP); }
static void gl_load(const GLfloat *m) { memcpy(record(CALL_LOAD)->v, m, 16 * sizeof(*m)); }
static void gl_vertex_pointer(GLint size, GLenum type, GLsizei stride, const void *pointer)
{
    struct call *c = record(CALL_VERTEX_POINTER);
    c->size = size; c->a = type; c->stride = stride; c->pointer = pointer;
}
static void gl_normal_pointer(GLenum type, GLsizei stride, const void *pointer)
{
    struct call *c = record(CALL_NORMAL_POINTER);
    c->a = type; c->stride = stride; c->pointer = pointer;
}
static void gl_normal(const GLfloat *n) { memcpy(record(CALL_NORMAL)->v, n, 3 * sizeof(*n)); }
static void gl_vertex(const GLfloat *v) { memcpy(record(CALL_VERTEX)->v, v, 4 * sizeof(*v)); }
static void gl_bind(GLenum target, GLuint name) { struct call *c = record(CALL_BIND); c->a = target; c->size = name; }

static const struct wined3d_format formats[] =
{
    {WINED3DFMT_R32_FLOAT, 4, 1, GL_FLOAT, 1},
    {WINED3DFMT_R32G32_FLOAT, 8, 2, GL_FLOAT, 2},
    {WINED3DFMT_R32G32B32_FLOAT, 12, 3, GL_FLOAT, 3},
    {WINED3DFMT_R32G32B32A32_FLOAT, 16, 4, GL_FLOAT, 4},
    {WINED3DFMT_B8G8R8A8_UNORM, 4, 4, 0x1401, 0x80e1},
    {WINED3DFMT_R16G16B16A16_FLOAT, 8, 4, 0x140b, 4},
    {WINED3DFMT_R16G16B16A16_SINT, 8, 4, 0x1402, 4},
};
static const struct wined3d_format *format(UINT id)
{
    unsigned int i;
    for (i = 0; i < sizeof(formats) / sizeof(*formats); ++i)
        if (formats[i].id == id)
            return &formats[i];
    abort();
}

static struct wined3d_gl_info gl;
static struct wined3d_d3d_info d3d;
static struct wined3d_context context = {&gl, &d3d};
static struct wined3d_state state;
static struct wined3d_stream_info si;
static struct wined3d_vertex_declaration declaration;
static struct wined3d_vertex_declaration_element elements[8];

/* A deterministic pseudo-random float in [-range, range] with a full
 * mantissa, so differently rounded formulas differ. */
static unsigned int seed = 12345;
static float random_float(float range)
{
    seed = seed * 1103515245u + 12345u;
    return (float)(((double)(seed >> 8) / 8388608.0 - 1.0) * range * 0.9876543);
}

/* The first position and normal in stream 0; the second ones in stream 0
 * too (D3D8's single-stream declarations) or in stream 1 (D3D9 tweening
 * between two key-frame buffers). Formats 0: absent. */
struct layout
{
    UINT position_format, normal_format, position2_format, normal2_format;
    UINT stride, position_offset, normal_offset;
    BOOL separate;
    UINT stride2, position2_offset, normal2_offset;
};
static BYTE memory[2][1 << 20];
static struct wined3d_buffer buffers[2];
static UINT stream_offsets[2];

static const struct layout layouts[] =
{
    /* D3D9's "Using Vertex Tweening" vertex: position, normal, position2,
     * normal2 in one stream. */
    {WINED3DFMT_R32G32B32_FLOAT, WINED3DFMT_R32G32B32_FLOAT, WINED3DFMT_R32G32B32_FLOAT,
            WINED3DFMT_R32G32B32_FLOAT, 48, 0, 12, FALSE, 0, 24, 36},
    /* Second elements first in the vertex, with a texture coordinate. */
    {WINED3DFMT_R32G32B32_FLOAT, WINED3DFMT_R32G32B32_FLOAT, WINED3DFMT_R32G32B32_FLOAT,
            WINED3DFMT_R32G32B32_FLOAT, 56, 24, 36, FALSE, 0, 0, 12},
    /* Two key frames in two streams. */
    {WINED3DFMT_R32G32B32_FLOAT, WINED3DFMT_R32G32B32_FLOAT, WINED3DFMT_R32G32B32_FLOAT,
            WINED3DFMT_R32G32B32_FLOAT, 32, 0, 12, TRUE, 28, 4, 16},
    /* Positions only, in two streams; four-component positions. */
    {WINED3DFMT_R32G32B32A32_FLOAT, 0, WINED3DFMT_R32G32B32A32_FLOAT, 0, 20, 0, 0, TRUE, 16, 0, 0},
    /* A three-component first and a four-component second position. */
    {WINED3DFMT_R32G32B32_FLOAT, WINED3DFMT_R32G32B32_FLOAT, WINED3DFMT_R32G32B32A32_FLOAT, 0,
            24, 0, 12, TRUE, 16, 0, 0},
    /* Normals only: the position is copied. */
    {WINED3DFMT_R32G32B32_FLOAT, WINED3DFMT_R32G32B32A32_FLOAT, 0, WINED3DFMT_R32G32B32_FLOAT,
            40, 0, 12, FALSE, 0, 0, 28},
    /* Position tweened, normal copied. */
    {WINED3DFMT_R32G32B32_FLOAT, WINED3DFMT_R32G32B32_FLOAT, WINED3DFMT_R32G32B32_FLOAT, 0,
            36, 0, 12, FALSE, 0, 24, 0},
};

static UINT components(UINT id) { return id == WINED3DFMT_R32G32B32A32_FLOAT ? 4 : 3; }
static BYTE *at(UINT stream, UINT stride, UINT offset, unsigned int vertex)
{
    return memory[stream] + stream_offsets[stream] + vertex * stride + offset;
}
static UINT stream2(const struct layout *l) { return l->separate ? 1 : 0; }
static UINT stride2(const struct layout *l) { return l->separate ? l->stride2 : l->stride; }

/* Vertex i's values; exact data are small integers (and w 1 or 2). */
static void fill(const struct layout *l, unsigned int vertices, BOOL exact)
{
    unsigned int i, k;
    for (i = 0; i < vertices; ++i)
    {
        float p[4], n[4], p2[4], n2[4];
        for (k = 0; k < 4; ++k)
        {
            p[k] = exact ? (float)((int)(i % 9) - 4 + (int)k) : random_float(4.0f);
            p2[k] = exact ? (float)((int)(i % 5) * 3 - 6 - (int)k) : random_float(4.0f);
            n[k] = exact ? (float)((int)(k == i % 3) * 2 - 1) : random_float(1.0f);
            n2[k] = exact ? (float)(k == (i + 1) % 3) : random_float(1.0f);
        }
        p[3] = exact ? 1.0f : 0.5f + fabsf(random_float(1.0f));
        p2[3] = exact ? 2.0f : 0.5f + fabsf(random_float(1.0f));
        memcpy(at(0, l->stride, l->position_offset, i), p, format(l->position_format)->byte_count);
        if (l->normal_format)
            memcpy(at(0, l->stride, l->normal_offset, i), n, format(l->normal_format)->byte_count);
        if (l->position2_format)
            memcpy(at(stream2(l), stride2(l), l->position2_offset, i), p2, format(l->position2_format)->byte_count);
        if (l->normal2_format)
            memcpy(at(stream2(l), stride2(l), l->normal2_offset, i), n2, format(l->normal2_format)->byte_count);
    }
}

static void element(unsigned int i, UINT id, UINT slot, UINT offset, BYTE usage, BYTE usage_idx)
{
    struct wined3d_vertex_declaration_element *e = &elements[i];
    memset(e, 0, sizeof(*e));
    e->format = format(id);
    e->ffp_valid = TRUE;
    e->input_slot = slot;
    e->offset = offset;
    e->input_slot_class = WINED3D_INPUT_PER_VERTEX_DATA;
    e->usage = usage;
    e->usage_idx = usage_idx;
}

/* Streams, stream info and declaration of layout l: first elements as
 * buffer-object offsets or system memory addresses, as Wine's stream info
 * holds them (stream offset plus element offset). */
static void streams(const struct layout *l, BOOL buffer_objects, unsigned int vertices)
{
    unsigned int i, count = 0;

    memset(&si, 0, sizeof(si));
    memset(state.streams, 0, sizeof(state.streams));
    for (i = 0; i < 2; ++i)
    {
        buffers[i].resource.size = stream_offsets[i] + vertices * (i ? l->stride2 : l->stride);
        buffers[i].resource.heap_memory = memory[i];
        buffers[i].buffer_object = buffer_objects ? 7 + i : 0;
        if (i && !l->separate)
            continue;
        state.streams[i].buffer = &buffers[i];
        state.streams[i].offset = stream_offsets[i];
        state.streams[i].stride = i ? l->stride2 : l->stride;
    }
    for (i = 0; i < 2; ++i)
    {
        UINT id = i ? l->normal_format : l->position_format, slot = i ? WINED3D_FFP_NORMAL : WINED3D_FFP_POSITION;
        UINT offset = stream_offsets[0] + (i ? l->normal_offset : l->position_offset);
        struct wined3d_stream_info_element *e = &si.elements[slot];
        if (!id)
            continue;
        e->format = format(id);
        e->stride = l->stride;
        e->stream_idx = 0;
        e->data.buffer_object = buffers[0].buffer_object;
        e->data.addr = (buffer_objects ? (BYTE *)NULL : memory[0]) + offset;
        si.use_map |= 1u << slot;
    }
    /* The declaration: a decoy texture coordinate of usage index 1, the
     * second elements before the first. */
    element(count++, WINED3DFMT_R32G32_FLOAT, 0, 0, WINED3D_DECL_USAGE_TEXCOORD, 1);
    if (l->position2_format)
        element(count++, l->position2_format, stream2(l), l->position2_offset, WINED3D_DECL_USAGE_POSITION, 1);
    if (l->normal2_format)
        element(count++, l->normal2_format, stream2(l), l->normal2_offset, WINED3D_DECL_USAGE_NORMAL, 1);
    element(count++, l->position_format, 0, l->position_offset, WINED3D_DECL_USAGE_POSITION, 0);
    if (l->normal_format)
        element(count++, l->normal_format, 0, l->normal_offset, WINED3D_DECL_USAGE_NORMAL, 0);
    declaration.elements = elements;
    declaration.element_count = count;
    state.vertex_declaration = &declaration;
}

static void set_tween(float t)
{
    memcpy(&state.render_states[WINED3D_RS_TWEENFACTOR], &t, sizeof(t));
}
static float tween_factor(void)
{
    float t;
    memcpy(&t, &state.render_states[WINED3D_RS_TWEENFACTOR], sizeof(t));
    return t;
}

static double worst;
static char current_case[200];
static void fail(const char *what, double actual, double expected)
{
    printf("FAIL %s: %.9g, expected %.9g (case: %s)\n", what, actual, expected, current_case);
    exit(1);
}

/* Reads a position (w 1 when missing) or a normal. */
static void read_vector(float *out, const BYTE *in, UINT id, BOOL position)
{
    out[0] = out[1] = out[2] = 0.0f;
    out[3] = 1.0f;
    memcpy(out, in, (position ? components(id) : 3) * sizeof(*out));
}

/* The documented formula in double precision, and its exact cases:
 * factor 0 the first value, 1 the second, 1/2 of integer data the midpoint. */
static void check_value(const float *actual, const float *first, const float *second, unsigned int count,
        BOOL exact, const char *what)
{
    double t = tween_factor(), s = 1.0 - t;
    unsigned int k;

    for (k = 0; k < count; ++k)
    {
        double expected = second ? first[k] * s + second[k] * t : first[k];
        double scale = second ? fabs(first[k] * s) + fabs(second[k] * t) : fabs(first[k]);
        double error = fabs(actual[k] - expected);

        if (scale && error / scale > worst)
            worst = error / scale;
        if (error > 4e-7 * scale + 1e-30)
            fail(what, actual[k], expected);
        if (!second || t == 0.0)
        {
            if (actual[k] != first[k])
                fail(what, actual[k], first[k]);
        }
        else if (t == 1.0)
        {
            if (actual[k] != second[k])
                fail(what, actual[k], second[k]);
        }
        else if (t == 0.5 && exact && actual[k] != expected)
        {
            fail(what, actual[k], expected);
        }
    }
}

static void check_vertex(const struct layout *l, unsigned int vertex, const float *p, const float *n, BOOL exact)
{
    float p0[4], p1[4], n0[4], n1[4];

    read_vector(p0, at(0, l->stride, l->position_offset, vertex), l->position_format, TRUE);
    if (l->position2_format)
        read_vector(p1, at(stream2(l), stride2(l), l->position2_offset, vertex), l->position2_format, TRUE);
    check_value(p, p0, l->position2_format ? p1 : NULL, 4, exact, "tweened position");
    if (!l->normal_format)
        return;
    read_vector(n0, at(0, l->stride, l->normal_offset, vertex), l->normal_format, FALSE);
    if (l->normal2_format)
        read_vector(n1, at(stream2(l), stride2(l), l->normal2_offset, vertex), l->normal2_format, FALSE);
    check_value(n, n0, l->normal2_format ? n1 : NULL, 3, exact, "tweened normal");
}

static void reset(void)
{
    call_count = 0;
    sysmem_calls = 0;
    fixmes = 0;
}

static unsigned int array_draws, slow_vertices, fallbacks;

/* A prepared array draw read back the way GL reads client arrays. Without
 * ARB_draw_elements_base_vertex Wine offsets every array by the base vertex
 * (load_base == base_vertex); with it GL adds base_vertex to each index. */
static void array_case_base(const struct layout *l, float t, BOOL buffer_objects, BOOL indexed, UINT index_size,
        INT load_base, INT base_vertex, UINT start, UINT count, unsigned int vertices, BOOL exact)
{
    INT index_base = gl.supported[ARB_DRAW_ELEMENTS_BASE_VERTEX] ? base_vertex : 0;
    struct dg_vertex_blend blend;
    struct dg_vertex_blend_arrays arrays;
    static BYTE index_memory[65536];
    struct wined3d_buffer index_buffer = {{sizeof(index_memory), index_memory}, 9};
    const struct call *vp, *np;
    int64_t first = INT64_MAX;
    unsigned int i;

    snprintf(current_case, sizeof(current_case),
            "layout %d t %g bo %d indexed %d/%u load %d base %d start %u count %u offsets %u/%u",
            (int)(l - layouts), t, buffer_objects, indexed, index_size, load_base, base_vertex, start, count,
            stream_offsets[0], stream_offsets[1]);
    fill(l, vertices, exact);
    streams(l, buffer_objects, vertices);
    state.render_states[WINED3D_RS_VERTEXBLEND] = WINED3D_VBF_TWEENING;
    set_tween(t);
    state.load_base_vertex_index = load_base;
    state.base_vertex_index = base_vertex;
    state.index_buffer = &index_buffer;
    for (i = 0; i < start + count; ++i)
    {
        UINT index = (i * 7919u + 13u) % (vertices - load_base - index_base);
        if (index_size == 2)
            ((WORD *)index_memory)[i] = (WORD)index;
        else
            ((DWORD *)index_memory)[i] = index;
    }
    reset();
    assert(dg_vertex_blend_init(&blend, &context, &state, &si));
    assert(!blend.matrices && !blend.weights && blend.tween == t && !call_count && !fixmes);
    assert(!!blend.position2 == !!l->position2_format && !!blend.normal2 == !!l->normal2_format);
    assert(dg_vertex_blend_prepare(&arrays, &blend, &context, &state, NULL, index_size, start, count, indexed));
    assert(!call_count && arrays.values);
    dg_vertex_blend_bind(&gl, &state, &blend, &arrays);

    /* GL_ARRAY_BUFFER 0 and the client arrays; the modelview stays. */
    i = 0;
    if (gl.supported[ARB_VERTEX_BUFFER_OBJECT])
        assert(calls[i].op == CALL_BIND && calls[i].a == GL_ARRAY_BUFFER && calls[i].size == 0), ++i;
    vp = &calls[i++];
    assert(vp->op == CALL_VERTEX_POINTER && vp->size == 4 && vp->a == GL_FLOAT && vp->stride == 0);
    np = NULL;
    if (l->normal_format)
    {
        np = &calls[i++];
        assert(np->op == CALL_NORMAL_POINTER && np->a == GL_FLOAT && np->stride == 0);
    }
    assert(i == call_count);

    for (i = 0; i < count; ++i)
    {
        UINT j = indexed ? (index_size == 2 ? ((WORD *)index_memory)[start + i]
                : ((DWORD *)index_memory)[start + i]) + index_base : start + i;
        const float *p = (const float *)((const BYTE *)vp->pointer + (size_t)j * 16);
        const float *n = np ? (const float *)((const BYTE *)np->pointer + (size_t)j * 12) : NULL;
        if (j < first) first = j;
        check_vertex(l, load_base + j, p, n, exact);
    }
    assert((const BYTE *)vp->pointer + first * 16 == (const BYTE *)arrays.values);

    call_count = 0;
    dg_vertex_blend_unbind(&gl, &state, &blend, &arrays);
    i = 0;
    if (gl.supported[ARB_VERTEX_BUFFER_OBJECT])
        assert(calls[i].op == CALL_BIND && calls[i].size == (GLint)buffers[0].buffer_object), ++i;
    assert(calls[i].op == CALL_VERTEX_POINTER && calls[i].size == format(l->position_format)->gl_vtx_format
            && calls[i].stride == (GLsizei)l->stride
            && calls[i].pointer == si.elements[WINED3D_FFP_POSITION].data.addr + load_base * (int)l->stride), ++i;
    if (l->normal_format)
    {
        if (gl.supported[ARB_VERTEX_BUFFER_OBJECT])
            assert(calls[i].op == CALL_BIND && calls[i].size == (GLint)buffers[0].buffer_object), ++i;
        assert(calls[i].op == CALL_NORMAL_POINTER && calls[i].a == GL_FLOAT && calls[i].stride == (GLsizei)l->stride
                && calls[i].pointer == si.elements[WINED3D_FFP_NORMAL].data.addr + load_base * (int)l->stride), ++i;
    }
    assert(i == call_count && !arrays.values && !fixmes);
    ++array_draws;
}

static void array_case(const struct layout *l, float t, BOOL buffer_objects, BOOL indexed, UINT index_size,
        INT load_base, UINT start, UINT count, unsigned int vertices, BOOL exact)
{
    array_case_base(l, t, buffer_objects, indexed, index_size, load_base, load_base, start, count, vertices, exact);
}

/* drawStridedSlow: system memory addresses, its own vertex numbering
 * (start + i, or index plus base vertex), the second streams found first. */
static void slow_case(const struct layout *l, float t, BOOL indexed, INT base_vertex, UINT start, UINT count,
        unsigned int vertices)
{
    static WORD indices[256];
    struct dg_vertex_blend blend;
    unsigned int i;

    snprintf(current_case, sizeof(current_case), "slow layout %d t %g indexed %d base %d start %u count %u",
            (int)(l - layouts), t, indexed, base_vertex, start, count);
    fill(l, vertices, FALSE);
    streams(l, FALSE, vertices);
    state.render_states[WINED3D_RS_VERTEXBLEND] = WINED3D_VBF_TWEENING;
    set_tween(t);
    state.base_vertex_index = base_vertex;
    for (i = 0; i < start + count; ++i)
        indices[i] = (WORD)((i * 31u + 5u) % (vertices - base_vertex));
    reset();
    assert(dg_vertex_blend_init(&blend, &context, &state, &si) && !blend.matrices);
    assert(dg_vertex_tween_immediate(&blend, &context, &state, indexed ? indices : NULL, indexed ? 2 : 0,
            start, count));
    assert(!call_count);
    for (i = 0; i < count; ++i)
    {
        LONG vertex = indexed ? indices[start + i] + base_vertex : (LONG)(start + i);
        call_count = 0;
        dg_vertex_blend_send(&gl, &blend, vertex);
        if (l->normal_format)
        {
            assert(call_count == 2 && calls[0].op == CALL_NORMAL && calls[1].op == CALL_VERTEX);
            check_vertex(l, (unsigned int)vertex, calls[1].v, calls[0].v, FALSE);
        }
        else
        {
            assert(call_count == 1 && calls[0].op == CALL_VERTEX);
            check_vertex(l, (unsigned int)vertex, calls[0].v, NULL, FALSE);
        }
        ++slow_vertices;
    }
    assert(!fixmes);
}

/* A draw that must stay untweened: FALSE before any GL call. */
static void declines_init(const char *what, unsigned int expected_fixmes)
{
    struct dg_vertex_blend blend;
    reset();
    if (dg_vertex_blend_init(&blend, &context, &state, &si) || call_count || fixmes != expected_fixmes)
    {
        printf("FAIL fallback %s\n", what);
        exit(1);
    }
    ++fallbacks;
}
static void declines_prepare(const char *what, BOOL indexed, UINT index_size, UINT start, UINT count)
{
    struct dg_vertex_blend blend;
    struct dg_vertex_blend_arrays arrays;
    reset();
    assert(dg_vertex_blend_init(&blend, &context, &state, &si));
    if (dg_vertex_blend_prepare(&arrays, &blend, &context, &state, NULL, index_size, start, count, indexed)
            || call_count || arrays.values)
    {
        printf("FAIL fallback %s\n", what);
        exit(1);
    }
    ++fallbacks;
}
static void declines_immediate(const char *what, const void *indices, UINT index_size, UINT start, UINT count)
{
    struct dg_vertex_blend blend;
    reset();
    assert(dg_vertex_blend_init(&blend, &context, &state, &si));
    if (dg_vertex_tween_immediate(&blend, &context, &state, indices, index_size, start, count) || call_count)
    {
        printf("FAIL fallback %s\n", what);
        exit(1);
    }
    ++fallbacks;
}

int main(void)
{
    static const float factors[] = {0.0f, 0.5f, 1.0f, 0.25f, 0.3f, 0.7f, -0.25f, 1.5f};
    struct wined3d_vertex_caps caps;
    unsigned int i, f, regnum;
    struct dg_vertex_blend blend;

    gl.supported[ARB_VERTEX_BUFFER_OBJECT] = TRUE;
    gl.limits.lights = 8;
    gl.limits.blends = 1;
    gl.limits.clipplanes = 6;
    gl.gl_ops.gl.p_glMatrixMode = gl_matrix_mode;
    gl.gl_ops.gl.p_glPushMatrix = gl_push;
    gl.gl_ops.gl.p_glPopMatrix = gl_pop;
    gl.gl_ops.gl.p_glLoadMatrixf = gl_load;
    gl.gl_ops.gl.p_glVertexPointer = gl_vertex_pointer;
    gl.gl_ops.gl.p_glNormalPointer = gl_normal_pointer;
    gl.gl_ops.gl.p_glNormal3fv = gl_normal;
    gl.gl_ops.gl.p_glVertex4fv = gl_vertex;
    gl.gl_ops.ext.p_glBindBuffer = gl_bind;

    /* Caps: TWEENING with and without ARB_vertex_blend. */
    vp_ffp_get_caps(&gl, &caps);
    assert(caps.vertex_processing_caps & WINED3DVTXPCAPS_TWEENING);
    printf("caps %#x\n", caps.vertex_processing_caps);
    gl.supported[ARB_VERTEX_BLEND] = TRUE;
    vp_ffp_get_caps(&gl, &caps);
    assert(caps.vertex_processing_caps & WINED3DVTXPCAPS_TWEENING);
    gl.supported[ARB_VERTEX_BLEND] = FALSE;

    /* The state handler is quiet for tweening; 0 weights still FIXMEs. */
    reset();
    state.render_states[WINED3D_RS_VERTEXBLEND] = WINED3D_VBF_TWEENING;
    state_vertexblend_w(NULL, &state, 0);
    assert(!fixmes);
    state.render_states[WINED3D_RS_VERTEXBLEND] = WINED3D_VBF_0WEIGHTS;
    state_vertexblend_w(NULL, &state, 0);
    assert(fixmes == 1);

    /* Fixed-function stream info: usage index 1 positions and normals are
     * declined quietly (drawprim.c reads them); others as before. */
    reset();
    assert(!fixed_get_input(WINED3D_DECL_USAGE_POSITION, 1, &regnum) && regnum == ~0u && !fixmes);
    assert(!fixed_get_input(WINED3D_DECL_USAGE_NORMAL, 1, &regnum) && regnum == ~0u && !fixmes);
    assert(fixed_get_input(WINED3D_DECL_USAGE_POSITION, 0, &regnum) && regnum == WINED3D_FFP_POSITION);
    assert(fixed_get_input(WINED3D_DECL_USAGE_NORMAL, 0, &regnum) && regnum == WINED3D_FFP_NORMAL);
    assert(fixed_get_input(WINED3D_DECL_USAGE_TEXCOORD, 1, &regnum) && regnum == WINED3D_FFP_TEXCOORD1);
    assert(fixed_get_input(WINED3D_DECL_USAGE_POSITIONT, 0, &regnum) && regnum == WINED3D_FFP_POSITION);
    assert(!fixmes);
    assert(!fixed_get_input(WINED3D_DECL_USAGE_POSITIONT, 1, &regnum) && fixmes == 1);
    assert(!fixed_get_input(WINED3D_DECL_USAGE_POSITION, 2, &regnum) && fixmes == 2);
    assert(!fixed_get_input(WINED3D_DECL_USAGE_TANGENT, 0, &regnum) && fixmes == 3);

    /* Exact data: factors 0, 1/2 and 1 bit for bit. */
    for (f = 0; f < 3; ++f)
        for (i = 0; i < sizeof(layouts) / sizeof(*layouts); ++i)
        {
            stream_offsets[0] = 0;
            stream_offsets[1] = 0;
            array_case(&layouts[i], factors[f], TRUE, FALSE, 0, 0, 0, 24, 24, TRUE);
            stream_offsets[1] = 64;
            array_case(&layouts[i], factors[f], FALSE, TRUE, 2, 3, 5, 30, 40, TRUE);
        }

    /* Random data, every factor: DrawArrays, DrawElements, local and heap
     * storage, stream offsets. */
    for (f = 0; f < sizeof(factors) / sizeof(*factors); ++f)
        for (i = 0; i < sizeof(layouts) / sizeof(*layouts); ++i)
        {
            const struct layout *l = &layouts[i];
            stream_offsets[0] = f & 1 ? 32 : 0;
            stream_offsets[1] = f & 2 ? 96 : 8;
            array_case(l, factors[f], TRUE, FALSE, 0, 0, 0, 3, 3, FALSE);
            array_case(l, factors[f], FALSE, FALSE, 0, 0, 17, 50, 80, FALSE);
            array_case(l, factors[f], TRUE, FALSE, 0, 4, 2, 300, 400, FALSE);
            array_case(l, factors[f], TRUE, TRUE, 2, 0, 0, 36, 64, FALSE);
            array_case(l, factors[f], FALSE, TRUE, 4, 7, 12, 900, 1000, FALSE);
            array_case(l, factors[f], TRUE, TRUE, 4, 2, 3, 2000, 1500, FALSE);
            slow_case(l, factors[f], FALSE, 0, 5, 30, 40);
            slow_case(l, factors[f], TRUE, 6, 4, 60, 50);
        }
    stream_offsets[0] = 16;
    stream_offsets[1] = 48;
    gl.supported[ARB_DRAW_ELEMENTS_BASE_VERTEX] = TRUE;
    array_case_base(&layouts[2], 0.375f, TRUE, TRUE, 2, 0, 9, 0, 60, 70, FALSE);
    array_case_base(&layouts[0], 0.625f, FALSE, TRUE, 4, 3, 5, 4, 200, 90, FALSE);
    gl.supported[ARB_DRAW_ELEMENTS_BASE_VERTEX] = FALSE;
    gl.supported[ARB_VERTEX_BUFFER_OBJECT] = FALSE;
    array_case(&layouts[3], 0.8f, FALSE, FALSE, 0, 0, 0, 30, 30, FALSE);
    gl.supported[ARB_VERTEX_BUFFER_OBJECT] = TRUE;
    /* Wine's ARB_vertex_blend path does not tween; this one does with it. */
    gl.supported[ARB_VERTEX_BLEND] = TRUE;
    array_case(&layouts[2], 0.5f, TRUE, FALSE, 0, 0, 0, 20, 20, FALSE);
    gl.supported[ARB_VERTEX_BLEND] = FALSE;

    /* Fallbacks: the draw stays exactly untweened. */
    stream_offsets[0] = 0;
    stream_offsets[1] = 32;
    fill(&layouts[2], 64, FALSE);
    streams(&layouts[2], TRUE, 64);
    set_tween(0.5f);
    {
        static const DWORD modes[] = {WINED3D_VBF_DISABLE, WINED3D_VBF_0WEIGHTS, WINED3D_VBF_1WEIGHTS,
                WINED3D_VBF_3WEIGHTS, 254, 256};
        for (i = 0; i < sizeof(modes) / sizeof(*modes); ++i)
        {
            state.render_states[WINED3D_RS_VERTEXBLEND] = modes[i];
            declines_init("blend mode", 0);
        }
    }
    state.render_states[WINED3D_RS_VERTEXBLEND] = WINED3D_VBF_TWEENING;
    state.vertex_shader = TRUE;
    declines_init("vertex shader", 0);
    state.vertex_shader = FALSE;
    si.position_transformed = 1;
    declines_init("pretransformed", 0);
    si.position_transformed = 0;
    d3d.ffp_generic_attributes = TRUE;
    declines_init("generic attributes", 0);
    d3d.ffp_generic_attributes = FALSE;
    state.vertex_declaration = NULL;
    declines_init("no declaration", 0);
    state.vertex_declaration = &declaration;
    si.use_map &= ~(1u << WINED3D_FFP_POSITION);
    declines_init("no position", 0);
    si.use_map |= 1u << WINED3D_FFP_POSITION;
    /* Second elements: usage index 1 only, of position and normal. */
    elements[1].usage_idx = 2;
    elements[2].usage_idx = 0;
    declines_init("no second element", 0);
    elements[2].usage_idx = 1;
    si.use_map &= ~(1u << WINED3D_FFP_NORMAL);
    declines_init("second normal without a first", 0);
    si.use_map |= 1u << WINED3D_FFP_NORMAL;
    elements[1].usage_idx = 1;
    elements[1].input_slot = MAX_STREAMS;
    elements[2].input_slot = MAX_STREAMS;
    declines_init("tessellation pseudo-stream", 0);
    elements[1].input_slot = 1;
    elements[2].input_slot = 1;
    elements[1].format = format(WINED3DFMT_R16G16B16A16_FLOAT);
    declines_init("FLOAT16 second position", 1);
    elements[1].format = format(WINED3DFMT_R32G32_FLOAT);
    declines_init("FLOAT2 second position", 0); /* FIXME once only */
    elements[1].format = format(WINED3DFMT_R32G32B32_FLOAT);
    elements[2].format = format(WINED3DFMT_R16G16B16A16_SINT);
    declines_init("SHORT4 second normal", 0);
    elements[2].format = format(WINED3DFMT_R32G32B32_FLOAT);
    si.elements[WINED3D_FFP_POSITION].format = format(WINED3DFMT_R32G32_FLOAT);
    declines_init("FLOAT2 position", 0);
    si.elements[WINED3D_FFP_POSITION].format = format(WINED3DFMT_R32G32B32_FLOAT);
    si.elements[WINED3D_FFP_NORMAL].format = format(WINED3DFMT_R16G16B16A16_FLOAT);
    declines_init("FLOAT16 normal", 0);
    si.elements[WINED3D_FFP_NORMAL].format = format(WINED3DFMT_R32G32B32_FLOAT);
    elements[1].input_slot_class = WINED3D_INPUT_PER_INSTANCE_DATA;
    declines_init("per-instance second position", 0);
    elements[1].input_slot_class = WINED3D_INPUT_PER_VERTEX_DATA;
    state.streams[1].flags = WINED3DSTREAMSOURCE_INSTANCEDATA;
    declines_init("instance data stream", 0);
    state.streams[1].flags = 0;
    si.elements[WINED3D_FFP_POSITION].divisor = 1;
    declines_init("instanced position", 0);
    si.elements[WINED3D_FFP_POSITION].divisor = 0;

    state.load_base_vertex_index = 0;
    state.base_vertex_index = 0;
    declines_prepare("vertices beyond the streams", FALSE, 0, 60, 5);
    buffers[1].resource.size -= 28;
    declines_prepare("vertices beyond the second stream", FALSE, 0, 0, 64);
    declines_immediate("immediate vertices beyond the second stream", NULL, 0, 60, 4);
    buffers[1].resource.size += 28;
    state.streams[1].offset = buffers[1].resource.size;
    declines_prepare("second stream offset beyond its buffer", FALSE, 0, 0, 1);
    state.streams[1].offset = 32;
    state.streams[1].stride = 0;
    declines_prepare("second stream stride zero", FALSE, 0, 0, 5);
    declines_immediate("immediate second stream stride zero", NULL, 0, 0, 5);
    state.streams[1].stride = 28;
    state.streams[1].buffer = NULL;
    declines_prepare("no second stream", FALSE, 0, 0, 5);
    declines_immediate("immediate no second stream", NULL, 0, 0, 5);
    state.streams[1].buffer = &buffers[1];
    state.load_base_vertex_index = -3;
    declines_prepare("negative vertices", FALSE, 0, 0, 5);
    state.load_base_vertex_index = 0;
    {
        static WORD indices[8] = {0, 1, 2, 63, 64, 5, 6, 7};
        struct wined3d_buffer index_buffer = {{sizeof(indices), indices}, 9};
        state.index_buffer = &index_buffer;
        declines_prepare("index beyond the streams", TRUE, 2, 0, 8);
        declines_prepare("indices beyond the index buffer", TRUE, 2, 4, 5);
        declines_immediate("immediate index beyond the streams", indices, 2, 0, 8);
        state.base_vertex_index = -4;
        declines_immediate("immediate negative base vertex", indices, 2, 0, 3);
        state.base_vertex_index = 0;
        state.index_buffer = NULL;
        declines_prepare("no index buffer", TRUE, 2, 0, 4);
    }
    /* The full readable range still tweens. */
    reset();
    assert(dg_vertex_blend_init(&blend, &context, &state, &si)
            && dg_vertex_tween_immediate(&blend, &context, &state, NULL, 0, 0, 64) && !call_count);

    /* Blending with a second position bound: 0052's blend, untouched. */
    si.elements[WINED3D_FFP_BLENDWEIGHT] = si.elements[WINED3D_FFP_POSITION];
    si.elements[WINED3D_FFP_BLENDWEIGHT].format = format(WINED3DFMT_R32_FLOAT);
    si.use_map |= 1u << WINED3D_FFP_BLENDWEIGHT;
    state.render_states[WINED3D_RS_VERTEXBLEND] = WINED3D_VBF_1WEIGHTS;
    reset();
    assert(dg_vertex_blend_init(&blend, &context, &state, &si) && blend.matrices == 2
            && blend.weights == &si.elements[WINED3D_FFP_BLENDWEIGHT] && !call_count);

    printf("PASS prepared vertex tweening: %u array draws and %u immediate-mode vertices tweened as "
            "POSITION1 * (1 - TWEENFACTOR) + POSITION2 * TWEENFACTOR in object space (worst relative error "
            "%.2g), factors 0, 1/2 and 1 exact, modelview untouched, Wine's arrays restored; %u fallbacks "
            "decline before any GL call\n", array_draws, slow_vertices, worst, fallbacks);
    return 0;
}
'''


def function(source, signature):
    start = source.index(signature)
    end = source.index('\n}\n', start) + 3
    return source[start:end] + '\n'


def block(source, opening):
    start = source.index(opening)
    return source[start:source.index('\n};\n', start) + 4]


def caps_bits(names, header):
    value = 0
    for name in names:
        value |= int(re.search(r'#define WINED3D' + name + r'\s+(0x[0-9a-fA-F]+)', header)[1], 16)
    return value


with tempfile.TemporaryDirectory(prefix='dreamgpu-vertex-tween-') as temporary:
    work = Path(temporary)
    for name in json.loads(MANIFEST.read_text())['files']:
        p = work / name
        p.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'vendor/wine9x' / name, p)
    patches.apply(work, MANIFEST)
    drawprim = (work / 'wined3d/drawprim.c').read_text()
    state = (work / 'wined3d/state.c').read_text()
    context = (work / 'wined3d/context.c').read_text()
    private = (work / 'wined3d/wined3d_private.h').read_text()
    public = (work / 'include/wine/wined3d.h').read_text()
    ddraw = (work / 'ddraw/ddraw.c').read_text()
    d3d9 = (ROOT / 'vendor/wine9x/d3d9/directx.c').read_text()
    d3d8 = (ROOT / 'vendor/wine9x/d3d8/directx.c').read_text()
    d3d8_declaration = (ROOT / 'vendor/wine9x/d3d8/vertexdeclaration.c').read_text()
    stateblock = (ROOT / 'vendor/wine9x/wined3d/stateblock.c').read_text()

    # The Direct3D values pass through unchanged, and the factor defaults to 0.
    assert re.search(r'WINED3D_RS_TWEENFACTOR\s+= 170,', public)
    assert re.search(r'WINED3D_VBF_TWEENING\s+= 255,', public)
    assert 'tmpfloat.f = 0.0f;\n    state->render_states[WINED3D_RS_TWEENFACTOR] = tmpfloat.d;' in stateblock
    # D3D8 shader declarations name the second position and normal
    # D3DVSDE_POSITION2 and NORMAL2: usage index 1, as D3D9's.
    assert re.search(r'/\* D3DVSDE_POSITION2 \*/\s+\{WINED3D_DECL_USAGE_POSITION,\s+1\}', d3d8_declaration)
    assert re.search(r'/\* D3DVSDE_NORMAL2 \*/\s+\{WINED3D_DECL_USAGE_NORMAL,\s+1\}', d3d8_declaration)

    # Device caps: D3D9 masks VertexProcessingCaps (TWEENING kept), D3D8
    # copies them, D3D7's mask has no TWEENING.
    vtxp = re.search(r'caps->vertex_processing_caps = (WINED3DVTXPCAPS_\w+(?:\s*\|\s*WINED3DVTXPCAPS_\w+)*);',
                     function(state, 'static void vp_ffp_get_caps('))[1]
    wined3d_vtxp = caps_bits(re.findall(r'WINED3D(VTXPCAPS_\w+)', vtxp), public)
    d3d9_mask = caps_bits(re.findall(r'D3D(VTXPCAPS_\w+)',
                                     re.search(r'pCaps->VertexProcessingCaps &=\s*([^;]+);', d3d9)[1]), public)
    d3d7_mask = caps_bits(re.findall(r'D3D(VTXPCAPS_\w+)',
                                     re.search(r'caps->dwVertexProcessingCaps &= \(([^;]+)\);', ddraw)[1]), public)
    tweening = caps_bits(['VTXPCAPS_TWEENING'], public)
    assert 'VertexProcessingCaps' not in d3d8 and d3d9_mask & tweening and not d3d7_mask & tweening
    print('D3D9 VertexProcessingCaps %#x, D3D8 %#x, D3D7 dwVertexProcessingCaps %#x'
          % (wined3d_vtxp & d3d9_mask, wined3d_vtxp, wined3d_vtxp & d3d7_mask))
    assert wined3d_vtxp & d3d9_mask == 0x17b and wined3d_vtxp == 0x17f
    assert wined3d_vtxp & d3d7_mask == (wined3d_vtxp & ~tweening) & d3d7_mask

    # Wine's ARB_vertex_blend handler disables GL blending for tweening
    # without its FIXME.
    handler = function(state, 'static void state_vertexblend(')
    assert re.search(r'/\* Fall through\. \*/\n.*\n\s+case WINED3D_VBF_TWEENING:\n\s+case WINED3D_VBF_DISABLE:',
                     handler)
    assert handler.index('case WINED3D_VBF_0WEIGHTS:') < handler.index('FIXME(') \
        < handler.index('case WINED3D_VBF_TWEENING:')

    # drawStridedSlow: the second streams are found before any GL call of
    # the draw; the modelview is replaced only for blending.
    slow = function(drawprim, 'static void drawStridedSlow(')
    init = slow.index('dg_vertex_blend_init(&blend, context, state, si)')
    assert slow.rindex('return;') < init
    assert init < slow.index('dg_vertex_tween_immediate(&blend, context, state, idxData, idxSize, startIdx, NumVertexes)') \
        < slow.index('if (blended && blend.matrices)\n        dg_vertex_blend_begin(gl_info, state);') \
        < slow.index('p_glBegin(glPrimType)')
    assert slow.index('p_glEnd()') < slow.index('if (blended && blend.matrices)\n        dg_vertex_blend_end(gl_info);')
    assert slow.count('dg_vertex_blend_begin(') == 1 and slow.count('dg_vertex_blend_end(') == 1
    draw = function(drawprim, 'void draw_primitive(')
    assert draw.count('dg_vertex_blend_init(') == 1 and '!instance_count && dg_vertex_blend_init(' in draw

    blend = drawprim[drawprim.index('/* DreamGPU: fixed-function vertex blending without GL_ARB_vertex_blend,'):
                     drawprim.index('/* Context activation is done by the caller. */\nstatic void drawStridedSlow(')]
    arrays = drawprim[drawprim.index('/* DreamGPU: a blended array draw (see dg_vertex_blend)'):
                      drawprim.index('/* Routine common to the draw primitive and draw indexed primitive routines */')]
    enums = ''.join(block(private, opening) for opening in ('enum wined3d_ffp_idx\n', 'struct wined3d_vertex_caps\n'))
    enums += ''.join(block(public, opening) for opening in (
        'enum wined3d_render_state\n', 'enum wined3d_vertex_blend_flags\n', 'enum wined3d_transform_state\n',
        'struct wined3d_matrix\n', 'enum wined3d_decl_usage\n', 'enum wined3d_input_classification\n'))
    for name in ('WINEHIGHEST_RENDER_STATE', 'WINED3D_TS_WORLD_MATRIX', 'WINED3DFVFCAPS_PSIZE',
                 'WINED3DPRASTERCAPS_FOGRANGE', 'WINED3DDP_MAXTEXCOORD', 'WINED3DSTREAMSOURCE_INSTANCEDATA'):
        enums += re.search(r'#define ' + re.escape(name) + r'\b.*\n', public)[0]
    enums += ''.join(re.findall(r'#define WINED3DVTXPCAPS_\w+\s+\w+\n', public))
    for name in ('MAX_VERTEX_BLENDS', 'MAX_STREAMS'):
        enums += re.search(r'#define ' + name + r'\s+\d+\n', private)[0]
    declaration = ''.join(block(private, opening) for opening in (
        'struct wined3d_vertex_declaration_element\n', 'struct wined3d_vertex_declaration\n'))

    body = (PREAMBLE.replace('@ENUMS@', enums).replace('@DECLARATION@', declaration)
            + function(state, 'static void vp_ffp_get_caps(')
            + function(state, 'static void state_vertexblend_w(')
            + function(context, 'static BOOL fixed_get_input(')
            + function(drawprim, 'static BOOL dg_fog_coord_range(')
            + function(drawprim, 'static BOOL dg_xyzrhw_span(')
            + blend + arrays + TEST)
    (work / 'test.c').write_text(body)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-function', '-Wno-unused-parameter', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', str(work / 'test.c'),
                    '-o', str(work / 'test'), '-lm'], check=True)
    result = subprocess.run([str(work / 'test')], capture_output=True, text=True)
    sys.stdout.write(result.stdout)
    sys.stderr.write(result.stderr)
    if result.returncode:
        sys.exit(result.returncode)
    caps = re.search(r'^caps (0x[0-9a-f]+)$', result.stdout, re.M)
    assert int(caps[1], 16) == wined3d_vtxp
