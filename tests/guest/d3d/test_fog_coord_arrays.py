#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Patch 0041 on actual pinned Wine source: specular-alpha vertex fog draws
from arrays instead of WineD3D's software emulation.

D3D vertex fog in the specular alpha ("manual fog coordinates") sent every
fogged fixed-function array draw to drawStridedSlow, which calls
glFogCoordfEXT((float)(specular >> 24)) once per vertex. The prepared helpers
build a float fog coordinate array with exactly those values for every vertex
an array draw reads, and run here under ASan/UBSan: non-indexed and 16/32-bit
indexed draws, base vertex with and without ARB_draw_elements_base_vertex,
odd strides and unaligned colours, a buffer object stream (read through its
system memory copy), a system memory (UP) stream, index buffers in memory and
in buffer objects, heap-sized ranges, no-array states and every fallback that
keeps the emulation. Each array value is compared with a reference that reads
memory the way drawStridedSlow does. Fails without the patch.
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
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned int UINT, DWORD, GLuint, GLenum;
typedef int BOOL, INT, GLsizei;
typedef unsigned short WORD;
typedef unsigned char BYTE;
typedef uintptr_t ULONG_PTR;
#define TRUE 1
#define FALSE 0
#define GL_FLOAT 0x1406
#define GL_ARRAY_BUFFER 0x8892
#define GL_FOG_COORDINATE_ARRAY_EXT 0x8457
#define GL_EXTCALL(f) (gl_info->gl_ops.ext.p_##f)
#define checkGLcall(text) do {} while (0)
enum { WINED3D_FFP_POSITION, WINED3D_FFP_NORMAL, WINED3D_FFP_DIFFUSE, WINED3D_FFP_SPECULAR,
       WINED3D_FFP_TEXCOORD0 };
enum { WINED3D_RS_FOGENABLE, WINED3D_RS_FOGVERTEXMODE, WINED3D_RS_FOGTABLEMODE, RS_COUNT };
enum { WINED3D_FOG_NONE, WINED3D_FOG_EXP, WINED3D_FOG_EXP2, WINED3D_FOG_LINEAR };
enum { WINED3DFMT_B8G8R8A8_UNORM = 1, WINED3DFMT_R32G32B32A32_FLOAT, WINED3DFMT_R32G32B32_FLOAT,
       WINED3DFMT_R8G8B8A8_UNORM };
enum { EXT_FOG_COORD, ARB_DRAW_ELEMENTS_BASE_VERTEX, ARB_VERTEX_BUFFER_OBJECT, EXT_COUNT };
struct wined3d_format { UINT id; };
struct wined3d_bo_address { GLuint buffer_object; BYTE *addr; };
struct wined3d_stream_info_element {
    const struct wined3d_format *format;
    struct wined3d_bo_address data;
    UINT offset;
    GLsizei stride;
    unsigned int stream_idx;
    unsigned int divisor;
};
struct wined3d_stream_info {
    struct wined3d_stream_info_element elements[16];
    DWORD position_transformed : 1;
    DWORD all_vbo : 1;
    WORD swizzle_map, use_map;
};
struct wined3d_resource { UINT size; void *heap_memory; };
struct wined3d_buffer { struct wined3d_resource resource; GLuint buffer_object; };
struct wined3d_stream_state { struct wined3d_buffer *buffer; UINT offset, stride; };
struct wined3d_state {
    DWORD render_states[RS_COUNT];
    struct wined3d_stream_state streams[16];
    struct wined3d_buffer *index_buffer;
    INT base_vertex_index, load_base_vertex_index;
};
struct wined3d_gl_info {
    BOOL supported[EXT_COUNT];
    struct {
        struct { void (*p_glEnableClientState)(GLenum); void (*p_glDisableClientState)(GLenum); } gl;
        struct {
            void (*p_glFogCoordPointerEXT)(GLenum, GLsizei, const void *);
            void (*p_glFogCoordfEXT)(float);
            void (*p_glBindBuffer)(GLenum, GLuint);
        } ext;
    } gl_ops;
};
struct wined3d_d3d_info { BOOL ffp_generic_attributes; };
struct wined3d_context { const struct wined3d_gl_info *gl_info; const struct wined3d_d3d_info *d3d_info; };
/* Wine keeps every buffer object's system memory copy (0037); this returns it. */
static unsigned int sysmem_calls;
static BYTE *buffer_get_sysmem(struct wined3d_buffer *buffer, struct wined3d_context *context)
{
    (void)context;
    ++sysmem_calls;
    return buffer->resource.heap_memory;
}
'''

TEST = r'''
/* GL boundary: fog coordinate array state and the current coordinate. */
static const void *fog_pointer = (const void *)1;
static GLsizei fog_stride = -1;
static BOOL fog_enabled;
static float fog_current = -1.0f;
static GLuint array_buffer = 77;
static unsigned int gl_calls, binds;
static void fog_coord_pointer(GLenum type, GLsizei stride, const void *pointer)
{
    assert(type == GL_FLOAT);
    ++gl_calls;
    fog_stride = stride;
    fog_pointer = pointer;
}
static void fog_coord(float value) { ++gl_calls; fog_current = value; }
static void bind_buffer(GLenum target, GLuint name) { assert(target == GL_ARRAY_BUFFER); ++gl_calls; ++binds; array_buffer = name; }
static void enable(GLenum cap) { assert(cap == GL_FOG_COORDINATE_ARRAY_EXT); ++gl_calls; fog_enabled = TRUE; }
static void disable(GLenum cap) { assert(cap == GL_FOG_COORDINATE_ARRAY_EXT); ++gl_calls; fog_enabled = FALSE; }

