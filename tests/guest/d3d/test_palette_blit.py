#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the prepared blitter's palette-key decision after texture loading."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("patches", ROOT / "tests/guest/support/patches.py")
patches = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patches)
MANIFEST = ROOT / "support/guest/d3d/patches/base/manifest.json"

PREAMBLE = r'''
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
typedef int BOOL; typedef int HRESULT;
#define FAILED(x) ((x)<0)
#define WINED3DERR_INVALIDCALL -1
#define WINED3D_OK 0
#define WINED3D_LOCATION_TEXTURE_RGB 1
#define FALSE 0
#define TRUE 1
#define WINED3DFMT_P8_UINT 1
#define WINED3D_TEXTURE_CONVERTED 4
#define GL_ALPHA_TEST 10
#define GL_NOTEQUAL 11
#define checkGLcall(...) ((void)0)
struct rect { int left, top, right, bottom; };
typedef struct rect RECT;
enum wined3d_texture_filter_type { POINT };
struct wined3d_format { unsigned id; };
struct wined3d_resource { const struct wined3d_format *format; unsigned draw_binding; };
struct wined3d_surface;
struct wined3d_texture;
struct wined3d_swapchain { struct wined3d_texture *front_buffer; };
struct wined3d_texture {
    struct wined3d_resource resource;
    unsigned flags;
    struct { struct { unsigned color_space_low_value; } src_blt_color_key; } async;
    struct wined3d_swapchain *swapchain;
};
struct wined3d_surface { struct wined3d_resource resource; struct wined3d_texture *container; };
struct wined3d_gl_info {
    struct { struct {
        void (*p_glEnable)(unsigned);
        void (*p_glDisable)(unsigned);
        void (*p_glAlphaFunc)(unsigned, float);
        void (*p_glFlush)(void);
    } gl; } gl_ops;
};
struct wined3d_context {
    struct wined3d_surface *current_rt;
    const struct wined3d_gl_info *gl_info;
    void *win_handle;
    BOOL valid;
};
struct blitter {
    HRESULT (*set_shader)(void *, struct wined3d_context *, struct wined3d_surface *, void *);
    void (*unset_shader)(const struct wined3d_gl_info *);
};
struct wined3d_device { const struct blitter *blitter; void *blit_priv; };
static struct { int strict_draw_ordering; } wined3d_settings;
static struct wined3d_context *current;
static struct wined3d_context *context_get_current(void){return current;}
static BOOL surface_is_full_rect(struct wined3d_surface*s,const RECT*r){assert(s&&r);return 1;}
static HRESULT surface_load_location(struct wined3d_surface*s,struct wined3d_context*c,unsigned loc){assert(s&&c);(void)loc;return 0;}
static unsigned alpha_calls, enabled, disabled, draws, loads, converted_on_load;
static float alpha_reference;
static void enable(unsigned cap) { assert(cap == GL_ALPHA_TEST); ++enabled; }
static void disable(unsigned cap) { assert(cap == GL_ALPHA_TEST); ++disabled; }
static void alpha(unsigned op, float reference)
{
    assert(op == GL_NOTEQUAL && loads == 1);
    ++alpha_calls; alpha_reference = reference;
}
static void flush(void) { assert(0); }
static HRESULT shader(void *p, struct wined3d_context *c, struct wined3d_surface *s, void *d)
{ (void)p; assert(c && s && !d); return 0; }
static void unshader(const struct wined3d_gl_info *g) { assert(g); }
static struct wined3d_context *context_acquire(const struct wined3d_device *d, struct wined3d_surface *s)
{ (void)d; (void)s; assert(0); return NULL; }
static void context_restore(struct wined3d_context *c, struct wined3d_surface *s)
{ (void)c; (void)s; assert(0); }
static void wined3d_texture_load(struct wined3d_texture *t, struct wined3d_context *c, BOOL srgb)
{ assert(t && c && !srgb); ++loads; if (converted_on_load) t->flags |= WINED3D_TEXTURE_CONVERTED; }
static void context_apply_blit_state(struct wined3d_context *c, const struct wined3d_device *d)
{ assert(c && d); }
static BOOL wined3d_resource_is_offscreen(const struct wined3d_resource *r) { assert(r); return TRUE; }
static void surface_translate_drawable_coords(struct wined3d_surface *s, void *w, RECT *r)
{ (void)s; (void)w; (void)r; assert(0); }
static void draw_textured_quad(struct wined3d_surface *s, struct wined3d_context *c,
        RECT *src, RECT *dst, enum wined3d_texture_filter_type f)
{ assert(s && c && src && dst && f == POINT); ++draws; }
'''
TEST = r'''
int main(void)
{
    struct wined3d_format format = {WINED3DFMT_P8_UINT};
    struct wined3d_texture texture = {0}, target = {0};
    struct wined3d_surface source = {{&format,0}, &texture}, destination = {{&format,0}, &target};
    const struct wined3d_gl_info gl = {{{enable, disable, alpha, flush}}};
    struct wined3d_context context = {&destination, &gl, NULL, TRUE};
    current = &context;
    const struct blitter blit = {shader, unshader};
    const struct wined3d_device device = {&blit, NULL};
    const RECT rect = {0, 0, 8, 8};
    const unsigned keys[] = {0, 37, 255};
    for (unsigned converted = 0; converted < 2; ++converted)
    {
        for (unsigned i = 0; i < 3; ++i)
        {
            texture.flags = 0;
            texture.async.src_blt_color_key.color_space_low_value = keys[i];
            converted_on_load = converted;
            alpha_calls = enabled = disabled = draws = loads = 0;
            surface_blt_to_drawable(&device, &context, POINT, TRUE,
                    &source, &rect, &destination, &rect);
            assert(alpha_calls == 1 && enabled == 1 && disabled == 1 && draws == 1 && loads == 1);
            assert(alpha_reference == (converted ? 0.0f : (float)keys[i] / 255.0f));
        }
    }
    // Non-paletted sources always compare against transparent alpha, even when
    // the arbitrary saved key is 255. An unkeyed blit never sets an alpha ref.
    format.id = 2; converted_on_load = 0;
    alpha_calls = enabled = disabled = draws = loads = 0;
    surface_blt_to_drawable(&device, &context, POINT, TRUE, &source, &rect, &destination, &rect);
    assert(alpha_calls == 1 && alpha_reference == 0.0f);
    alpha_calls = enabled = disabled = draws = loads = 0;
    surface_blt_to_drawable(&device, &context, POINT, FALSE, &source, &rect, &destination, &rect);
    assert(!alpha_calls && !enabled && disabled == 1 && draws == 1 && loads == 1);
    puts("PASS prepared drawable blitter: converted/unconverted P8 keys 0/37/255, post-load conversion and unkeyed behavior");
}
'''

with tempfile.TemporaryDirectory(prefix="dreamgpu-palette-blit-") as directory:
    work = Path(directory)
    for name in json.loads(MANIFEST.read_text())["files"]:
        destination = work / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / "vendor/wine9x" / name, destination)
    patches.apply(work, MANIFEST)
    source = (work / "wined3d/surface.c").read_text()
    start = source.index("static HRESULT surface_blt_to_drawable(")
    function = source[start:source.index("\n}", start) + 2]
    test = work / "test.c"
    test.write_text(PREAMBLE + function + TEST)
    subprocess.run([os.environ.get("CC", "cc"), "-std=c99", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer", str(test),
                    "-o", str(work / "test")], check=True)
    subprocess.run([str(work / "test")], check=True)
