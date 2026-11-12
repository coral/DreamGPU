#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute prepared Wine reset/uninit/unload code when no GL context exists (0059).

Warcraft III (D3D8) reset its device while its fullscreen window was
minimized. The old frontend could not create a context there, so the reset
destroyed every context and failed; the next reset dereferenced the NULL
context_acquire() result in delete_opengl_contexts. The prepared functions run
here against deterministic context, GL and allocation boundaries under
ASan/UBSan: double frees of the shader/blitter state or the swapchain context
array, or a GL call without a context, fail the test.
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

COMMON = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wine/list.h"
typedef int BOOL, HRESULT;
typedef unsigned int DWORD, UINT, ULONG, GLuint, GLenum;
typedef unsigned char BYTE;
#define TRUE 1
#define FALSE 0
#define CDECL
#define TRACE(...) ((void)0)
#define WARN(...) ((void)0)
#define FIXME(...) ((void)0)
#define ERR(...) ((void)0)
#define checkGLcall(...) ((void)0)
#define FAILED(x) ((HRESULT)(x) < 0)
#define WINED3D_OK 0
#define E_FAIL ((HRESULT)0x80004005)
#define E_OUTOFMEMORY ((HRESULT)0x8007000e)
#define WINED3DERR_INVALIDCALL ((HRESULT)0x8876086c)
#define WINED3DERR_DEVICELOST ((HRESULT)0x88760868)
#define VBOX_WITH_WINE_FIX_ZEROVERTATTR
/* GL calls are counted with and without a current context. */
static unsigned gl_bound, gl_unbound;
static struct wined3d_context *current;
static void gl_call(void) { if (current) ++gl_bound; else ++gl_unbound; }
struct wined3d_gl_info {
    struct { struct { void (*p_glDeleteTextures)(int, const GLuint *); void (*p_glFlush)(void); } gl; } gl_ops;
    struct { void (*glDeleteRenderbuffers)(int, const GLuint *); } fbo_ops;
    struct { void (*p_glDeleteBuffers)(int, const GLuint *); } ext;
    struct { unsigned buffers; } limits;
};
#define GL_EXTCALL(f) (gl_info->ext.p_##f)
static void delete_names(int n, const GLuint *names) { (void)n; assert(names); gl_call(); }
static void flush(void) { gl_call(); }
static struct wined3d_gl_info gl = {{{delete_names, flush}}, {delete_names}, {delete_names}, {4}};
struct wined3d_context { const struct wined3d_gl_info *gl_info; int valid; struct wined3d_swapchain *swapchain; };
'''

DEVICE = COMMON + r'''
struct wined3d_device;
struct wined3d_resource;
struct wined3d_resource_ops { void (*resource_unload)(struct wined3d_resource *); };
struct wined3d_resource {
    const struct wined3d_resource_ops *resource_ops; struct list resource_list_entry;
    struct wined3d_device *device; int unloads, unloads_without_context;
};
struct wined3d_shader { struct list shader_list_entry; };
struct wined3d_texture { struct wined3d_resource resource; };
struct wined3d_surface { struct wined3d_texture *container; };
struct wined3d_rendertarget_view { int refs; };
struct wined3d_format { int id; };
struct wined3d_swapchain {
    struct wined3d_device *device; unsigned num_contexts; struct wined3d_context **context;
    struct wined3d_texture **back_buffers, *front_buffer; const struct wined3d_format *ds_format;
    int refs;
};
struct wined3d_shader_backend_ops {
    void (*shader_destroy)(struct wined3d_shader *);
    void (*shader_free_private)(struct wined3d_device *);
    HRESULT (*shader_alloc_private)(struct wined3d_device *, const void *, const void *);
};
struct blit_shader { HRESULT (*alloc_private)(struct wined3d_device *); void (*free_private)(struct wined3d_device *); };
struct wined3d_adapter { struct wined3d_gl_info gl_info; const void *vertex_pipe, *fragment_pipe; };
struct wined3d_state { int bound; };
struct wine_rb_tree { int entries; };
struct wined3d_device {
    struct wined3d_adapter *adapter; const struct wined3d_shader_backend_ops *shader_backend;
    void *shader_priv, *blit_priv; const struct blit_shader *blitter;
    BYTE d3d_initialized : 1; BYTE inScene : 1; BYTE contexts_lost : 1;
    GLuint depth_blt_texture; struct list resources, shaders;
    UINT context_count; struct wined3d_context **contexts;
    struct wined3d_texture *logo_texture, *cursor_texture; struct wined3d_state state;
    struct wine_rb_tree samplers; struct wined3d_surface *onscreen_depth_stencil;
    struct { struct wined3d_rendertarget_view **render_targets, *depth_stencil; } fb;
    struct wined3d_rendertarget_view *auto_depth_stencil_view, *back_buffer_view;
    UINT swapchain_count; struct wined3d_swapchain **swapchains;
};
void swapchain_destroy_contexts(struct wined3d_swapchain *swapchain);
static unsigned acquires, releases, level, unusable, create_fails, created, destroyed;
static unsigned shader_destroys, dummies, zv_destroys, swapchain_releases, rt_unsets;
static void *wined3d_texture_get_sub_resource(struct wined3d_texture *t, unsigned i) { assert(t && !i); return t; }
static struct wined3d_surface *surface_from_resource(void *p) { static struct wined3d_surface s; (void)p; return &s; }
/* swapchain_get_context(): the primary swapchain's context, or a new one. */
static struct wined3d_context *context_create(struct wined3d_swapchain *s, struct wined3d_surface *t,
        const struct wined3d_format *ds) {
    struct wined3d_device *d = s->device; struct wined3d_context *c;
    (void)t; (void)ds;
    if (create_fails) return NULL;
    c = calloc(1, sizeof(*c)); assert(c);
    c->gl_info = &d->adapter->gl_info; c->valid = 1; c->swapchain = s;
    d->contexts = realloc(d->contexts, (d->context_count + 1) * sizeof(*d->contexts));
    d->contexts[d->context_count++] = c; ++created; ++level; current = c;
    return c;
}
static void context_destroy(struct wined3d_device *d, struct wined3d_context *c) {
    UINT i;
    for (i = 0; i < d->context_count && d->contexts[i] != c; ++i) {}
    assert(i < d->context_count);
    memmove(d->contexts + i, d->contexts + i + 1, (d->context_count - i - 1) * sizeof(*d->contexts));
    if (!--d->context_count) { free(d->contexts); d->contexts = NULL; }
    if (current == c) current = NULL;
    free(c); ++destroyed;
}
/* Mirrors the prepared context_acquire() boundaries (test_context_migration.py
 * runs the real one): no context after a failed reset, the primary context
 * otherwise, created on demand. */