static const struct wined3d_format specular_format = {WINED3DFMT_B8G8R8A8_UNORM};
static const struct wined3d_format float4 = {WINED3DFMT_R32G32B32A32_FLOAT};
static const struct wined3d_format float3 = {WINED3DFMT_R32G32B32_FLOAT};
static const struct wined3d_format ubyte4n = {WINED3DFMT_R8G8B8A8_UNORM};

static struct wined3d_gl_info gl;
static struct wined3d_d3d_info d3d_info;
static struct wined3d_context context = {&gl, &d3d_info};
static struct wined3d_state state;
static struct wined3d_stream_info si;

/* A vertex buffer: stride, the specular element's offset in a vertex and the
 * stream offset; every specular alpha distinct (also from red and blue). */
#define VERTICES 600
static BYTE vertex_memory[VERTICES * 37 + 64];
static struct wined3d_buffer vertex_buffer = {{sizeof(vertex_memory), vertex_memory}, 0};
static UINT stride, element_offset, stream_offset;

static void layout(UINT new_stride, UINT new_element_offset, UINT new_stream_offset, GLuint buffer_object)
{
    UINT v;
    stride = new_stride;
    element_offset = new_element_offset;
    stream_offset = new_stream_offset;
    memset(vertex_memory, 0xcd, sizeof(vertex_memory));
    for (v = 0; stream_offset + v * stride + element_offset + 4 <= sizeof(vertex_memory); ++v)
    {
        /* D3DCOLOR 0xAARRGGBB: alpha differs per vertex; red and blue differ
         * from it and each other, so a swizzled read would show. */
        DWORD color = ((v * 37u + 11u) & 0xffu) << 24 | 0x00a01030u;
        memcpy(vertex_memory + stream_offset + v * stride + element_offset, &color, sizeof(color));
    }
    vertex_buffer.buffer_object = buffer_object;
    memset(&si, 0, sizeof(si));
    si.use_map = (1u << WINED3D_FFP_POSITION) | (1u << WINED3D_FFP_DIFFUSE) | (1u << WINED3D_FFP_SPECULAR);
    si.elements[WINED3D_FFP_POSITION].format = &float3;
    si.elements[WINED3D_FFP_DIFFUSE].format = &specular_format;
    si.elements[WINED3D_FFP_SPECULAR].format = &specular_format;
    for (v = 0; v < 16; ++v)
    {
        struct wined3d_stream_info_element *e = &si.elements[v];
        UINT offset = v == WINED3D_FFP_SPECULAR ? element_offset : 0;
        e->stride = (GLsizei)stride;
        e->offset = offset;
        e->data.buffer_object = buffer_object;
        /* context_update_stream_info: a buffer object element holds its
         * offset, a system memory one (UP draws) its address. */
        e->data.addr = (BYTE *)(stream_offset + offset + (buffer_object ? 0 : (ULONG_PTR)vertex_memory));
    }
    state.streams[0].buffer = &vertex_buffer;
}

/* drawStridedSlow, after remove_vbos: the element's memory address, vertex
 * start + i or index + base_vertex_index, glFogCoordfEXT((float)(c >> 24)). */
