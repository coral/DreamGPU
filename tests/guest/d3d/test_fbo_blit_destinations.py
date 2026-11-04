#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Patch 0045 on actual pinned Wine source: blit destinations with framebuffer objects.

Without framebuffer objects a GPU blit drew into the window drawable and then
copied it into the destination. With them the destination is drawn in its own
GL image:

- A surface that is its own source is one GL image, and GL leaves a blit, copy
  or textured draw between overlapping regions of it undefined (DirectDraw
  scrolling within a surface). wined3d_surface_blt() sends such a blit to the
  CPU blitter, which copies overlapping rectangles in a safe order, before any
  GPU path; non-overlapping blits within one surface keep the FBO blit.
- The arbfp blitter drew into any destination; one the FBO cannot attach then
  dropped the blit while the destination was marked current. It now declines
  such a destination unless it is a render target, as the FBO blitter and the
  fixed-function color fill already do.
The prepared helper and arbfp_blit_supported() run here under ASan/UBSan, and
the guard's place in wined3d_surface_blt() is checked. It fails without the patch.
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
#include <stdio.h>
typedef int BOOL;
typedef unsigned int DWORD;
typedef struct { int left, top, right, bottom; } RECT;
#define TRUE 1
#define FALSE 0
#define TRACE(...) ((void)0)
#define FIXME(...) ((void)0)
#define TRACE_ON(channel) 0
#define VBOX_WITH_WINE_FIX_BLIT_ALPHATEST
#define ORM_BACKBUFFER 0
#define ORM_FBO 1
#define ARB_FRAGMENT_PROGRAM 0
#define WINED3D_GL_RES_TYPE_TEX_2D 1
static struct { int offscreen_rendering_mode; } wined3d_settings;
enum wined3d_blit_op { WINED3D_BLIT_OP_COLOR_BLIT, WINED3D_BLIT_OP_COLOR_BLIT_ALPHATEST, WINED3D_BLIT_OP_COLOR_BLIT_CKEY,
        WINED3D_BLIT_OP_COLOR_FILL, WINED3D_BLIT_OP_DEPTH_FILL, WINED3D_BLIT_OP_DEPTH_BLIT };
enum wined3d_pool { WINED3D_POOL_DEFAULT, WINED3D_POOL_MANAGED, WINED3D_POOL_SYSTEM_MEM, WINED3D_POOL_SCRATCH };
enum complex_fixup { COMPLEX_FIXUP_NONE, COMPLEX_FIXUP_YUY2, COMPLEX_FIXUP_UYVY, COMPLEX_FIXUP_YV12,
        COMPLEX_FIXUP_P8, COMPLEX_FIXUP_NV12 };
/* kind: 0 identity, 1 swizzle (not identity, not complex), otherwise complex fixup kind - 1 */
struct color_fixup_desc { int kind; };
struct wined3d_format { struct color_fixup_desc color_fixup; DWORD flags[7]; };
struct wined3d_gl_info { BOOL supported[1]; };
struct wined3d_d3d_info { BOOL shader_color_key; };
struct wined3d_surface { int unused; };
static BOOL is_identity_fixup(struct color_fixup_desc f) { return !f.kind; }
static BOOL is_complex_fixup(struct color_fixup_desc f) { return f.kind > 1; }
static enum complex_fixup get_complex_fixup(struct color_fixup_desc f) { return f.kind > 1 ? (enum complex_fixup)(f.kind - 1) : COMPLEX_FIXUP_NONE; }
static void dump_color_fixup_desc(struct color_fixup_desc f) { (void)f; }
'''

TEST = r'''
static BOOL supported(const struct wined3d_format *src, const struct wined3d_format *dst, DWORD dst_usage,
        enum wined3d_blit_op op)
{
    static const struct wined3d_gl_info gl = {{TRUE}};
    static const struct wined3d_d3d_info d3d = {TRUE};
    RECT r = {0, 0, 4, 4};
    return arbfp_blit_supported(&gl, &d3d, op, &r, 0, WINED3D_POOL_DEFAULT, src,
            &r, dst_usage, WINED3D_POOL_DEFAULT, dst);
}