static struct wined3d_context *context_acquire(const struct wined3d_device *d, struct wined3d_surface *t) {
    struct wined3d_swapchain *s = d->swapchains[0]; struct wined3d_context *c;
    (void)t; ++acquires;
    if (d->contexts_lost) return NULL;
    if (!s->num_contexts) {
        if (!(c = context_create(s, NULL, NULL))) return NULL;
        --level;
        s->context = realloc(s->context, sizeof(*s->context)); s->context[0] = c; s->num_contexts = 1;
    }
    c = s->context[0]; c->valid = !unusable; ++level; current = c->valid ? c : NULL;
    return c;
}
static void context_release(struct wined3d_context *c) {
    if (!c) return;
    assert(level); ++releases;
    if (!--level) current = NULL;
}
static unsigned shader_live, blit_live;
static HRESULT alloc_shader(struct wined3d_device *d, const void *v, const void *f) {
    (void)v; (void)f; assert(!shader_live++); d->shader_priv = malloc(8); return WINED3D_OK;
}
/* Neither clears the pointer, as Wine's: a second free is a double free. */
static void free_shader(struct wined3d_device *d) { gl_call(); assert(shader_live--); free(d->shader_priv); }
static void destroy_shader(struct wined3d_shader *s) { (void)s; ++shader_destroys; }
static HRESULT alloc_blit(struct wined3d_device *d) { assert(!blit_live++); d->blit_priv = malloc(8); return WINED3D_OK; }
static void free_blit(struct wined3d_device *d) { assert(blit_live--); free(d->blit_priv); }
static const struct wined3d_shader_backend_ops shader_ops = {destroy_shader, free_shader, alloc_shader};
static const struct blit_shader blitter = {alloc_blit, free_blit};
static void create_dummy_textures(struct wined3d_device *d, struct wined3d_context *c) {
    (void)d; assert(c && current == c); ++dummies;
}
static void destroy_dummy_textures(struct wined3d_device *d, const struct wined3d_gl_info *g) {
    (void)d; assert(g == &d->adapter->gl_info && dummies); --dummies; g->gl_ops.gl.p_glDeleteTextures(1, &d->depth_blt_texture);
}
static void zv_destroy(struct wined3d_device *d) { (void)d; ++zv_destroys; }
/* Resource unload acquires its own context, as surface/buffer/volume do. */
static void unload(struct wined3d_resource *r) {
    struct wined3d_context *c = context_acquire(r->device, NULL);
    ++r->unloads; if (!c) ++r->unloads_without_context;
    context_release(c);
}
static const struct wined3d_resource_ops resource_ops = {unload};
static unsigned wined3d_texture_decref(struct wined3d_texture *t) { (void)t; return 0; }
static void state_unbind_resources(struct wined3d_state *s) { s->bound = 0; }
static void device_free_sampler(void) {}
static void wine_rb_clear(struct wine_rb_tree *t, void (*f)(void), void *c) { (void)f; (void)c; t->entries = 0; }
static ULONG wined3d_rendertarget_view_decref(struct wined3d_rendertarget_view *v) { return --v->refs; }
static void wined3d_device_set_rendertarget_view(struct wined3d_device *d, UINT i, void *v, BOOL b) {
    (void)d; (void)i; (void)v; (void)b; ++rt_unsets;
}
static ULONG wined3d_swapchain_decref(struct wined3d_swapchain *s) {
    ++swapchain_releases; swapchain_destroy_contexts(s); free(s->context); s->context = NULL; return --s->refs;
}
'''

DEVICE_TEST = r'''
/* The three DreamGPU steps of wined3d_device_reset(reset_state = TRUE), in
 * its order; the Python side checks that order in the prepared source. */
