#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Patch 0056 on actual pinned Wine source: rectangle-only maps of render
targets whose current copy is their GL texture.

Wine downloaded the whole surface (GetTexImage of every texel) for any map
that found system memory stale, although Direct3D lets the application touch
only the locked rectangle. Unreal Tournament 2003's ReduceMouseLag locks one
back buffer pixel read-only after each present: 800x600 texels, 30 DreamGPU
round trips, every frame.

The prepared map/unmap state machine (wined3d_surface_map,
wined3d_surface_unmap, surface_unmap and 0056's dg_surface_partial_map_*) is
compiled unchanged under ASan/UBSan against a pixel-tracking GL: each
surface's texture holds real texels; ReadPixels reads the texture attached to
the read framebuffer honouring PACK_ROW_LENGTH/PACK_ALIGNMENT; TexSubImage
(through the upload helper) writes a rectangle; Wine's location loader is
modelled with full GetTexImage downloads and full uploads; a draw rewrites
texels and invalidates every other location as drawprim does.

Checked: read-only 1x1 and 16x16 boxes read exactly that rectangle with one
ReadPixels and no download, return the same pointer and pitch as Wine's full
map, leave bytes outside the box and the location set untouched, and unmap
without GL; repeated maps without draws and maps interleaved with draws
always see the current texels; a writable box uploads exactly the box at
unmap and the texture stays the only current copy, so a following full map
downloads the merged image; 16-bit formats with padded pitch; GL pack state
and framebuffer state restored. Wine's full path stays for full and
discarding maps, NO_DIRTY_UPDATE writes, current system memory, depth/stencil
and every other ineligible surface (formats, flags, bindings, targets, front
buffer, multisampling, ORM_BACKBUFFER, no blit framebuffers, odd pitch), and
an unusable context falls back to it without a transfer.
"""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
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
typedef uint32_t DWORD; typedef unsigned UINT; typedef unsigned char BYTE; typedef int LONG;
typedef int HRESULT, BOOL; typedef unsigned GLenum, GLuint; typedef int GLint; typedef int GLsizei;
typedef struct { LONG left, top, right, bottom; } RECT;
typedef struct { LONG x, y; } POINT;
#define CDECL
#define TRUE 1
#define FALSE 0
#define TRACE(...) ((void)0)
#define WARN(...) ((void)0)
#define WARN_(channel) WARN
#define FIXME(...) ((void)0)
static unsigned errors;
#define ERR(...) (++errors)
#define checkGLcall(...) ((void)0)
#define GL_EXTCALL(call) ((void)gl_info, call)
#define FAILED(hr) ((hr)<0)
#define WINED3D_OK 0
#define WINED3DERR_INVALIDCALL -1
#define E_OUTOFMEMORY -2
#define WINEDDERR_NOTLOCKED -3
#define MAXLOCKCOUNT 50
#define WINED3D_MAP_READONLY 0x0010
#define WINED3D_MAP_NOSYSLOCK 0x0800
#define WINED3D_MAP_NOOVERWRITE 0x1000
#define WINED3D_MAP_DISCARD 0x2000
#define WINED3D_MAP_NO_DIRTY_UPDATE 0x8000
#define WINED3D_LOCATION_DISCARDED 0x001
#define WINED3D_LOCATION_SYSMEM 0x002
#define WINED3D_LOCATION_USER_MEMORY 0x004
#define WINED3D_LOCATION_BUFFER 0x008
#define WINED3D_LOCATION_TEXTURE_RGB 0x010
#define WINED3D_LOCATION_TEXTURE_SRGB 0x020
#define WINED3D_LOCATION_DRAWABLE 0x040
#define WINED3D_LOCATION_RB_MULTISAMPLE 0x080
#define WINED3D_LOCATION_RB_RESOLVED 0x100
#define WINED3D_LOCATION_DIB 0x200
#define WINED3DUSAGE_RENDERTARGET 0x1
#define WINED3DUSAGE_DEPTHSTENCIL 0x2
#define WINED3DUSAGE_DYNAMIC 0x200
#define WINED3D_POOL_DEFAULT 0
#define WINED3D_RESOURCE_ACCESS_CPU 1
#define WINED3DFMT_FLAG_DEPTH 0x4
#define WINED3DFMT_FLAG_STENCIL 0x8
#define WINED3DFMT_FLAG_FBO_ATTACHABLE 0x40
#define WINED3DFMT_FLAG_COMPRESSED 0x8000
#define WINED3DFMT_FLAG_BROKEN_PITCH 0x10000
#define WINED3DFMT_FLAG_BLOCKS 0x20000
#define WINED3DFMT_FLAG_HEIGHT_SCALE 0x40000
#define WINED3D_TEXTURE_DYNAMIC_MAP 0x1
#define WINED3D_TEXTURE_CONVERTED 0x80
#define WINED3D_TEXTURE_PIN_SYSMEM 0x100
#define SFLAG_CLIENT 0x1
#define SFLAG_DIBSECTION 0x2
#define ORM_BACKBUFFER 0
#define ORM_FBO 1
#define GL_TEXTURE_2D 0x0de1
#define GL_TEXTURE_RECTANGLE_ARB 0x84f5
#define GL_TEXTURE_CUBE_MAP_POSITIVE_X 0x8515
#define GL_READ_FRAMEBUFFER 0x8ca8
#define GL_COLOR_ATTACHMENT0 0x8ce0
#define GL_PACK_ALIGNMENT 0x0d05
#define GL_PACK_ROW_LENGTH 0x0d02
#define GL_PIXEL_UNPACK_BUFFER 0x88ec
#define GL_READ_WRITE 0x88ba
#define GL_BGRA 0x80e1
#define GL_RGB 0x1907
#define GL_LUMINANCE 0x1909
#define GL_UNSIGNED_INT_8_8_8_8_REV 0x8367
#define GL_UNSIGNED_SHORT_5_6_5 0x8363
#define GL_UNSIGNED_BYTE 0x1401
#define STATE_FRAMEBUFFER 77
enum wined3d_format_id { WINED3DFMT_UNKNOWN, WINED3DFMT_B8G8R8A8_UNORM, WINED3DFMT_B8G8R8X8_UNORM,
    WINED3DFMT_R8G8B8A8_UNORM, WINED3DFMT_R8G8B8X8_UNORM, WINED3DFMT_B5G6R5_UNORM,
    WINED3DFMT_B5G5R5X1_UNORM, WINED3DFMT_B5G5R5A1_UNORM, WINED3DFMT_B4G4R4A4_UNORM,
    WINED3DFMT_B4G4R4X4_UNORM, WINED3DFMT_B8G8R8_UNORM, WINED3DFMT_L8_UNORM, WINED3DFMT_D24_UNORM_S8_UINT };
struct color_fixup_desc { unsigned identity; };
static BOOL is_identity_fixup(struct color_fixup_desc f) { return f.identity; }
struct wined3d_format { enum wined3d_format_id id; unsigned byte_count, block_width, block_height,
    block_byte_count, glFormat, glType; void (*convert)(void); struct color_fixup_desc color_fixup; };
struct fbo_ops { void (*glBlitFramebuffer)(void); };
struct wined3d_gl_info_static { struct fbo_ops fbo_ops; };
struct wined3d_adapter { struct wined3d_gl_info_static gl_info; };
struct wined3d_device { BOOL d3d_initialized; struct wined3d_adapter *adapter; unsigned surface_alignment; };
static struct { unsigned offscreen_rendering_mode; } wined3d_settings = {ORM_FBO};
struct resource { const struct wined3d_format *format; struct wined3d_device *device; unsigned map_count,
    pool, access_flags, map_binding, draw_binding, width, height, usage, format_flags, multisample_type;
    void *heap_memory; };
struct wined3d_swapchain;
struct wined3d_texture { struct resource resource; unsigned flags; struct wined3d_swapchain *swapchain;
    unsigned color_key; };
struct wined3d_swapchain { struct wined3d_texture *front_buffer; };
struct wined3d_box { unsigned left, top, right, bottom, front, back; };
struct wined3d_map_desc { unsigned row_pitch, slice_pitch; void *data; };
struct wined3d_surface;
struct wined3d_surface_ops { void (*surface_unmap)(struct wined3d_surface *surface); };
struct wined3d_surface { struct resource resource; const struct wined3d_surface_ops *surface_ops;
    struct wined3d_texture *container; void *user_memory; DWORD locations, flags; UINT pitch;
    GLuint pbo; GLenum texture_target; unsigned texture_level; RECT lockedRect; int lockCount;
    DWORD dg_partial_map; struct { BYTE *bitmap_data; } dib;
    BYTE *texels; /* test: the GL texture image, tightly packed rows */ };
struct gl_ops { struct { void (*p_glReadBuffer)(GLenum); void (*p_glPixelStorei)(GLenum, GLint);
    void (*p_glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *); } gl; };
struct wined3d_gl_info { struct gl_ops gl_ops; };
struct wined3d_context { BOOL valid; const struct wined3d_gl_info *gl_info; };
struct wined3d_const_bo_address { GLuint buffer_object; const BYTE *addr; };

/* ---- recording GL and context ---- */
static unsigned readpixels, getteximage, texsubimage, full_uploads, fbo_binds, read_buffer, fbo_checks,
    invalidated_fbo, acquires, releases, loads, bind_dirtify, gl_other, draws;
static GLint pack_alignment = 4, pack_row_length;
static GLenum last_read_buffer;
static struct wined3d_surface *read_fbo; /* surface attached to the read framebuffer */
static struct wined3d_surface *bound;    /* texture bound for upload */
static struct { GLint x, y; GLsizei w, h; } last_read;
static RECT last_upload;
static int acquire_mode; /* 0 valid, 1 NULL, 2 invalid, 3 not current */
static struct wined3d_context context, *current;
static struct wined3d_context *context_acquire(struct wined3d_device *d, struct wined3d_surface *target)
{
    (void)target; assert(d); ++acquires;
    if (acquire_mode == 1) return NULL;
    context.valid = acquire_mode != 2; current = acquire_mode == 3 ? NULL : &context;
    return &context;
}
static void context_release(struct wined3d_context *c) { assert(c == &context); ++releases; current = NULL; }
static struct wined3d_context *context_get_current(void) { return current; }
static void context_apply_fbo_state_blit(struct wined3d_context *c, GLenum target,
        struct wined3d_surface *rt, struct wined3d_surface *ds, DWORD location)
{
    assert(c == current && target == GL_READ_FRAMEBUFFER && rt && !ds && location == WINED3D_LOCATION_TEXTURE_RGB);
    assert(rt->locations & WINED3D_LOCATION_TEXTURE_RGB);
    read_fbo = rt; ++fbo_binds;
}
static void context_check_fbo_status(const struct wined3d_context *c, GLenum target)
{ assert(c == current && target == GL_READ_FRAMEBUFFER); ++fbo_checks; }
static void context_invalidate_state(struct wined3d_context *c, DWORD state)
{ assert(c == current && state == STATE_FRAMEBUFFER); ++invalidated_fbo; }
static void glReadBuffer(GLenum b) { assert(current); last_read_buffer = b; ++read_buffer; }
static void glPixelStorei(GLenum name, GLint value)
{
    assert(current);
    if (name == GL_PACK_ALIGNMENT) { assert(value == 1 || value == 2 || value == 4 || value == 8); pack_alignment = value; }
    else { assert(name == GL_PACK_ROW_LENGTH && value >= 0); pack_row_length = value; }
}
static void glReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void *out)
{
    const struct wined3d_surface *s = read_fbo;
    unsigned bpp, stride, row;
    assert(current && s && last_read_buffer == GL_COLOR_ATTACHMENT0);
    assert(format == s->resource.format->glFormat && type == s->resource.format->glType);
    assert(x >= 0 && y >= 0 && w > 0 && h > 0 && (unsigned)(x + w) <= s->resource.width
            && (unsigned)(y + h) <= s->resource.height);
    bpp = s->resource.format->byte_count;
    stride = (pack_row_length ? (unsigned)pack_row_length : (unsigned)w) * bpp;
    stride = (stride + pack_alignment - 1) / pack_alignment * pack_alignment;
    for (row = 0; row < (unsigned)h; ++row)
        memcpy((BYTE *)out + row * stride, s->texels + ((y + row) * s->resource.width + x) * bpp, w * bpp);
    last_read.x = x; last_read.y = y; last_read.w = w; last_read.h = h;
    ++readpixels;
}
static void glBindBuffer(GLenum t, GLuint b) { (void)t; (void)b; ++gl_other; }
static void glUnmapBuffer(GLenum t) { (void)t; ++gl_other; }
static void *glMapBuffer(GLenum t, GLenum a) { (void)t; (void)a; ++gl_other; return NULL; }
static const struct wined3d_gl_info gl = {{{glReadBuffer, glPixelStorei, glReadPixels}}};

static UINT wined3d_surface_get_pitch(const struct wined3d_surface *s)
{
    UINT a = s->resource.device->surface_alignment;
    if (s->pitch) return s->pitch;
    return (s->resource.width * s->resource.format->byte_count + a - 1) & ~(a - 1);
}
static const void *wined3d_format_get_color_key_conversion(const struct wined3d_texture *t, BOOL need_alpha)
{ assert(need_alpha); return t->color_key ? (const void *)t : NULL; }
static void wined3d_texture_bind_and_dirtify(struct wined3d_texture *t, struct wined3d_context *c, BOOL srgb)
{ assert(c == current && !srgb); ++bind_dirtify; (void)t; bound = NULL; }
static void surface_validate_location(struct wined3d_surface *s, DWORD l) { s->locations |= l; }
static void surface_invalidate_location(struct wined3d_surface *s, DWORD l) { s->locations &= ~l; }
static BOOL surface_check_block_align(struct wined3d_surface *s, const struct wined3d_box *b)
{ (void)s; (void)b; return TRUE; }
static void surface_prepare_map_memory(struct wined3d_surface *s)
{ if (s->resource.map_binding == WINED3D_LOCATION_SYSMEM && !s->resource.heap_memory) abort(); }

/* Wine's TexSubImage upload of src_rect at dst_point (wined3d_surface_upload_data). */
static void wined3d_surface_upload_data(struct wined3d_surface *s, const struct wined3d_gl_info *g,
        const struct wined3d_format *f, const RECT *r, UINT pitch, const POINT *p, BOOL srgb,
        const struct wined3d_const_bo_address *data)
{
    unsigned row, bpp = f->byte_count;
    assert(g == &gl && current && !srgb && f == s->resource.format && !data->buffer_object && data->addr);
    assert(p->x == r->left && p->y == r->top && pitch == wined3d_surface_get_pitch(s));
    for (row = r->top; row < (unsigned)r->bottom; ++row)
        memcpy(s->texels + (row * s->resource.width + r->left) * bpp,
                data->addr + row * pitch + r->left * bpp, (r->right - r->left) * bpp);
    last_upload = *r; ++texsubimage;
}
/* Wine's location loader for the locations used here: a full GetTexImage
 * download into system memory, a full upload back, or a CPU copy. */
static HRESULT surface_load_location(struct wined3d_surface *s, struct wined3d_context *c, DWORD location)
{
    unsigned row, bpp = s->resource.format->byte_count, pitch = wined3d_surface_get_pitch(s);
    ++loads;
    if (s->locations & location) return WINED3D_OK;
    if (location == WINED3D_LOCATION_SYSMEM)
    {
        if (!(s->locations & WINED3D_LOCATION_TEXTURE_RGB)) return WINED3DERR_INVALIDCALL;
        if (!c || !c->valid || c != current) return WINED3DERR_INVALIDCALL;
        for (row = 0; row < s->resource.height; ++row)
            memcpy((BYTE *)s->resource.heap_memory + row * pitch, s->texels + row * s->resource.width * bpp,
                    s->resource.width * bpp);
        ++getteximage;
    }
    else if (location == WINED3D_LOCATION_TEXTURE_RGB)
    {
        if (!(s->locations & WINED3D_LOCATION_SYSMEM)) return WINED3DERR_INVALIDCALL;
        for (row = 0; row < s->resource.height; ++row)
            memcpy(s->texels + row * s->resource.width * bpp, (BYTE *)s->resource.heap_memory + row * pitch,
                    s->resource.width * bpp);
        ++full_uploads;
    }
    else
        return WINED3DERR_INVALIDCALL;
    s->locations |= location;
    return WINED3D_OK;
}
#include "wine-map-policy.h"
'''

TEST = r'''
static void surface_unmap_entry(struct wined3d_surface *s) { surface_unmap(s); }
static const struct wined3d_surface_ops ops = {surface_unmap_entry};
static struct wined3d_format fmt_x8 = {WINED3DFMT_B8G8R8X8_UNORM, 4, 1, 1, 4, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL, {1}};
static struct wined3d_format fmt_565 = {WINED3DFMT_B5G6R5_UNORM, 2, 1, 1, 2, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, NULL, {1}};
static struct wined3d_format fmt_888 = {WINED3DFMT_B8G8R8_UNORM, 3, 1, 1, 3, GL_BGRA, GL_UNSIGNED_BYTE, NULL, {1}};
static struct wined3d_format fmt_l8 = {WINED3DFMT_L8_UNORM, 1, 1, 1, 1, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL, {1}};
static struct wined3d_format fmt_d24 = {WINED3DFMT_D24_UNORM_S8_UINT, 4, 1, 1, 4, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, NULL, {1}};
static struct wined3d_adapter adapter = {{{(void (*)(void))abort}}};
static struct wined3d_device device = {1, &adapter, 4};
static struct wined3d_swapchain swapchain;
static struct wined3d_texture texture;
static struct wined3d_surface surface;
static struct wined3d_map_desc map;
static BYTE heap[64 * 64 * 4], texels[64 * 64 * 4], reference[64 * 64 * 4];
static unsigned partial_maps;

static BYTE texel_value(unsigned x, unsigned y, unsigned c, unsigned seed) { return (BYTE)(x * 7 + y * 13 + c * 29 + seed * 101 + 1); }
static void draw(unsigned seed) /* a draw into the render target: drawprim's location update */
{
    unsigned x, y, c, bpp = surface.resource.format->byte_count;
    for (y = 0; y < surface.resource.height; ++y)
        for (x = 0; x < surface.resource.width; ++x)
            for (c = 0; c < bpp; ++c)
                texels[(y * surface.resource.width + x) * bpp + c] = texel_value(x, y, c, seed);
    surface.locations |= WINED3D_LOCATION_TEXTURE_RGB;
    surface.locations &= WINED3D_LOCATION_TEXTURE_RGB;
    ++draws;
}
static void counters(void)
{
    readpixels = getteximage = texsubimage = full_uploads = fbo_binds = read_buffer = fbo_checks = invalidated_fbo = 0;
    acquires = releases = loads = bind_dirtify = gl_other = errors = 0;
}
static void reset(const struct wined3d_format *f, unsigned width, unsigned height)
{
    memset(&surface, 0, sizeof(surface)); memset(&texture, 0, sizeof(texture));
    swapchain.front_buffer = NULL;
    texture.resource.format = f; texture.resource.draw_binding = WINED3D_LOCATION_TEXTURE_RGB;
    texture.resource.format_flags = WINED3DFMT_FLAG_FBO_ATTACHABLE;
    texture.swapchain = &swapchain;
    surface.resource = (struct resource){f, &device, 0, WINED3D_POOL_DEFAULT, WINED3D_RESOURCE_ACCESS_CPU,
        WINED3D_LOCATION_SYSMEM, WINED3D_LOCATION_TEXTURE_RGB, width, height, WINED3DUSAGE_RENDERTARGET, 0, 0, heap};
    surface.surface_ops = &ops; surface.container = &texture; surface.texture_target = GL_TEXTURE_RECTANGLE_ARB;
    surface.texels = texels;
    memset(heap, 0xcd, sizeof(heap)); memset(texels, 0, sizeof(texels));
    device.d3d_initialized = 1; device.surface_alignment = 4; wined3d_settings.offscreen_rendering_mode = ORM_FBO;
    adapter.gl_info.fbo_ops.glBlitFramebuffer = (void (*)(void))abort;
    context.gl_info = &gl; acquire_mode = 0; pack_alignment = 4; pack_row_length = 0; read_fbo = bound = NULL; current = NULL;
    draw(0); counters();
}
static unsigned pitch(void) { return wined3d_surface_get_pitch(&surface); }
/* The box's bytes in system memory equal the texture's; everything else is untouched. */
static void check_box(const struct wined3d_box *b, BOOL outside_untouched)
{
    unsigned x, y, c, bpp = surface.resource.format->byte_count;
    for (y = 0; y < surface.resource.height; ++y)
        for (x = 0; x < pitch(); ++x)
        {
            BYTE got = heap[y * pitch() + x];
            unsigned px = x / bpp;
            BOOL inside = y >= b->top && y < b->bottom && px >= b->left && px < b->right && px < surface.resource.width;
            if (inside)
            {
                c = x % bpp;
                assert(got == texels[(y * surface.resource.width + px) * bpp + c]);
            }
            else if (outside_untouched)
                assert(got == 0xcd);
        }
}
static HRESULT map_box(const struct wined3d_box *b, DWORD flags)
{
    HRESULT hr;
    map = (struct wined3d_map_desc){99, 98, (void *)(uintptr_t)0x1234};
    hr = wined3d_surface_map(&surface, &map, b, flags);
    if (!hr && surface.dg_partial_map) ++partial_maps;
    return hr;
}
static void unmap(void) { assert(!wined3d_surface_unmap(&surface) && !surface.resource.map_count); }
static void expect_partial_read(const struct wined3d_box *b, DWORD flags, DWORD mode)
{
    DWORD locations = surface.locations;
    assert(!map_box(b, flags));
    assert(surface.dg_partial_map == mode && surface.resource.map_count == 1);
    assert(readpixels == 1 && !getteximage && !full_uploads && !texsubimage && !gl_other);
    assert(last_read.x == (GLint)b->left && last_read.y == (GLint)b->top);
    assert(last_read.w == (GLsizei)(b->right - b->left) && last_read.h == (GLsizei)(b->bottom - b->top));
    assert(fbo_binds == 1 && read_buffer == 1 && fbo_checks == 1 && invalidated_fbo == 1 && read_fbo == &surface);
    assert(acquires == 1 && releases == 1 && !current && !loads && !errors);
    assert(pack_alignment == 4 && pack_row_length == 0); /* restored */
    assert(surface.locations == locations && !(surface.locations & WINED3D_LOCATION_SYSMEM));
    /* Exactly Wine's pointer and pitch for the box. */
    assert(map.row_pitch == pitch() && map.slice_pitch == pitch() * surface.resource.height);
    assert(map.data == heap + b->top * pitch() + b->left * surface.resource.format->byte_count);
    assert(surface.lockedRect.left == (LONG)b->left && surface.lockedRect.bottom == (LONG)b->bottom);
}
static void expect_full_path(const struct wined3d_box *b, DWORD flags, unsigned downloads)
{
    assert(!map_box(b, flags));
    assert(!surface.dg_partial_map && !readpixels && getteximage == downloads && surface.resource.map_count == 1);
    if (b)
        assert(map.data == heap + b->top * pitch() + b->left * surface.resource.format->byte_count);
}
/* An ineligible surface: Wine's full transfer, then the old unmap. */
static void expect_declined(void)
{
    struct wined3d_box b = {2, 3, 4, 5, 0, 1};
    counters(); expect_full_path(&b, WINED3D_MAP_READONLY, 1);
    assert(!fbo_binds && !texsubimage);
    unmap(); assert(!texsubimage && !full_uploads);
}

int main(void)
{
    struct wined3d_box one = {5, 7, 6, 8, 0, 1}, sixteen = {3, 2, 19, 18, 0, 1}, last = {36, 22, 37, 23, 0, 1};
    struct wined3d_box full = {0, 0, 37, 23, 0, 1}, wbox = {10, 4, 14, 9, 0, 1};
    unsigned i, x, y, c;

    /* Read-only 1x1 (ReduceMouseLag) and 16x16 boxes of an 800x600-like target. */
    reset(&fmt_x8, 37, 23);
    expect_partial_read(&one, WINED3D_MAP_READONLY, 1); check_box(&one, TRUE);
    counters(); unmap(); assert(!readpixels && !texsubimage && !acquires && !full_uploads && !gl_other);
    assert(surface.locations == WINED3D_LOCATION_TEXTURE_RGB && !surface.dg_partial_map && !surface.lockedRect.right);
    counters(); expect_partial_read(&sixteen, WINED3D_MAP_READONLY, 1); check_box(&sixteen, FALSE);
    counters(); unmap(); assert(!acquires);
    counters(); expect_partial_read(&last, WINED3D_MAP_READONLY | WINED3D_MAP_NOSYSLOCK, 1); unmap();
    /* Repeated maps without draws: one rectangle read each, never a download. */
    for (i = 0; i < 60; ++i)
    {
        counters(); expect_partial_read(&one, WINED3D_MAP_READONLY, 1); check_box(&one, FALSE); unmap();
    }
    /* Maps interleaved with draws always see the current texels. */
    for (i = 1; i < 6; ++i)
    {
        draw(i); counters(); expect_partial_read(&sixteen, WINED3D_MAP_READONLY, 1);
        check_box(&sixteen, FALSE);
        assert(*(BYTE *)map.data == texel_value(sixteen.left, sixteen.top, 0, i));
        unmap();
    }

    /* Writable box: read the box, the application writes it, unmap uploads
     * exactly the box; the texture stays the only current copy. */
    reset(&fmt_x8, 37, 23); memcpy(reference, texels, sizeof(texels));
    expect_partial_read(&wbox, 0, 2); check_box(&wbox, TRUE);
    for (y = 0; y < wbox.bottom - wbox.top; ++y)
        for (x = 0; x < (wbox.right - wbox.left) * 4; ++x)
            ((BYTE *)map.data)[y * map.row_pitch + x] = (BYTE)(0x80 + x + y * 16);
    counters(); unmap();
    assert(texsubimage == 1 && bind_dirtify == 1 && acquires == 1 && releases == 1 && !readpixels && !getteximage);
    assert(last_upload.left == (LONG)wbox.left && last_upload.top == (LONG)wbox.top);
    assert(last_upload.right == (LONG)wbox.right && last_upload.bottom == (LONG)wbox.bottom);
    assert(surface.locations == WINED3D_LOCATION_TEXTURE_RGB && !surface.dg_partial_map);
    for (y = 0; y < 23; ++y)
        for (x = 0; x < 37; ++x)
            for (c = 0; c < 4; ++c)
            {
                BOOL in = x >= wbox.left && x < wbox.right && y >= wbox.top && y < wbox.bottom;
                BYTE expect = in ? (BYTE)(0x80 + (x - wbox.left) * 4 + c + (y - wbox.top) * 16)
                        : reference[(y * 37 + x) * 4 + c];
                assert(texels[(y * 37 + x) * 4 + c] == expect);
            }
    /* A full read after the writable map downloads the merged image. */
    memcpy(reference, texels, sizeof(texels));
    counters(); expect_full_path(NULL, WINED3D_MAP_READONLY, 1);
    for (y = 0; y < 23; ++y) assert(!memcmp(heap + y * pitch(), reference + y * 37 * 4, 37 * 4));
    assert(surface.locations == (WINED3D_LOCATION_TEXTURE_RGB | WINED3D_LOCATION_SYSMEM));
    unmap();
    /* System memory now current: a box needs no GL at all (Wine's path). */
    counters(); expect_full_path(&one, WINED3D_MAP_READONLY, 0); assert(!acquires && loads == 1); unmap();
    /* NOOVERWRITE is no discard: writable box as above. */
    draw(9); counters(); expect_partial_read(&wbox, WINED3D_MAP_NOOVERWRITE, 2);
    ((BYTE *)map.data)[0] ^= 0xff; counters(); unmap(); assert(texsubimage == 1);
    assert(texels[(wbox.top * 37 + wbox.left) * 4] == (BYTE)~texel_value(wbox.left, wbox.top, 0, 9));

    /* Full maps keep Wine's download; a full-surface box too. */
    reset(&fmt_x8, 37, 23); expect_full_path(NULL, WINED3D_MAP_READONLY, 1); unmap();
    reset(&fmt_x8, 37, 23); expect_full_path(&full, WINED3D_MAP_READONLY, 1); unmap();
    reset(&fmt_x8, 37, 23); expect_full_path(&full, 0, 1);
    assert(surface.locations == WINED3D_LOCATION_SYSMEM); unmap();
    /* A full writable map leaves system memory current; the next draw uploads it. */
    assert(surface.locations == WINED3D_LOCATION_SYSMEM && !texsubimage);
    /* DISCARD: no transfer, system memory published (Wine's behaviour). */
    reset(&fmt_x8, 37, 23); expect_full_path(&wbox, WINED3D_MAP_DISCARD, 0);
    assert(!acquires && !loads && surface.locations == WINED3D_LOCATION_SYSMEM); unmap();
    /* NO_DIRTY_UPDATE writes keep Wine's path; read-only ones may read the box. */
    reset(&fmt_x8, 37, 23); expect_full_path(&wbox, WINED3D_MAP_NO_DIRTY_UPDATE, 1);
    assert(surface.locations == (WINED3D_LOCATION_TEXTURE_RGB | WINED3D_LOCATION_SYSMEM)); unmap();
    reset(&fmt_x8, 37, 23); expect_partial_read(&wbox, WINED3D_MAP_NO_DIRTY_UPDATE | WINED3D_MAP_READONLY, 1); unmap();

    /* 16-bit target with a padded pitch (37 * 2 = 74 -> 76). */
    reset(&fmt_565, 37, 23); assert(pitch() == 76);
    expect_partial_read(&sixteen, WINED3D_MAP_READONLY, 1); check_box(&sixteen, TRUE); unmap();
    counters(); expect_partial_read(&last, 0, 2); check_box(&last, FALSE);
    ((BYTE *)map.data)[1] = 0x5a; counters(); unmap(); assert(texsubimage == 1);
    assert(texels[(22 * 37 + 36) * 2 + 1] == 0x5a && texels[(22 * 37 + 36) * 2] == texel_value(36, 22, 0, 0));
    /* An application pitch not divisible by the pixel size declines. */
    reset(&fmt_565, 37, 23); surface.pitch = 75; expect_declined();

    /* Ineligible surfaces keep Wine's full transfer. */
    reset(&fmt_x8, 37, 23); surface.resource.usage = 0; expect_declined();
    reset(&fmt_d24, 37, 23); surface.resource.usage = WINED3DUSAGE_DEPTHSTENCIL; expect_declined();
    reset(&fmt_d24, 37, 23); texture.resource.format_flags |= WINED3DFMT_FLAG_DEPTH | WINED3DFMT_FLAG_STENCIL;
    expect_declined();
    reset(&fmt_x8, 37, 23); texture.resource.format_flags = 0; expect_declined();
    reset(&fmt_x8, 37, 23); texture.resource.format_flags |= WINED3DFMT_FLAG_BLOCKS; expect_declined();
    reset(&fmt_x8, 37, 23); texture.resource.format_flags |= WINED3DFMT_FLAG_COMPRESSED; expect_declined();
    reset(&fmt_x8, 37, 23); texture.resource.format_flags |= WINED3DFMT_FLAG_BROKEN_PITCH; expect_declined();
    reset(&fmt_x8, 37, 23); texture.resource.format_flags |= WINED3DFMT_FLAG_HEIGHT_SCALE; expect_declined();
    reset(&fmt_888, 37, 23); expect_declined();
    reset(&fmt_l8, 37, 23); expect_declined();
    reset(&fmt_x8, 37, 23); fmt_x8.color_fixup.identity = 0; expect_declined(); fmt_x8.color_fixup.identity = 1;
    reset(&fmt_x8, 37, 23); fmt_x8.convert = (void (*)(void))abort; expect_declined(); fmt_x8.convert = NULL;
    reset(&fmt_x8, 37, 23); texture.resource.format = &fmt_565; expect_declined();
    reset(&fmt_x8, 37, 23); texture.flags = WINED3D_TEXTURE_CONVERTED; expect_declined();
    reset(&fmt_x8, 37, 23); texture.flags = WINED3D_TEXTURE_PIN_SYSMEM; expect_declined();
    reset(&fmt_x8, 37, 23); texture.color_key = 1; expect_declined();
    reset(&fmt_x8, 37, 23); swapchain.front_buffer = &texture; expect_declined();
    reset(&fmt_x8, 37, 23); texture.swapchain = NULL; expect_partial_read(&one, WINED3D_MAP_READONLY, 1); unmap();
    reset(&fmt_x8, 37, 23); texture.resource.draw_binding = WINED3D_LOCATION_DRAWABLE; expect_declined();
    reset(&fmt_x8, 37, 23); surface.resource.multisample_type = 4; expect_declined();
    reset(&fmt_x8, 37, 23); surface.flags = SFLAG_CLIENT; expect_declined();
    reset(&fmt_x8, 37, 23); surface.flags = SFLAG_DIBSECTION; expect_declined();
    reset(&fmt_x8, 37, 23); surface.texture_target = GL_TEXTURE_2D; expect_partial_read(&one, WINED3D_MAP_READONLY, 1); unmap();
    reset(&fmt_x8, 37, 23); surface.texture_target = GL_TEXTURE_CUBE_MAP_POSITIVE_X; expect_declined();
    reset(&fmt_x8, 37, 23); surface.texture_level = 1; expect_declined();
    reset(&fmt_x8, 37, 23); wined3d_settings.offscreen_rendering_mode = ORM_BACKBUFFER; expect_declined();
    reset(&fmt_x8, 37, 23); adapter.gl_info.fbo_ops.glBlitFramebuffer = NULL; expect_declined();
    /* Texture not current (only the drawable or a resolved buffer): Wine's path. */
    reset(&fmt_x8, 37, 23); surface.locations = WINED3D_LOCATION_RB_RESOLVED; counters();
    assert(map_box(&one, WINED3D_MAP_READONLY) == WINED3DERR_INVALIDCALL && !readpixels && !surface.resource.map_count);
    /* DIB and user-memory bindings keep Wine's path (and its loader). */
    reset(&fmt_x8, 37, 23); surface.resource.map_binding = WINED3D_LOCATION_DIB; surface.dib.bitmap_data = heap;
    counters(); assert(map_box(&one, WINED3D_MAP_READONLY) == WINED3DERR_INVALIDCALL && !readpixels && loads == 1);
    reset(&fmt_x8, 37, 23); surface.resource.map_binding = WINED3D_LOCATION_USER_MEMORY; surface.user_memory = heap;
    counters(); assert(map_box(&one, WINED3D_MAP_READONLY) == WINED3DERR_INVALIDCALL && !readpixels && loads == 1);
    /* Without Direct3D initialised (DirectDraw only) nothing changes. */
    reset(&fmt_x8, 37, 23); device.d3d_initialized = 0; counters();
    assert(map_box(&one, WINED3D_MAP_READONLY) == WINED3DERR_INVALIDCALL && !readpixels && !acquires);

    /* An unusable context declines without a transfer; Wine's loader then
     * reports the failure exactly as before, with the map count balanced. */
    for (i = 1; i <= 3; ++i)
    {
        reset(&fmt_x8, 37, 23); acquire_mode = (int)i;
        assert(map_box(&one, WINED3D_MAP_READONLY) == WINED3DERR_INVALIDCALL);
        assert(!readpixels && !fbo_binds && !getteximage && !surface.resource.map_count && !surface.dg_partial_map);
        assert(acquires == 2 && releases == (i == 1 ? 0u : 2u));
        assert(surface.locations == WINED3D_LOCATION_TEXTURE_RGB);
    }
    /* A writable box whose unmap finds no context reports it and keeps the texture. */
    reset(&fmt_x8, 37, 23); memcpy(reference, texels, sizeof(texels));
    expect_partial_read(&wbox, 0, 2); ((BYTE *)map.data)[0] ^= 0xff;
    counters(); acquire_mode = 2; unmap();
    assert(errors == 1 && !texsubimage && releases == 1 && surface.locations == WINED3D_LOCATION_TEXTURE_RGB);
    assert(!memcmp(texels, reference, sizeof(texels)) && !surface.dg_partial_map);
    /* Locked surfaces cannot be mapped again; unmapping twice fails. */
    reset(&fmt_x8, 37, 23); expect_partial_read(&one, WINED3D_MAP_READONLY, 1);
    assert(map_box(&one, WINED3D_MAP_READONLY) == WINED3DERR_INVALIDCALL && surface.resource.map_count == 1);
    unmap(); assert(wined3d_surface_unmap(&surface) == WINEDDERR_NOTLOCKED);
    /* An invalid box fails before any transfer. */
    reset(&fmt_x8, 37, 23);
    {
        struct wined3d_box bad = {5, 7, 5, 8, 0, 1};
        assert(map_box(&bad, WINED3D_MAP_READONLY) == WINED3DERR_INVALIDCALL && !acquires && !readpixels);
    }

    printf("PASS prepared rectangle-only render target maps (0056): %u box maps (one ReadPixels each, no "
            "download), %u draws, writable box upload, full/discard/no-dirty/depth/ineligible fallbacks, "
            "context failures\n", partial_maps, draws);
    return 0;
}
'''


def region(source, start, end):
    first = source.index(start)
    return source[first:source.index(end, first)]


with tempfile.TemporaryDirectory(prefix='dreamgpu-partial-maps-') as directory:
    work = Path(directory)
    for name in json.loads(MANIFEST.read_text())['files']:
        path = work / name
        path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'vendor/wine9x' / name, path)
    patches.apply(work, MANIFEST)
    source = (work / 'wined3d/surface.c').read_text()
    private = (work / 'wined3d/wined3d_private.h').read_text()
    assert 'DWORD                     dg_partial_map;' in private
    helpers = region(source, '/* DreamGPU (0056):', '\nstatic BOOL surface_is_full_rect(')
    mapping = region(source, 'HRESULT CDECL wined3d_surface_unmap(', '\nHRESULT CDECL wined3d_surface_getdc(')
    (work / 'test.c').write_text(PREAMBLE + helpers + mapping + TEST)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-function', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-I' + str(ROOT / 'guest/d3d'), str(work / 'test.c'), '-o', str(work / 'test')], check=True)
    subprocess.run([str(work / 'test')], check=True)
