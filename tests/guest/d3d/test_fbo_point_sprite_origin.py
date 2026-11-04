#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Patch 0046 on actual pinned Wine source: point sprite origin below GL 2.0.

Wine flips offscreen rendering vertically and compensates point sprites with
GL_POINT_SPRITE_COORD_ORIGIN, which it set only on GL 2.0 (psorigin). With
framebuffer objects every D3D target renders offscreen, so on DreamGPU's GL 1.1
every point sprite texture came out upside down. DreamGPU's scalar
glPointParameterfARB takes the origin. The prepared
wined3d_check_point_sprite_origin() and psorigin_w() run here under
ASan/UBSan against a mock driver:

- adapter initialization asks once (GL below 2.0, ARB_point_sprite, the entry
  point), with a bounded error drain, and leaves the GL default (UPPER_LEFT);
  a driver that rejects the parameter, a GL 2.0 one (upstream psorigin) and a
  missing extension or entry point issue nothing further;
- the state handler then sets LOWER_LEFT offscreen and UPPER_LEFT onscreen
  through the scalar float form (never glPointParameteri, which DreamGPU does
  not export), from the context's own gl_info, never the TLS context.
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
#include <stdio.h>
typedef int BOOL;
typedef unsigned int DWORD, GLenum;
typedef float GLfloat;
#define TRUE 1
#define FALSE 0
#define TRACE(...) ((void)0)
#define WARN(...) ((void)0)
#define checkGLcall(...) ((void)0)
#define GL_EXTCALL(f) (gl_info->gl_ops.ext.p_##f)
enum { WINED3D_GL_VERSION_2_0, ARB_POINT_SPRITE, EXTENSIONS };
struct wined3d_gl_info {
    struct {
        struct { GLenum (*p_glGetError)(void); } gl;
        struct { void (*p_glPointParameterfARB)(GLenum, GLfloat); } ext;
    } gl_ops;
    BOOL supported[EXTENSIONS];
    BOOL point_sprite_origin_arb;
};
struct wined3d_context { const struct wined3d_gl_info *gl_info; BOOL render_offscreen; };
struct wined3d_state { int unused; };
static struct wined3d_context *context_get_current(void) { assert(!"state handlers use their own context"); return 0; }

