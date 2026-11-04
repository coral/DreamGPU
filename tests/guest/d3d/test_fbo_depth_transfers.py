#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Patch 0043 on actual pinned Wine source: CPU depth transfers through framebuffer objects.

With EXT_framebuffer_object (DG_CAP_GL_FRAMEBUFFER_OBJECT) Wine renders every
target offscreen, and an offscreen depth surface has its own GL storage: its
draw binding, a depth texture (or a renderbuffer). GetTexImage takes no depth
format on DreamGPU (ARB_depth_texture is not claimed), and 0017's transfer only
knew the drawable, so locking a depth surface failed and CPU depth never
reached the GPU. The prepared surface_transfer_depth() and
surface_load_ds_location() run here under ASan/UBSan against a mock GL that
holds separate texture and renderbuffer attachments:

- downloads read the surface's own attachment, alone in an FBO, unflipped, in
  all seven DirectDraw/D3D layouts, without consulting backbuffer ownership;
- uploads allocate the binding first, draw DEPTH_COMPONENT pixels, and pack
  stencil formats as DEPTH_STENCIL/UNSIGNED_INT_24_8 (no STENCIL_INDEX
  DrawPixels on an FBO, as the GL contract requires);
- failed reads and writes publish nothing and retry;
- a CPU-only surface reaches its offscreen binding (and the drawable through
  the depth texture) instead of a copy of the window's depth; a partly current
  binding without a drawable copy is complete as it is, while the upstream
  drawable/texture copies and the discarded shortcut remain.
