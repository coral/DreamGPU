#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Patch 0044 on actual pinned Wine source: shared depth surfaces with framebuffer objects.

EXT_framebuffer_object requires attachments of one size, so this Wine attaches
a depth surface larger than the render target through a separate
renderbuffer of the target's size (surface_set_compatible_renderbuffer()).
The depth surface's own contents never reach it, and surface_load_ds_location()
then refuses to load the surface ("Not supported with fixed up depth
stencil"), which skips draws and clears. D3D shares one depth buffer between
targets of different sizes (a reflection or shadow target with the auto depth
buffer), and DreamGPU's framebuffer objects follow the GL 3 completeness rules,
so mixed sizes are complete. The prepared check_fbo_mixed_sizes() and
surface_set_compatible_renderbuffer() run here under ASan/UBSan: the probe
asks the implementation (and cleans up), ARB_framebuffer_object implies the
answer, no probe runs without framebuffer objects, and a complete mixed-size
answer attaches the surface itself while an incomplete one keeps the upstream
renderbuffer substitution. It fails without the patch.
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
#include <stdlib.h>
#include <string.h>
typedef int BOOL, GLint, GLsizei;
typedef unsigned int UINT, DWORD, GLuint, GLenum;
#define TRUE 1
#define FALSE 0
#define TRACE(...) ((void)0)
#define checkGLcall(...) ((void)0)
#define ORM_BACKBUFFER 0
#define ORM_FBO 1
#define ARB_FRAMEBUFFER_OBJECT 0
#define WINED3DFMT_NULL 99
#include "list.h"
static struct { int offscreen_rendering_mode; } wined3d_settings;
struct wined3d_gl_info {
    struct { struct { GLenum (*p_glGetError)(void); } gl; } gl_ops;
    struct {
        void (*glGenFramebuffers)(GLsizei, GLuint *);
        void (*glBindFramebuffer)(GLenum, GLuint);
        void (*glGenRenderbuffers)(GLsizei, GLuint *);
        void (*glBindRenderbuffer)(GLenum, GLuint);
        void (*glRenderbufferStorage)(GLenum, GLenum, GLsizei, GLsizei);
        void (*glFramebufferRenderbuffer)(GLenum, GLenum, GLenum, GLuint);
        GLenum (*glCheckFramebufferStatus)(GLenum);
        void (*glDeleteFramebuffers)(GLsizei, const GLuint *);
        void (*glDeleteRenderbuffers)(GLsizei, const GLuint *);
    } fbo_ops;
    BOOL supported[1];
    BOOL fbo_mixed_sizes;
};
struct wined3d_adapter { struct wined3d_gl_info gl_info; };
struct wined3d_device { struct wined3d_adapter *adapter; };
struct wined3d_format { int id; GLenum glInternal; };
struct wined3d_resource { struct wined3d_device *device; const struct wined3d_format *format; };
struct wined3d_renderbuffer_entry { struct list entry; GLuint id; UINT width; UINT height; };
struct wined3d_surface {
    struct wined3d_resource resource;
    unsigned int pow2Width, pow2Height;
    struct list renderbuffers;
    struct wined3d_renderbuffer_entry *current_renderbuffer;
};

/* A mock framebuffer: complete when its color and depth sizes match, or
 * always (GL 3 rules). */
static BOOL gl3_rules;
static GLuint next_name = 1, bound_fbo, bound_rb, color_rb, depth_rb, live_fbos, live_rbs;
static GLsizei rb_width[16], rb_height[16];
static GLenum rb_format[16];
static unsigned gl_calls, errors;
static GLenum get_error(void) { ++gl_calls; if (errors) { --errors; return 0x0500; } return 0; }
static void gen_fbos(GLsizei n, GLuint *names) { ++gl_calls; assert(n == 1); *names = next_name++; ++live_fbos; }
static void bind_fbo(GLenum target, GLuint name) { ++gl_calls; assert(target == GL_FRAMEBUFFER); bound_fbo = name; }
static void gen_rbs(GLsizei n, GLuint *names)
{ GLsizei i; ++gl_calls; for (i = 0; i < n; ++i) { names[i] = next_name++; ++live_rbs; } }
static void bind_rb(GLenum target, GLuint name) { ++gl_calls; assert(target == GL_RENDERBUFFER); bound_rb = name; }
static void rb_storage(GLenum target, GLenum format, GLsizei w, GLsizei h)
{
    ++gl_calls; assert(target == GL_RENDERBUFFER && bound_rb && bound_rb < 16);
    rb_format[bound_rb] = format; rb_width[bound_rb] = w; rb_height[bound_rb] = h;
}
static void fbo_rb(GLenum target, GLenum attachment, GLenum rbtarget, GLuint name)
{
    ++gl_calls; assert(target == GL_FRAMEBUFFER && rbtarget == GL_RENDERBUFFER && bound_fbo);
    if (attachment == GL_COLOR_ATTACHMENT0) color_rb = name;
    else { assert(attachment == GL_DEPTH_ATTACHMENT); depth_rb = name; }
}
static GLenum check_status(GLenum target)
{
    ++gl_calls; assert(target == GL_FRAMEBUFFER && bound_fbo && color_rb && depth_rb);
    assert(rb_format[color_rb] == GL_RGBA8 && rb_format[depth_rb] == GL_DEPTH_COMPONENT);
    if (gl3_rules || (rb_width[color_rb] == rb_width[depth_rb] && rb_height[color_rb] == rb_height[depth_rb]))
        return GL_FRAMEBUFFER_COMPLETE;
    return 0x8cd9; /* FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT */
}
static void delete_fbos(GLsizei n, const GLuint *names) { ++gl_calls; assert(n == 1 && names[0]); --live_fbos; }
static void delete_rbs(GLsizei n, const GLuint *names) { GLsizei i; ++gl_calls; for (i = 0; i < n; ++i) { assert(names[i]); --live_rbs; } }
'''

TEST = r'''
int main(void)
{
    struct wined3d_adapter adapter;
    struct wined3d_device device = {&adapter};
    struct wined3d_format d24s8 = {1, 0x88f0}, x8r8g8b8 = {2, 0x8051};
    struct wined3d_surface ds, rt, small, null_rt;
    struct wined3d_gl_info *gl = &adapter.gl_info;
    struct wined3d_format null_format = {WINED3DFMT_NULL, 0};
    unsigned before;

    memset(&adapter, 0, sizeof(adapter));
    gl->gl_ops.gl.p_glGetError = get_error;
    gl->fbo_ops.glGenFramebuffers = gen_fbos;
    gl->fbo_ops.glBindFramebuffer = bind_fbo;
    gl->fbo_ops.glGenRenderbuffers = gen_rbs;
    gl->fbo_ops.glBindRenderbuffer = bind_rb;
    gl->fbo_ops.glRenderbufferStorage = rb_storage;
    gl->fbo_ops.glFramebufferRenderbuffer = fbo_rb;
    gl->fbo_ops.glCheckFramebufferStatus = check_status;
    gl->fbo_ops.glDeleteFramebuffers = delete_fbos;
    gl->fbo_ops.glDeleteRenderbuffers = delete_rbs;

    /* Without framebuffer objects nothing is asked. */
    wined3d_settings.offscreen_rendering_mode = ORM_BACKBUFFER;
    assert(!check_fbo_mixed_sizes(gl) && !gl_calls);
    /* ARB_framebuffer_object completes mixed sizes by definition. */
    wined3d_settings.offscreen_rendering_mode = ORM_FBO;
    gl->supported[ARB_FRAMEBUFFER_OBJECT] = TRUE;
    assert(check_fbo_mixed_sizes(gl) && !gl_calls);
    gl->supported[ARB_FRAMEBUFFER_OBJECT] = FALSE;
    /* EXT: ask. GL 3 rules complete; the EXT rule does not. The probe leaves
     * the window framebuffer bound, deletes everything and drains its errors
     * with a bound. */
    gl3_rules = TRUE; errors = 3;
    assert(check_fbo_mixed_sizes(gl));
    assert(!bound_fbo && !live_fbos && !live_rbs && !errors);
    assert(rb_width[color_rb] != rb_width[depth_rb] && rb_height[color_rb] != rb_height[depth_rb]);
    gl3_rules = FALSE; errors = 100;
    assert(!check_fbo_mixed_sizes(gl));
    assert(!bound_fbo && !live_fbos && !live_rbs && errors == 84);
    errors = 0;

    memset(&ds, 0, sizeof(ds)); memset(&rt, 0, sizeof(rt)); memset(&small, 0, sizeof(small));
    memset(&null_rt, 0, sizeof(null_rt));
    ds.resource.device = &device; ds.resource.format = &d24s8; ds.pow2Width = 1024; ds.pow2Height = 512;
    list_init(&ds.renderbuffers);
    rt.resource.format = &x8r8g8b8; rt.pow2Width = 1024; rt.pow2Height = 512;
    small.resource.format = &x8r8g8b8; small.pow2Width = 256; small.pow2Height = 256;
    null_rt.resource.format = &null_format; null_rt.pow2Width = 64; null_rt.pow2Height = 64;

    /* Mixed sizes complete: the depth surface itself is attached. */
    gl->fbo_mixed_sizes = TRUE; before = gl_calls;
    surface_set_compatible_renderbuffer(&ds, &small);
    assert(!ds.current_renderbuffer && list_empty(&ds.renderbuffers) && gl_calls == before);
    surface_set_compatible_renderbuffer(&ds, &rt);
    surface_set_compatible_renderbuffer(&ds, NULL);
    surface_set_compatible_renderbuffer(&ds, &null_rt);
    assert(!ds.current_renderbuffer && list_empty(&ds.renderbuffers) && gl_calls == before);
    /* Incomplete (the EXT rule): the upstream substitution remains. */
    gl->fbo_mixed_sizes = FALSE;
    surface_set_compatible_renderbuffer(&ds, &small);
    assert(ds.current_renderbuffer && ds.current_renderbuffer->width == 256 && ds.current_renderbuffer->height == 256);
    assert(rb_format[ds.current_renderbuffer->id] == 0x88f0 && rb_width[ds.current_renderbuffer->id] == 256);
    surface_set_compatible_renderbuffer(&ds, &rt);
    assert(!ds.current_renderbuffer);
    while (!list_empty(&ds.renderbuffers))
    {
        struct wined3d_renderbuffer_entry *entry = LIST_ENTRY(list_head(&ds.renderbuffers),
                struct wined3d_renderbuffer_entry, entry);
        list_remove(&entry->entry);
        free(entry);
    }
    puts("PASS prepared FBO mixed-size attachments: the implementation is asked (EXT), ARB implies it, no probe "
         "without FBOs, cleanup, shared depth attached directly; EXT rule keeps renderbuffer substitution");
    return 0;
}
'''


def function(source, prefix):
    start = source.index(prefix)
    return source[start:source.index('\n}', start) + 2] + '\n'


with tempfile.TemporaryDirectory(prefix='dreamgpu-fbo-mixed-') as temporary:
    work = Path(temporary)
    for name in json.loads(MANIFEST.read_text())['files']:
        p = work / name
        p.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'vendor/wine9x' / name, p)
    patches.apply(work, MANIFEST)
    utils = (work / 'wined3d/utils.c').read_text()
    surface = (work / 'wined3d/surface.c').read_text()
    init = function(utils, 'BOOL wined3d_adapter_init_format_info(')
    assert init.index('init_format_fbo_compat_info(ctx);') \
        < init.index('gl_info->fbo_mixed_sizes = check_fbo_mixed_sizes(gl_info);')
    private = (work / 'wined3d/wined3d_private.h').read_text()
    gl_info = private[private.index('struct wined3d_gl_info\n{'):]
    assert 'BOOL fbo_mixed_sizes;' in gl_info[:gl_info.index('\n};')]
    code = function(utils, 'static BOOL check_fbo_mixed_sizes(') + \
        function(surface, 'void surface_set_compatible_renderbuffer(')
    header = (ROOT / 'vendor/wine9x/include/wine/wgl.h').read_text()
    values = dict(re.findall(r'^#define\s+(GL_[A-Z_0-9]+)\s+(0x[0-9a-fA-F]+|[0-9]+)\s*$', header, re.M))
    enums = sorted(set(re.findall(r'\bGL_[A-Z_0-9]+\b', code + PREAMBLE)))
    gl = '\n'.join('#define %s %s' % (e, values[e]) for e in enums)
    (work / 'test.c').write_text(gl + PREAMBLE + code + TEST)
    shutil.copyfile(ROOT / 'vendor/wine9x/include/wine/list.h', work / 'list.h')
    subprocess.run([os.environ.get('CC', 'cc'), '-std=gnu99', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                    '-Wno-unused-function', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-fno-omit-frame-pointer', '-I', str(work), str(work / 'test.c'), '-o', str(work / 'test')],
                   check=True)
    subprocess.run([str(work / 'test')], check=True)
