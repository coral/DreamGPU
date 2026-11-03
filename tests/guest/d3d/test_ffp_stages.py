#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute the prepared fixed-function colour-op handler under ASan/UBSan.

GL applies a texture environment only while the unit has an enabled target, so
an active stage that reads no texture (TFACTOR, DIFFUSE, CURRENT) must still
enable one or its combiner is silently skipped.
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
#include <stdio.h>
typedef unsigned DWORD;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define TRACE(...) ((void)0)
#define WARN(...) ((void)0)
#define FIXME(...) ((void)0)
#define checkGLcall(x) ((void)0)
#define WINED3D_UNMAPPED_STAGE (~0u)
#define WINED3D_HIGHEST_TEXTURE_STATE 31
#define STATE_TEXTURESTAGE(stage, num) (100 + (stage) * (WINED3D_HIGHEST_TEXTURE_STATE + 1) + (num))
#define STATE_SAMPLER(num) (1000 + (num))
#define WINED3D_TSS_COLOR_OP 0
#define WINED3D_TSS_COLOR_ARG1 1
#define WINED3D_TSS_COLOR_ARG2 2
#define WINED3D_TSS_COLOR_ARG0 3
#define GL_TEXTURE_2D 1
#define GL_TEXTURE_3D 2
#define GL_TEXTURE_CUBE_MAP_ARB 3
#define GL_TEXTURE_RECTANGLE_ARB 4
#define ARB_TEXTURE_CUBE_MAP 0
#define ARB_TEXTURE_RECTANGLE 1
struct wined3d_texture { int unused; };
struct wined3d_gl_info {
    BOOL supported[2];
    struct { unsigned textures; } limits;
    struct { struct { void (*p_glDisable)(unsigned); } gl; } gl_ops;
};
struct wined3d_state {
    struct wined3d_texture *textures[8];
    DWORD texture_states[8][32];
    BOOL ps;
};
struct wined3d_context {
    const struct wined3d_gl_info *gl_info;
    unsigned fixed_function_usage_map, lowest_disabled_stage;
    DWORD tex_unit_map[8];
    BOOL dirty_sampler[8];
};
static unsigned activated, activated_null, ops, disabled, active_unit;
static const struct wined3d_texture *activated_with;
static BOOL use_ps(const struct wined3d_state *s) { return s->ps; }
static BOOL isStateDirty(const struct wined3d_context *c, DWORD id)
{ return id >= STATE_SAMPLER(0) && c->dirty_sampler[id - STATE_SAMPLER(0)]; }
static void context_active_texture(struct wined3d_context *c, const struct wined3d_gl_info *g, unsigned unit)
{ (void)c; (void)g; active_unit = unit; }
static void texture_activate_dimensions(const struct wined3d_texture *t, const struct wined3d_gl_info *g)
{ (void)g; ++activated; activated_with = t; if (!t) ++activated_null; }
static void set_tex_op(const struct wined3d_gl_info *g, const struct wined3d_state *s, BOOL alpha, int stage,
        DWORD op, DWORD a1, DWORD a2, DWORD a0)
{ (void)g; (void)s; (void)alpha; (void)stage; (void)op; (void)a1; (void)a2; (void)a0; ++ops; }
static void gl_disable(unsigned cap) { (void)cap; ++disabled; }
'''
TEST = r'''
static void reset(void) { activated = activated_null = ops = disabled = 0; activated_with = 0; }
int main(void)
{
    struct wined3d_gl_info gl = {{1, 1}, {8}, {{gl_disable}}};
    struct wined3d_texture base;
    struct wined3d_state state = {{&base}, {{0}}, FALSE};
    struct wined3d_context c = {&gl, 1u << 0, 3, {0, 1, 2, 3, 4, 5, 6, 7}, {0}};
    /* Stage 0 reads its texture: the bound texture's dimension is enabled. */
    reset(); tex_colorop(&c, &state, STATE_TEXTURESTAGE(0, WINED3D_TSS_COLOR_OP));
    assert(activated == 1 && activated_with == &base && ops == 1 && active_unit == 0);
    /* Stage 2 is active but reads only TFACTOR/CURRENT: its unit still needs
     * an enabled target or GL skips the combiner. */
    reset(); tex_colorop(&c, &state, STATE_TEXTURESTAGE(2, WINED3D_TSS_COLOR_OP));
    assert(activated == 1 && activated_null == 1 && ops == 1 && active_unit == 2);
    /* An application texture bound on a non-reading stage changes nothing. */
    state.textures[1] = &base;
    reset(); tex_colorop(&c, &state, STATE_TEXTURESTAGE(1, WINED3D_TSS_COLOR_OP));
    assert(activated_null == 1 && ops == 1);
    /* A dirty sampler activates the texture-reading stage itself. */
    c.dirty_sampler[0] = TRUE;
    reset(); tex_colorop(&c, &state, STATE_TEXTURESTAGE(0, WINED3D_TSS_COLOR_OP));
    assert(activated == 0 && ops == 1);
    c.dirty_sampler[0] = FALSE;
    /* Disabled stages are disabled, never activated. */
    reset(); tex_colorop(&c, &state, STATE_TEXTURESTAGE(3, WINED3D_TSS_COLOR_OP));
    assert(activated == 0 && ops == 0 && disabled >= 2);
    /* Unmapped or beyond the unit limit: no stray target on another unit. */
    c.tex_unit_map[2] = WINED3D_UNMAPPED_STAGE;
    reset(); tex_colorop(&c, &state, STATE_TEXTURESTAGE(2, WINED3D_TSS_COLOR_OP));
    assert(activated == 0 && ops == 1);
    c.tex_unit_map[2] = 2; gl.limits.textures = 2;
    reset(); tex_colorop(&c, &state, STATE_TEXTURESTAGE(2, WINED3D_TSS_COLOR_OP));
    assert(activated == 0);
    /* Pixel shaders own the fragment stage entirely. */
    gl.limits.textures = 8; state.ps = TRUE;
    reset(); tex_colorop(&c, &state, STATE_TEXTURESTAGE(2, WINED3D_TSS_COLOR_OP));
    assert(activated == 0 && ops == 0);
    puts("PASS prepared fixed-function stages: texture-less active stages enable a target, "
         "texture stages, dirty samplers, disabled/unmapped/limited stages and pixel shaders unchanged");
    return 0;
}
'''


def function(source, signature):
    start = source.index(signature)
    end = source.index('\n}\n', start) + 3
    return source[start:end] + '\n'


with tempfile.TemporaryDirectory(prefix='dreamgpu-ffp-stages-') as temporary:
    work = Path(temporary)
    for name in json.loads(MANIFEST.read_text())['files']:
        p = work / name
        p.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'vendor/wine9x' / name, p)
    patches.apply(work, MANIFEST)
    state = (work / 'wined3d/state.c').read_text()
    body = function(state, 'static void tex_colorop(')
    body = body.replace('gl_info->gl_ops.gl.p_glDisable', 'gl_info->gl_ops.gl.p_glDisable')
    (work / 'test.c').write_text(PREAMBLE + body + TEST)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    str(work / 'test.c'), '-o', str(work / 'test')], check=True)
    subprocess.run([str(work / 'test')], check=True)