static float reference(const BYTE *indices, UINT index_size, UINT start, UINT i)
{
    const struct wined3d_stream_info_element *e = &si.elements[WINED3D_FFP_SPECULAR];
    const BYTE *specular = (const BYTE *)((ULONG_PTR)e->data.addr + (e->data.buffer_object ? (ULONG_PTR)vertex_memory : 0));
    long skip = start + i;
    DWORD specularColor;
    if (indices)
        skip = (index_size == 2 ? ((const WORD *)indices)[start + i] : ((const DWORD *)indices)[start + i])
                + state.base_vertex_index;
    memcpy(&specularColor, specular + skip * e->stride, sizeof(specularColor));
    return (float)(specularColor >> 24);
}

/* Prepare and bind as draw_primitive does, then read the bound array the way
 * GL does (index j at pointer + 4j, plus GL's base vertex for an indexed draw)
 * for every vertex, compare with the reference, and unbind. */
static void draw(const void *idx_data, const BYTE *indices, UINT index_size, UINT start, UINT count)
{
    struct dg_fog_coords fog;
    const BOOL indexed = index_size != 0;
    const INT gl_base = indexed && gl.supported[ARB_DRAW_ELEMENTS_BASE_VERTEX] ? state.base_vertex_index : 0;
    long low = -1;
    UINT i;
    assert(dg_fog_coords_prepare(&fog, &context, &state, &si, idx_data, index_size, start, count, indexed, 0));
    assert(fog.values);
    /* The array starts at the lowest GL array index the draw reads. */
    for (i = 0; i < count; ++i)
    {
        long j = indexed ? (long)(index_size == 2 ? ((const WORD *)indices)[start + i]
                                                  : ((const DWORD *)indices)[start + i]) + gl_base
                         : (long)(start + i);
        if (low < 0 || j < low)
            low = j;
    }
    assert((ULONG_PTR)fog.values - fog.pointer == (ULONG_PTR)low * sizeof(float));
    array_buffer = 77;
    dg_fog_coords_bind(&gl, &fog);
    assert(fog_enabled && fog_stride == 0 && (ULONG_PTR)fog_pointer == fog.pointer);
    assert(array_buffer == (gl.supported[ARB_VERTEX_BUFFER_OBJECT] ? 0u : 77u));
    for (i = 0; i < count; ++i)
    {
        long j = indexed ? (long)(index_size == 2 ? ((const WORD *)indices)[start + i]
                                                  : ((const DWORD *)indices)[start + i]) + gl_base
                         : (long)(start + i);
        const float *value = (const float *)(fog.pointer + (ULONG_PTR)j * sizeof(float));
        assert(*value == reference(indices, index_size, start, i));
        /* Coordinates are D3DCOLOR alpha bytes, fog start 255, end 0. */
        assert(*value >= 0.0f && *value <= 255.0f && *value == (float)(int)*value);
    }
    assert(fog.final == reference(indices, index_size, start, count - 1));
    dg_fog_coords_unbind(&gl, &fog);
    assert(!fog_enabled && !fog_pointer && fog_current == reference(indices, index_size, start, count - 1));
    assert(!fog.values);
}

/* No coordinate array: drawStridedSlow sets none either. Nothing touches GL. */
static void none(void)
{
    struct dg_fog_coords fog;
    const unsigned int calls = gl_calls;
    assert(dg_fog_coords_prepare(&fog, &context, &state, &si, NULL, 0, 0, 4, FALSE, 0));
    assert(!fog.values && gl_calls == calls);
}

/* drawStridedSlow stays. */
static void emulated(const void *idx_data, UINT index_size, UINT start, UINT count, UINT instances)
{
    struct dg_fog_coords fog;
    const unsigned int calls = gl_calls;
    assert(!dg_fog_coords_prepare(&fog, &context, &state, &si, idx_data, index_size, start, count,
            index_size != 0, instances));
    assert(!fog.values && gl_calls == calls);
}