static HRESULT reset(struct wined3d_device *d) {
    struct wined3d_swapchain *s = d->swapchains[0];
    HRESULT hr = WINED3D_OK;
    if (!device_reset_context_usable(d)) return WINED3DERR_DEVICELOST;
    if (d->d3d_initialized) delete_opengl_contexts(d, s);
    if (d->d3d_initialized) hr = device_reset_create_context(d, s);
    return hr;
}
static struct wined3d_adapter adapter;
static struct wined3d_texture back, *backs[] = {&back};
static struct wined3d_format ds;
static struct wined3d_resource managed;
static struct wined3d_shader shader;
static struct wined3d_rendertarget_view rt_views[4];
static void init(struct wined3d_device *d, struct wined3d_swapchain *s) {
    memset(d, 0, sizeof(*d)); memset(s, 0, sizeof(*s));
    adapter.gl_info = gl; d->adapter = &adapter; d->shader_backend = &shader_ops; d->blitter = &blitter;
    list_init(&d->resources); list_init(&d->shaders);
    s->device = d; s->back_buffers = backs; s->ds_format = &ds; s->refs = 1;
    d->swapchains = malloc(sizeof(*d->swapchains)); d->swapchains[0] = s; d->swapchain_count = 1;
    d->fb.render_targets = calloc(4, sizeof(*d->fb.render_targets));
    managed.resource_ops = &resource_ops; managed.device = d; managed.unloads = managed.unloads_without_context = 0;
    list_add_tail(&d->resources, &managed.resource_list_entry);
    list_add_tail(&d->shaders, &shader.shader_list_entry);
    assert(create_primary_opengl_context(d, s) == WINED3D_OK);
    assert(!level && s->num_contexts == 1 && d->context_count == 1);
    d->d3d_initialized = 1; d->depth_blt_texture = 9;
    acquires = releases = 0; gl_bound = gl_unbound = 0;
}
int main(void) {
    struct wined3d_device d; struct wined3d_swapchain s;
    struct wined3d_context *old;
    unsigned before;

    /* An ordinary reset replaces the context and its state with GL bound. */
    init(&d, &s);
    old = s.context[0];
    assert(reset(&d) == WINED3D_OK && !d.contexts_lost && s.num_contexts == 1 && s.context[0] != old);
    assert(d.context_count == 1 && managed.unloads == 1 && !managed.unloads_without_context);
    assert(!d.depth_blt_texture && dummies == 1 && shader_destroys == 1 && zv_destroys == 1);
    assert(!gl_unbound && gl_bound && !level && shader_live == 1 && blit_live == 1);

    /* A context that cannot be made current changes nothing: lost device,
     * contents and contexts kept, and the retry succeeds. */
    unusable = 1; before = created;
    d.depth_blt_texture = 9;
    assert(reset(&d) == WINED3DERR_DEVICELOST && !d.contexts_lost && created == before);
    assert(s.num_contexts == 1 && d.context_count == 1 && managed.unloads == 1 && d.depth_blt_texture == 9);
    assert(!level);
    unusable = 0;
    assert(reset(&d) == WINED3D_OK && managed.unloads == 2 && !level);

    /* Warcraft III: the primary context cannot be created again (minimized
     * window, old frontend). The reset destroys the old contexts and state
     * once, reports a lost device and keeps no dangling context array. */
    create_fails = 1; gl_bound = gl_unbound = 0;
    assert(reset(&d) == WINED3DERR_DEVICELOST && d.contexts_lost);
    assert(!s.num_contexts && !s.context && !d.context_count && !dummies && !level && !shader_live && !blit_live);
    /* Only Wine's own cleanup of the failed creation frees the shader state
     * without a context (a no-op GL call in DreamGPU's frontend). */
    assert(managed.unloads == 3 && !managed.unloads_without_context && gl_unbound == 1);
    gl_unbound = 0;
    /* No context is acquired or created while lost. */
    before = created;
    assert(!context_acquire(&d, NULL) && created == before);
    /* Its retry: nothing to tear down, no double free, still lost. */
    before = acquires; gl_bound = 0;
    assert(reset(&d) == WINED3DERR_DEVICELOST && d.contexts_lost && managed.unloads == 3);
    assert(acquires == before && gl_unbound == 1 && !gl_bound && !level && !shader_live && !blit_live);
    gl_unbound = 0;
    /* The game's loop still completes scenes without a context. */
    d.inScene = 1;
    assert(wined3d_device_end_scene(&d) == WINED3D_OK && !d.inScene && !gl_unbound && !gl_bound);
    assert(wined3d_device_end_scene(&d) == WINED3DERR_INVALIDCALL);
    /* Once a context can be created again, the reset completes. */
    create_fails = 0;
    assert(reset(&d) == WINED3D_OK && !d.contexts_lost && s.num_contexts == 1 && d.context_count == 1);
    assert(dummies == 1 && shader_live == 1 && blit_live == 1 && !level);
    assert(context_acquire(&d, NULL) == s.context[0]);
    context_release(s.context[0]);
    d.inScene = 1; before = gl_bound;
    assert(wined3d_device_end_scene(&d) == WINED3D_OK && gl_bound == before + 1 && !level);

    /* Uninit of a lost device: no GL call, nothing freed twice, all views
     * and swapchains released; a later init starts with contexts again. */
    create_fails = 1;
    assert(reset(&d) == WINED3DERR_DEVICELOST && d.contexts_lost);
    gl_bound = gl_unbound = 0; before = zv_destroys;
    d.depth_blt_texture = 0; d.back_buffer_view = &rt_views[0]; rt_views[0].refs = 1;
    d.auto_depth_stencil_view = &rt_views[1]; rt_views[1].refs = 1;
    assert(wined3d_device_uninit_3d(&d) == WINED3D_OK);
    assert(!d.d3d_initialized && !d.contexts_lost && !d.swapchains && swapchain_releases == 1);
    assert(!d.back_buffer_view && !d.auto_depth_stencil_view && rt_unsets == 4 && !d.fb.render_targets);
    assert(!gl_bound && !gl_unbound && zv_destroys == before && managed.unloads_without_context == 1);
    assert(!level);
    create_fails = 0;

    /* Uninit with a context tears GL state down once, with GL bound. */
    init(&d, &s);
    before = zv_destroys;
    assert(wined3d_device_uninit_3d(&d) == WINED3D_OK && !d.d3d_initialized && zv_destroys == before + 1);
    assert(!dummies && !gl_unbound && gl_bound && !level && !d.context_count && !shader_live && !blit_live);

    /* delete_opengl_contexts without any context (none current, none can
     * be created) still unloads, frees the state and destroys the contexts,
     * issuing no GL call through a context it does not hold. */
    init(&d, &s);
    {
        struct wined3d_swapchain other = {0}; struct wined3d_context **list = malloc(sizeof(*list));
        other.device = &d; other.context = list; other.num_contexts = 1; list[0] = s.context[0];
        s.context[0]->swapchain = &other; s.num_contexts = 0; free(s.context); s.context = NULL;
        create_fails = 1; gl_bound = gl_unbound = 0; before = destroyed;
        delete_opengl_contexts(&d, &s);
        assert(managed.unloads_without_context == 1 && !d.depth_blt_texture && !d.context_count);
        assert(destroyed == before + 1 && !dummies && !gl_bound && !level && !s.context && !d.contexts_lost);
        assert(!shader_live && !blit_live);
        /* Only the shader/blitter/dummy teardown reaches GL, with no context
         * current: DreamGPU's frontend ignores those calls. */
        assert(gl_unbound == 2);
        /* Uninit of that device takes Wine's original no-context return. */
        assert(device_reset_create_context(&d, &s) == WINED3DERR_DEVICELOST && d.contexts_lost);
        create_fails = 0; free(other.context);
        free(d.fb.render_targets); free(d.swapchains);
    }
    puts("PASS prepared Wine reset without a context: unusable context leaves the device intact, failed "
         "re-creation reports a lost device once, retries/end-scene/uninit without context or double free, "
         "recovery and context-free teardown");
    return 0;
}
'''

UNLOAD = COMMON + r'''
#define WINED3D_POOL_DEFAULT 0
#define WINED3D_POOL_MANAGED 1
#define WINED3D_LOCATION_DISCARDED 0x1
#define WINED3D_LOCATION_SYSMEM 0x2
#define WINED3D_LOCATION_USER_MEMORY 0x4
#define WINED3D_LOCATION_DIB 0x8
#define WINED3D_LOCATION_BUFFER 0x10
#define WINED3D_LOCATION_TEXTURE_RGB 0x20
#define WINED3D_LOCATION_TEXTURE_SRGB 0x40
#define SFLAG_LOST 0x1
#define WINED3D_BUFFER_DOUBLEBUFFER 0x1
#define WINED3D_BUFFER_CREATEBO 0x2
#define WINED3D_BUFFER_HASDESC 0x4
#define WINED3D_BUFFER_APPLESYNC 0x8
#define WINED3D_VFLAG_CLIENT_STORAGE 0x1
struct wined3d_device { int contexts_lost; struct wined3d_context *context; };
struct wined3d_resource { struct wined3d_device *device; int pool; DWORD map_binding; void *heap_memory; UINT size; int unloaded; };
struct wined3d_renderbuffer_entry { struct list entry; GLuint id; };
struct gl_texture { GLuint name; };
struct wined3d_texture { struct wined3d_resource resource; struct gl_texture texture_rgb, texture_srgb; int dirty; };
struct wined3d_surface {
    struct wined3d_resource resource; DWORD locations, flags; void *user_memory;
    struct { void *bitmap_data; } dib; GLuint pbo, rb_multisample, rb_resolved;
    struct list renderbuffers; void *current_renderbuffer; int loads;
};
struct wined3d_event_query { int x; };
struct wined3d_buffer {
    struct wined3d_resource resource; GLuint buffer_object; DWORD flags; struct wined3d_event_query *query;
    void *conversion_map; UINT stride, conversion_stride; int downloads, dirty_cleared;
};
struct wined3d_volume { struct wined3d_resource resource; DWORD locations, flags; GLuint pbo; int loads; };
static unsigned level;
static struct wined3d_context *context_acquire(const struct wined3d_device *d, void *t) {
    (void)t;
    if (d->contexts_lost) return NULL;
    ++level; current = d->context; return d->context;
}
static void context_release(struct wined3d_context *c) { if (!c) return; assert(level); if (!--level) current = NULL; }
static void resource_unload(struct wined3d_resource *r) { ++r->unloaded; }
static BOOL wined3d_resource_allocate_sysmem(struct wined3d_resource *r) {
    assert(!r->heap_memory); r->heap_memory = calloc(1, r->size); return r->heap_memory != NULL;
}
/* surface.c */
static struct wined3d_surface *surface_from_resource(struct wined3d_resource *r) { return (struct wined3d_surface *)r; }
static void surface_prepare_system_memory(struct wined3d_surface *s) {
    if (!s->resource.heap_memory) s->resource.heap_memory = malloc(s->resource.size);
}
static void surface_prepare_map_memory(struct wined3d_surface *s) { assert(current); surface_prepare_system_memory(s); }
static void surface_validate_location(struct wined3d_surface *s, DWORD l) { s->locations |= l; }
static void surface_invalidate_location(struct wined3d_surface *s, DWORD l) { s->locations &= ~l; }
static HRESULT surface_load_location(struct wined3d_surface *s, struct wined3d_context *c, DWORD l) {
    assert(c && c == current); ++s->loads; s->locations |= l; return WINED3D_OK;
}
static void surface_remove_pbo(struct wined3d_surface *s, const struct wined3d_gl_info *g) {
    assert(g && current); gl_call(); s->pbo = 0; s->locations &= ~WINED3D_LOCATION_BUFFER;
}
/* buffer.c */
static struct wined3d_buffer *buffer_from_resource(struct wined3d_resource *r) { return (struct wined3d_buffer *)r; }
static BYTE *buffer_get_sysmem(struct wined3d_buffer *b, struct wined3d_context *c) {
    assert(c == current && c); ++b->downloads;
    if (!b->resource.heap_memory) wined3d_resource_allocate_sysmem(&b->resource);
    memset(b->resource.heap_memory, 0x5a, b->resource.size); b->flags |= WINED3D_BUFFER_DOUBLEBUFFER;
    return b->resource.heap_memory;
}
static void buffer_clear_dirty_areas(struct wined3d_buffer *b) { ++b->dirty_cleared; }
static void wined3d_event_query_destroy(struct wined3d_event_query *q) { free(q); }
/* volume.c */
static struct wined3d_volume *volume_from_resource(struct wined3d_resource *r) { return (struct wined3d_volume *)r; }
static BOOL volume_prepare_system_memory(struct wined3d_volume *v) {
    return v->resource.heap_memory || wined3d_resource_allocate_sysmem(&v->resource);
}
static void wined3d_volume_validate_location(struct wined3d_volume *v, DWORD l) { v->locations |= l; }
static void wined3d_volume_invalidate_location(struct wined3d_volume *v, DWORD l) { v->locations &= ~l; }
static void wined3d_volume_load_location(struct wined3d_volume *v, struct wined3d_context *c, DWORD l) {
    assert(c == current && c); ++v->loads; v->locations |= l;
}
/* texture.c */
static void gltexture_delete(const struct wined3d_gl_info *g, struct gl_texture *t) { (void)g; gl_call(); t->name = 0; }
static void wined3d_texture_set_dirty(struct wined3d_texture *t) { ++t->dirty; }
'''

UNLOAD_TEST = r'''
static void renderbuffers(struct wined3d_surface *s) {
    struct wined3d_renderbuffer_entry *e = calloc(1, sizeof(*e));
    list_init(&s->renderbuffers); e->id = 3; list_add_tail(&s->renderbuffers, &e->entry);
    s->rb_multisample = 4; s->rb_resolved = 5; s->current_renderbuffer = e;
}
int main(void) {
    struct wined3d_context context = {&gl, 1, NULL};
    struct wined3d_device live = {0, &context}, lost = {1, NULL};
    unsigned char user[16], dib[16];
    struct wined3d_surface s;

    /* With a context: Wine's downloads and GL deletes, unchanged. */
    memset(&s, 0, sizeof(s)); s.resource.device = &live; s.resource.pool = WINED3D_POOL_MANAGED;
    s.resource.size = 16; s.resource.map_binding = WINED3D_LOCATION_SYSMEM; s.locations = WINED3D_LOCATION_TEXTURE_RGB;
    s.pbo = 7; renderbuffers(&s); gl_bound = gl_unbound = 0;
    surface_unload(&s.resource);
    assert(s.loads == 1 && s.locations == WINED3D_LOCATION_SYSMEM && !s.pbo && gl_bound == 4 && !gl_unbound);
    assert(list_empty(&s.renderbuffers) && !s.rb_multisample && !s.rb_resolved && s.resource.unloaded == 1 && !level);
    free(s.resource.heap_memory);

    /* Without a context: no GL call; a valid CPU copy stays as it is. */
    memset(&s, 0, sizeof(s)); s.resource.device = &lost; s.resource.pool = WINED3D_POOL_MANAGED; s.resource.size = 16;
    s.resource.map_binding = WINED3D_LOCATION_SYSMEM; s.resource.heap_memory = malloc(16); memset(s.resource.heap_memory, 0x33, 16);
    s.locations = WINED3D_LOCATION_SYSMEM | WINED3D_LOCATION_TEXTURE_RGB; s.pbo = 7; renderbuffers(&s); gl_bound = gl_unbound = 0;
    surface_unload(&s.resource);
    assert(!s.loads && s.locations == WINED3D_LOCATION_SYSMEM && ((unsigned char *)s.resource.heap_memory)[15] == 0x33);
    assert(!s.pbo && list_empty(&s.renderbuffers) && !s.current_renderbuffer && !s.rb_multisample && !s.rb_resolved);
    assert(!gl_bound && !gl_unbound && s.resource.unloaded == 1);
    free(s.resource.heap_memory);
    /* Content only in GL is lost: system memory defined as zero; a PBO map
     * binding falls back to system memory. */
    memset(&s, 0, sizeof(s)); s.resource.device = &lost; s.resource.pool = WINED3D_POOL_MANAGED; s.resource.size = 16;
    s.resource.map_binding = WINED3D_LOCATION_BUFFER; s.locations = WINED3D_LOCATION_TEXTURE_RGB | WINED3D_LOCATION_BUFFER;
    s.pbo = 7; list_init(&s.renderbuffers);
    surface_unload(&s.resource);
    assert(s.resource.map_binding == WINED3D_LOCATION_SYSMEM && s.locations == WINED3D_LOCATION_SYSMEM && !s.pbo);
    for (unsigned i = 0; i < 16; ++i) assert(!((unsigned char *)s.resource.heap_memory)[i]);
    assert(!gl_bound && !gl_unbound);
    free(s.resource.heap_memory);
    /* Application and DIB memory keep their bytes and become the copy. */
    memset(&s, 0, sizeof(s)); s.resource.device = &lost; s.resource.pool = WINED3D_POOL_MANAGED; s.resource.size = 16;
    memset(user, 0x44, sizeof(user)); s.user_memory = user; s.resource.map_binding = WINED3D_LOCATION_USER_MEMORY;
    s.locations = WINED3D_LOCATION_TEXTURE_RGB; list_init(&s.renderbuffers);
    surface_unload(&s.resource);
    assert(s.locations == WINED3D_LOCATION_USER_MEMORY && user[0] == 0x44 && !s.resource.heap_memory);
    memset(&s, 0, sizeof(s)); s.resource.device = &lost; s.resource.pool = WINED3D_POOL_MANAGED; s.resource.size = 16;
    s.dib.bitmap_data = dib; s.resource.map_binding = WINED3D_LOCATION_DIB; s.locations = WINED3D_LOCATION_TEXTURE_RGB;
    list_init(&s.renderbuffers);
    surface_unload(&s.resource);
    assert(s.locations == WINED3D_LOCATION_DIB && !gl_bound && !gl_unbound);
    /* Missing storage leaves no location (later loads report the loss). */
    memset(&s, 0, sizeof(s)); s.resource.device = &lost; s.resource.pool = WINED3D_POOL_MANAGED; s.resource.size = 16;
    s.resource.map_binding = WINED3D_LOCATION_DIB; s.locations = WINED3D_LOCATION_TEXTURE_RGB; list_init(&s.renderbuffers);
    surface_unload(&s.resource);
    assert(!s.locations && s.resource.unloaded == 1);
    /* Implicit default-pool targets: zero system memory and lost, as Wine. */
    memset(&s, 0, sizeof(s)); s.resource.device = &lost; s.resource.pool = WINED3D_POOL_DEFAULT; s.resource.size = 16;
    s.resource.map_binding = WINED3D_LOCATION_SYSMEM; s.locations = WINED3D_LOCATION_TEXTURE_RGB; s.rb_resolved = 2;
    list_init(&s.renderbuffers);
    surface_unload(&s.resource);
    assert(s.locations == WINED3D_LOCATION_SYSMEM && (s.flags & SFLAG_LOST) && !s.rb_resolved && !gl_bound && !gl_unbound);
    free(s.resource.heap_memory);

    /* Buffers: with a context, download and delete. */
    {
        struct wined3d_buffer b;
        memset(&b, 0, sizeof(b)); b.resource.device = &live; b.resource.size = 8; b.buffer_object = 3;
        b.query = malloc(sizeof(*b.query)); b.conversion_map = malloc(4); b.flags = WINED3D_BUFFER_HASDESC;
        gl_bound = gl_unbound = 0;
        buffer_unload(&b.resource);
        assert(b.downloads == 1 && !b.buffer_object && !b.query && gl_bound == 1 && !gl_unbound && !level);
        assert((b.flags & WINED3D_BUFFER_CREATEBO) && !(b.flags & (WINED3D_BUFFER_DOUBLEBUFFER | WINED3D_BUFFER_HASDESC)));
        assert(((unsigned char *)b.resource.heap_memory)[7] == 0x5a && !b.conversion_map && b.resource.unloaded == 1);
        free(b.resource.heap_memory);
        /* Without: no GL; content defined as zero in new system memory. */
        memset(&b, 0, sizeof(b)); b.resource.device = &lost; b.resource.size = 8; b.buffer_object = 3;
        b.query = malloc(sizeof(*b.query)); b.conversion_map = malloc(4); gl_bound = gl_unbound = 0;
        buffer_unload(&b.resource);
        assert(!b.downloads && !b.buffer_object && !b.query && !gl_bound && !gl_unbound && b.dirty_cleared == 1);
        assert(b.resource.heap_memory && !((unsigned char *)b.resource.heap_memory)[7] && (b.flags & WINED3D_BUFFER_CREATEBO));
        free(b.resource.heap_memory);
        /* A double-buffered copy stays authoritative. */
        memset(&b, 0, sizeof(b)); b.resource.device = &lost; b.resource.size = 8; b.buffer_object = 3;
        b.flags = WINED3D_BUFFER_DOUBLEBUFFER; b.resource.heap_memory = malloc(8); memset(b.resource.heap_memory, 0x11, 8);
        buffer_unload(&b.resource);
        assert(((unsigned char *)b.resource.heap_memory)[0] == 0x11 && (b.flags & WINED3D_BUFFER_DOUBLEBUFFER) && !gl_unbound);
        free(b.resource.heap_memory);
    }

    /* Volumes. */
    {
        struct wined3d_volume v;
        memset(&v, 0, sizeof(v)); v.resource.device = &live; v.resource.size = 8; v.locations = WINED3D_LOCATION_TEXTURE_RGB;
        v.pbo = 5; gl_bound = gl_unbound = 0;
        volume_unload(&v.resource);
        assert(v.loads == 1 && v.locations == WINED3D_LOCATION_SYSMEM && !v.pbo && gl_bound == 1 && !level);
        free(v.resource.heap_memory);
        memset(&v, 0, sizeof(v)); v.resource.device = &lost; v.resource.size = 8; v.locations = WINED3D_LOCATION_TEXTURE_RGB;
        v.pbo = 5; gl_bound = gl_unbound = 0;
        volume_unload(&v.resource);
        assert(!v.loads && v.locations == WINED3D_LOCATION_SYSMEM && !v.pbo && !gl_bound && !gl_unbound);
        assert(!((unsigned char *)v.resource.heap_memory)[7] && v.resource.unloaded == 1);
        /* A valid CPU copy stays. */
        memset(v.resource.heap_memory, 0x22, 8); v.locations = WINED3D_LOCATION_SYSMEM | WINED3D_LOCATION_TEXTURE_RGB;
        volume_unload(&v.resource);
        assert(((unsigned char *)v.resource.heap_memory)[7] == 0x22 && v.locations == WINED3D_LOCATION_SYSMEM);
        free(v.resource.heap_memory);
    }

    /* Textures forget their names without a context. */
    {
        struct wined3d_texture t;
        memset(&t, 0, sizeof(t)); t.resource.device = &live; t.texture_rgb.name = 1; t.texture_srgb.name = 2;
        gl_bound = gl_unbound = 0;
        wined3d_texture_unload_gl_texture(&t);
        assert(!t.texture_rgb.name && !t.texture_srgb.name && gl_bound == 2 && !level && t.dirty == 1);
        memset(&t, 0, sizeof(t)); t.resource.device = &lost; t.texture_rgb.name = 1; t.texture_srgb.name = 2;
        gl_bound = gl_unbound = 0;
        wined3d_texture_unload_gl_texture(&t);
        assert(!t.texture_rgb.name && !t.texture_srgb.name && !gl_bound && !gl_unbound && t.dirty == 1 && t.resource.unloaded == 1);
    }
    puts("PASS prepared Wine unloads without a context: surfaces (CPU copies kept, lost content defined, PBO "
         "and renderbuffer names forgotten), buffers, volumes and textures make no GL call");
    return 0;
}
'''


def function(source, prefix):
    start = source.index(prefix)
    return source[start:source.index('\n}', start) + 2]


def build(work, name, text):
    path = work / (name + '.c')
    path.write_text(text)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    '-Wno-unused-parameter', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-fno-omit-frame-pointer', '-g', '-I', str(work / 'include'), str(path), '-o', str(work / name)],
                   check=True)
    subprocess.run([str(work / name)], check=True)


with tempfile.TemporaryDirectory(prefix='dreamgpu-reset-context-') as directory:
    work = Path(directory)
    for name in json.loads(MANIFEST.read_text())['files']:
        destination = work / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'vendor/wine9x' / name, destination)
    patches.apply(work, MANIFEST)
    (work / 'include/wine').mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ROOT / 'vendor/wine9x/include/wine/list.h', work / 'include/wine/list.h')
    device = (work / 'wined3d/device.c').read_text()
    swapchain = (work / 'wined3d/swapchain.c').read_text()
    surface = (work / 'wined3d/surface.c').read_text()
    buffer = (work / 'wined3d/buffer.c').read_text()
    volume = (work / 'wined3d/volume.c').read_text()
    texture = (work / 'wined3d/texture.c').read_text()
    private = (work / 'wined3d/wined3d_private.h').read_text()
    context = (work / 'wined3d/context.c').read_text()

    # The flag replaces one padding bit: the device layout is unchanged.
    assert '    BYTE contexts_lost : 1;\n    BYTE padding : 1;\n' in private
    acquire = function(context, 'struct wined3d_context *context_acquire(')
    assert acquire.index('if (device->contexts_lost)') < acquire.index('swapchain_get_context(')

    # wined3d_device_reset: the usability check precedes every change, the
    # teardown precedes the re-creation, which goes through the lost-state step.
    reset = function(device, 'HRESULT CDECL wined3d_device_reset(')
    check = reset.index('if (reset_state && !device_reset_context_usable(device))')
    assert check < reset.index('DisplayModeChanged = swapchain->reapply_mode;')
    assert check < reset.index('if (device->logo_texture)') < reset.index('callback(resource)')
    assert reset.index('delete_opengl_contexts(device, swapchain);') < reset.index(
        'hr = device_reset_create_context(device, swapchain);')
    assert 'create_primary_opengl_context(' not in reset
    # Entry points a lost device's application keeps calling skip without a
    # context, as they already did for an invalid one.
    for text, prefix in [(device, 'void device_clear_render_targets('),
                         ((work / 'wined3d/drawprim.c').read_text(), 'void draw_primitive(')]:
        assert 'if (!context || !context->valid)' in function(text, prefix), prefix
    assert 'if (!context || !context->valid)\n    {\n        context_release(context);\n        WARN("Invalid context, skipping present.' in swapchain

    code = [function(swapchain, 'void swapchain_destroy_contexts(')]
    for prefix in ('static void delete_opengl_contexts(', 'static HRESULT create_primary_opengl_context(',
                   'static BOOL device_reset_context_usable(', 'static HRESULT device_reset_create_context(',
                   'HRESULT CDECL wined3d_device_end_scene(', 'HRESULT CDECL wined3d_device_uninit_3d('):
        code.append(function(device, prefix))
    build(work, 'device', DEVICE + '\n'.join(code) + DEVICE_TEST)

    code = [function(surface, 'static void dg_surface_keep_cpu_copy('), function(surface, 'static void surface_unload('),
            function(buffer, 'static void delete_gl_buffer('), function(buffer, 'static void buffer_unload('),
            function(volume, 'static void wined3d_volume_free_pbo('), function(volume, 'static void volume_unload('),
            function(texture, 'static void wined3d_texture_unload_gl_texture(')]
    build(work, 'unload', UNLOAD + '\n'.join(code) + UNLOAD_TEST)