It fails without the patch.
"""
import importlib.util
import json
import os
from pathlib import Path
import re
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
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef int BOOL, HRESULT, GLint, GLsizei;
typedef uint32_t DWORD, GLuint, GLenum, GLbitfield, UINT;
typedef uint8_t BYTE;
typedef uint64_t ULONGLONG;
typedef size_t SIZE_T;
#define TRUE 1
#define FALSE 0
#define WINED3D_OK 0
#define WINED3DERR_INVALIDCALL -1
#define E_OUTOFMEMORY -2
#define FAILED(hr) ((hr) < 0)
#define TRACE(...) ((void)0)
#define ERR(...) ((void)0)
#define FIXME(...) ((void)0)
#define checkGLcall(...) ((void)0)
#define min(a, b) ((a) < (b) ? (a) : (b))
#define ORM_BACKBUFFER 0
#define ORM_FBO 1
#define STATE_FRAMEBUFFER 77
#define ARB_FRAGMENT_PROGRAM 0
#define ARB_VERTEX_PROGRAM 1
#define EXT_PACKED_DEPTH_STENCIL 2
#define ARB_FRAMEBUFFER_OBJECT 3
#define WINED3DFMT_D16_LOCKABLE 1
#define WINED3DFMT_D16_UNORM 2
#define WINED3DFMT_D32_UNORM 3
#define WINED3DFMT_D24_UNORM_S8_UINT 4
#define WINED3DFMT_X8D24_UNORM 5
#define WINED3DFMT_S1_UINT_D15_UNORM 6
#define WINED3DFMT_S4X4_UINT_D24_UNORM 7
static struct { int offscreen_rendering_mode; BOOL strict_draw_ordering; } wined3d_settings = {ORM_FBO, FALSE};
struct format { unsigned id, byte_count, depth_size, stencil_size; };
struct wined3d_surface;
struct wined3d_device { struct wined3d_surface *onscreen_depth_stencil; GLuint depth_blt_texture; };
struct resource { unsigned width, height; struct format *format; struct wined3d_device *device; };
struct texture { struct { GLuint name; } texture_rgb; };
struct renderbuffer_entry { int unused; };
struct wined3d_surface {
    struct resource resource;
    struct texture *container;
    DWORD locations;
    struct { unsigned cx, cy; } ds_current_size;
    struct renderbuffer_entry *current_renderbuffer;
    GLenum texture_target;
    unsigned pow2Height;
};
struct swapchain { struct { int backbuffer_width, backbuffer_height; } desc; void *front_buffer; };
struct wined3d_gl_info {
    struct { struct {
        GLenum (*p_glGetError)(void);
        void (*p_glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *);
        void (*p_glDrawPixels)(GLsizei, GLsizei, GLenum, GLenum, const void *);
        void (*p_glCopyTexImage2D)(GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLsizei, GLint);
        void (*p_glStencilMask)(GLuint);
        void (*p_glPushAttrib)(GLbitfield);
        void (*p_glPopAttrib)(void);
        void (*p_glPushMatrix)(void);
        void (*p_glPopMatrix)(void);
        void (*p_glPushClientAttrib)(GLbitfield);
        void (*p_glPopClientAttrib)(void);
        void (*noop)();
    } gl; } gl_ops;
    BOOL supported[4];
};
struct wined3d_context {
    BOOL valid;
    struct wined3d_surface *current_rt;
    const struct wined3d_gl_info *gl_info;
    BOOL render_offscreen;
    struct swapchain *swapchain;
};
struct wined3d_bo_address { BYTE *addr; GLuint buffer_object; };
/*CONSTANTS*/
enum { TEXTURE, RENDERBUFFER, ATTACHMENTS };
static GLuint gpu_depth[ATTACHMENTS][6];
static BYTE gpu_stencil[ATTACHMENTS][6];
static BYTE storage[48];
static unsigned row_pitch;
static struct wined3d_context *current;
static struct wined3d_surface *bound_ds;
static DWORD bound_location, prepared, invalidated;
static unsigned binds, reads, draws, copies, depth_blts, owner_checks, gl_calls, stencil_write_mask;
static unsigned fail_read, fail_write, pending_error;
static struct wined3d_surface front;

static int attachment(void)
{
    assert(bound_ds && binds);
    assert(bound_location == WINED3D_LOCATION_TEXTURE_RGB || bound_location == WINED3D_LOCATION_RB_RESOLVED);
    return bound_location == WINED3D_LOCATION_TEXTURE_RGB ? TEXTURE : RENDERBUFFER;
}
static GLenum get_error(void) { GLenum e = pending_error; pending_error = 0; return e; }
static void read_pixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, void *data)
{
    int a = attachment();
    ++gl_calls; ++reads;
    assert(!x && !y && w == 3 && h == 2);
    if (fail_read) { pending_error = 0x0506; memset(data, 0, format == GL_DEPTH_COMPONENT ? 24 : 6); return; }
    if (format == GL_DEPTH_COMPONENT) { assert(type == GL_UNSIGNED_INT); memcpy(data, gpu_depth[a], 24); }
    else { assert(format == GL_STENCIL_INDEX && type == GL_UNSIGNED_BYTE); memcpy(data, gpu_stencil[a], 6); }
}
static void draw_pixels(GLsizei w, GLsizei h, GLenum format, GLenum type, const void *data)
{
    int a = attachment(), i;
    ++gl_calls; ++draws;
    assert(w == 3 && h == 2);
    /* The FBO contract: DEPTH_COMPONENT or packed DEPTH_STENCIL pixels only. */
    assert(format != GL_STENCIL_INDEX);
    if (fail_write) { pending_error = 0x0502; return; }
    if (format == GL_DEPTH_COMPONENT)
    {
        assert(type == GL_UNSIGNED_INT);
        memcpy(gpu_depth[a], data, 24);
        return;
    }
    assert(format == GL_DEPTH_STENCIL && type == GL_UNSIGNED_INT_24_8);
    for (i = 0; i < 6; ++i)
    {
        GLuint v = ((const GLuint *)data)[i];
        gpu_depth[a][i] = (v & 0xffffff00u) | (v >> 24);   /* 24 bits replicated to 32 */
        gpu_stencil[a][i] = (BYTE)((gpu_stencil[a][i] & ~stencil_write_mask) | (v & stencil_write_mask));
    }
}
static void copy_tex_image(GLenum t, GLint l, GLenum f, GLint x, GLint y, GLsizei w, GLsizei h, GLint b)
{ (void)t; (void)l; (void)f; (void)x; (void)y; (void)w; (void)h; (void)b; ++gl_calls; ++copies; }
static void stencil_mask(GLuint mask) { ++gl_calls; stencil_write_mask = mask; }
static void push_attrib(GLbitfield mask) { ++gl_calls; assert(!(mask & ~0x000fffffu)); }
static void pop(void) { ++gl_calls; }
static void push(void) { ++gl_calls; }
static void push_client(GLbitfield mask) { (void)mask; ++gl_calls; }
static void noop() { ++gl_calls; }

static struct wined3d_context *context_get_current(void) { return current; }
static BOOL device_owns_backbuffer_ds(const struct wined3d_device *d, const struct wined3d_context *c,
        const struct wined3d_surface *s)
{ (void)d; (void)c; (void)s; ++owner_checks; return FALSE; }
static void surface_get_drawable_size(const struct wined3d_surface *s, const struct wined3d_context *c,
        unsigned *w, unsigned *h)
{ (void)s; (void)c; (void)w; (void)h; assert(!"framebuffer objects never use the drawable size"); }
static UINT wined3d_surface_get_pitch(const struct wined3d_surface *s) { (void)s; return row_pitch; }
static void surface_get_memory(const struct wined3d_surface *s, struct wined3d_bo_address *m, DWORD l)
{ (void)s; assert(l); m->addr = storage + 4; m->buffer_object = 0; }
static void surface_validate_location(struct wined3d_surface *s, DWORD l) { s->locations |= l; }
static void surface_modify_ds_location(struct wined3d_surface *s, DWORD l, UINT w, UINT h)
{ s->locations = l; s->ds_current_size.cx = w; s->ds_current_size.cy = h; }
static void wined3d_surface_prepare(struct wined3d_surface *s, struct wined3d_context *c, DWORD l)
{ (void)s; (void)c; prepared |= l; }
static void context_apply_fbo_state_blit(struct wined3d_context *c, GLenum target,
        struct wined3d_surface *rt, struct wined3d_surface *ds, DWORD location)
{
    assert(c == current);
    if (location == WINED3D_LOCATION_DRAWABLE) { bound_ds = NULL; bound_location = location; return; }
    assert(target == GL_FRAMEBUFFER && !rt && ds);
    /* An upload's storage exists before it is attached. */
    assert(!(ds->locations & surface_simple_locations) || (ds->locations & location) || (prepared & location));
    bound_ds = ds; bound_location = location; ++binds;
}
static void context_invalidate_state(struct wined3d_context *c, DWORD state)
{ assert(c == current); if (state == STATE_FRAMEBUFFER) ++invalidated; }
static void context_set_draw_buffer(struct wined3d_context *c, GLenum buffer) { (void)c; (void)buffer; }
static void surface_depth_blt(const struct wined3d_surface *s, struct wined3d_context *c, GLuint texture,
        GLint x, GLint y, GLsizei w, GLsizei h, GLenum target)
{ (void)s; (void)c; (void)texture; (void)x; (void)y; (void)w; (void)h; (void)target; ++depth_blts; }
static void *wined3d_texture_get_sub_resource(void *texture, unsigned i) { (void)i; return texture; }
static struct wined3d_surface *surface_from_resource(void *resource) { (void)resource; return &front; }
'''

TEST = r'''
static void reset(void) { binds = reads = draws = copies = depth_blts = owner_checks = 0; prepared = invalidated = 0;
        bound_ds = NULL; bound_location = 0; fail_read = fail_write = pending_error = 0; }
static DWORD cpu_value(const struct format *f, unsigned i, unsigned j)
{ DWORD v = 0; memcpy(&v, storage + 4 + i * row_pitch + j * f->byte_count, f->byte_count); return v; }
int main(void)
{
    struct format format = {1, 2, 16, 0};
    struct wined3d_device device = {NULL, 0};
    struct texture texture = {{9}};
    struct swapchain swapchain = {{3, 2}, NULL};
    struct wined3d_surface surface = {{3, 2, &format, &device}, &texture, 0, {0, 0}, NULL, GL_TEXTURE_2D, 4};
    struct wined3d_gl_info gl = {{{get_error, read_pixels, draw_pixels, copy_tex_image, stencil_mask,
            push_attrib, pop, push, pop, push_client, pop, noop}}, {1, 1, 1, 0}};
    struct wined3d_context context = {1, &surface, &gl, FALSE, &swapchain};
    BYTE saved[sizeof(storage)];
    DWORD mask, smask, shift;
    unsigned id, a, i, j;

    current = &context;
    for (a = 0; a < ATTACHMENTS; ++a)
    for (id = 1; id <= 7; ++id)
    {
        DWORD binding = a == TEXTURE ? WINED3D_LOCATION_TEXTURE_RGB : WINED3D_LOCATION_RB_RESOLVED;
        format.id = id;
        format.byte_count = id == 1 || id == 2 || id == 6 ? 2 : 4;
        row_pitch = 3 * format.byte_count + 2;
        assert(surface_depth_layout(&surface, &mask, &smask));
        shift = mask == 0x7fff ? 15 : 24;
        for (i = 0; i < 6; ++i)
        {
            gpu_depth[a][i] = i == 0 ? 0 : i == 5 ? 0xffffffffu : i * 0x22222222u;
            gpu_stencil[a][i] = (BYTE)(i + 1);
            gpu_depth[!a][i] = 0x5a5a5a5au;
        }
        memset(storage, 0xa5, sizeof(storage));

        /* Download: the binding's own attachment, unflipped although the
         * context renders onscreen, without any backbuffer ownership. */
        reset(); surface.locations = binding;
        assert(surface_transfer_depth(&surface, &context, WINED3D_LOCATION_SYSMEM) == WINED3D_OK);
        assert(surface.locations == (binding | WINED3D_LOCATION_SYSMEM));
        assert(binds == 1 && bound_ds == &surface && bound_location == binding && invalidated == 1);
        assert(!prepared && !owner_checks && reads == (smask ? 2u : 1u) && !draws);
        for (i = 0; i < 2; ++i)
            for (j = 0; j < 3; ++j)
            {
                DWORD v = cpu_value(&format, i, j);
                unsigned index = i * 3 + j;
                assert((v & mask) == (DWORD)(((ULONGLONG)gpu_depth[a][index] * mask + 0x7fffffff) / 0xffffffffu));
                if (smask) assert((v >> shift) == (gpu_stencil[a][index] & smask));
            }
        assert(storage[0] == 0xa5 && storage[3] == 0xa5 && storage[4 + 3 * format.byte_count] == 0xa5);
        assert(storage[4 + row_pitch + 3 * format.byte_count] == 0xa5 && storage[sizeof(storage) - 1] == 0xa5);
        assert(surface.ds_current_size.cx == 3 && surface.ds_current_size.cy == 2);

        /* Upload a CPU edit: allocate, attach, draw; packed for stencil. */
        {
            DWORD value = mask | (smask << shift);
            memcpy(storage + 4, &value, format.byte_count);
            value = 0;
            memcpy(storage + 4 + row_pitch + 2 * format.byte_count, &value, format.byte_count);
        }
        memcpy(saved, storage, sizeof(storage));
        reset(); surface.locations = WINED3D_LOCATION_SYSMEM;
        assert(surface_transfer_depth(&surface, &context, binding) == WINED3D_OK);
        assert(surface.locations == (binding | WINED3D_LOCATION_SYSMEM) && prepared == binding);
        assert(binds == 1 && bound_location == binding && draws == 1 && !reads && !owner_checks);
        assert(stencil_write_mask == smask);
        assert(!memcmp(saved, storage, sizeof(storage)));
        /* Read back through the same attachment: the CPU values survive exactly. */
        memset(storage + 4, 0, sizeof(storage) - 8);
        reset(); surface.locations = binding;
        assert(surface_transfer_depth(&surface, &context, WINED3D_LOCATION_SYSMEM) == WINED3D_OK);
        for (i = 0; i < 2; ++i)
            for (j = 0; j < 3; ++j)
            {
                DWORD expected, actual = cpu_value(&format, i, j);
                memcpy(&expected, saved + 4 + i * row_pitch + j * format.byte_count, 4);
                if (format.byte_count == 2) expected &= 0xffff;
                assert((actual & (mask | (smask << shift))) == (expected & (mask | (smask << shift))));
            }
        assert(gpu_depth[!a][0] == 0x5a5a5a5au);
    }

    /* No GPU copy, a GL-side target that is not a binding, and no CPU source
     * for an upload are errors without GL work. */
    format.id = 4; format.byte_count = 4; row_pitch = 14;
    reset(); i = gl_calls; surface.locations = WINED3D_LOCATION_DRAWABLE;
    assert(surface_transfer_depth(&surface, &context, WINED3D_LOCATION_SYSMEM) == WINED3DERR_INVALIDCALL);
    surface.locations = WINED3D_LOCATION_TEXTURE_RGB;
    assert(surface_transfer_depth(&surface, &context, WINED3D_LOCATION_TEXTURE_RGB) == WINED3DERR_INVALIDCALL);
    assert(surface_transfer_depth(&surface, &context, WINED3D_LOCATION_DRAWABLE) == WINED3DERR_INVALIDCALL);
    surface.locations = WINED3D_LOCATION_SYSMEM;
    current = NULL;
    assert(surface_transfer_depth(&surface, &context, WINED3D_LOCATION_TEXTURE_RGB) == WINED3DERR_INVALIDCALL);
    current = &context;
    assert(gl_calls == i && !binds && surface.locations == WINED3D_LOCATION_SYSMEM);

    /* Failed transfers publish nothing; both retry. */
    memset(storage, 0x3c, sizeof(storage)); memcpy(saved, storage, sizeof(storage));
    reset(); fail_read = 1; surface.locations = WINED3D_LOCATION_TEXTURE_RGB;
    assert(surface_transfer_depth(&surface, &context, WINED3D_LOCATION_SYSMEM) == WINED3DERR_INVALIDCALL);
    assert(surface.locations == WINED3D_LOCATION_TEXTURE_RGB && !memcmp(saved, storage, sizeof(storage)));
    reset();
    assert(surface_transfer_depth(&surface, &context, WINED3D_LOCATION_SYSMEM) == WINED3D_OK);
    reset(); fail_write = 1; surface.locations = WINED3D_LOCATION_SYSMEM;
    assert(surface_transfer_depth(&surface, &context, WINED3D_LOCATION_TEXTURE_RGB) == WINED3DERR_INVALIDCALL);
    assert(surface.locations == WINED3D_LOCATION_SYSMEM);
    reset();
    assert(surface_transfer_depth(&surface, &context, WINED3D_LOCATION_TEXTURE_RGB) == WINED3D_OK);

    /* surface_load_ds_location: CPU depth reaches the offscreen binding... */
    reset(); surface.locations = WINED3D_LOCATION_SYSMEM; surface.ds_current_size.cx = surface.ds_current_size.cy = 0;
    assert(surface_load_ds_location(&surface, &context, WINED3D_LOCATION_TEXTURE_RGB) == WINED3D_OK);
    assert(draws == 1 && !copies && !depth_blts);
    assert(surface.locations == (WINED3D_LOCATION_SYSMEM | WINED3D_LOCATION_TEXTURE_RGB));
    assert(surface.ds_current_size.cx == 3 && surface.ds_current_size.cy == 2);
    /* ...and the drawable through the depth texture. */
    reset(); surface.locations = WINED3D_LOCATION_SYSMEM;
    assert(surface_load_ds_location(&surface, &context, WINED3D_LOCATION_DRAWABLE) == WINED3D_OK);
    assert(draws == 1 && !copies && depth_blts == 1);
    assert(surface.locations == (WINED3D_LOCATION_SYSMEM | WINED3D_LOCATION_TEXTURE_RGB | WINED3D_LOCATION_DRAWABLE));
    /* A failed upload is returned and leaves the CPU copy authoritative. */
    reset(); fail_write = 1; surface.locations = WINED3D_LOCATION_SYSMEM;
    assert(surface_load_ds_location(&surface, &context, WINED3D_LOCATION_TEXTURE_RGB) == WINED3DERR_INVALIDCALL);
    assert(surface.locations == WINED3D_LOCATION_SYSMEM && !copies);
    /* A partly current binding with no drawable copy is complete as it is
     * (for example a depth blit's destination), never the window's depth. */
    reset(); i = gl_calls; surface.locations = WINED3D_LOCATION_TEXTURE_RGB;
    surface.ds_current_size.cx = surface.ds_current_size.cy = 0;
    assert(surface_load_ds_location(&surface, &context, WINED3D_LOCATION_TEXTURE_RGB) == WINED3D_OK);
    assert(gl_calls == i && !copies && !depth_blts && surface.locations == WINED3D_LOCATION_TEXTURE_RGB);
    assert(surface.ds_current_size.cx == 3 && surface.ds_current_size.cy == 2);
    /* Upstream: a drawable copy completes the texture; the texture fills the
     * drawable; a discarded surface is only prepared. */
    reset(); surface.locations = WINED3D_LOCATION_TEXTURE_RGB | WINED3D_LOCATION_DRAWABLE;
    surface.ds_current_size.cx = 1;
    assert(surface_load_ds_location(&surface, &context, WINED3D_LOCATION_TEXTURE_RGB) == WINED3D_OK);
    assert(copies == 1 && depth_blts == 1 && !draws);
    reset(); surface.locations = WINED3D_LOCATION_DRAWABLE;
    assert(surface_load_ds_location(&surface, &context, WINED3D_LOCATION_TEXTURE_RGB) == WINED3D_OK);
    assert(copies == 1 && !draws && (surface.locations & WINED3D_LOCATION_TEXTURE_RGB));
    reset(); surface.locations = WINED3D_LOCATION_DISCARDED | WINED3D_LOCATION_SYSMEM;
    assert(surface_load_ds_location(&surface, &context, WINED3D_LOCATION_TEXTURE_RGB) == WINED3D_OK);
    assert(!draws && !copies && prepared == WINED3D_LOCATION_TEXTURE_RGB);
    assert(surface.locations == (WINED3D_LOCATION_SYSMEM | WINED3D_LOCATION_TEXTURE_RGB));
    puts("PASS prepared FBO depth transfers: seven layouts through texture and renderbuffer attachments, "
         "unflipped, packed stencil uploads, failures and retries, CPU depth to binding and drawable, "
         "partly current bindings, upstream copies and discards");
    return 0;
}
'''


def function(source, prefix):
    start = source.index(prefix)
    return source[start:source.index('\n}', start) + 2] + '\n'


with tempfile.TemporaryDirectory(prefix='dreamgpu-fbo-depth-') as temporary:
    work = Path(temporary)
    for name in json.loads(MANIFEST.read_text())['files']:
        p = work / name
        p.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'vendor/wine9x' / name, p)
    patches.apply(work, MANIFEST)
    source = (work / 'wined3d/surface.c').read_text()
    private = (work / 'wined3d/wined3d_private.h').read_text()
    locations = '\n'.join('#define %s %s' % pair for pair in
                          re.findall(r'^#define (WINED3D_LOCATION_\w+)\s+(0x[0-9a-fA-F]+)', private, re.M))
    constants = re.search(r'^static const DWORD surface_simple_locations =[^;]*;', source, re.M)[0] + '\n'
    constants += re.search(r'^static const DWORD surface_fbo_depth_locations =[^;]*;', source, re.M)[0] + '\n'
    code = ''.join(function(source, prefix) for prefix in (
        'static BOOL surface_depth_layout(', 'static void surface_depth_convert(',
        'static BOOL surface_depth_transfer_available(', 'static HRESULT surface_transfer_depth(',
        'HRESULT surface_load_ds_location('))
    header = (ROOT / 'vendor/wine9x/include/wine/wgl.h').read_text()
    values = dict(re.findall(r'^#define\s+(GL_[A-Z_0-9]+)\s+(0x[0-9a-fA-F]+|[0-9]+)\s*$', header, re.M))
    enums = sorted(set(re.findall(r'\bGL_[A-Z_0-9]+\b', code + TEST)) - {'GL_ALL_ATTRIB_BITS'})
    gl = '\n'.join('#define %s %s' % (e, values[e]) for e in enums)
    special = {'p_glGetError', 'p_glReadPixels', 'p_glDrawPixels', 'p_glCopyTexImage2D', 'p_glStencilMask',
               'p_glPushAttrib', 'p_glPopAttrib', 'p_glPushMatrix', 'p_glPopMatrix', 'p_glPushClientAttrib',
               'p_glPopClientAttrib'}
    gl += '\n' + '\n'.join('#define %s noop' % n for n in sorted(set(re.findall(r'p_gl\w+', code)) - special))
    program = locations + '\n' + PREAMBLE.replace('#define ORM_BACKBUFFER 0', gl + '\n#define ORM_BACKBUFFER 0') \
        .replace('/*CONSTANTS*/', constants) + code + TEST
    (work / 'test.c').write_text(program)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                    '-Wno-unused-function', '-Wno-strict-prototypes', '-Wno-old-style-definition',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer',
                    str(work / 'test.c'), '-o', str(work / 'test')], check=True)
    subprocess.run([str(work / 'test')], check=True)
