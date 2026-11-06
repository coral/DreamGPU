#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Patch 0052 on actual pinned Wine source: fixed-function vertex blending
(D3DRS_VERTEXBLEND 1-3 weights, WORLD..WORLD3) without GL_ARB_vertex_blend.

DreamGPU's host (Mesa) has no ARB_vertex_blend and a GeForce FX never listed
it, so WineD3D claimed one blend matrix (MaxVertexBlendMatrices 1) and drew
skinned meshes in their bind pose. The FX's Direct3D driver claimed four
matrices and no indexed blending (MaxVertexBlendMatrixIndex 0). 0052 blends
positions and normals into world space on the CPU and draws them under the
view matrix alone.

The prepared caps function runs with and without ARB_vertex_blend (4 and 0
without it; GL's MAX_VERTEX_UNITS with it), and the D3D9 mask and the D3D8
copy give the FX's MaxVertexBlendMatrices 4 and MaxVertexBlendMatrixIndex 0
with VertexProcessingCaps unchanged. The prepared state handler stays quiet
for 1-3 weights and keeps its FIXME for tweening and 0 weights.

The prepared blending helpers (dg_vertex_blend_*), 0041's index scan and
0048's span check are compiled unchanged under ASan/UBSan against a
recording GL. Every case blends against a double-precision reference that
applies ARB_vertex_blend's definition in eye space (sum of w_i
(view WORLDi) v, normals through each inverse transpose, the last weight 1
minus the others): 2, 3 and 4 matrices, FLOAT1-4 weights with missing
components, D3DCOLOR weights, three- and four-component positions,
non-uniform scale, shear, a singular matrix (identity normals, as Mesa),
exact power-of-two cases bit for bit. Array draws are read back as GL reads
client arrays (index j at the biased pointer) for DrawArrays from a start
vertex and DrawElements with 16- and 32-bit indices, load base vertices,
buffer-object (offset) and system-memory (address) elements, local and heap
storage; each restores Wine's own position and normal pointers and the
modelview exactly. Every fallback (no blending, tweening, 0 weights, vertex
shaders, pretransformed positions, Wine's own ARB_vertex_blend path, a
generic-attribute pipeline, missing weights, unsupported formats, vertices
outside their streams, stride zero, indices outside the index buffer)
answers FALSE before any GL call, so the draw is exactly the unblended one.
drawStridedSlow's per-vertex path sends the same blended values. The source
placement in draw_primitive and drawStridedSlow is checked. Fails without
the patch.
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
struct wined3d_stream_state { struct wined3d_buffer *buffer; UINT offset, stride; };
struct wined3d_state
{
    DWORD render_states[WINEHIGHEST_RENDER_STATE + 1];
    struct wined3d_matrix transforms[WINED3D_TS_WORLD_LAST + 1];
    struct wined3d_stream_state streams[16];
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

/* A deterministic pseudo-random float in [-range, range]. */
static unsigned int seed = 12345;
static float random_float(float range)
{
    seed = seed * 1103515245u + 12345u;
    return ((float)((seed >> 8) & 0xffff) / 32768.0f - 1.0f) * range;
}

/* An affine world matrix: rotation about an axis, non-uniform scale, shear
 * and translation, all of the seed. */
static void world_matrix(struct wined3d_matrix *m, int kind)
{
    float a = random_float(3.0f), c = cosf(a), s = sinf(a);
    float sx = 1.0f, sy = 1.0f, sz = 1.0f, shear = 0.0f;

    if (kind & 1) { sx = 0.5f + fabsf(random_float(2.0f)); sy = 0.5f + fabsf(random_float(2.0f)); sz = 1.5f; }
    if (kind & 2) shear = random_float(0.7f);
    memset(m, 0, sizeof(*m));
    /* Direct3D rows: v' = v * m. Rotation about z, then scale, then shear. */
    m->_11 = c * sx; m->_12 = s * sy; m->_13 = shear;
    m->_21 = -s * sx; m->_22 = c * sy; m->_23 = 0.0f;
    m->_31 = shear * 0.5f; m->_32 = 0.0f; m->_33 = sz;
    m->_41 = random_float(10.0f); m->_42 = random_float(10.0f); m->_43 = random_float(10.0f); m->_44 = 1.0f;
}

static void set_view(void)
{
    struct wined3d_matrix *v = &state.transforms[WINED3D_TS_VIEW];
    float a = 0.6f, c = cosf(a), s = sinf(a);

    memset(v, 0, sizeof(*v));
    v->_11 = c; v->_13 = -s; v->_22 = 1.0f; v->_31 = s; v->_33 = c;
    v->_41 = 1.0f; v->_42 = -2.0f; v->_43 = 5.0f; v->_44 = 1.0f;
}

/* Double precision reference, ARB_vertex_blend in eye space: positions
 * sum w_i v (W_i V), normals sum w_i n ((W_i V)^-1)^T (row vectors). */
static void mat_mul(double out[4][4], const struct wined3d_matrix *a, const struct wined3d_matrix *b)
{
    const float *x = &a->_11, *y = &b->_11;
    int i, j, k;
    for (i = 0; i < 4; ++i)
        for (j = 0; j < 4; ++j)
            for (out[i][j] = 0.0, k = 0; k < 4; ++k)
                out[i][j] += (double)x[i * 4 + k] * y[k * 4 + j];
}
/* The inverse of m's upper 3x3; FALSE when singular. */
static BOOL inverse3(double out[3][3], double m[4][4])
{
    double det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1])
            - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0])
            + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    if (fabs(det) < 1e-12)
        return FALSE;
    out[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) / det;
    out[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) / det;
    out[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) / det;
    out[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) / det;
    out[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) / det;
    out[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) / det;
    out[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) / det;
    out[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) / det;
    out[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) / det;
    return TRUE;
}
static void reference_eye(const double w[4], unsigned int matrices, const float v[4], const float *n,
        double eye[4], double eye_normal[3])
{
    unsigned int i, j, k;
    memset(eye, 0, 4 * sizeof(*eye));
    memset(eye_normal, 0, 3 * sizeof(*eye_normal));
    for (i = 0; i < matrices; ++i)
    {
        double m[4][4], inverse[3][3];
        mat_mul(m, &state.transforms[WINED3D_TS_WORLD_MATRIX(i)], &state.transforms[WINED3D_TS_VIEW]);
        for (j = 0; j < 4; ++j)
            for (k = 0; k < 4; ++k)
                eye[j] += w[i] * v[k] * m[k][j];
        if (!n)
            continue;
        assert(inverse3(inverse, m));
        /* n * inverse^T: component j is sum_k inverse[j][k] n[k]. */
        for (j = 0; j < 3; ++j)
            for (k = 0; k < 3; ++k)
                eye_normal[j] += w[i] * inverse[j][k] * n[k];
    }
}
/* What GL computes from a blended world-space vertex under the view
 * modelview: position * view, normal * view^-T. */