/* A driver taking (or rejecting) the origin through the scalar float form. */
static BOOL accepts;
static GLenum origin = 0, pending;
static unsigned calls, persistent;
static GLenum get_error(void)
{
    GLenum e = pending;
    if (persistent) { --persistent; return 0x0505; }
    pending = 0;
    return e;
}
static void point_parameter(GLenum pname, GLfloat value)
{
    ++calls;
    if (!accepts || pname != GL_POINT_SPRITE_COORD_ORIGIN) { pending = 0x0500; return; }
    assert(value == (GLfloat)GL_LOWER_LEFT || value == (GLfloat)GL_UPPER_LEFT);
    origin = (GLenum)value;
}
'''

TEST = r'''
int main(void)
{
    struct wined3d_gl_info gl = {{{get_error}, {point_parameter}}, {FALSE, TRUE}, FALSE};
    struct wined3d_context context = {&gl, TRUE};
    struct wined3d_state state;

    (void)context_get_current;
    /* An accepting driver: asked once, left at the GL default. */
    accepts = TRUE; pending = 0x0502;
    assert(wined3d_check_point_sprite_origin(&gl));
    assert(calls == 2 && origin == GL_UPPER_LEFT && !pending);
    /* A rejecting driver: no state, its error drained. */
    accepts = FALSE; calls = 0; origin = 0;
    assert(!wined3d_check_point_sprite_origin(&gl));
    assert(calls == 1 && !origin && !pending);
    /* GL 2.0 (upstream psorigin), no ARB_point_sprite, no entry point, a
     * transport that keeps reporting errors: nothing is set. */
    accepts = TRUE; calls = 0;
    gl.supported[WINED3D_GL_VERSION_2_0] = TRUE;
    assert(!wined3d_check_point_sprite_origin(&gl));
    gl.supported[WINED3D_GL_VERSION_2_0] = FALSE; gl.supported[ARB_POINT_SPRITE] = FALSE;
    assert(!wined3d_check_point_sprite_origin(&gl));
    gl.supported[ARB_POINT_SPRITE] = TRUE; gl.gl_ops.ext.p_glPointParameterfARB = 0;
    assert(!wined3d_check_point_sprite_origin(&gl));
    gl.gl_ops.ext.p_glPointParameterfARB = point_parameter; persistent = 100;
    assert(!wined3d_check_point_sprite_origin(&gl) && persistent == 84);
    persistent = 0;
    assert(!calls);

    /* The state handler follows psorigin(): offscreen LOWER_LEFT. */
    gl.point_sprite_origin_arb = TRUE;
    psorigin_w(&context, &state, 0);
    assert(calls == 1 && origin == GL_LOWER_LEFT);
    context.render_offscreen = FALSE;
    psorigin_w(&context, &state, 0);
    assert(calls == 2 && origin == GL_UPPER_LEFT);
    /* Without driver support it only warns, as before. */
    gl.point_sprite_origin_arb = FALSE;
    psorigin_w(&context, &state, 0);
    assert(calls == 2);
    puts("PASS prepared point sprite origin: asked once below GL 2.0 through glPointParameterfARB, default kept, "
         "rejection/GL 2.0/missing support issue nothing, state handler LOWER_LEFT offscreen and UPPER_LEFT onscreen");
    return 0;
}
'''


def function(source, prefix):
    start = source.index(prefix)
    return source[start:source.index('\n}', start) + 2] + '\n'


with tempfile.TemporaryDirectory(prefix='dreamgpu-psorigin-') as temporary:
    work = Path(temporary)
    for name in json.loads(MANIFEST.read_text())['files']:
        p = work / name
        p.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'vendor/wine9x' / name, p)
    patches.apply(work, MANIFEST)
    directx = (work / 'wined3d/directx.c').read_text()
    state = (work / 'wined3d/state.c').read_text()
    caps = function(directx, 'static BOOL wined3d_adapter_init_gl_caps(')
    probe = caps.index('gl_info->point_sprite_origin_arb = wined3d_check_point_sprite_origin(gl_info);')
    # After the extension fixups and limits, with the caps context current.
    assert caps.index('wined3d_adapter_init_limits(gl_info);') < probe
    assert probe < caps.index('adapter->shader_backend = select_shader_backend(gl_info);')
    # The GL 2.0 handler still comes first; the fallback handles every other GL.
    assert re.search(r'psorigin\s*\}, WINED3D_GL_VERSION_2_0\s*\},\s*\n\s*\{ STATE_POINTSPRITECOORDORIGIN,'
                     r'\s*\{ STATE_POINTSPRITECOORDORIGIN,\s*psorigin_w\s*\}, WINED3D_GL_EXT_NONE', state)
    handler = function(state, 'static void psorigin_w(')
    assert 'glPointParameteri' not in handler and 'context_get_current' not in handler
    code = function(directx, 'static BOOL wined3d_check_point_sprite_origin(') + handler
    header = (ROOT / 'vendor/wine9x/include/wine/wgl.h').read_text()
    values = dict(re.findall(r'^#define\s+(GL_[A-Z_0-9]+)\s+(0x[0-9a-fA-F]+|[0-9]+)\s*$', header, re.M))
    enums = sorted(set(re.findall(r'\bGL_[A-Z_0-9]+\b', code + PREAMBLE + TEST)) - {'GL_EXTCALL'})
    gl = '\n'.join('#define %s %s' % (e, values[e]) for e in enums)
    (work / 'test.c').write_text(gl + PREAMBLE + code + TEST)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                    '-Wno-unused-function', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-fno-omit-frame-pointer', str(work / 'test.c'), '-o', str(work / 'test')], check=True)
    subprocess.run([str(work / 'test')], check=True)