int main(void)
{
    struct wined3d_surface a = {0}, b = {0};
    struct wined3d_format rgb = {{0}, {0}}, alpha = {{0}, {0}}, yuy2 = {{2}, {0}};
    RECT r = {0, 0, 8, 8}, right = {8, 0, 16, 8}, shifted = {0, 4, 8, 12}, corner = {7, 7, 9, 9};

    rgb.flags[WINED3D_GL_RES_TYPE_TEX_2D] = WINED3DFMT_FLAG_FBO_ATTACHABLE;

    /* Overlap within one surface: CPU, only with framebuffer objects. */
    wined3d_settings.offscreen_rendering_mode = ORM_FBO;
    assert(dg_surface_blt_self_overlap(&a, &r, &a, &shifted));
    assert(dg_surface_blt_self_overlap(&a, &shifted, &a, &r));
    assert(dg_surface_blt_self_overlap(&a, &r, &a, &r));
    assert(dg_surface_blt_self_overlap(&a, &r, &a, &corner));
    assert(!dg_surface_blt_self_overlap(&a, &r, &a, &right));
    assert(!dg_surface_blt_self_overlap(&a, &right, &a, &r));
    assert(!dg_surface_blt_self_overlap(&a, &r, &b, &shifted));
    assert(!dg_surface_blt_self_overlap(NULL, &r, &a, &r));

    /* The arbfp blitter draws only into attachable destinations or targets. */
    assert(supported(&rgb, &rgb, 0, WINED3D_BLIT_OP_COLOR_BLIT));
    assert(supported(&rgb, &rgb, 0, WINED3D_BLIT_OP_COLOR_BLIT_CKEY));
    assert(supported(&yuy2, &rgb, 0, WINED3D_BLIT_OP_COLOR_BLIT));
    assert(!supported(&rgb, &alpha, 0, WINED3D_BLIT_OP_COLOR_BLIT));
    assert(!supported(&rgb, &alpha, 0, WINED3D_BLIT_OP_COLOR_BLIT_ALPHATEST));
    assert(supported(&rgb, &alpha, WINED3DUSAGE_RENDERTARGET, WINED3D_BLIT_OP_COLOR_BLIT));
    assert(!supported(&rgb, &rgb, 0, WINED3D_BLIT_OP_COLOR_FILL));

    /* Without framebuffer objects both stay as before. */
    wined3d_settings.offscreen_rendering_mode = ORM_BACKBUFFER;
    assert(!dg_surface_blt_self_overlap(&a, &r, &a, &shifted));
    assert(supported(&rgb, &alpha, 0, WINED3D_BLIT_OP_COLOR_BLIT));
    puts("PASS prepared FBO blit destinations: overlapping blits within one surface go to the CPU, "
         "arbfp draws only into attachable destinations or targets, non-FBO behaviour unchanged");
    return 0;
}
'''


def function(source, prefix):
    start = source.index(prefix)
    return source[start:source.index('\n}', start) + 2] + '\n'


with tempfile.TemporaryDirectory(prefix='dreamgpu-fbo-blit-') as temporary:
    work = Path(temporary)
    for name in json.loads(MANIFEST.read_text())['files']:
        p = work / name
        p.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'vendor/wine9x' / name, p)
    patches.apply(work, MANIFEST)
    surface = (work / 'wined3d/surface.c').read_text()
    arb = (work / 'wined3d/arb_program_shader.c').read_text()
    blt = function(surface, 'HRESULT CDECL wined3d_surface_blt(')
    guard = blt.index('if (dg_surface_blt_self_overlap(src_surface, &src_rect, dst_surface, &dst_rect))')
    # After the rectangles are validated and before any accelerated or special path.
    assert blt.index('WARN("D3D not initialized, using fallback.\\n");') < guard
    assert blt.index('Application gave us bad source rectangle for Blt.') < guard
    assert guard < blt.index('goto cpu;', guard) < blt.index('if (flags & ~simple_blit)')
    for later in ('wined3d_surface_depth_fill(', 'wined3d_surface_depth_blt(', 'surface_color_fill(',
                  'surface_upload_from_surface(', 'fbo_blit_supported(', 'wined3d_select_blitter(',
                  'surface_blt_special('):
        assert guard < blt.index(later), later
    code = function(surface, 'static BOOL dg_surface_blt_self_overlap(') + \
        function(arb, 'static BOOL arbfp_blit_supported(')
    headers = (work / 'wined3d/wined3d_private.h').read_text() + (ROOT / 'vendor/wine9x/include/wine/wined3d.h').read_text()
    values = dict(re.findall(r'^#define\s+(WINED3D\w+)\s+(0x[0-9a-fA-F]+|\d+)\b', headers, re.M))
    defines = '\n'.join('#define %s %s' % (n, values[n]) for n in
                        ('WINED3DUSAGE_RENDERTARGET', 'WINED3DFMT_FLAG_FBO_ATTACHABLE'))
    (work / 'test.c').write_text('#include <stddef.h>\n' + PREAMBLE + defines + '\n' + code + TEST)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                    '-Wno-unused-function', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-fno-omit-frame-pointer', str(work / 'test.c'), '-o', str(work / 'test')], check=True)
    subprocess.run([str(work / 'test')], check=True)