static void gl_eye(const float p[4], const float *n, double eye[4], double eye_normal[3])
{
    const float *v = &state.transforms[WINED3D_TS_VIEW]._11;
    double m[4][4], inverse[3][3];
    unsigned int j, k;

    for (j = 0; j < 4; ++j)
        for (k = 0; k < 4; ++k)
            m[j][k] = v[j * 4 + k];
    for (j = 0; j < 4; ++j)
        for (eye[j] = 0.0, k = 0; k < 4; ++k)
            eye[j] += (double)p[k] * m[k][j];
    if (!n)
        return;
    assert(inverse3(inverse, m));
    for (j = 0; j < 3; ++j)
        for (eye_normal[j] = 0.0, k = 0; k < 3; ++k)
            eye_normal[j] += inverse[j][k] * n[k];
}
static double worst;
static char current_case[160];
static void close_to(double actual, double expected, double scale, const char *what)
{
    double error = fabs(actual - expected) / (fabs(scale) + 1.0);
    if (error > worst)
        worst = error;
    if (error > 2e-5)
    {
        printf("FAIL %s: %.9g, expected %.9g (case: %s)\n", what, actual, expected, current_case);
        exit(1);
    }
}

/* One stream per element, laid out interleaved: weights, position, normal. */
struct layout
{
    UINT weight_format, position_format, normal_format; /* normal 0: none */
    UINT stride, weight_offset, position_offset, normal_offset;
};
static BYTE memory[1][1 << 20];
static struct wined3d_buffer buffers[4];