int main(void)
{
    WORD indices16[64];
    DWORD indices32[64];
    BYTE index_memory[sizeof(indices32)];
    struct wined3d_buffer index_buffer = {{sizeof(index_memory), index_memory}, 0};
    UINT i;

    gl.supported[EXT_FOG_COORD] = TRUE;
    gl.gl_ops.gl.p_glEnableClientState = enable;
    gl.gl_ops.gl.p_glDisableClientState = disable;
    gl.gl_ops.ext.p_glFogCoordPointerEXT = fog_coord_pointer;
    gl.gl_ops.ext.p_glFogCoordfEXT = fog_coord;
    gl.gl_ops.ext.p_glBindBuffer = bind_buffer;
    state.render_states[WINED3D_RS_FOGENABLE] = TRUE;
    state.render_states[WINED3D_RS_FOGVERTEXMODE] = WINED3D_FOG_NONE;
    state.render_states[WINED3D_RS_FOGTABLEMODE] = WINED3D_FOG_NONE;
    state.index_buffer = &index_buffer;

    /* A static buffer object (D3D7 DrawPrimitiveVB, D3D8/9 DrawPrimitive):
     * stride 32, specular at 20, stream offset 64, start vertex 3. */
    gl.supported[ARB_VERTEX_BUFFER_OBJECT] = TRUE;
    layout(32, 20, 64, 5);
    sysmem_calls = 0;
    draw(NULL, NULL, 0, 3, 7);
    assert(sysmem_calls == 1);
    /* An odd stride and an unaligned colour, from vertex 0. */
    layout(30, 14, 6, 5);
    draw(NULL, NULL, 0, 0, 11);

    /* 16-bit indices in system memory (D3D7 DrawIndexedPrimitiveVB, a dynamic
     * index buffer): start index 4 past decoys, unordered and repeated
     * indices, base vertex 5 loaded into the arrays (no base vertex draws). */
    layout(28, 12, 16, 5);
    for (i = 0; i < 64; ++i)
        indices16[i] = (WORD)(i < 4 ? 500 : (i * 7) % 13);
    indices16[9] = 2;
    memcpy(index_memory, indices16, sizeof(indices16));
    state.base_vertex_index = state.load_base_vertex_index = 5;
    draw(index_memory, index_memory, 2, 4, 9);
    /* The same from an index buffer object: its system memory copy. */
    sysmem_calls = 0;
    draw(NULL, index_memory, 2, 4, 9);
    assert(sysmem_calls == 2);

    /* 32-bit indices with ARB_draw_elements_base_vertex: GL adds base vertex 7
     * to each index, the arrays are loaded from vertex 0. */
    for (i = 0; i < 16; ++i)
        indices32[i] = (DWORD)((i * 5) % 17 + 1);
    memcpy(index_memory, indices32, 16 * sizeof(DWORD));
    gl.supported[ARB_DRAW_ELEMENTS_BASE_VERTEX] = TRUE;
    state.base_vertex_index = 7;
    state.load_base_vertex_index = 0;
    draw(NULL, index_memory, 4, 2, 12);
    gl.supported[ARB_DRAW_ELEMENTS_BASE_VERTEX] = FALSE;
    state.base_vertex_index = state.load_base_vertex_index = 0;

    /* A system memory stream (UP draws and D3DPOOL_SYSTEMMEM buffers) without
     * ARB_vertex_buffer_object: no buffer binding, indexed and not. */
    gl.supported[ARB_VERTEX_BUFFER_OBJECT] = FALSE;
    layout(24, 16, 48, 0);
    binds = 0;
    draw(NULL, NULL, 0, 5, 3);
    memcpy(index_memory, indices16, sizeof(indices16));
    draw(index_memory, index_memory, 2, 6, 5);
    assert(binds == 0);
    gl.supported[ARB_VERTEX_BUFFER_OBJECT] = TRUE;

    /* Pretransformed vertices with a vertex fog mode still take the alpha. */
    si.elements[WINED3D_FFP_POSITION].format = &float4;
    state.render_states[WINED3D_RS_FOGVERTEXMODE] = WINED3D_FOG_LINEAR;
    draw(NULL, NULL, 0, 0, 4);
    si.elements[WINED3D_FFP_POSITION].format = &float3;

    /* More vertices than the local array: heap storage, freed on unbind. */
    layout(36, 8, 0, 5);
    state.render_states[WINED3D_RS_FOGVERTEXMODE] = WINED3D_FOG_NONE;
    draw(NULL, NULL, 0, 2, 500);
    for (i = 0; i < 16; ++i)
        indices16[i] = (WORD)(i & 1 ? 590 : 3);
    memcpy(index_memory, indices16, sizeof(indices16));
    draw(index_memory, index_memory, 2, 0, 16);

    /* No coordinate array where drawStridedSlow sets none: fog disabled, an
     * untransformed vertex fog mode, table fog, no specular, or a specular
     * format other than D3DCOLOR. */
    layout(32, 20, 64, 5);
    state.render_states[WINED3D_RS_FOGENABLE] = FALSE;
    none();
    state.render_states[WINED3D_RS_FOGENABLE] = TRUE;
    state.render_states[WINED3D_RS_FOGVERTEXMODE] = WINED3D_FOG_EXP;
    none();
    state.render_states[WINED3D_RS_FOGVERTEXMODE] = WINED3D_FOG_NONE;
    state.render_states[WINED3D_RS_FOGTABLEMODE] = WINED3D_FOG_LINEAR;
    none();
    state.render_states[WINED3D_RS_FOGTABLEMODE] = WINED3D_FOG_NONE;
    si.use_map &= ~(1u << WINED3D_FFP_SPECULAR);
    none();
    si.use_map |= 1u << WINED3D_FFP_SPECULAR;
    si.elements[WINED3D_FFP_SPECULAR].format = &ubyte4n;
    none();
    si.elements[WINED3D_FFP_SPECULAR].format = &specular_format;

    /* The emulation stays where the array path cannot match it. */
    gl.gl_ops.ext.p_glFogCoordPointerEXT = NULL; /* gl_compat.c's emulated coordinates */
    emulated(NULL, 0, 0, 4, 0);
    gl.gl_ops.ext.p_glFogCoordPointerEXT = fog_coord_pointer;
    d3d_info.ffp_generic_attributes = TRUE;
    emulated(NULL, 0, 0, 4, 0);
    d3d_info.ffp_generic_attributes = FALSE;
    emulated(NULL, 0, 0, 4, 2); /* instancing */
    si.use_map &= ~(1u << WINED3D_FFP_POSITION);
    emulated(NULL, 0, 0, 4, 0);
    si.use_map |= 1u << WINED3D_FFP_POSITION;
    si.elements[WINED3D_FFP_DIFFUSE].stride = 0; /* any stride zero element */
    emulated(NULL, 0, 0, 4, 0);
    si.elements[WINED3D_FFP_DIFFUSE].stride = 32;
    state.load_base_vertex_index = 3; /* array draws load from vertex 0 */
    emulated(NULL, 0, 0, 4, 0);
    state.base_vertex_index = 2; /* indexed: loaded base vertex differs */
    emulated(index_memory, 2, 0, 4, 0);
    state.base_vertex_index = state.load_base_vertex_index = 0;
    /* Vertices beyond the buffer: the last whole one is fine, one more is not. */
    {
        const UINT last = (sizeof(vertex_memory) - 64 - 20 - 4) / 32;
        struct dg_fog_coords fog;
        assert(dg_fog_coords_prepare(&fog, &context, &state, &si, NULL, 0, last, 1, FALSE, 0) && fog.values);
        dg_fog_coords_unbind(&gl, &fog);
        emulated(NULL, 0, last, 2, 0);
        emulated(NULL, 0, 0xffffff00u, 0x100, 0);
        indices16[0] = (WORD)(last + 1);
        memcpy(index_memory, indices16, sizeof(indices16));
        emulated(index_memory, 2, 0, 1, 0);
    }
    /* Indices beyond the index buffer (128 16-bit or 64 32-bit entries). */
    emulated(index_memory, 2, 128, 1, 0);
    emulated(index_memory, 4, 63, 2, 0);
    emulated(index_memory, 2, 0xffffffffu, 2, 0);
    /* A negative GL array index (base vertex draws). */
    gl.supported[ARB_DRAW_ELEMENTS_BASE_VERTEX] = TRUE;
    state.base_vertex_index = -4;
    indices16[0] = 3;
    memcpy(index_memory, indices16, sizeof(indices16));
    emulated(index_memory, 2, 0, 1, 0);
    state.base_vertex_index = 0;
    gl.supported[ARB_DRAW_ELEMENTS_BASE_VERTEX] = FALSE;
    /* A system memory element below its buffer's memory. */
    layout(32, 20, 64, 0);
    si.elements[WINED3D_FFP_SPECULAR].data.addr = (BYTE *)((ULONG_PTR)vertex_memory - 4);
    emulated(NULL, 0, 0, 1, 0);

    puts("PASS prepared fog coordinate arrays: drawStridedSlow's specular alpha for every vertex of "
         "array, indexed and base vertex draws from buffer objects and system memory, no array "
         "without coordinates, emulation kept where memory cannot be read");
    return 0;
}
'''


def function(source, signature):
    start = source.index(signature)
    end = source.index('\n}\n', start) + 3
    return source[start:end] + '\n'


with tempfile.TemporaryDirectory(prefix='dreamgpu-fog-coords-') as temporary:
    work = Path(temporary)
    for name in json.loads(MANIFEST.read_text())['files']:
        p = work / name
        p.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'vendor/wine9x' / name, p)
    patches.apply(work, MANIFEST)
    drawprim = (work / 'wined3d/drawprim.c').read_text()
    compat = (work / 'wined3d/gl_compat.c').read_text()

    # The reference above is drawStridedSlow's: its condition, index loads
    # and coordinate stay as they were.
    slow = function(drawprim, 'static void drawStridedSlow(')
    assert re.search(r'if \(state->render_states\[WINED3D_RS_FOGENABLE\]\s+'
                     r'&& \(state->render_states\[WINED3D_RS_FOGVERTEXMODE\] == WINED3D_FOG_NONE\s+'
                     r'\|\| si->elements\[WINED3D_FFP_POSITION\]\.format->id == WINED3DFMT_R32G32B32A32_FLOAT\)\s+'
                     r'&& state->render_states\[WINED3D_RS_FOGTABLEMODE\] == WINED3D_FOG_NONE\)', slow)
    assert 'if (element->format->id == WINED3DFMT_B8G8R8A8_UNORM) specular_fog = TRUE;' in slow
    assert 'SkipnStrides = pIdxBufS[startIdx + vx_index] + state->base_vertex_index;' in slow
    assert 'SkipnStrides = pIdxBufL[startIdx + vx_index] + state->base_vertex_index;' in slow
    assert 'DWORD specularColor = *(const DWORD *)ptrToCoords;' in slow
    assert 'GL_EXTCALL(glFogCoordfEXT((float) (specularColor >> 24)));' in slow

    # draw_primitive: only an unreadable array draw is still emulated; the
    # array binds around drawStridedFast alone.
    draw = function(drawprim, 'void draw_primitive(')
    materials = draw.index('Using software emulation because not all material properties could be tracked.')
    branch = draw.index('else if (context->fog_coord && state->render_states[WINED3D_RS_FOGENABLE]')
    condition = draw[branch:draw.index('{', branch)]
    assert materials < branch
    assert re.fullmatch(r'else if \(context->fog_coord && state->render_states\[WINED3D_RS_FOGENABLE\]\s+'
                        r'&& !context->use_immediate_mode_draw\s+'
                        r'&& !dg_fog_coords_prepare\(&fog, context, state, stream_info, idx_data, idx_size,\s+'
                        r'start_idx, index_count, indexed, instance_count\)\)\s+', condition), condition
    assert draw.index('emulation = TRUE;', branch) < draw.index('if (emulation)')
    assert draw.index('fog.values = NULL;') < materials
    assert draw.index('if (context->instance_count)') < branch < draw.index('if (context->use_immediate_mode_draw || emulation)')
    fast = draw.index('drawStridedFast(gl_info,')
    assert draw.rindex('dg_fog_coords_bind(gl_info, &fog);', 0, fast) > draw.rindex('else', 0, fast) > \
        draw.index('drawStridedInstanced(')
    assert fast < draw.index('dg_fog_coords_unbind(gl_info, &fog);') < draw.index('wined3d_event_query_issue')
    assert draw.count('dg_fog_coords_bind(') == draw.count('dg_fog_coords_unbind(') == 1

    # gl_compat.c's emulated coordinates have no array.
    emulation = compat[compat.index('case EXT_FOG_COORD:'):compat.index('default:', compat.index('case EXT_FOG_COORD:'))]
    assert emulation.index('gl_info->gl_ops.ext.p_glFogCoordfEXT = wine_glFogCoordfEXT;') < \
        emulation.index('gl_info->gl_ops.ext.p_glFogCoordPointerEXT = NULL;') < \
        emulation.index('gl_info->supported[EXT_FOG_COORD] = TRUE;')
    # The packed pretransformed path needs the same entry point for its fog.
    assert 'gl_info->gl_ops.ext.p_glFogCoordPointerEXT &&' in (ROOT / 'guest/d3d/wine-vertex-array.h').read_text()

    start = drawprim.index('#define DG_FOG_LOCAL_COORDS')
    declarations = drawprim[start:drawprim.index('\n};\n', start) + 4]
    body = declarations + ''.join(function(drawprim, signature) for signature in (
        'static BOOL dg_fog_coord_range(', 'static BOOL dg_fog_coord_fill(',
        'static BOOL dg_fog_coords_prepare(', 'static void dg_fog_coords_bind(',
        'static void dg_fog_coords_unbind('))
    (work / 'test.c').write_text(PREAMBLE + body + TEST)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer',
                    str(work / 'test.c'), '-o', str(work / 'test')], check=True)
    sys.exit(subprocess.run([str(work / 'test')]).returncode)
