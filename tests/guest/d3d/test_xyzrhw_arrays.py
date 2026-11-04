#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Patch 0048 on actual pinned Wine source: pretransformed (XYZRHW)
fixed-function draws outside buffer objects draw from arrays instead of
drawStridedSlow's immediate mode.

The prepared drawStridedSlow, 0041's fog coordinate helpers, the 0048 array
path and directx.c's fixed-function attribute functions (position_float4,
diffuse_d3dcolor, ..., wined3d_adapter_init_ffp_attrib_ops) are compiled
unchanged under ASan/UBSan against a recording GL. That GL assembles vertices
the way GL does: immediate-mode glVertex takes the current attributes, an
array draw reads each enabled array (size, type, stride, BGRA and
normalization) at each array index and the current value otherwise, and the
current value of every enabled array becomes undefined (poisoned) after an
array draw. Each case draws once through drawStridedSlow's immediate mode
(the 0048 call answering FALSE) and once through the 0048 path, and compares
every vertex bit for bit (position, colour, secondary colour, normal, all
eight texture coordinate sets, fog coordinate) and the current attributes
left behind. Cases: user-memory sized batches below and above the local
storage, 16- and 32-bit indices with positive, zero and negative base
vertices and unordered repeated indices, rhw 0, 1, -0, fractional and large,
one to four texture units with swapped, missing, generated and untextured
stages, a pixel shader, every accepted colour, normal and coordinate format,
specular-alpha fog and every no-coordinate fog state, absent inputs, ignored
point size and blend weights, every primitive type, and hosts without buffer
objects, secondary colour or real multitexture. Every fallback must answer
FALSE before any GL call, after which drawStridedSlow draws as before. The
recorded calls are also priced with the frontend's record format (32-byte
headers, one CALL record per immediate-mode attribute, one DATA record per
array draw laid out by guest/include/gl-arrays.h, 1024 records and 64 KiB per
packet) and printed. Fails without the patch.
"""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import importlib.util

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('patches', ROOT / 'tests/guest/support/patches.py')
patches = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patches)
MANIFEST = ROOT / 'support/guest/d3d/patches/base/manifest.json'

PREAMBLE = r'''
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gl-arrays.h"
typedef unsigned int UINT, DWORD, GLuint, GLenum;
typedef int BOOL, INT, GLint, GLsizei, LONG;
typedef unsigned short WORD, GLushort, GLhalfNV;
typedef short GLshort;
typedef unsigned char BYTE, GLubyte;
typedef float GLfloat;
typedef uintptr_t ULONG_PTR;
#define TRUE 1
#define FALSE 0
#define WINE_GLAPI
#define MAX_TEXTURES 8
#define MAX_ATTRIBS 16
#define MAX_COMBINED_SAMPLERS 20
#define WINED3DDP_MAXTEXCOORD 8
#define WINED3D_UNMAPPED_STAGE ~0U
#define GL_EXTCALL(f) (gl_info->gl_ops.ext.p_##f)
#define checkGLcall(text) do {} while (0)
#define TRACE(...) do {} while (0)
#define TRACE_(channel) TRACE
#define WARN(...) do {} while (0)
#define FIXME(...) do {} while (0)
#define ERR(...) do { ++wine_errors; } while (0)
#define DebugBreak() abort()
static unsigned int wine_errors;
static const char *debug_d3dformat(DWORD id) { (void)id; return ""; }
#define D3DCOLOR_B_R(dw) (((dw) >> 16) & 0xff)
#define D3DCOLOR_B_G(dw) (((dw) >>  8) & 0xff)
#define D3DCOLOR_B_B(dw) (((dw) >>  0) & 0xff)
#define D3DCOLOR_B_A(dw) (((dw) >> 24) & 0xff)
#define GL_POINTS 0x0000
#define GL_LINES 0x0001
#define GL_LINE_STRIP 0x0003
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_TRIANGLE_FAN 0x0006
#define GL_FRONT_AND_BACK 0x0408
#define GL_SHORT 0x1402
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_SHORT 0x1403
#define GL_UNSIGNED_INT 0x1405
#define GL_FLOAT 0x1406
#define GL_BGRA 0x80e1
#define GL_VERTEX_ARRAY 0x8074
#define GL_NORMAL_ARRAY 0x8075
#define GL_COLOR_ARRAY 0x8076
#define GL_TEXTURE_COORD_ARRAY 0x8078
#define GL_FOG_COORDINATE_ARRAY_EXT 0x8457
#define GL_SECONDARY_COLOR_ARRAY_EXT 0x845e
#define GL_TEXTURE0_ARB 0x84c0
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
@ENUMS@
enum { ARB_MULTITEXTURE, ARB_VERTEX_BUFFER_OBJECT, EXT_SECONDARY_COLOR, EXT_FOG_COORD, NV_HALF_FLOAT,
       ARB_VERTEX_ARRAY_BGRA, EXT_COUNT };
enum { WINED3DFMT_UNKNOWN, WINED3DFMT_R32_FLOAT, WINED3DFMT_R32G32_FLOAT, WINED3DFMT_R32G32B32_FLOAT,
       WINED3DFMT_R32G32B32A32_FLOAT, WINED3DFMT_B8G8R8A8_UNORM, WINED3DFMT_R8G8B8A8_UNORM,
       WINED3DFMT_R16G16_SINT, WINED3DFMT_R16G16B16A16_SINT, WINED3DFMT_R16G16B16A16_SNORM,
       WINED3DFMT_R16G16_FLOAT };
struct wined3d_format
{
    UINT id;
    UINT byte_count;
    enum wined3d_ffp_emit_idx emit_idx;
    GLint component_count;
    GLenum gl_vtx_type;
    GLint gl_vtx_format;
    BOOL gl_normalized;
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
struct wined3d_texture { int unused; };
struct wined3d_shader { int unused; };
struct wined3d_state
{
    DWORD render_states[WINEHIGHEST_RENDER_STATE + 1];
    DWORD texture_states[MAX_TEXTURES][WINED3D_HIGHEST_TEXTURE_STATE + 1];
    struct wined3d_texture *textures[MAX_COMBINED_SAMPLERS];
    struct wined3d_stream_state streams[16];
    struct wined3d_buffer *index_buffer;
    INT base_vertex_index, load_base_vertex_index;
    struct wined3d_shader *pixel_shader;
};
static BOOL use_ps(const struct wined3d_state *state) { return !!state->pixel_shader; }
struct wined3d_device { struct wined3d_state state; };
typedef void (WINE_GLAPI *wined3d_ffp_attrib_func)(const void *data);
typedef void (WINE_GLAPI *wined3d_ffp_texcoord_func)(GLenum unit, const void *data);
struct wined3d_ffp_attrib_ops
{
    wined3d_ffp_attrib_func position[WINED3D_FFP_EMIT_COUNT];
    wined3d_ffp_attrib_func diffuse[WINED3D_FFP_EMIT_COUNT];
    wined3d_ffp_attrib_func specular[WINED3D_FFP_EMIT_COUNT];
    wined3d_ffp_attrib_func normal[WINED3D_FFP_EMIT_COUNT];
    wined3d_ffp_texcoord_func texcoord[WINED3D_FFP_EMIT_COUNT];
};
struct wined3d_d3d_info
{
    struct { UINT ffp_blend_stages; } limits;
    struct wined3d_ffp_attrib_ops ffp_attrib_ops;
    BOOL xyzrhw, ffp_generic_attributes;
};
/* Entry points the attribute table only takes through its casts take its
 * parameter types. */
typedef void (*vector)(const void *);
typedef void (*unit_vector)(GLenum, const void *);
struct wined3d_gl_info
{
    BOOL supported[EXT_COUNT];
    struct { UINT textures, texture_coords; } limits;
    struct
    {
        struct
        {
            void (*p_glBegin)(GLenum);
            void (*p_glEnd)(void);
            void (*p_glVertex4f)(GLfloat, GLfloat, GLfloat, GLfloat);
            vector p_glVertex3fv, p_glVertex4fv, p_glVertex2sv;
            void (*p_glVertex4s)(GLshort, GLshort, GLshort, GLshort);
            void (*p_glColor4f)(GLfloat, GLfloat, GLfloat, GLfloat);
            void (*p_glColor4ub)(GLubyte, GLubyte, GLubyte, GLubyte);
            vector p_glColor3fv, p_glColor4fv, p_glColor4ubv, p_glColor4sv, p_glColor4usv;
            void (*p_glNormal3f)(GLfloat, GLfloat, GLfloat);
            vector p_glNormal3fv;
            void (*p_glTexCoord4f)(GLfloat, GLfloat, GLfloat, GLfloat);
            void (*p_glMaterialfv)(GLenum, GLenum, const GLfloat *);
            void (*p_glVertexPointer)(GLint, GLenum, GLsizei, const void *);
            void (*p_glNormalPointer)(GLenum, GLsizei, const void *);
            void (*p_glColorPointer)(GLint, GLenum, GLsizei, const void *);
            void (*p_glTexCoordPointer)(GLint, GLenum, GLsizei, const void *);
            void (*p_glEnableClientState)(GLenum);
            void (*p_glDisableClientState)(GLenum);
            void (*p_glDrawArrays)(GLenum, GLint, GLsizei);
            void (*p_glDrawElements)(GLenum, GLsizei, GLenum, const void *);
        } gl;
        struct
        {
            void (*p_glBindBuffer)(GLenum, GLuint);
            void (*p_glClientActiveTextureARB)(GLenum);
            void (*p_glSecondaryColor3fEXT)(GLfloat, GLfloat, GLfloat);
            vector p_glSecondaryColor3fvEXT, p_glSecondaryColor3ubvEXT;
            void (*p_glSecondaryColorPointerEXT)(GLint, GLenum, GLsizei, const void *);
            void (*p_glFogCoordfEXT)(GLfloat);
            void (*p_glFogCoordPointerEXT)(GLenum, GLsizei, const void *);
            void (*p_glMultiTexCoord4fARB)(GLenum, GLfloat, GLfloat, GLfloat, GLfloat);
            unit_vector p_glMultiTexCoord1fvARB, p_glMultiTexCoord2fvARB, p_glMultiTexCoord3fvARB,
                    p_glMultiTexCoord4fvARB, p_glMultiTexCoord2svARB, p_glMultiTexCoord4svARB,
                    p_glMultiTexCoord2hvNV, p_glMultiTexCoord4hvNV;
        } ext;
    } gl_ops;
};
struct wined3d_context
{
    const struct wined3d_gl_info *gl_info;
    const struct wined3d_d3d_info *d3d_info;
    DWORD tex_unit_map[MAX_COMBINED_SAMPLERS];
    DWORD use_immediate_mode_draw : 1;
    DWORD namedArraysLoaded : 1;
    DWORD numberedArraysLoaded : 1;
    DWORD fog_coord : 1;
    UINT num_untracked_materials;
    GLenum untracked_materials[2];
};
struct wined3d_adapter { struct wined3d_gl_info gl_info; struct wined3d_d3d_info d3d_info; };
static struct wined3d_context *current_context;
static struct wined3d_context *context_get_current(void) { return current_context; }
static unsigned int sysmem_calls;
/* Wine keeps every buffer's system memory copy (0037); this returns it. */
static BYTE *buffer_get_sysmem(struct wined3d_buffer *buffer, struct wined3d_context *context)
{
    (void)context;
    ++sysmem_calls;
    return buffer->resource.heap_memory;
}
/* 0006's packed path, which runs first, declines here: every case reaches 0048. */
static unsigned int packed_calls;
static BOOL dg_wine_draw_transformed(const struct wined3d_device *device, struct wined3d_context *context,
        const struct wined3d_stream_info *si, UINT count, GLenum primitive, const void *indices,
        UINT index_size, UINT first)
{
    (void)device; (void)context; (void)si; (void)count; (void)primitive; (void)indices;
    (void)index_size; (void)first;
    ++packed_calls;
    return FALSE;
}
/* drawStridedSlow's 0048 call: FALSE draws the immediate-mode reference. */
static BOOL dg_xyzrhw_arrays(const struct wined3d_device *device, struct wined3d_context *context,
        const struct wined3d_stream_info *si, UINT count, GLenum primitive,
        const void *indices, UINT index_size, UINT start);
static BOOL use_arrays, arrays_result;
static unsigned int arrays_calls, gl_calls, calls_in_arrays;
static BOOL test_arrays_hook(const struct wined3d_device *device, struct wined3d_context *context,
        const struct wined3d_stream_info *si, UINT count, GLenum primitive,
        const void *indices, UINT index_size, UINT start)
{
    unsigned int before = gl_calls;
    if (!use_arrays)
        return FALSE;
    ++arrays_calls;
    arrays_result = dg_xyzrhw_arrays(device, context, si, count, primitive, indices, index_size, start);
    calls_in_arrays = gl_calls - before;
    return arrays_result;
}
'''

TEST = r'''
/* ------------------------------------------------------------------------
 * The recording GL. */
struct attribs
{
    float color[4], secondary[3], normal[3], tex[MAX_TEXTURES][4], fog;
};
struct vertex
{
    float position[4];
    struct attribs a;
};
enum { SLOT_VERTEX, SLOT_NORMAL, SLOT_COLOR, SLOT_SECONDARY, SLOT_FOG, SLOT_TEX0, SLOT_COUNT = SLOT_TEX0 + MAX_TEXTURES };
struct array
{
    BOOL enabled;
    GLint size;
    GLenum type;
    GLsizei stride;
    const void *pointer;
};
/* The frontend's transport, priced per record; log holds each record's
 * payload words so that back-to-back draws can be replayed into packets. */
struct cost
{
    unsigned long calls, records, bytes, packets, used_words, packet_records;
    unsigned int *log, logged, capacity;
};
static struct
{
    struct attribs current;
    struct array arrays[SLOT_COUNT];
    GLuint array_buffer, element_buffer;
    unsigned int client_unit, begins, draws, vertex_calls, binds;
    BOOL in_begin, vbo;
    struct vertex *out;
    unsigned int out_count, out_capacity;
    struct cost cost;
} gl;

static void packet(struct cost *c, unsigned long words)
{
    /* Record(): an 8-word header; flush at the packet's words or records. */
    if (c->used_words + 8 + words > DG_ESCAPE_WORDS || c->packet_records == 1024)
    {
        ++c->packets;
        c->used_words = c->packet_records = 0;
    }
    c->used_words += 8 + words;
    ++c->packet_records;
    ++c->records;
    c->bytes += (8 + words) * 4;
    if (c->log)
    {
        if (c->logged == c->capacity)
        {
            c->capacity *= 2;
            c->log = realloc(c->log, c->capacity * sizeof(*c->log));
            assert(c->log);
        }
        c->log[c->logged++] = (unsigned int)words;
    }
}
/* A CALL record: function and argument words. */
static void call_record(unsigned int words) { packet(&gl.cost, 1 + words); }
static void finish(struct cost *c)
{
    if (c->packet_records)
        ++c->packets;
    c->used_words = c->packet_records = 0;
}

static void emit(float x, float y, float z, float w)
{
    struct vertex *v;
    assert(gl.in_begin);
    if (gl.out_count == gl.out_capacity)
    {
        gl.out_capacity = gl.out_capacity ? 2 * gl.out_capacity : 256;
        gl.out = realloc(gl.out, gl.out_capacity * sizeof(*gl.out));
        assert(gl.out);
    }
    v = &gl.out[gl.out_count++];
    v->position[0] = x;
    v->position[1] = y;
    v->position[2] = z;
    v->position[3] = w;
    v->a = gl.current;
}
static float ub(GLubyte v) { return v / 255.0f; }
static unsigned int unit(GLenum target)
{
    assert(target >= GL_TEXTURE0_ARB && target < GL_TEXTURE0_ARB + MAX_TEXTURES);
    return target - GL_TEXTURE0_ARB;
}
static void set_tex(unsigned int u, float s, float t, float r, float q)
{
    gl.current.tex[u][0] = s;
    gl.current.tex[u][1] = t;
    gl.current.tex[u][2] = r;
    gl.current.tex[u][3] = q;
}

#define COUNTED ++gl_calls; ++gl.cost.calls
static void rec_begin(GLenum mode) { COUNTED; (void)mode; assert(!gl.in_begin); gl.in_begin = TRUE; ++gl.begins; call_record(1); }
static void rec_end(void) { COUNTED; assert(gl.in_begin); gl.in_begin = FALSE; call_record(0); }
static void rec_vertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w) { COUNTED; ++gl.vertex_calls; emit(x, y, z, w); call_record(4); }
static void rec_vertex3fv(const void *p) { const float *v = p; COUNTED; ++gl.vertex_calls; emit(v[0], v[1], v[2], 1.0f); call_record(3); }
static void rec_vertex4fv(const void *p) { const float *v = p; COUNTED; ++gl.vertex_calls; emit(v[0], v[1], v[2], v[3]); call_record(4); }
static void rec_unexpected_vector(const void *p) { (void)p; assert(!"unexpected attribute entry point"); }
static void rec_unexpected_vertex4s(GLshort x, GLshort y, GLshort z, GLshort w) { (void)x; (void)y; (void)z; (void)w; assert(!"unexpected glVertex4s"); }
static void rec_color4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a)
{
    COUNTED;
    gl.current.color[0] = r; gl.current.color[1] = g; gl.current.color[2] = b; gl.current.color[3] = a;
    call_record(4);
}
static void rec_color4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a)
{
    COUNTED;
    gl.current.color[0] = ub(r); gl.current.color[1] = ub(g); gl.current.color[2] = ub(b); gl.current.color[3] = ub(a);
    call_record(4);
}
static void rec_color4ubv(const void *p)
{
    const GLubyte *v = p;
    COUNTED;
    gl.current.color[0] = ub(v[0]); gl.current.color[1] = ub(v[1]); gl.current.color[2] = ub(v[2]); gl.current.color[3] = ub(v[3]);
    call_record(4);
}
static void rec_color3fv(const void *p)
{
    const float *v = p;
    COUNTED;
    gl.current.color[0] = v[0]; gl.current.color[1] = v[1]; gl.current.color[2] = v[2]; gl.current.color[3] = 1.0f;
    call_record(3);
}
static void rec_color4fv(const void *p)
{
    const float *v = p;
    COUNTED;
    memcpy(gl.current.color, v, sizeof(gl.current.color));
    call_record(4);
}
static void rec_normal3f(GLfloat x, GLfloat y, GLfloat z)
{
    COUNTED;
    gl.current.normal[0] = x; gl.current.normal[1] = y; gl.current.normal[2] = z;
    call_record(3);
}
static void rec_normal3fv(const void *p) { const float *v = p; rec_normal3f(v[0], v[1], v[2]); }
static void rec_secondary3f(GLfloat r, GLfloat g, GLfloat b)
{
    COUNTED;
    gl.current.secondary[0] = r; gl.current.secondary[1] = g; gl.current.secondary[2] = b;
    call_record(3);
}
static void rec_secondary3fv(const void *p) { const float *v = p; rec_secondary3f(v[0], v[1], v[2]); }
static void rec_secondary3ubv(const void *p) { const GLubyte *v = p; rec_secondary3f(ub(v[0]), ub(v[1]), ub(v[2])); }
static void rec_fog(GLfloat f) { COUNTED; gl.current.fog = f; call_record(1); }
static void rec_texcoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q) { COUNTED; set_tex(0, s, t, r, q); call_record(4); }
static void rec_multi4f(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q) { COUNTED; set_tex(unit(target), s, t, r, q); call_record(5); }
static void rec_multi1fv(GLenum target, const void *p) { const float *v = p; COUNTED; set_tex(unit(target), v[0], 0, 0, 1); call_record(5); }
static void rec_multi2fv(GLenum target, const void *p) { const float *v = p; COUNTED; set_tex(unit(target), v[0], v[1], 0, 1); call_record(5); }
static void rec_multi3fv(GLenum target, const void *p) { const float *v = p; COUNTED; set_tex(unit(target), v[0], v[1], v[2], 1); call_record(5); }
static void rec_multi4fv(GLenum target, const void *p) { const float *v = p; COUNTED; set_tex(unit(target), v[0], v[1], v[2], v[3]); call_record(5); }
static void rec_multi2sv(GLenum target, const void *p) { const GLshort *v = p; COUNTED; set_tex(unit(target), v[0], v[1], 0, 1); call_record(5); }
static void rec_multi4sv(GLenum target, const void *p) { const GLshort *v = p; COUNTED; set_tex(unit(target), v[0], v[1], v[2], v[3]); call_record(5); }
static void rec_unexpected_unit_vector(GLenum target, const void *p) { (void)target; (void)p; assert(!"unexpected coordinate entry point"); }
static void rec_material(GLenum face, GLenum name, const GLfloat *v) { COUNTED; (void)face; (void)name; (void)v; call_record(6); }

/* Client array state is guest-local in the frontend: no record. */
static void pointer(unsigned int slot, GLint size, GLenum type, GLsizei stride, const void *p)
{
    COUNTED;
    assert(!gl.in_begin && stride >= 0);
    /* Client memory: Wine's array loaders leave a buffer object bound. */
    assert(!gl.vbo || !gl.array_buffer);
    gl.arrays[slot].size = size;
    gl.arrays[slot].type = type;
    gl.arrays[slot].stride = stride;
    gl.arrays[slot].pointer = p;
}
static void rec_vertex_pointer(GLint size, GLenum type, GLsizei stride, const void *p)
{
    assert(size == 4 && type == GL_FLOAT);
    pointer(SLOT_VERTEX, size, type, stride, p);
}
static void rec_normal_pointer(GLenum type, GLsizei stride, const void *p)
{
    assert(type == GL_FLOAT);
    pointer(SLOT_NORMAL, 3, type, stride, p);
}
static void rec_color_pointer(GLint size, GLenum type, GLsizei stride, const void *p)
{
    assert((size == GL_BGRA && type == GL_UNSIGNED_BYTE) || ((size == 3 || size == 4) && (type == GL_FLOAT || type == GL_UNSIGNED_BYTE)));
    pointer(SLOT_COLOR, size, type, stride, p);
}
static void rec_secondary_pointer(GLint size, GLenum type, GLsizei stride, const void *p)
{
    assert((size == GL_BGRA && type == GL_UNSIGNED_BYTE) || (size == 3 && type == GL_FLOAT));
    pointer(SLOT_SECONDARY, size, type, stride, p);
}
static void rec_fog_pointer(GLenum type, GLsizei stride, const void *p)
{
    assert(type == GL_FLOAT);
    pointer(SLOT_FOG, 1, type, stride, p);
}
static void rec_texcoord_pointer(GLint size, GLenum type, GLsizei stride, const void *p)
{
    assert(size >= 1 && size <= 4 && (type == GL_FLOAT || type == GL_SHORT));
    pointer(SLOT_TEX0 + gl.client_unit, size, type, stride, p);
}
static unsigned int cap_slot(GLenum cap)
{
    switch (cap)
    {
        case GL_VERTEX_ARRAY: return SLOT_VERTEX;
        case GL_NORMAL_ARRAY: return SLOT_NORMAL;
        case GL_COLOR_ARRAY: return SLOT_COLOR;
        case GL_SECONDARY_COLOR_ARRAY_EXT: return SLOT_SECONDARY;
        case GL_FOG_COORDINATE_ARRAY_EXT: return SLOT_FOG;
        case GL_TEXTURE_COORD_ARRAY: return SLOT_TEX0 + gl.client_unit;
        default: assert(!"client state"); return 0;
    }
}
static void rec_enable(GLenum cap) { COUNTED; assert(!gl.in_begin); gl.arrays[cap_slot(cap)].enabled = TRUE; }
static void rec_disable(GLenum cap) { COUNTED; assert(!gl.in_begin); gl.arrays[cap_slot(cap)].enabled = FALSE; }
static void rec_client_active(GLenum texture) { COUNTED; gl.client_unit = unit(texture); }
static void rec_bind(GLenum target, GLuint name)
{
    COUNTED;
    assert(gl.vbo && !gl.in_begin);
    ++gl.binds;
    if (target == GL_ARRAY_BUFFER)
        gl.array_buffer = name;
    else
    {
        assert(target == GL_ELEMENT_ARRAY_BUFFER);
        gl.element_buffer = name;
    }
}

static unsigned int type_bytes(GLenum type)
{
    switch (type)
    {
        case GL_UNSIGNED_BYTE: return 1;
        case GL_SHORT: case GL_UNSIGNED_SHORT: return 2;
        case GL_FLOAT: case GL_UNSIGNED_INT: return 4;
        default: assert(!"type"); return 0;
    }
}
static float component(const BYTE *p, GLenum type, BOOL normalized)
{
    switch (type)
    {
        case GL_FLOAT: { float v; memcpy(&v, p, 4); return v; }
        case GL_UNSIGNED_BYTE: return normalized ? ub(*p) : *p;
        case GL_SHORT: { GLshort v; memcpy(&v, p, 2); assert(!normalized); return v; }
        default: assert(!"component type"); return 0;
    }
}
/* GL's array element j: each enabled array, else the current value. */
static void element(unsigned int j)
{
    float values[SLOT_COUNT][4];
    unsigned int s, n;
    struct attribs saved = gl.current;
    assert(gl.arrays[SLOT_VERTEX].enabled);
    for (s = 0; s < SLOT_COUNT; ++s)
    {
        const struct array *a = &gl.arrays[s];
        const BOOL bgra = a->size == GL_BGRA;
        const unsigned int size = bgra ? 4 : (unsigned int)a->size, unit_bytes = a->enabled ? type_bytes(a->type) : 0;
        const BYTE *p;
        if (!a->enabled)
            continue;
        assert(a->pointer);
        p = (const BYTE *)a->pointer + (ULONG_PTR)j * (a->stride ? (unsigned int)a->stride : size * unit_bytes);
        values[s][0] = values[s][1] = values[s][2] = 0.0f;
        values[s][3] = 1.0f;
        for (n = 0; n < size; ++n)
        {
            /* BGRA: memory B, G, R, A is R, G, B, A. */
            const unsigned int at = bgra && n != 1 && n != 3 ? 2 - n : n;
            values[s][n] = component(p + at * unit_bytes, a->type, s == SLOT_COLOR || s == SLOT_SECONDARY || s == SLOT_NORMAL);
        }
    }
    if (gl.arrays[SLOT_COLOR].enabled)
        memcpy(gl.current.color, values[SLOT_COLOR], sizeof(gl.current.color));
    if (gl.arrays[SLOT_SECONDARY].enabled)
        memcpy(gl.current.secondary, values[SLOT_SECONDARY], sizeof(gl.current.secondary));
    if (gl.arrays[SLOT_NORMAL].enabled)
        memcpy(gl.current.normal, values[SLOT_NORMAL], sizeof(gl.current.normal));
    if (gl.arrays[SLOT_FOG].enabled)
        gl.current.fog = values[SLOT_FOG][0];
    for (s = 0; s < MAX_TEXTURES; ++s)
        if (gl.arrays[SLOT_TEX0 + s].enabled)
            memcpy(gl.current.tex[s], values[SLOT_TEX0 + s], sizeof(gl.current.tex[s]));
    gl.in_begin = TRUE;
    emit(values[SLOT_VERTEX][0], values[SLOT_VERTEX][1], values[SLOT_VERTEX][2], values[SLOT_VERTEX][3]);
    gl.in_begin = FALSE;
    gl.current = saved;
}
/* After an array draw an enabled array's current attribute is undefined. */
static void poison(void)
{
    static const unsigned int bits = 0x7fc0dead;
    float p;
    unsigned int s, n;
    memcpy(&p, &bits, sizeof(p));
    if (gl.arrays[SLOT_COLOR].enabled)
        for (n = 0; n < 4; ++n) gl.current.color[n] = p;
    if (gl.arrays[SLOT_SECONDARY].enabled)
        for (n = 0; n < 3; ++n) gl.current.secondary[n] = p;
    if (gl.arrays[SLOT_NORMAL].enabled)
        for (n = 0; n < 3; ++n) gl.current.normal[n] = p;
    if (gl.arrays[SLOT_FOG].enabled)
        gl.current.fog = p;
    for (s = 0; s < MAX_TEXTURES; ++s)
        if (gl.arrays[SLOT_TEX0 + s].enabled)
            for (n = 0; n < 4; ++n) gl.current.tex[s][n] = p;
}
/* The frontend's ClientDraw: packed client sections, split to its packets. */
static void draw_records(GLenum mode, GLsizei count)
{
    unsigned int descriptors[DG_GL_ARRAY_EXT_SLOTS] = {0}, offsets[DG_GL_ARRAY_EXT_SLOTS], length, slots;
    unsigned int mask = DG_GL_ARRAY_RAW, vertex_bytes = 0, capacity, limit, s, at = 0;
    for (s = 0; s < SLOT_COUNT; ++s)
    {
        const struct array *a = &gl.arrays[s];
        /* Frontend slots: vertex 0, colour 1, normal 2, unit 0 3, secondary 4,
         * units 1-7 7-13, fog 14. */
        const unsigned int slot = s == SLOT_VERTEX ? 0 : s == SLOT_COLOR ? 1 : s == SLOT_NORMAL ? 2
                : s == SLOT_SECONDARY ? 4 : s == SLOT_FOG ? 14 : s == SLOT_TEX0 ? 3 : 6 + (s - SLOT_TEX0);
        unsigned int size, type;
        if (!a->enabled)
            continue;
        type = a->type;
        size = a->size == GL_BGRA ? (s == SLOT_SECONDARY ? 3 : 4) : (unsigned int)a->size;
        if ((slot == 1 || slot == 2 || slot == 4) && type == GL_SHORT)
            type = GL_FLOAT;
        mask |= 1u << slot;
        descriptors[slot] = type | size << 16;
        vertex_bytes += type_bytes(type) * size;
    }
    slots = (mask & ~(DG_GL_ARRAY_RAW | DG_GL_ARRAY_MASK)) ? DG_GL_ARRAY_EXT_SLOTS : 7;
    limit = (DG_ESCAPE_WORDS - 10 - 4) * 4;
    capacity = (limit - slots * 4) / vertex_bytes;
    if (capacity > DG_GL_MAX_VERTICES)
        capacity = DG_GL_MAX_VERTICES;
    while (capacity && (!DgRawArrayLayoutSlots(mask, capacity, descriptors, offsets, &length, slots) || length > limit))
        --capacity;
    assert(capacity >= 4);
    while (at < (unsigned int)count)
    {
        unsigned int n = count - at;
        const BOOL fan = mode == GL_TRIANGLE_FAN && at;
        if (fan)
            n += 2;
        if (n > capacity)
        {
            n = capacity;
            if (mode == GL_LINES || mode == GL_TRIANGLE_STRIP)
                n &= ~1u;
            if (mode == GL_TRIANGLES)
                n -= n % 3;
        }
        assert(DgRawArrayLayoutSlots(mask, n, descriptors, offsets, &length, slots));
        packet(&gl.cost, 2 + 4 + (length + 3) / 4);
        at += fan ? n - 2 : n;
        if (at >= (unsigned int)count)
            break;
        if (mode == GL_LINE_STRIP)
            --at;
        if (mode == GL_TRIANGLE_STRIP)
            at -= 2;
    }
}
static void rec_draw_arrays(GLenum mode, GLint first, GLsizei count)
{
    GLsizei i;
    COUNTED;
    assert(!gl.in_begin && first >= 0 && count > 0);
    ++gl.draws;
    for (i = 0; i < count; ++i)
        element(first + i);
    draw_records(mode, count);
    poison();
}
static void rec_draw_elements(GLenum mode, GLsizei count, GLenum type, const void *indices)
{
    GLsizei i;
    COUNTED;
    assert(!gl.in_begin && count > 0 && indices);
    /* Client indices. */
    assert(!gl.vbo || !gl.element_buffer);
    ++gl.draws;
    for (i = 0; i < count; ++i)
    {
        unsigned int index;
        if (type == GL_UNSIGNED_SHORT)
        {
            GLushort v;
            memcpy(&v, (const BYTE *)indices + 2 * i, 2);
            index = v;
        }
        else
        {
            assert(type == GL_UNSIGNED_INT);
            memcpy(&index, (const BYTE *)indices + 4 * i, 4);
        }
        element(index);
    }
    draw_records(mode, count);
    poison();
}

/* ------------------------------------------------------------------------
 * Wine state. */
static struct wined3d_adapter adapter;
static struct wined3d_gl_info *const gl_info_ = &adapter.gl_info;
static struct wined3d_d3d_info *const d3d_info_ = &adapter.d3d_info;
static struct wined3d_context context;
static struct wined3d_device device;
static struct wined3d_stream_info si;
static struct wined3d_texture texture;
static struct wined3d_shader pixel_shader;

static const struct wined3d_format f_float1 = {WINED3DFMT_R32_FLOAT, 4, WINED3D_FFP_EMIT_FLOAT1, 1, GL_FLOAT, 1, FALSE};
static const struct wined3d_format f_float2 = {WINED3DFMT_R32G32_FLOAT, 8, WINED3D_FFP_EMIT_FLOAT2, 2, GL_FLOAT, 2, FALSE};
static const struct wined3d_format f_float3 = {WINED3DFMT_R32G32B32_FLOAT, 12, WINED3D_FFP_EMIT_FLOAT3, 3, GL_FLOAT, 3, FALSE};
static const struct wined3d_format f_float4 = {WINED3DFMT_R32G32B32A32_FLOAT, 16, WINED3D_FFP_EMIT_FLOAT4, 4, GL_FLOAT, 4, FALSE};
/* With ARB_vertex_array_bgra (utils.c): GL_BGRA. */
static const struct wined3d_format f_color = {WINED3DFMT_B8G8R8A8_UNORM, 4, WINED3D_FFP_EMIT_D3DCOLOR, 4, GL_UNSIGNED_BYTE, GL_BGRA, TRUE};
static const struct wined3d_format f_color_rgba = {WINED3DFMT_B8G8R8A8_UNORM, 4, WINED3D_FFP_EMIT_D3DCOLOR, 4, GL_UNSIGNED_BYTE, 4, TRUE};
static const struct wined3d_format f_ubyte4n = {WINED3DFMT_R8G8B8A8_UNORM, 4, WINED3D_FFP_EMIT_UBYTE4N, 4, GL_UNSIGNED_BYTE, 4, TRUE};
static const struct wined3d_format f_short2 = {WINED3DFMT_R16G16_SINT, 4, WINED3D_FFP_EMIT_SHORT2, 2, GL_SHORT, 2, FALSE};
static const struct wined3d_format f_short4 = {WINED3DFMT_R16G16B16A16_SINT, 8, WINED3D_FFP_EMIT_SHORT4, 4, GL_SHORT, 4, FALSE};
static const struct wined3d_format f_short4n = {WINED3DFMT_R16G16B16A16_SNORM, 8, WINED3D_FFP_EMIT_SHORT4N, 4, GL_SHORT, 4, TRUE};
static const struct wined3d_format f_half2 = {WINED3DFMT_R16G16_FLOAT, 4, WINED3D_FFP_EMIT_FLOAT16_2, 2, GL_FLOAT, 2, FALSE};

/* One vertex stream of 6000 vertices; elements at chosen offsets. */
#define VERTICES 6000
#define STRIDE 76
static BYTE vertex_memory[VERTICES * STRIDE];
static struct wined3d_buffer vertex_buffer = {{sizeof(vertex_memory), vertex_memory}, 0};
static BYTE index_memory[4 * 12288];
/* A second stream: coordinate pairs with a 12-byte stride. */
static BYTE coord_memory[VERTICES * 12];
static struct wined3d_buffer coord_buffer = {{sizeof(coord_memory), coord_memory}, 0};
static struct wined3d_buffer index_buffer = {{sizeof(index_memory), index_memory}, 0};
/* Offsets in a vertex: position 0 (16), diffuse 16 (16), specular 32 (12),
 * normal 44 (12), coordinate sets 56 (8) and 64 (8), blend weight / point size 72 (4). */
enum { POS = 0, DIFF = 16, SPEC = 32, NORM = 44, TEX_A = 56, TEX_B = 64, MISC = 72 };

static float rhw_of(unsigned int v)
{
    /* rhw 1 and 0 take glVertex3fv; -0 compares equal to 0. */
    switch (v % 9)
    {
        case 0: return 1.0f;
        case 1: return 0.0f;
        case 2: return -0.0f;
        case 3: return 0.5f;
        case 4: return 3.0f;
        case 5: return 1.0f / 3.0f;
        case 6: return 0.0123f + v * 1e-4f;
        case 7: return 1234.5f;
        default: return 7.0f / (1.0f + v);
    }
}
static void fill_vertices(void)
{
    unsigned int v;
    for (v = 0; v < VERTICES; ++v)
    {
        BYTE *p = vertex_memory + v * STRIDE;
        float f[16];
        DWORD c[4];
        short s[4];
        unsigned int i;
        f[0] = 17.25f + v * 0.37f;
        f[1] = 3.5f - v * 0.11f;
        f[2] = 0.25f + (v % 97) * 0.0071f;
        f[3] = rhw_of(v);
        memcpy(p + POS, f, 16);
        for (i = 0; i < 4; ++i)
        {
            /* D3DCOLOR 0xAARRGGBB with red != blue and alpha distinct per vertex. */
            c[i] = (DWORD)((v * 37u + 11u + i) & 0xff) << 24 | (DWORD)((v * 5u + 0x30 + i) & 0xff) << 16
                    | (DWORD)((v * 3u + 0x81) & 0xff) << 8 | (DWORD)((v * 7u + 0x0c + 2 * i) & 0xff);
        }
        /* Diffuse as a D3DCOLOR, FLOAT3/FLOAT4 or UBYTE4N at DIFF. */
        memcpy(p + DIFF, c, 16);
        for (i = 0; i < 4; ++i)
            f[i] = (float)((v * 13 + i * 29) % 256) / 251.0f;
        f[4] = 0.5f + v * 0.001f;
        f[5] = -0.25f + v * 0.002f;
        f[6] = 1.0f / (2 + v);
        memcpy(p + SPEC, c + 1, 4);
        memcpy(p + SPEC + 4, f + 4, 8);
        memcpy(p + NORM, f + 4, 12);
        f[8] = v * 0.0625f + 0.5f;
        f[9] = 1.0f - v * 0.03125f;
        memcpy(p + TEX_A, f + 8, 8);
        s[0] = (short)(v * 3 - 700);
        s[1] = (short)(1000 - v * 5);
        s[2] = (short)(v % 17);
        s[3] = (short)(-(int)v % 13);
        memcpy(p + TEX_B, s, 8);
        memcpy(p + MISC, &f[2], 4);
        f[10] = -2.5f + v * 0.125f;
        f[11] = 0.75f * v;
        memcpy(coord_memory + 12 * v + 4, f + 10, 8);
    }
}
static void element_at(unsigned int idx, const struct wined3d_format *format, UINT offset, UINT stride)
{
    struct wined3d_stream_info_element *e = &si.elements[idx];
    e->format = format;
    e->offset = offset;
    e->stride = (GLsizei)stride;
    e->stream_idx = 0;
    e->divisor = 0;
    /* After remove_vbos: system memory addresses. */
    e->data.buffer_object = 0;
    e->data.addr = vertex_memory + offset;
    si.use_map |= 1u << idx;
}
static void reset_state(void)
{
    unsigned int i;
    memset(&si, 0, sizeof(si));
    si.position_transformed = 1;
    memset(&device, 0, sizeof(device));
    device.state.streams[0].buffer = &vertex_buffer;
    device.state.streams[1].buffer = &coord_buffer;
    device.state.index_buffer = &index_buffer;
    for (i = 0; i < MAX_TEXTURES; ++i)
        device.state.texture_states[i][WINED3D_TSS_TEXCOORD_INDEX] = i;
    memset(&context, 0, sizeof(context));
    context.gl_info = gl_info_;
    context.d3d_info = d3d_info_;
    context.use_immediate_mode_draw = 1;
    for (i = 0; i < MAX_COMBINED_SAMPLERS; ++i)
        context.tex_unit_map[i] = i < MAX_TEXTURES ? i : WINED3D_UNMAPPED_STAGE;
    current_context = &context;
    gl_info_->supported[ARB_MULTITEXTURE] = TRUE;
    gl_info_->supported[ARB_VERTEX_BUFFER_OBJECT] = TRUE;
    gl_info_->supported[EXT_SECONDARY_COLOR] = TRUE;
    gl_info_->supported[EXT_FOG_COORD] = TRUE;
    gl_info_->supported[ARB_VERTEX_ARRAY_BGRA] = TRUE;
    gl_info_->supported[NV_HALF_FLOAT] = FALSE;
    gl_info_->limits.textures = gl_info_->limits.texture_coords = 8;
    gl_info_->gl_ops.ext.p_glFogCoordPointerEXT = rec_fog_pointer;
    d3d_info_->limits.ffp_blend_stages = 8;
    d3d_info_->xyzrhw = FALSE;
    d3d_info_->ffp_generic_attributes = FALSE;
}
/* The D3D7 TL vertex: XYZRHW, D3DCOLOR diffuse and specular, one coordinate set. */
static void tl_vertex(void)
{
    element_at(WINED3D_FFP_POSITION, &f_float4, POS, STRIDE);
    element_at(WINED3D_FFP_DIFFUSE, &f_color, DIFF, STRIDE);
    element_at(WINED3D_FFP_SPECULAR, &f_color, SPEC, STRIDE);
    element_at(WINED3D_FFP_TEXCOORD0, &f_float2, TEX_A, STRIDE);
    device.state.textures[0] = &texture;
}
/* directx.c picks the specular functions by EXT_secondary_color. */
static void secondary(BOOL supported)
{
    gl_info_->supported[EXT_SECONDARY_COLOR] = supported;
    wined3d_adapter_init_ffp_attrib_ops(&adapter);
}
static void vertex_fog(void)
{
    device.state.render_states[WINED3D_RS_FOGENABLE] = TRUE;
    device.state.render_states[WINED3D_RS_FOGVERTEXMODE] = WINED3D_FOG_NONE;
    device.state.render_states[WINED3D_RS_FOGTABLEMODE] = WINED3D_FOG_NONE;
    context.fog_coord = 1;
}

/* GL state before a draw: distinct current values a draw must not leak
 * into, arrays disabled, a stale buffer object bound. */
static void reset_gl(void)
{
    unsigned int u;
    free(gl.out);
    free(gl.cost.log);
    memset(&gl, 0, sizeof(gl));
    gl.cost.capacity = 1024;
    gl.cost.log = malloc(gl.cost.capacity * sizeof(*gl.cost.log));
    assert(gl.cost.log);
    gl.vbo = gl_info_->supported[ARB_VERTEX_BUFFER_OBJECT];
    gl.array_buffer = gl.vbo ? 77 : 0;
    gl.element_buffer = gl.vbo ? 55 : 0;
    gl.current.color[0] = 0.125f; gl.current.color[1] = 0.25f; gl.current.color[2] = 0.375f; gl.current.color[3] = 0.5f;
    gl.current.secondary[0] = 0.625f; gl.current.secondary[1] = 0.75f; gl.current.secondary[2] = 0.875f;
    gl.current.normal[0] = 9; gl.current.normal[1] = 8; gl.current.normal[2] = 7;
    for (u = 0; u < MAX_TEXTURES; ++u)
        set_tex(u, 100.0f + u, 200.0f + u, 300.0f + u, 400.0f + u);
    gl.current.fog = 42.0f;
}

struct result
{
    struct vertex *vertices;
    unsigned int count, calls, begins, draws, vertex_calls;
    struct attribs current;
    struct cost cost;
};
static void run(BOOL arrays, GLenum primitive, UINT count, UINT index_size, UINT start, struct result *r)
{
    const void *indices = index_size ? index_memory : NULL;
    reset_gl();
    use_arrays = arrays;
    arrays_calls = 0;
    arrays_result = FALSE;
    gl_calls = 0;
    drawStridedSlow(&device, &context, &si, count, primitive, indices, index_size, start);
    finish(&gl.cost);
    assert(!gl.in_begin);
    r->vertices = gl.out;
    r->count = gl.out_count;
    gl.out = NULL;
    r->calls = gl_calls;
    r->begins = gl.begins;
    r->draws = gl.draws;
    r->vertex_calls = gl.vertex_calls;
    r->current = gl.current;
    r->cost = gl.cost;
    gl.cost.log = NULL;
}
static void release(struct result *r)
{
    free(r->vertices);
    free(r->cost.log);
    r->vertices = NULL;
    r->cost.log = NULL;
}

static unsigned int cases;
/* drawStridedSlow's immediate mode against the 0048 array draw. */
static void same(const char *name, GLenum primitive, UINT count, UINT index_size, UINT start,
        struct result *slow_out, struct result *array_out)
{
    struct result slow, fast;
    unsigned int i, s;
    run(FALSE, primitive, count, index_size, start, &slow);
    run(TRUE, primitive, count, index_size, start, &fast);
    if (!arrays_result)
    {
        fprintf(stderr, "FAIL %s: 0048 declined\n", name);
        exit(1);
    }
    assert(arrays_calls == 1 && fast.begins == 0 && fast.vertex_calls == 0 && fast.draws == 1);
    assert(slow.begins == 1 && slow.draws == 0 && slow.count == count);
    if (fast.count != slow.count)
    {
        fprintf(stderr, "FAIL %s: %u vertices, reference %u\n", name, fast.count, slow.count);
        exit(1);
    }
    for (i = 0; i < count; ++i)
    {
        if (memcmp(&fast.vertices[i], &slow.vertices[i], sizeof(slow.vertices[i])))
        {
            const float *a = (const float *)&fast.vertices[i], *b = (const float *)&slow.vertices[i];
            unsigned int n;
            fprintf(stderr, "FAIL %s: vertex %u differs\n", name, i);
            for (n = 0; n < sizeof(struct vertex) / sizeof(float); ++n)
                if (memcmp(a + n, b + n, sizeof(float)))
                    fprintf(stderr, "  float %u: %.9g, reference %.9g\n", n, a[n], b[n]);
            exit(1);
        }
    }
    if (memcmp(&fast.current, &slow.current, sizeof(slow.current)))
    {
        fprintf(stderr, "FAIL %s: current attributes after the draw differ\n", name);
        exit(1);
    }
    /* Every array is disabled, nothing keeps the converted storage, and the
     * client unit is unit 0. */
    for (s = 0; s < SLOT_COUNT; ++s)
        assert(!gl.arrays[s].enabled);
    assert(!gl.arrays[SLOT_VERTEX].pointer && !gl.arrays[SLOT_FOG].pointer);
    assert(gl.client_unit == 0);
    assert(!gl.vbo || (gl.array_buffer == 0 && gl.element_buffer == (index_size ? 0u : 55u)));
    assert(gl.vbo || gl.binds == 0);
    if (slow_out)
        *slow_out = slow;
    else
        release(&slow);
    if (array_out)
        *array_out = fast;
    else
        release(&fast);
    ++cases;
}
/* 0048 declines before any GL call; drawStridedSlow then draws as before. */
static unsigned int fallbacks;
static void declines(const char *name, GLenum primitive, UINT count, UINT index_size, UINT start, BOOL reference)
{
    struct result r;
    if (reference)
    {
        struct result slow;
        run(FALSE, primitive, count, index_size, start, &slow);
        run(TRUE, primitive, count, index_size, start, &r);
        assert(arrays_calls == 1);
        if (arrays_result || calls_in_arrays)
        {
            fprintf(stderr, "FAIL %s: 0048 drew (%u GL calls)\n", name, calls_in_arrays);
            exit(1);
        }
        assert(r.begins == 1 && r.draws == 0 && r.count == slow.count);
        assert(!memcmp(r.vertices, slow.vertices, slow.count * sizeof(*slow.vertices)));
        release(&slow);
        release(&r);
    }
    else
    {
        /* drawStridedSlow itself would stop (invalid_func: DebugBreak). */
        reset_gl();
        gl_calls = 0;
        assert(!dg_xyzrhw_arrays(&device, &context, &si, count, primitive, index_size ? index_memory : NULL,
                index_size, start));
        assert(!gl_calls);
    }
    ++fallbacks;
}

static void index16(UINT start, UINT count, unsigned int (*value)(unsigned int))
{
    unsigned int i;
    for (i = 0; i < start + count; ++i)
    {
        WORD v = (WORD)value(i);
        memcpy(index_memory + 2 * i, &v, 2);
    }
}
static void index32(UINT start, UINT count, unsigned int (*value)(unsigned int))
{
    unsigned int i;
    for (i = 0; i < start + count; ++i)
    {
        DWORD v = value(i);
        memcpy(index_memory + 4 * i, &v, 4);
    }
}
/* Unordered and repeated, decoys below the start index. */
static unsigned int scattered(unsigned int i) { return i < 3 ? 5000 : (i * 7) % 53 + 9; }
static unsigned int grid(unsigned int i)
{
    /* A 64 x 32 vertex grid of 2 x 63 x 31 triangles. */
    const unsigned int quad = i / 6, corner = i % 6, x = quad % 63, y = quad / 63;
    static const unsigned int dx[6] = {0, 1, 0, 0, 1, 1}, dy[6] = {0, 0, 1, 1, 0, 1};
    return (y + dy[corner]) * 64 + x + dx[corner];
}
static unsigned int wide(unsigned int i) { return i & 1 ? 2 + i % 11 : 3000 + i % 5; }
static unsigned int from_two(unsigned int i) { return 2 + i % 40; }

/* draws back-to-back copies of one draw's records, as the frontend packs them. */
static struct cost replay(const struct cost *one, unsigned int draws)
{
    struct cost c;
    unsigned int d, r;
    memset(&c, 0, sizeof(c));
    for (d = 0; d < draws; ++d)
        for (r = 0; r < one->logged; ++r)
            packet(&c, one->log[r]);
    finish(&c);
    c.calls = one->calls * draws;
    return c;
}
static void report(const char *name, unsigned int draws, struct result *slow, struct result *fast)
{
    const struct cost a = replay(&slow->cost, draws), b = replay(&fast->cost, draws);
    printf("%-42s immediate: %7lu GL calls %7lu records %9lu bytes %4lu packets"
            " | arrays: %6lu GL calls %6lu records %8lu bytes %3lu packets\n",
            name, a.calls, a.records, a.bytes, a.packets, b.calls, b.records, b.bytes, b.packets);
    fflush(stdout);
    /* Large and small batches both take fewer records, bytes and packets. */
    assert(b.records < a.records && b.bytes < a.bytes && b.packets <= a.packets);
    release(slow);
    release(fast);
}

int main(void)
{
    struct result slow, fast;
    unsigned int i;

    adapter.gl_info.gl_ops.gl.p_glBegin = rec_begin;
    adapter.gl_info.gl_ops.gl.p_glEnd = rec_end;
    adapter.gl_info.gl_ops.gl.p_glVertex4f = rec_vertex4f;
    adapter.gl_info.gl_ops.gl.p_glVertex3fv = rec_vertex3fv;
    adapter.gl_info.gl_ops.gl.p_glVertex4fv = rec_vertex4fv;
    adapter.gl_info.gl_ops.gl.p_glVertex2sv = rec_unexpected_vector;
    adapter.gl_info.gl_ops.gl.p_glVertex4s = rec_unexpected_vertex4s;
    adapter.gl_info.gl_ops.gl.p_glColor4f = rec_color4f;
    adapter.gl_info.gl_ops.gl.p_glColor4ub = rec_color4ub;
    adapter.gl_info.gl_ops.gl.p_glColor3fv = rec_color3fv;
    adapter.gl_info.gl_ops.gl.p_glColor4fv = rec_color4fv;
    adapter.gl_info.gl_ops.gl.p_glColor4ubv = rec_color4ubv;
    adapter.gl_info.gl_ops.gl.p_glColor4sv = rec_unexpected_vector;
    adapter.gl_info.gl_ops.gl.p_glColor4usv = rec_unexpected_vector;
    adapter.gl_info.gl_ops.gl.p_glNormal3f = rec_normal3f;
    adapter.gl_info.gl_ops.gl.p_glNormal3fv = rec_normal3fv;
    adapter.gl_info.gl_ops.gl.p_glTexCoord4f = rec_texcoord4f;
    adapter.gl_info.gl_ops.gl.p_glMaterialfv = rec_material;
    adapter.gl_info.gl_ops.gl.p_glVertexPointer = rec_vertex_pointer;
    adapter.gl_info.gl_ops.gl.p_glNormalPointer = rec_normal_pointer;
    adapter.gl_info.gl_ops.gl.p_glColorPointer = rec_color_pointer;
    adapter.gl_info.gl_ops.gl.p_glTexCoordPointer = rec_texcoord_pointer;
    adapter.gl_info.gl_ops.gl.p_glEnableClientState = rec_enable;
    adapter.gl_info.gl_ops.gl.p_glDisableClientState = rec_disable;
    adapter.gl_info.gl_ops.gl.p_glDrawArrays = rec_draw_arrays;
    adapter.gl_info.gl_ops.gl.p_glDrawElements = rec_draw_elements;
    adapter.gl_info.gl_ops.ext.p_glBindBuffer = rec_bind;
    adapter.gl_info.gl_ops.ext.p_glClientActiveTextureARB = rec_client_active;
    adapter.gl_info.gl_ops.ext.p_glSecondaryColor3fEXT = rec_secondary3f;
    adapter.gl_info.gl_ops.ext.p_glSecondaryColor3fvEXT = rec_secondary3fv;
    adapter.gl_info.gl_ops.ext.p_glSecondaryColor3ubvEXT = rec_secondary3ubv;
    adapter.gl_info.gl_ops.ext.p_glSecondaryColorPointerEXT = rec_secondary_pointer;
    adapter.gl_info.gl_ops.ext.p_glFogCoordfEXT = rec_fog;
    adapter.gl_info.gl_ops.ext.p_glFogCoordPointerEXT = rec_fog_pointer;
    adapter.gl_info.gl_ops.ext.p_glMultiTexCoord4fARB = rec_multi4f;
    adapter.gl_info.gl_ops.ext.p_glMultiTexCoord1fvARB = rec_multi1fv;
    adapter.gl_info.gl_ops.ext.p_glMultiTexCoord2fvARB = rec_multi2fv;
    adapter.gl_info.gl_ops.ext.p_glMultiTexCoord3fvARB = rec_multi3fv;
    adapter.gl_info.gl_ops.ext.p_glMultiTexCoord4fvARB = rec_multi4fv;
    adapter.gl_info.gl_ops.ext.p_glMultiTexCoord2svARB = rec_multi2sv;
    adapter.gl_info.gl_ops.ext.p_glMultiTexCoord4svARB = rec_multi4sv;
    adapter.gl_info.gl_ops.ext.p_glMultiTexCoord2hvNV = rec_unexpected_unit_vector;
    adapter.gl_info.gl_ops.ext.p_glMultiTexCoord4hvNV = rec_unexpected_unit_vector;
    adapter.gl_info.supported[EXT_SECONDARY_COLOR] = TRUE;
    /* directx.c's own table, as the adapter builds it. */
    wined3d_adapter_init_ffp_attrib_ops(&adapter);
    fill_vertices();

    /* D3D7 TL vertices with specular-alpha fog from user memory (ddraw's
     * dynamic buffer): local storage, heap storage, a start vertex. */
    reset_state();
    tl_vertex();
    vertex_fog();
    same("TL fog strip, 4 vertices", GL_TRIANGLE_STRIP, 4, 0, 12, &slow, &fast);
    report("1 TL quad (fog, 1 unit)", 1, &slow, &fast);
    same("TL fog strip, 4 vertices", GL_TRIANGLE_STRIP, 4, 0, 12, &slow, &fast);
    report("1000 TL quads (fog, 1 unit)", 1000, &slow, &fast);
    same("TL fog list, 63 vertices", GL_TRIANGLES, 63, 0, 5, NULL, NULL);
    same("TL fog list, 300 vertices", GL_TRIANGLES, 300, 0, 5, &slow, &fast);
    report("300-vertex TL list (fog, 1 unit)", 1, &slow, &fast);
    same("TL fog list, 3000 vertices", GL_TRIANGLES, 3000, 0, 1, &slow, &fast);
    report("3000-vertex TL list (fog, 1 unit)", 1, &slow, &fast);
    same("TL fog fan, 2000 vertices", GL_TRIANGLE_FAN, 2000, 0, 7, NULL, NULL);
    same("TL fog strip, 5998 vertices", GL_TRIANGLE_STRIP, VERTICES - 2, 0, 2, NULL, NULL);

    /* Indices: 16-bit unordered and repeated with decoys below the start
     * index, base vertices 7, 0 and -2 (every referenced vertex exists),
     * 32-bit sparse indices, a grid. */
    index16(3, 60, scattered);
    device.state.base_vertex_index = 7;
    same("16-bit indices, base vertex 7, start 3", GL_TRIANGLES, 60, 2, 3, NULL, NULL);
    device.state.base_vertex_index = 0;
    same("16-bit indices, base vertex 0", GL_TRIANGLES, 60, 2, 3, NULL, NULL);
    index16(0, 300, from_two);
    device.state.base_vertex_index = -2;
    same("16-bit indices, base vertex -2", GL_TRIANGLES, 300, 2, 0, NULL, NULL);
    device.state.load_base_vertex_index = 5; /* drawStridedSlow ignores it */
    same("16-bit indices, loaded base vertex ignored", GL_TRIANGLES, 300, 2, 0, NULL, NULL);
    device.state.load_base_vertex_index = 0;
    index32(2, 4000, wide);
    device.state.base_vertex_index = 11;
    same("32-bit sparse indices, base vertex 11", GL_LINES, 4000, 4, 2, NULL, NULL);
    index16(0, 63 * 31 * 6, grid);
    device.state.base_vertex_index = 100;
    same("16-bit 64x32 grid", GL_TRIANGLES, 63 * 31 * 6, 2, 0, &slow, &fast);
    report("64x32 indexed grid, 11718 indices", 1, &slow, &fast);
    index32(0, 63 * 31 * 6, grid);
    same("32-bit 64x32 grid", GL_TRIANGLES, 63 * 31 * 6, 4, 0, NULL, NULL);
    device.state.base_vertex_index = 0;

    /* Primitive types. */
    same("points", GL_POINTS, 100, 0, 0, NULL, NULL);
    same("line strip without a pattern", GL_LINE_STRIP, 5000, 0, 0, NULL, NULL);
    same("lines", GL_LINES, 64, 0, 0, NULL, NULL);

    /* Untextured coloured quads (not 0006's packed path). */
    reset_state();
    element_at(WINED3D_FFP_POSITION, &f_float4, POS, STRIDE);
    element_at(WINED3D_FFP_DIFFUSE, &f_color, DIFF, STRIDE);
    same("untextured coloured quad", GL_TRIANGLE_FAN, 4, 0, 40, &slow, &fast);
    report("1000 untextured coloured quads", 1000, &slow, &fast);
    /* A coordinate set the stage does not read is ignored. */
    element_at(WINED3D_FFP_TEXCOORD0 + 3, &f_float2, TEX_A, STRIDE);
    same("unread coordinate set", GL_TRIANGLES, 30, 0, 0, NULL, NULL);
    /* Point size and blend weights: drawStridedSlow sends neither. */
    element_at(WINED3D_FFP_PSIZE, &f_float1, MISC, STRIDE);
    element_at(WINED3D_FFP_BLENDWEIGHT, &f_float1, MISC, STRIDE);
    same("point size and blend weight ignored", GL_POINTS, 30, 0, 0, NULL, NULL);

    /* Texture stages: four units, a swapped coordinate set, a textured stage
     * without coordinates (0, 0, 0, 1), an untextured stage, a generated
     * one, a stage mapped to another unit, an unmapped one. */
    reset_state();
    element_at(WINED3D_FFP_POSITION, &f_float4, POS, STRIDE);
    element_at(WINED3D_FFP_DIFFUSE, &f_color, DIFF, STRIDE);
    element_at(WINED3D_FFP_TEXCOORD0, &f_float2, TEX_A, STRIDE);
    element_at(WINED3D_FFP_TEXCOORD0 + 1, &f_short2, TEX_B, STRIDE);
    element_at(WINED3D_FFP_TEXCOORD0 + 2, &f_float3, NORM, STRIDE);
    element_at(WINED3D_FFP_TEXCOORD0 + 3, &f_float4, POS, STRIDE);
    element_at(WINED3D_FFP_TEXCOORD0 + 4, &f_short4, TEX_B, STRIDE);
    element_at(WINED3D_FFP_TEXCOORD0 + 5, &f_float1, SPEC + 4, STRIDE);
    for (i = 0; i < 8; ++i)
        device.state.textures[i] = &texture;
    device.state.texture_states[0][WINED3D_TSS_TEXCOORD_INDEX] = 1;
    device.state.texture_states[1][WINED3D_TSS_TEXCOORD_INDEX] = 0;
    device.state.texture_states[2][WINED3D_TSS_TEXCOORD_INDEX] = 6; /* no such set: defaults */
    device.state.textures[3] = NULL; /* untextured: skipped */
    device.state.texture_states[4][WINED3D_TSS_TEXCOORD_INDEX] = 0x10000 | 2; /* camera space position */
    device.state.texture_states[5][WINED3D_TSS_TEXCOORD_INDEX] = 3;
    device.state.texture_states[6][WINED3D_TSS_TEXCOORD_INDEX] = 4;
    device.state.texture_states[7][WINED3D_TSS_TEXCOORD_INDEX] = 5;
    context.tex_unit_map[6] = 2;
    context.tex_unit_map[2] = 6;
    context.tex_unit_map[7] = WINED3D_UNMAPPED_STAGE;
    same("stages: swapped, defaults, skipped, generated, remapped, unmapped", GL_TRIANGLES, 300, 0, 3, &slow, &fast);
    report("300 vertices, 4 coordinate units", 1, &slow, &fast);
    index16(0, 120, scattered);
    device.state.base_vertex_index = 3;
    same("stages, indexed", GL_TRIANGLE_STRIP, 120, 2, 0, NULL, NULL);
    device.state.base_vertex_index = 0;
    /* A pixel shader: untextured stages take coordinates too. */
    device.state.pixel_shader = &pixel_shader;
    same("pixel shader, untextured stage", GL_TRIANGLES, 30, 0, 0, NULL, NULL);
    device.state.pixel_shader = NULL;
    /* Coordinates from unit 1 on: the client unit returns to 0. */
    device.state.textures[0] = NULL;
    same("stage 0 untextured", GL_TRIANGLES, 30, 0, 0, NULL, NULL);
    device.state.textures[0] = &texture;
    /* Without real multitexture (gl_compat.c): stage 0 alone, no client unit. */
    gl_info_->supported[ARB_MULTITEXTURE] = FALSE;
    gl_info_->limits.texture_coords = 1;
    context.tex_unit_map[2] = 2;
    context.tex_unit_map[6] = 6;
    same("one unit without ARB_multitexture", GL_TRIANGLES, 30, 0, 0, NULL, NULL);
    gl_info_->supported[ARB_MULTITEXTURE] = TRUE;
    gl_info_->limits.texture_coords = 8;

    /* Every other accepted format: FLOAT3/FLOAT4/UBYTE4N diffuse, FLOAT3
     * specular (no fog coordinate), FLOAT3/FLOAT4 normals. */
    reset_state();
    vertex_fog();
    element_at(WINED3D_FFP_POSITION, &f_float4, POS, STRIDE);
    element_at(WINED3D_FFP_DIFFUSE, &f_float4, SPEC + 4, STRIDE);
    element_at(WINED3D_FFP_SPECULAR, &f_float3, SPEC + 4, STRIDE);
    element_at(WINED3D_FFP_NORMAL, &f_float3, NORM, STRIDE);
    same("FLOAT4 diffuse, FLOAT3 specular, FLOAT3 normal, fogged without coordinates", GL_TRIANGLES, 90, 0, 0, NULL, NULL);
    element_at(WINED3D_FFP_DIFFUSE, &f_float3, NORM, STRIDE);
    element_at(WINED3D_FFP_NORMAL, &f_float4, POS, STRIDE);
    same("FLOAT3 diffuse, FLOAT4 normal", GL_TRIANGLES, 90, 0, 0, NULL, NULL);
    element_at(WINED3D_FFP_DIFFUSE, &f_ubyte4n, DIFF + 4, STRIDE);
    same("UBYTE4N diffuse", GL_TRIANGLES, 90, 0, 0, NULL, NULL);
    /* Absent inputs: drawStridedSlow's defaults. */
    reset_state();
    element_at(WINED3D_FFP_POSITION, &f_float4, POS, STRIDE);
    same("position alone", GL_TRIANGLES, 33, 0, 9, NULL, NULL);
    secondary(FALSE);
    same("position alone without secondary colour", GL_TRIANGLES, 33, 0, 9, NULL, NULL);
    /* A D3DCOLOR specular without secondary colour still gives fog. */
    tl_vertex();
    vertex_fog();
    same("fog coordinates without secondary colour", GL_TRIANGLES, 33, 0, 9, NULL, NULL);
    secondary(TRUE);

    /* Fog states without coordinates, where the current one stays. */
    reset_state();
    tl_vertex();
    vertex_fog();
    device.state.render_states[WINED3D_RS_FOGTABLEMODE] = WINED3D_FOG_LINEAR;
    same("table fog", GL_TRIANGLES, 30, 0, 0, NULL, NULL);
    device.state.render_states[WINED3D_RS_FOGTABLEMODE] = WINED3D_FOG_NONE;
    device.state.render_states[WINED3D_RS_FOGVERTEXMODE] = WINED3D_FOG_LINEAR; /* XYZRHW still takes the alpha */
    same("vertex fog mode with XYZRHW", GL_TRIANGLES, 30, 0, 0, NULL, NULL);
    device.state.render_states[WINED3D_RS_FOGENABLE] = FALSE;
    same("fog disabled", GL_TRIANGLES, 30, 0, 0, NULL, NULL);
    device.state.render_states[WINED3D_RS_FOGENABLE] = TRUE;
    gl_info_->supported[EXT_FOG_COORD] = FALSE;
    context.fog_coord = 0;
    same("no EXT_fog_coord", GL_TRIANGLES, 30, 0, 0, NULL, NULL);
    gl_info_->supported[EXT_FOG_COORD] = TRUE;
    context.fog_coord = 1;
    si.use_map &= ~(1u << WINED3D_FFP_SPECULAR);
    same("coordinate fog without specular: the current coordinate", GL_TRIANGLES, 30, 0, 0, NULL, NULL);

    /* Streams with different buffers and strides (mixed in D3D8/9). */
    reset_state();
    tl_vertex();
    vertex_fog();
    si.elements[WINED3D_FFP_TEXCOORD0].stream_idx = 1;
    si.elements[WINED3D_FFP_TEXCOORD0].stride = 12;
    si.elements[WINED3D_FFP_TEXCOORD0].data.addr = coord_memory + 4;
    same("two streams", GL_TRIANGLES, 300, 0, 100, NULL, NULL);
    index16(0, 300, from_two);
    device.state.base_vertex_index = 4;
    same("two streams, indexed", GL_TRIANGLES, 300, 2, 0, NULL, NULL);
    /* The second stream ends before the first: its vertices are checked too. */
    coord_buffer.resource.size = 12 * (VERTICES - 10);
    same("second stream's last vertex", GL_POINTS, 1, 0, VERTICES - 11, NULL, NULL);
    declines("second stream too short", GL_POINTS, 1, 0, VERTICES - 10, FALSE);
    coord_buffer.resource.size = sizeof(coord_memory);
    device.state.base_vertex_index = 0;

    /* No buffer objects: no bindings. */
    reset_state();
    tl_vertex();
    vertex_fog();
    gl_info_->supported[ARB_VERTEX_BUFFER_OBJECT] = FALSE;
    same("no ARB_vertex_buffer_object", GL_TRIANGLES, 300, 0, 0, NULL, NULL);
    index16(0, 300, from_two);
    same("no ARB_vertex_buffer_object, indexed", GL_TRIANGLES, 300, 2, 0, NULL, NULL);
    gl_info_->supported[ARB_VERTEX_BUFFER_OBJECT] = TRUE;
    /* A start vertex beyond 64 local vertices and a stream offset. */
    for (i = 0; i < MAX_ATTRIBS; ++i)
        if (si.use_map & (1u << i))
            si.elements[i].data.addr += 3 * STRIDE;
    same("stream offset", GL_TRIANGLES, 300, 0, 200, NULL, NULL);
    /* The last vertex of the stream is drawn, one more is not (below). */
    same("last vertex", GL_POINTS, 1, 0, VERTICES - 4, NULL, NULL);

    /* ------------------------------------------------------------------
     * Fallbacks. */
    reset_state();
    tl_vertex();
    vertex_fog();
    declines("vertex beyond the stream", GL_POINTS, 2, 0, VERTICES - 1, FALSE);
    declines("start vertex beyond INT_MAX", GL_POINTS, 1, 0, 0x80000000u, FALSE);
    index16(0, 4, from_two);
    device.state.base_vertex_index = -3;
    declines("negative vertex", GL_TRIANGLES, 3, 2, 0, FALSE);
    device.state.base_vertex_index = 0;
    declines("indices beyond the index buffer", GL_TRIANGLES, 3, 2, sizeof(index_memory) / 2 - 2, FALSE);
    declines("indices beyond the index buffer (32-bit)", GL_TRIANGLES, 3, 4, 0xfffffffeu, FALSE);
    si.elements[WINED3D_FFP_DIFFUSE].stride = 0;
    declines("stride zero", GL_TRIANGLES, 3, 0, 0, TRUE);
    si.elements[WINED3D_FFP_DIFFUSE].stride = STRIDE;
    si.elements[WINED3D_FFP_SPECULAR].stride = 0;
    declines("stride zero fog", GL_TRIANGLES, 3, 0, 0, TRUE);
    si.elements[WINED3D_FFP_SPECULAR].stride = STRIDE;
    context.num_untracked_materials = 1;
    context.untracked_materials[0] = 0x1200;
    declines("untracked materials", GL_TRIANGLES, 30, 0, 0, TRUE);
    context.num_untracked_materials = 0;
    context.namedArraysLoaded = 1;
    declines("named arrays loaded", GL_TRIANGLES, 30, 0, 0, TRUE);
    context.namedArraysLoaded = 0;
    context.numberedArraysLoaded = 1;
    declines("numbered arrays loaded", GL_TRIANGLES, 30, 0, 0, TRUE);
    context.numberedArraysLoaded = 0;
    context.use_immediate_mode_draw = 0;
    declines("emulation, not immediate mode", GL_TRIANGLES, 30, 0, 0, TRUE);
    context.use_immediate_mode_draw = 1;
    d3d_info_->xyzrhw = TRUE;
    declines("xyzrhw pipeline", GL_TRIANGLES, 30, 0, 0, FALSE);
    d3d_info_->xyzrhw = FALSE;
    d3d_info_->ffp_generic_attributes = TRUE;
    declines("generic attributes", GL_TRIANGLES, 30, 0, 0, FALSE);
    d3d_info_->ffp_generic_attributes = FALSE;
    si.position_transformed = 0;
    declines("untransformed", GL_TRIANGLES, 30, 0, 0, FALSE);
    si.position_transformed = 1;
    device.state.render_states[WINED3D_RS_LINEPATTERN] = 0xaaaa0001; /* repeat 1 */
    declines("stippled line strip", GL_LINE_STRIP, 30, 0, 0, TRUE);
    same("stippled lines", GL_LINES, 30, 0, 0, NULL, NULL);
    device.state.render_states[WINED3D_RS_LINEPATTERN] = 0;
    /* gl_compat.c's emulated fog coordinates: no array entry point. */
    gl_info_->gl_ops.ext.p_glFogCoordPointerEXT = NULL;
    declines("emulated fog coordinates", GL_TRIANGLES, 30, 0, 0, TRUE);
    si.use_map &= ~(1u << WINED3D_FFP_SPECULAR);
    declines("emulated fog without specular", GL_TRIANGLES, 30, 0, 0, TRUE);
    device.state.render_states[WINED3D_RS_FOGENABLE] = FALSE;
    same("emulated fog coordinates, fog disabled", GL_TRIANGLES, 30, 0, 0, NULL, NULL);
    gl_info_->gl_ops.ext.p_glFogCoordPointerEXT = rec_fog_pointer;
    device.state.render_states[WINED3D_RS_FOGENABLE] = TRUE;
    si.use_map |= 1u << WINED3D_FFP_SPECULAR;
    /* A D3DCOLOR Wine swizzles (no usable ARB_vertex_array_bgra). */
    si.swizzle_map = 1u << WINED3D_FFP_DIFFUSE;
    declines("swizzled diffuse", GL_TRIANGLES, 30, 0, 0, TRUE);
    si.swizzle_map = 1u << WINED3D_FFP_SPECULAR;
    declines("swizzled specular", GL_TRIANGLES, 30, 0, 0, TRUE);
    si.swizzle_map = 0;
    si.elements[WINED3D_FFP_DIFFUSE].format = &f_color_rgba;
    declines("D3DCOLOR without GL_BGRA", GL_TRIANGLES, 30, 0, 0, FALSE);
    si.elements[WINED3D_FFP_DIFFUSE].format = &f_short4n;
    declines("SHORT4N diffuse", GL_TRIANGLES, 30, 0, 0, FALSE);
    si.elements[WINED3D_FFP_DIFFUSE].format = &f_color;
    si.elements[WINED3D_FFP_SPECULAR].format = &f_float4;
    declines("FLOAT4 specular", GL_TRIANGLES, 30, 0, 0, FALSE);
    si.elements[WINED3D_FFP_SPECULAR].format = &f_color;
    element_at(WINED3D_FFP_NORMAL, &f_color, DIFF, STRIDE);
    declines("D3DCOLOR normal", GL_TRIANGLES, 30, 0, 0, FALSE);
    si.use_map &= ~(1u << WINED3D_FFP_NORMAL);
    si.elements[WINED3D_FFP_TEXCOORD0].format = &f_half2;
    declines("FLOAT16 coordinates", GL_TRIANGLES, 30, 0, 0, FALSE);
    si.elements[WINED3D_FFP_TEXCOORD0].format = &f_float2;
    si.elements[WINED3D_FFP_POSITION].format = &f_float3;
    declines("FLOAT3 position", GL_TRIANGLES, 30, 0, 0, FALSE);
    si.elements[WINED3D_FFP_POSITION].format = &f_float4;
    si.use_map &= ~(1u << WINED3D_FFP_POSITION);
    declines("no position", GL_TRIANGLES, 30, 0, 0, FALSE);
    si.use_map |= 1u << WINED3D_FFP_POSITION;
    gl_info_->limits.texture_coords = 1;
    element_at(WINED3D_FFP_TEXCOORD0 + 1, &f_float2, TEX_A, STRIDE);
    device.state.textures[1] = &texture;
    declines("coordinates beyond the client units", GL_TRIANGLES, 30, 0, 0, TRUE);
    gl_info_->limits.texture_coords = 8;
    si.elements[WINED3D_FFP_TEXCOORD0 + 1].data.buffer_object = 3;
    declines("a buffer object element", GL_TRIANGLES, 30, 0, 0, FALSE);
    si.elements[WINED3D_FFP_TEXCOORD0 + 1].data.buffer_object = 0;
    declines("no draw", GL_TRIANGLES, 0, 0, 0, FALSE);
    assert(!wine_errors);

    printf("PASS prepared XYZRHW arrays: %u draws match drawStridedSlow bit for bit (vertices and the "
            "attributes left current), %u fallbacks decline before any GL call\n", cases, fallbacks);
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


with tempfile.TemporaryDirectory(prefix='dreamgpu-xyzrhw-arrays-') as temporary:
    work = Path(temporary)
    for name in json.loads(MANIFEST.read_text())['files']:
        p = work / name
        p.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'vendor/wine9x' / name, p)
    patches.apply(work, MANIFEST)
    drawprim = (work / 'wined3d/drawprim.c').read_text()
    directx = (work / 'wined3d/directx.c').read_text()
    context = (work / 'wined3d/context.c').read_text()
    private = (work / 'wined3d/wined3d_private.h').read_text()
    public = (work / 'include/wine/wined3d.h').read_text()

    # drawStridedSlow tries 0006's packed path, then 0048, then draws in
    # immediate mode; its own code is the reference and unchanged.
    slow = function(drawprim, 'static void drawStridedSlow(')
    packed = slow.index('if (dg_wine_draw_transformed(device, context, si, NumVertexes, glPrimType, idxData, idxSize, startIdx))')
    arrays = slow.index('if (dg_xyzrhw_arrays(device, context, si, NumVertexes, glPrimType, idxData, idxSize, startIdx))')
    assert packed < arrays < slow.index('TRACE_(d3d_perf)("Using slow vertex array code\\n");')
    assert slow.count('dg_xyzrhw_arrays(') == 1
    assert 'ops->position[si->elements[WINED3D_FFP_POSITION].format->emit_idx](ptrToCoords);' in slow
    assert 'GL_EXTCALL(glFogCoordfEXT((float) (specularColor >> 24)));' in slow
    # Immediate mode is still what XYZRHW streams outside buffer objects
    # select; 0048 replaces drawStridedSlow's drawing, not the decision.
    assert re.search(r'if \(\(\(stream_info->position_transformed && !d3d_info->xyzrhw\)\s+'
                     r'\|\| \(stream_info->use_map & slow_mask\)\) && !stream_info->all_vbo\)\s+'
                     r'context->use_immediate_mode_draw = TRUE;', context)
    # draw_primitive's immediate mode: buffer objects replaced by their
    # system memory copies (0039) before drawStridedSlow.
    draw = function(drawprim, 'void draw_primitive(')
    assert draw.index('remove_vbos(context, state, &si_emulated);') < draw.index('drawStridedSlow(device, context, stream_info')

    declaration = drawprim[drawprim.index('static BOOL dg_xyzrhw_arrays('):]
    declaration = declaration[:declaration.index(';\n') + 2]
    assert drawprim.index(declaration) < drawprim.index('static void drawStridedSlow(')
    start = drawprim.index('#define DG_XYZRHW_LOCAL_VERTICES')
    definition = drawprim.index('static BOOL dg_xyzrhw_arrays(', start)
    end = drawprim.index('\n}\n', definition) + 3
    xyzrhw = drawprim[start:end] + '\n'
    fog = drawprim[drawprim.index('#define DG_FOG_LOCAL_COORDS'):]
    fog = fog[:fog.index('\n};\n') + 4]
    attributes = directx[directx.index('static void WINE_GLAPI invalid_func('):
                         directx.index('static void wined3d_adapter_init_fb_cfgs(')]

    enums = ''.join(block(private, opening) for opening in ('enum wined3d_ffp_idx\n', 'enum wined3d_ffp_emit_idx\n'))
    enums += ''.join(block(public, opening) for opening in (
        'enum wined3d_render_state\n', 'enum wined3d_fog_mode\n', 'enum wined3d_texture_stage_state\n',
        'struct wined3d_line_pattern\n'))
    for name in ('WINEHIGHEST_RENDER_STATE', 'WINED3D_HIGHEST_TEXTURE_STATE'):
        enums += re.search(r'#define ' + name + r'\s+\w+\n', public)[0]
    enums += '#define DG_ESCAPE_WORDS (%d / 4)\n' % int(re.search(
        r'#define DG_ESCAPE_MAX_BYTES (\d+)', (ROOT / 'guest/nt/include/dg-escape.h').read_text())[1])

    body = (PREAMBLE.replace('@ENUMS@', enums) + attributes + fog
            + function(drawprim, 'static BOOL dg_fog_coord_range(')
            + function(drawprim, 'static BOOL dg_fog_coord_fill(')
            + slow.replace('if (dg_xyzrhw_arrays(', 'if (test_arrays_hook(') + xyzrhw + TEST)
    (work / 'test.c').write_text(body)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-function', '-Wno-unused-parameter', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-I' + str(ROOT / 'guest/include'),
                    str(work / 'test.c'), '-o', str(work / 'test')], check=True)
    sys.exit(subprocess.run([str(work / 'test')]).returncode)