/* Vertex i's values: weights (float or D3DCOLOR), position, normal. */
static void fill(const struct layout *l, unsigned int vertices, BOOL exact)
{
    unsigned int i, k;
    for (i = 0; i < vertices; ++i)
    {
        BYTE *v = memory[0] + i * l->stride;
        float weights[4], position[4], normal[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        for (k = 0; k < 4; ++k)
            weights[k] = exact ? (float)(1 << k) / 8.0f : random_float(0.6f);
        for (k = 0; k < 3; ++k)
        {
            position[k] = exact ? (float)((int)(i % 7) - 3 + (int)k) : random_float(4.0f);
            normal[k] = exact ? (float)(k == i % 3) : random_float(1.0f);
        }
        position[3] = exact || l->position_format != WINED3DFMT_R32G32B32A32_FLOAT ? 1.0f : 0.5f + fabsf(random_float(1.0f));
        if (l->weight_format == WINED3DFMT_B8G8R8A8_UNORM)
        {
            DWORD color = (DWORD)(i * 7 % 64) << 16 | (DWORD)(i * 11 % 64) << 8 | (DWORD)(i * 13 % 64) | 0x80u << 24;
            memcpy(v + l->weight_offset, &color, sizeof(color));
        }
        else
            memcpy(v + l->weight_offset, weights, format(l->weight_format)->byte_count);
        memcpy(v + l->position_offset, position, format(l->position_format)->byte_count);
        if (l->normal_format)
            memcpy(v + l->normal_offset, normal, format(l->normal_format)->byte_count);
    }
}

/* The reference weights of a vertex: the first matrices - 1 components of
 * its weight element (missing ones 0), then 1 minus their float sum. */
static void reference_weights(const struct layout *l, const BYTE *v, unsigned int matrices, double w[4])
{
    float f[4] = {0.0f, 0.0f, 0.0f, 0.0f}, sum = 0.0f;
    unsigned int i;
    if (l->weight_format == WINED3DFMT_B8G8R8A8_UNORM)
    {
        DWORD c;
        memcpy(&c, v + l->weight_offset, sizeof(c));
        f[0] = ((c >> 16) & 0xff) / 255.0f; f[1] = ((c >> 8) & 0xff) / 255.0f;
        f[2] = (c & 0xff) / 255.0f; f[3] = (c >> 24) / 255.0f;
    }
    else
        memcpy(f, v + l->weight_offset, format(l->weight_format)->byte_count);
    for (i = 0; i < matrices - 1; ++i)
    {
        w[i] = f[i];
        sum += f[i];
    }
    w[matrices - 1] = 1.0f - sum;
}

/* Streams of layout l from buffer 0, as buffer-object offsets or system
 * memory addresses. */
static void streams(const struct layout *l, BOOL buffer_objects, unsigned int vertices)
{
    static const UINT slots[] = {WINED3D_FFP_BLENDWEIGHT, WINED3D_FFP_POSITION, WINED3D_FFP_NORMAL};
    const UINT ids[] = {l->weight_format, l->position_format, l->normal_format};
    const UINT offsets[] = {l->weight_offset, l->position_offset, l->normal_offset};
    unsigned int i;

    memset(&si, 0, sizeof(si));
    buffers[0].resource.size = vertices * l->stride;
    buffers[0].resource.heap_memory = memory[0];
    buffers[0].buffer_object = buffer_objects ? 7 : 0;
    state.streams[0].buffer = &buffers[0];
    for (i = 0; i < 3; ++i)
    {
        struct wined3d_stream_info_element *e = &si.elements[slots[i]];
        if (!ids[i])
            continue;
        e->format = format(ids[i]);
        e->stride = l->stride;
        e->stream_idx = 0;
        e->data.buffer_object = buffers[0].buffer_object;
        e->data.addr = (buffer_objects ? (BYTE *)NULL : memory[0]) + offsets[i];
        si.use_map |= 1u << slots[i];
    }
}

static void check_vertex(const struct layout *l, unsigned int matrices, unsigned int vertex,
        const float *p, const float *n, BOOL exact)
{
    const BYTE *v = memory[0] + vertex * l->stride;
    float position[4] = {0.0f, 0.0f, 0.0f, 1.0f}, normal[3];
    double w[4], eye[4], eye_normal[3], ref[4], ref_normal[3];
    unsigned int k;

    reference_weights(l, v, matrices, w);
    memcpy(position, v + l->position_offset, format(l->position_format)->byte_count);
    if (l->normal_format)
        memcpy(normal, v + l->normal_offset, sizeof(normal));
    reference_eye(w, matrices, position, l->normal_format ? normal : NULL, ref, ref_normal);
    gl_eye(p, l->normal_format ? n : NULL, eye, eye_normal);
    for (k = 0; k < 4; ++k)
        close_to(eye[k], ref[k], 50.0, "eye position");
    if (l->normal_format)
        for (k = 0; k < 3; ++k)
            close_to(eye_normal[k], ref_normal[k], 4.0, "eye normal");
    if (exact)
    {
        /* Integer matrices, power-of-two weights: the world position and
         * normal are exact in float. */
        double world[4] = {0.0, 0.0, 0.0, 0.0};
        unsigned int i, j;
        for (i = 0; i < matrices; ++i)
        {
            const float *m = &state.transforms[WINED3D_TS_WORLD_MATRIX(i)]._11;
            for (j = 0; j < 4; ++j)
                for (k = 0; k < 4; ++k)
                    world[j] += w[i] * position[k] * m[k * 4 + j];
        }
        for (j = 0; j < 4; ++j)
            if ((double)p[j] != world[j])
            {
                printf("FAIL exact blended position %u: %.9g, expected %.9g\n", j, p[j], world[j]);
                exit(1);
            }
    }
}

static const struct layout layouts[] =
{
    /* D3DFVF_XYZB1..B4 | D3DFVF_NORMAL: weights after the position in the FVF;
     * here in any order, as declarations allow. */
    {WINED3DFMT_R32_FLOAT, WINED3DFMT_R32G32B32_FLOAT, WINED3DFMT_R32G32B32_FLOAT, 28, 12, 0, 16},
    {WINED3DFMT_R32G32_FLOAT, WINED3DFMT_R32G32B32_FLOAT, WINED3DFMT_R32G32B32_FLOAT, 32, 12, 0, 20},
    {WINED3DFMT_R32G32B32_FLOAT, WINED3DFMT_R32G32B32_FLOAT, WINED3DFMT_R32G32B32_FLOAT, 36, 12, 0, 24},
    {WINED3DFMT_R32G32B32A32_FLOAT, WINED3DFMT_R32G32B32A32_FLOAT, WINED3DFMT_R32G32B32A32_FLOAT, 52, 0, 16, 32},
    {WINED3DFMT_B8G8R8A8_UNORM, WINED3DFMT_R32G32B32_FLOAT, WINED3DFMT_R32G32B32_FLOAT, 32, 28, 0, 12},
    {WINED3DFMT_R32G32B32_FLOAT, WINED3DFMT_R32G32B32_FLOAT, 0, 24, 12, 0, 0},
};

static void reset(void)
{
    call_count = 0;
    sysmem_calls = 0;
    fixmes = 0;
}

/* A prepared array draw read back the way GL reads client arrays. */
static unsigned int array_draws, slow_vertices, fallbacks;
/* Without ARB_draw_elements_base_vertex Wine offsets every array by the
 * base vertex (load_base == base_vertex); with it GL adds base_vertex to
 * each index and load_base is the arrays' offset. */
static void array_case_base(const struct layout *l, unsigned int mode, BOOL buffer_objects, BOOL indexed,
        UINT index_size, INT load_base, INT base_vertex, UINT start, UINT count, unsigned int vertices, BOOL exact)
{
    INT index_base = gl.supported[ARB_DRAW_ELEMENTS_BASE_VERTEX] ? base_vertex : 0;

    snprintf(current_case, sizeof(current_case), "layout %d mode %u bo %d indexed %d/%u load %d base %d start %u count %u",
            (int)(l - layouts), mode, buffer_objects, indexed, index_size, load_base, base_vertex, start, count);
    struct dg_vertex_blend blend;
    struct dg_vertex_blend_arrays arrays;
    static BYTE index_memory[65536];
    struct wined3d_buffer index_buffer = {{sizeof(index_memory), index_memory}, 9};
    const struct call *vp, *np;
    unsigned int i;
    int64_t first = INT64_MAX, last = -1;

    fill(l, vertices, exact);
    streams(l, buffer_objects, vertices);
    state.render_states[WINED3D_RS_VERTEXBLEND] = mode;
    state.load_base_vertex_index = load_base;
    state.base_vertex_index = base_vertex;
    state.index_buffer = &index_buffer;
    /* Indices reaching vertices up to vertices - 1, unordered and repeated. */
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
    assert(blend.matrices == mode + 1 && !call_count);
    assert(dg_vertex_blend_prepare(&arrays, &blend, &context, &state, NULL, index_size, start, count, indexed));
    assert(!call_count && arrays.values);
    assert((count <= 64) == (arrays.values == arrays.local) || indexed);
    dg_vertex_blend_bind(&gl, &state, &blend, &arrays);

    /* GL_ARRAY_BUFFER 0, the client arrays, then the view as modelview. */
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
    assert(calls[i].op == CALL_MATRIX_MODE && calls[i].a == GL_MODELVIEW), ++i;
    assert(calls[i].op == CALL_PUSH), ++i;
    assert(calls[i].op == CALL_LOAD && !memcmp(calls[i].v, &state.transforms[WINED3D_TS_VIEW], 64)), ++i;
    assert(i == call_count);

    /* GL array index j reads vertex load_base + j of every stream. */
    for (i = 0; i < count; ++i)
    {
        UINT j = indexed ? (index_size == 2 ? ((WORD *)index_memory)[start + i]
                : ((DWORD *)index_memory)[start + i]) + index_base : start + i;
        const float *p = (const float *)((const BYTE *)vp->pointer + (size_t)j * 16);
        const float *n = np ? (const float *)((const BYTE *)np->pointer + (size_t)j * 12) : NULL;
        if (j < first) first = j;
        if (j > last) last = j;
        check_vertex(l, mode + 1, load_base + j, p, n, exact);
    }
    /* Exactly the referenced range was blended. */
    assert((const BYTE *)vp->pointer + first * 16 == (const BYTE *)arrays.values);

    call_count = 0;
    dg_vertex_blend_unbind(&gl, &state, &blend, &arrays);
    i = 0;
    assert(calls[i].op == CALL_POP), ++i;
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

static void array_case(const struct layout *l, unsigned int mode, BOOL buffer_objects, BOOL indexed,
        UINT index_size, INT load_base, UINT start, UINT count, unsigned int vertices, BOOL exact)
{
    array_case_base(l, mode, buffer_objects, indexed, index_size, load_base, load_base, start, count, vertices, exact);
}

/* drawStridedSlow's per-vertex path: system memory addresses. */
static void slow_case(const struct layout *l, unsigned int mode)
{
    struct dg_vertex_blend blend;
    unsigned int i;

    fill(l, 40, FALSE);
    streams(l, FALSE, 40);
    state.render_states[WINED3D_RS_VERTEXBLEND] = mode;
    reset();
    assert(dg_vertex_blend_init(&blend, &context, &state, &si));
    dg_vertex_blend_begin(&gl, &state);
    assert(call_count == 3 && calls[0].op == CALL_MATRIX_MODE && calls[1].op == CALL_PUSH && calls[2].op == CALL_LOAD);
    for (i = 0; i < 40; ++i)
    {
        call_count = 0;
        dg_vertex_blend_send(&gl, &blend, (LONG)i);
        if (l->normal_format)
        {
            assert(call_count == 2 && calls[0].op == CALL_NORMAL && calls[1].op == CALL_VERTEX);
            check_vertex(l, mode + 1, i, calls[1].v, calls[0].v, FALSE);
        }
        else
        {
            assert(call_count == 1 && calls[0].op == CALL_VERTEX);
            check_vertex(l, mode + 1, i, calls[0].v, NULL, FALSE);
        }
        ++slow_vertices;
    }
    call_count = 0;
    dg_vertex_blend_end(&gl);
    assert(call_count == 1 && calls[0].op == CALL_POP && !fixmes);
}

/* A draw that must stay unblended: FALSE before any GL call. */
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

int main(void)
{
    struct wined3d_vertex_caps caps;
    unsigned int i, mode, kind;
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

    /* Caps: four matrices and no index without ARB_vertex_blend (a GeForce
     * FX's answer), GL's vertex units with it. */
    vp_ffp_get_caps(&gl, &caps);
    assert(caps.max_vertex_blend_matrices == 4 && caps.max_vertex_blend_matrix_index == 0);
    printf("caps %u %u %#x\n", caps.max_vertex_blend_matrices, caps.max_vertex_blend_matrix_index,
            caps.vertex_processing_caps);
    gl.supported[ARB_VERTEX_BLEND] = TRUE;
    gl.limits.blends = 2;
    vp_ffp_get_caps(&gl, &caps);
    assert(caps.max_vertex_blend_matrices == 2 && caps.max_vertex_blend_matrix_index == 0);
    gl.supported[ARB_VERTEX_BLEND] = FALSE;
    gl.limits.blends = 1;

    /* The state handler without ARB_vertex_blend: quiet for 1-3 weights. */
    {
        static const DWORD quiet[] = {WINED3D_VBF_DISABLE, WINED3D_VBF_1WEIGHTS, WINED3D_VBF_2WEIGHTS,
                WINED3D_VBF_3WEIGHTS};
        static const DWORD loud[] = {WINED3D_VBF_TWEENING, WINED3D_VBF_0WEIGHTS};
        for (i = 0; i < 4; ++i)
        {
            reset();
            state.render_states[WINED3D_RS_VERTEXBLEND] = quiet[i];
            state_vertexblend_w(NULL, &state, 0);
            assert(!fixmes);
        }
        for (i = 0; i < 2; ++i)
        {
            reset();
            state.render_states[WINED3D_RS_VERTEXBLEND] = loud[i];
            state_vertexblend_w(NULL, &state, 0);
            assert(fixmes == !i);
        }
    }

    set_view();
    /* Exact: integer world matrices (translations and a permutation), power
     * of two weights. */
    for (mode = WINED3D_VBF_1WEIGHTS; mode <= WINED3D_VBF_3WEIGHTS; ++mode)
    {
        for (i = 0; i < 4; ++i)
        {
            struct wined3d_matrix *m = &state.transforms[WINED3D_TS_WORLD_MATRIX(i)];
            memset(m, 0, sizeof(*m));
            m->_11 = m->_22 = m->_33 = m->_44 = 1.0f;
            if (i == 2) { m->_11 = m->_22 = 0.0f; m->_12 = 1.0f; m->_21 = -1.0f; }
            m->_41 = (float)(2 * i + 1); m->_42 = -(float)i; m->_43 = (float)(i * i);
        }
        array_case(&layouts[2], mode, TRUE, FALSE, 0, 0, 0, 24, 24, TRUE);
        array_case(&layouts[3], mode, FALSE, TRUE, 2, 3, 5, 30, 40, TRUE);
    }

    /* Random affine worlds: rotation, non-uniform scale, shear. */
    for (kind = 0; kind < 4; ++kind)
    {
        for (i = 0; i < 4; ++i)
            world_matrix(&state.transforms[WINED3D_TS_WORLD_MATRIX(i)], kind);
        for (mode = WINED3D_VBF_1WEIGHTS; mode <= WINED3D_VBF_3WEIGHTS; ++mode)
        {
            for (i = 0; i < sizeof(layouts) / sizeof(*layouts); ++i)
            {
                const struct layout *l = &layouts[i];
                array_case(l, mode, TRUE, FALSE, 0, 0, 0, 3, 3, FALSE);
                array_case(l, mode, FALSE, FALSE, 0, 0, 17, 50, 80, FALSE);
                array_case(l, mode, TRUE, FALSE, 0, 4, 2, 300, 400, FALSE);
                array_case(l, mode, TRUE, TRUE, 2, 0, 0, 36, 64, FALSE);
                array_case(l, mode, FALSE, TRUE, 4, 7, 12, 900, 1000, FALSE);
                array_case(l, mode, TRUE, TRUE, 4, 2, 3, 2000, 1500, FALSE);
                slow_case(l, mode);
            }
        }
    }
    gl.supported[ARB_DRAW_ELEMENTS_BASE_VERTEX] = TRUE;
    array_case_base(&layouts[1], WINED3D_VBF_2WEIGHTS, TRUE, TRUE, 2, 0, 9, 0, 60, 70, FALSE);
    array_case_base(&layouts[4], WINED3D_VBF_3WEIGHTS, FALSE, TRUE, 4, 3, 5, 4, 200, 90, FALSE);
    gl.supported[ARB_DRAW_ELEMENTS_BASE_VERTEX] = FALSE;
    gl.supported[ARB_VERTEX_BUFFER_OBJECT] = FALSE;
    array_case(&layouts[0], WINED3D_VBF_1WEIGHTS, FALSE, FALSE, 0, 0, 0, 30, 30, FALSE);
    gl.supported[ARB_VERTEX_BUFFER_OBJECT] = TRUE;

    /* A singular world matrix leaves its normals unchanged (Mesa's identity
     * inverse); the position still blends. */
    {
        struct wined3d_matrix *m = &state.transforms[WINED3D_TS_WORLD_MATRIX(1)];
        float weights[3] = {0.0f, 1.0f, 0.0f}, position[3] = {1.0f, 2.0f, 3.0f}, normal[3] = {0.0f, 0.6f, 0.8f};
        float p[4], n[3];
        BYTE vertex[36];
        memset(m, 0, sizeof(*m));
        m->_11 = 2.0f; m->_44 = 1.0f; m->_41 = 5.0f;
        memcpy(vertex, position, 12); memcpy(vertex + 12, weights, 12); memcpy(vertex + 24, normal, 12);
        streams(&layouts[2], FALSE, 1);
        state.render_states[WINED3D_RS_VERTEXBLEND] = WINED3D_VBF_2WEIGHTS;
        assert(dg_vertex_blend_init(&blend, &context, &state, &si));
        dg_vertex_blend_vertex(&blend, vertex + 12, vertex, vertex + 24, p, n);
        assert(p[0] == 7.0f && p[1] == 0.0f && p[2] == 0.0f && p[3] == 1.0f);
        assert(n[0] == 0.0f && n[1] == 0.6f && n[2] == 0.8f);
    }

    /* Fallbacks: the draw stays exactly unblended. */
    for (i = 0; i < 4; ++i)
        world_matrix(&state.transforms[WINED3D_TS_WORLD_MATRIX(i)], 3);
    fill(&layouts[2], 64, FALSE);
    streams(&layouts[2], TRUE, 64);
    {
        static const DWORD modes[] = {WINED3D_VBF_DISABLE, WINED3D_VBF_TWEENING, WINED3D_VBF_0WEIGHTS, 4};
        for (i = 0; i < 4; ++i)
        {
            state.render_states[WINED3D_RS_VERTEXBLEND] = modes[i];
            declines_init("blend mode", 0);
        }
    }
    state.render_states[WINED3D_RS_VERTEXBLEND] = WINED3D_VBF_2WEIGHTS;
    state.vertex_shader = TRUE;
    declines_init("vertex shader", 0);
    state.vertex_shader = FALSE;
    si.position_transformed = 1;
    declines_init("pretransformed", 0);
    si.position_transformed = 0;
    gl.supported[ARB_VERTEX_BLEND] = TRUE;
    declines_init("ARB_vertex_blend", 0);
    gl.supported[ARB_VERTEX_BLEND] = FALSE;
    d3d.ffp_generic_attributes = TRUE;
    declines_init("generic attributes", 0);
    d3d.ffp_generic_attributes = FALSE;
    si.use_map &= ~(1u << WINED3D_FFP_BLENDWEIGHT);
    declines_init("no weights", 0);
    si.use_map |= 1u << WINED3D_FFP_BLENDWEIGHT;
    si.use_map &= ~(1u << WINED3D_FFP_POSITION);
    declines_init("no position", 0);
    si.use_map |= 1u << WINED3D_FFP_POSITION;
    si.elements[WINED3D_FFP_BLENDWEIGHT].format = format(WINED3DFMT_R16G16B16A16_FLOAT);
    declines_init("FLOAT16 weights", 1);
    si.elements[WINED3D_FFP_BLENDWEIGHT].format = format(WINED3DFMT_R32G32B32_FLOAT);
    si.elements[WINED3D_FFP_NORMAL].format = format(WINED3DFMT_R16G16B16A16_SINT);
    declines_init("SHORT4 normal", 0); /* FIXME once only */
    si.elements[WINED3D_FFP_NORMAL].format = format(WINED3DFMT_R32G32B32_FLOAT);
    si.elements[WINED3D_FFP_POSITION].format = format(WINED3DFMT_R32G32_FLOAT);
    declines_init("FLOAT2 position", 0);
    si.elements[WINED3D_FFP_POSITION].format = format(WINED3DFMT_R32G32B32_FLOAT);

    state.load_base_vertex_index = 0;
    declines_prepare("vertices beyond the stream", FALSE, 0, 60, 5);
    state.load_base_vertex_index = -3;
    declines_prepare("negative vertices", FALSE, 0, 0, 5);
    state.load_base_vertex_index = 2;
    declines_prepare("load base beyond the stream", FALSE, 0, 0, 63);
    state.load_base_vertex_index = 0;
    si.elements[WINED3D_FFP_NORMAL].stride = 0;
    declines_prepare("stride zero", FALSE, 0, 0, 5);
    si.elements[WINED3D_FFP_NORMAL].stride = 36;
    {
        static WORD indices[8] = {0, 1, 2, 63, 64, 5, 6, 7};
        struct wined3d_buffer index_buffer = {{sizeof(indices), indices}, 9};
        state.index_buffer = &index_buffer;
        declines_prepare("index beyond the stream", TRUE, 2, 0, 8);
        declines_prepare("indices beyond the index buffer", TRUE, 2, 4, 5);
        declines_prepare("index size", TRUE, 1, 0, 4);
        state.index_buffer = NULL;
        declines_prepare("no index buffer", TRUE, 2, 0, 4);
    }

    printf("PASS prepared vertex blending: %u array draws and %u immediate-mode vertices blended as "
            "ARB_vertex_blend in eye space (worst relative error %.2g), exact cases bit for bit, Wine's "
            "arrays and modelview restored; %u fallbacks decline before any GL call\n",
            array_draws, slow_vertices, worst, fallbacks);
    return 0;
}
'''

# The pinned FX values (D3D9 GetDeviceCaps on a GeForce FX 5800 HAL device).
FX = {'MaxVertexBlendMatrices': 4, 'MaxVertexBlendMatrixIndex': 0}


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


with tempfile.TemporaryDirectory(prefix='dreamgpu-vertex-blend-') as temporary:
    work = Path(temporary)
    for name in json.loads(MANIFEST.read_text())['files']:
        p = work / name
        p.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'vendor/wine9x' / name, p)
    patches.apply(work, MANIFEST)
    drawprim = (work / 'wined3d/drawprim.c').read_text()
    state = (work / 'wined3d/state.c').read_text()
    private = (work / 'wined3d/wined3d_private.h').read_text()
    public = (work / 'include/wine/wined3d.h').read_text()
    d3d9 = (ROOT / 'vendor/wine9x/d3d9/directx.c').read_text()
    d3d8 = (ROOT / 'vendor/wine9x/d3d8/directx.c').read_text()
    directx = (work / 'wined3d/directx.c').read_text()

    # The device caps copy the vertex pipeline's answer; D3D8 copies it
    # unchanged and D3D9 masks only VertexProcessingCaps.
    assert 'caps->MaxVertexBlendMatrices           = vertex_caps.max_vertex_blend_matrices;' in directx
    assert 'caps->MaxVertexBlendMatrixIndex        = vertex_caps.max_vertex_blend_matrix_index;' in directx
    assert 'adapter->d3d_info.limits.ffp_vertex_blend_matrices = vertex_caps.max_vertex_blend_matrices;' in directx
    assert 'VertexBlend' not in d3d8 and 'VertexBlend' not in d3d9
    assert 'VertexProcessingCaps' not in d3d8
    assert '#define MAX_VERTEX_BLENDS           4' in private

    # drawStridedSlow: the blended path replaces the normal and position of
    # each vertex, with the modelview around glBegin/glEnd, pushed after the
    # last early return (an unmatched push would grow the stack).
    slow = function(drawprim, 'static void drawStridedSlow(')
    assert slow.rindex('return;') < slow.index('dg_vertex_blend_init(&blend, context, state, si)')
    assert slow.index('dg_xyzrhw_arrays(') < slow.index('dg_vertex_blend_init(&blend, context, state, si)') \
        < slow.index('dg_vertex_blend_begin(gl_info, state);') < slow.index('p_glBegin(glPrimType)')
    assert slow.index('dg_vertex_blend_send(gl_info, &blend, SkipnStrides);') \
        < slow.index('else if (normal)') < slow.index('if (position && !blended)')
    assert slow.index('p_glEnd()') < slow.index('dg_vertex_blend_end(gl_info);')
    # draw_primitive: the array path prepares before any GL call and binds
    # around drawStridedFast alone; immediate mode and instancing never
    # take arrays.
    draw = function(drawprim, 'void draw_primitive(')
    fast = draw[draw.index('drawStridedInstanced('):]
    assert fast.index('dg_vertex_blend_init(&blend, context, state, stream_info)') \
        < fast.index('dg_vertex_blend_prepare(&blend_arrays, &blend, context, state, idx_data, idx_size,') \
        < fast.index('dg_fog_coords_bind(gl_info, &fog);') \
        < fast.index('dg_vertex_blend_bind(gl_info, state, &blend, &blend_arrays);') \
        < fast.index('drawStridedFast(') < fast.index('dg_vertex_blend_unbind(gl_info, state, &blend, &blend_arrays);') \
        < fast.index('dg_fog_coords_unbind(gl_info, &fog);')
    assert '!instance_count && dg_vertex_blend_init(' in fast
    assert draw.count('dg_vertex_blend_init(') == 1 and 'blend_arrays.values = NULL;' in draw

    # Device caps from the prepared source: before and after 0052.
    vtxp = re.search(r'caps->vertex_processing_caps = (WINED3DVTXPCAPS_\w+(?:\s*\|\s*WINED3DVTXPCAPS_\w+)*);',
                     function(state, 'static void vp_ffp_get_caps('))[1]
    wined3d_vtxp = caps_bits(re.findall(r'WINED3D(VTXPCAPS_\w+)', vtxp), public)
    mask = re.search(r'pCaps->VertexProcessingCaps &=\s*([^;]+);', d3d9)[1]
    d3d9_vtxp = wined3d_vtxp & caps_bits(re.findall(r'D3D(VTXPCAPS_\w+)', mask), public)
    print('D3D9 VertexProcessingCaps %#x, D3D8 %#x (unchanged by 0052)' % (d3d9_vtxp, wined3d_vtxp))
    assert d3d9_vtxp == 0x13b and wined3d_vtxp == 0x13f

    blend = drawprim[drawprim.index('/* DreamGPU: fixed-function vertex blending without GL_ARB_vertex_blend,'):
                     drawprim.index('/* Context activation is done by the caller. */\nstatic void drawStridedSlow(')]
    arrays = drawprim[drawprim.index('/* DreamGPU: a blended array draw (see dg_vertex_blend)'):
                      drawprim.index('/* Routine common to the draw primitive and draw indexed primitive routines */')]
    enums = ''.join(block(private, opening) for opening in ('enum wined3d_ffp_idx\n', 'struct wined3d_vertex_caps\n'))
    enums += ''.join(block(public, opening) for opening in (
        'enum wined3d_render_state\n', 'enum wined3d_vertex_blend_flags\n', 'enum wined3d_transform_state\n',
        'struct wined3d_matrix\n'))
    for name in ('WINEHIGHEST_RENDER_STATE', 'WINED3D_TS_WORLD_MATRIX', 'WINED3DFVFCAPS_PSIZE',
                 'WINED3DPRASTERCAPS_FOGRANGE'):
        enums += re.search(r'#define ' + re.escape(name) + r'\b.*\n', public)[0]
    enums += ''.join(re.findall(r'#define WINED3DVTXPCAPS_\w+\s+\w+\n', public))
    enums += re.search(r'#define MAX_VERTEX_BLENDS\s+\d+\n', private)[0]

    body = (PREAMBLE.replace('@ENUMS@', enums)
            + function(state, 'static void vp_ffp_get_caps(')
            + function(state, 'static void state_vertexblend_w(')
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
    caps = re.search(r'^caps (\d+) (\d+) (0x[0-9a-f]+)$', result.stdout, re.M)
    assert int(caps[1]) == FX['MaxVertexBlendMatrices'] and int(caps[2]) == FX['MaxVertexBlendMatrixIndex']
    assert int(caps[3], 16) == wined3d_vtxp
