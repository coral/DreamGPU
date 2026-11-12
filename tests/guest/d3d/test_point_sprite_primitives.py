#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Patch 0058 on actual pinned Wine source: point sprites only while points rasterize.

Direct3D's D3DRS_POINTSPRITEENABLE and GL_POINT_SPRITE both replace texture
coordinates of points only. Wine set GL_POINT_SPRITE from the render state
alone (COORD_REPLACE is on for every unit), and Mesa 26 (radeonsi, llvmpipe)
then drops those coordinates from its fixed-function vertex processing for
every primitive: Halo enables sprites once, and its pretransformed quad that
copies the scene to the back buffer sampled one texel. The prepared
state_pointsprite_draw(), state_pointsprite_primitive() and state_fillmode()
run here under ASan/UBSan against a recording GL and a model of WineD3D's
dirty-state machinery (device invalidation, the draw-time apply loop with
representatives, context creation and SetupForBlit):

- after every draw, GL_POINT_SPRITE is enabled exactly when the render state
  is set and points rasterize (a point list, or triangles in point fill mode),
  over fixed scenarios (Halo's sequence, point/triangle alternation, fill mode
  changes with and without sprites, blits, new contexts) and 200000 random
  operations;
- with sprites off, primitive and fill-mode changes send no GL_POINT_SPRITE
  call; with them on, a draw sends at most two (a primitive change to or from
  points and a fill-mode change), and one per render-state change, context
  creation or blit, as before;
- the fixed-function vertex template selects the new handlers with
  ARB_point_sprite (Wine's FIXME and nop handlers otherwise); the GLSL vertex
  pipe keeps upstream's state_pointsprite; device.c, stateblock.c and
  SetupForBlit still invalidate STATE_POINT_ENABLE and the render state as the
  model assumes.
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
#include <string.h>
typedef int BOOL;
typedef unsigned int DWORD, GLenum;
#define TRUE 1
#define FALSE 0
#define FIXME(...) ((void)0)
#define checkGLcall(...) ((void)0)
#define STATE_RENDER(a) (a)
#define STATE_POINT_ENABLE 300
#define STATE_COUNT 301
struct wined3d_gl_info {
    struct { struct {
        void (*p_glEnable)(GLenum);
        void (*p_glDisable)(GLenum);
        void (*p_glPolygonMode)(GLenum, GLenum);
    } gl; } gl_ops;
};
struct wined3d_state { DWORD render_states[256]; GLenum gl_primitive_type; };
/* Wine's dirty bits and list: the apply loop clears an entry's bit before
 * calling its representative's handler, in insertion order. */
struct wined3d_context {
    const struct wined3d_gl_info *gl_info;
    BOOL dirty[STATE_COUNT];
    DWORD list[STATE_COUNT], count;
};
static BOOL isStateDirty(const struct wined3d_context *context, DWORD state) { return context->dirty[state]; }
static void context_apply_state(struct wined3d_context *context, const struct wined3d_state *state, DWORD state_id);
'''

TEST = r'''
/* The recording GL: one context's GL_POINT_SPRITE and polygon mode. */
static BOOL sprite, sprite_known;
static GLenum polygon = GL_FILL;
static unsigned sprite_calls, other_calls;
static void gl_enable(GLenum cap) { assert(cap == GL_POINT_SPRITE_ARB); sprite = TRUE; ++sprite_calls; }
static void gl_disable(GLenum cap) { assert(cap == GL_POINT_SPRITE_ARB); sprite = FALSE; ++sprite_calls; }
static void gl_polygon_mode(GLenum face, GLenum mode)
{
    assert(face == GL_FRONT_AND_BACK);
    assert(mode == GL_POINT || mode == GL_LINE || mode == GL_FILL);
    polygon = mode;
    ++other_calls;
}
static const struct wined3d_gl_info gl = {{{gl_enable, gl_disable, gl_polygon_mode}}};

/* The ffp vertex pipe's table with ARB_point_sprite, as compile_state_table
 * builds it: each state represents itself. */
static void apply(struct wined3d_context *context, const struct wined3d_state *state, DWORD rep)
{
    switch (rep)
    {
        case STATE_RENDER(WINED3D_RS_POINTSPRITEENABLE): state_pointsprite_draw(context, state, rep); break;
        case STATE_POINT_ENABLE: state_pointsprite_primitive(context, state, rep); break;
        case STATE_RENDER(WINED3D_RS_FILLMODE): state_fillmode(context, state, rep); break;
        default: assert(!"unexpected state");
    }
}
static void context_apply_state(struct wined3d_context *context, const struct wined3d_state *state, DWORD state_id)
{
    apply(context, state, state_id);
}

static struct wined3d_state device;
static struct wined3d_context context = {&gl, {0}, {0}, 0};
static void invalidate(DWORD state)
{
    if (context.dirty[state]) return;
    context.dirty[state] = TRUE;
    context.list[context.count++] = state;
}
/* context_create: every state dirty, GL defaults (no sprites, filled). */
static void create_context(void)
{
    memset(context.dirty, 0, sizeof(context.dirty));
    context.count = 0;
    invalidate(STATE_RENDER(WINED3D_RS_FILLMODE));
    invalidate(STATE_POINT_ENABLE);
    invalidate(STATE_RENDER(WINED3D_RS_POINTSPRITEENABLE));
    sprite = FALSE; polygon = GL_FILL; sprite_known = TRUE;
}
/* wined3d_device_set_render_state: only a changed value invalidates. */
static void set_render_state(DWORD index, DWORD value)
{
    if (device.render_states[index] == value) return;
    device.render_states[index] = value;
    invalidate(STATE_RENDER(index));
}
/* wined3d_device_set_primitive_type (checked against device.c below). */
static void set_primitive(GLenum type)
{
    GLenum prev = device.gl_primitive_type;
    device.gl_primitive_type = type;
    if (type != prev && (type == GL_POINTS || prev == GL_POINTS))
        invalidate(STATE_POINT_ENABLE);
}
/* SetupForBlit (checked against context.c below). */
static void blit(void)
{
    gl_disable(GL_POINT_SPRITE_ARB);
    invalidate(STATE_RENDER(WINED3D_RS_POINTSPRITEENABLE));
}
static BOOL expected(void)
{
    return device.render_states[WINED3D_RS_POINTSPRITEENABLE]
            && (device.gl_primitive_type == GL_POINTS
            || device.render_states[WINED3D_RS_FILLMODE] == WINED3D_FILL_POINT);
}
static GLenum polygon_mode(void)
{
    switch (device.render_states[WINED3D_RS_FILLMODE])
    {
        case WINED3D_FILL_POINT: return GL_POINT;
        case WINED3D_FILL_WIREFRAME: return GL_LINE;
        default: return GL_FILL;
    }
}
static unsigned draws;
/* context_apply_draw_state's loop, then the draw. */
static void draw(void)
{
    DWORD i;
    for (i = 0; i < context.count; ++i)
    {
        DWORD rep = context.list[i];
        context.dirty[rep] = FALSE;
        apply(&context, &device, rep);
    }
    context.count = 0;
    if (sprite != expected() || polygon != polygon_mode())
    {
        printf("draw %u: sprite %d expected %d (state %u, primitive %#x, fill %u), polygon %#x\n", draws, sprite,
                expected(), device.render_states[WINED3D_RS_POINTSPRITEENABLE], device.gl_primitive_type,
                device.render_states[WINED3D_RS_FILLMODE], polygon);
        assert(0);
    }
    ++draws;
}
static const GLenum primitives[] = {GL_POINTS, GL_LINES, GL_LINE_STRIP, GL_TRIANGLES, GL_TRIANGLE_STRIP, GL_TRIANGLE_FAN};

int main(void)
{
    unsigned i, seed = 12345, before;

    device.gl_primitive_type = ~0u;
    device.render_states[WINED3D_RS_FILLMODE] = WINED3D_FILL_SOLID;
    create_context();

    /* Halo: sprites on from device creation, then only triangle draws (its
     * occlusion and final pretransformed quads are fixed-function). */
    set_render_state(WINED3D_RS_POINTSPRITEENABLE, TRUE);
    set_primitive(GL_TRIANGLE_STRIP); draw();
    assert(!sprite);
    set_primitive(GL_TRIANGLE_FAN); before = sprite_calls; draw();
    assert(!sprite && sprite_calls == before);
    /* A particle point list, then back to triangles. */
    set_primitive(GL_POINTS); draw(); assert(sprite);
    set_primitive(GL_TRIANGLES); draw(); assert(!sprite);
    set_primitive(GL_LINES); before = sprite_calls; draw(); assert(!sprite && sprite_calls == before);
    /* Point fill mode rasterizes triangles as points: sprites as before. */
    set_render_state(WINED3D_RS_FILLMODE, WINED3D_FILL_POINT); set_primitive(GL_TRIANGLES); draw();
    assert(sprite && polygon == GL_POINT);
    set_render_state(WINED3D_RS_FILLMODE, WINED3D_FILL_WIREFRAME); draw(); assert(!sprite && polygon == GL_LINE);
    set_render_state(WINED3D_RS_FILLMODE, WINED3D_FILL_SOLID); draw();
    /* A blit clears the enable; the next point draw restores it. */
    set_primitive(GL_POINTS); draw(); blit(); assert(!sprite); draw(); assert(sprite);
    /* Sprites off: primitive and fill changes send nothing. */
    set_render_state(WINED3D_RS_POINTSPRITEENABLE, FALSE); draw(); assert(!sprite);
    before = sprite_calls;
    for (i = 0; i < 64; ++i)
    {
        set_primitive(primitives[i % 6]);
        set_render_state(WINED3D_RS_FILLMODE, 1 + i % 3);
        draw();
    }
    assert(sprite_calls == before && !sprite);
    /* A new context applies everything once. */
    set_render_state(WINED3D_RS_POINTSPRITEENABLE, TRUE); set_primitive(GL_POINTS);
    create_context(); draw(); assert(sprite);

    /* Random operations against the rule. Sprites off: nothing unless the
     * render state itself is applied. Sprites on: at most the primitive and
     * fill-mode reapplications, and any change of the answer is sent. */
    for (i = 0; i < 200000; ++i)
    {
        BOOL was = sprite;
        unsigned op, calls = sprite_calls;
        BOOL render_dirty;
        seed = seed * 1103515245u + 12345u;
        op = (seed >> 16) % 16;
        if (op < 6) set_primitive(primitives[op]);
        else if (op < 9) set_render_state(WINED3D_RS_FILLMODE, op - 5);
        else if (op < 11) set_render_state(WINED3D_RS_POINTSPRITEENABLE, op - 9);
        else if (op == 11) { blit(); was = sprite; calls = sprite_calls; }
        else if (op == 12) { create_context(); was = sprite; calls = sprite_calls; }
        render_dirty = context.dirty[STATE_RENDER(WINED3D_RS_POINTSPRITEENABLE)];
        draw();
        if (render_dirty)
            assert(sprite_calls == calls + 1);
        else if (!device.render_states[WINED3D_RS_POINTSPRITEENABLE])
            assert(sprite_calls == calls && !was && !sprite);
        else
            assert(sprite_calls <= calls + 2 && (was == sprite || sprite_calls > calls));
    }
    printf("PASS prepared point sprite primitives: GL_POINT_SPRITE exactly while points rasterize over %u draws "
           "(Halo's triangle-only sequence, point/triangle switches, point fill mode, blits, new contexts, "
           "200000 random operations), no calls for primitive or fill changes with sprites off\n", draws);
    return 0;
}
'''


def function(source, prefix):
    start = source.index(prefix)
    return source[start:source.index('\n}', start) + 2] + '\n'


def entries(source, template):
    start = re.search(r'^(?:static )?const struct StateEntryTemplate %s\[\] =' % template, source, re.M).start()
    return source[start:source.index('\n};', start)]


with tempfile.TemporaryDirectory(prefix='dreamgpu-point-sprite-') as temporary:
    work = Path(temporary)
    for name in json.loads(MANIFEST.read_text())['files']:
        p = work / name
        p.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'vendor/wine9x' / name, p)
    patches.apply(work, MANIFEST)
    state = (work / 'wined3d/state.c').read_text()
    device = (work / 'wined3d/device.c').read_text()
    context = (work / 'wined3d/context.c').read_text()
    stateblock = (ROOT / 'vendor/wine9x/wined3d/stateblock.c').read_text()
    glsl = (ROOT / 'vendor/wine9x/wined3d/glsl_shader.c').read_text()
    assert 'wined3d/stateblock.c' not in json.loads(MANIFEST.read_text())['files']
    assert 'wined3d/glsl_shader.c' not in json.loads(MANIFEST.read_text())['files']

    # The fixed-function vertex pipe (ARB backend) takes the new handlers with
    # ARB_point_sprite, upstream's FIXME/nop otherwise, in that order.
    ffp = entries(state, 'vp_ffp_states')
    row = r'\{\s*%s,\s*\{\s*%s,\s*%s\s*\},\s*%s\s*\},\s*\n\s*'
    sprite_rs = r'STATE_RENDER\(WINED3D_RS_POINTSPRITEENABLE\)'
    assert re.search(row % (sprite_rs, sprite_rs, 'state_pointsprite_draw', 'ARB_POINT_SPRITE')
                     + row % (sprite_rs, sprite_rs, 'state_pointsprite_w', 'WINED3D_GL_EXT_NONE'), ffp)
    assert re.search(row % ('STATE_POINT_ENABLE', 'STATE_POINT_ENABLE', 'state_pointsprite_primitive', 'ARB_POINT_SPRITE')
                     + row % ('STATE_POINT_ENABLE', 'STATE_POINT_ENABLE', 'state_nop', 'WINED3D_GL_EXT_NONE'), ffp)
    assert ffp.count('POINTSPRITEENABLE') == 4 and ffp.count('STATE_POINT_ENABLE,') == 4
    assert 'state_pointsprite ' not in ffp and 'state_pointsprite}' not in ffp
    # Fill mode stays a misc state applying the polygon mode.
    misc = entries(state, 'misc_state_template')
    assert re.search(row % (r'STATE_RENDER\(WINED3D_RS_FILLMODE\)', r'STATE_RENDER\(WINED3D_RS_FILLMODE\)',
                            'state_fillmode', 'WINED3D_GL_EXT_NONE'), misc)
    # The GLSL vertex pipe keeps upstream's handler, which stays exported.
    assert re.search(row % (sprite_rs, sprite_rs, 'state_pointsprite', 'ARB_POINT_SPRITE'),
                     entries(glsl, 'glsl_vertex_pipe_vp_states'))
    assert 'void state_pointsprite(struct wined3d_context *context' in state
    # The model's invalidations: primitive type changes to or from points
    # (directly and through state blocks), and blits clear and invalidate.
    for source in (function(device, 'void CDECL wined3d_device_set_primitive_type('),
                   stateblock[stateblock.index('if (stateblock->changed.primitive_type)'):]):
        assert re.search(r'if \(gl_primitive_type != prev && \(gl_primitive_type == GL_POINTS \|\| prev == GL_POINTS\)\)'
                         r'\s*\n\s*device_invalidate_state\(device, STATE_POINT_ENABLE\);', source)
    blit = function(context, 'static void SetupForBlit(')
    assert re.search(r'glDisable\(GL_POINT_SPRITE_ARB\);\s*\n.*\n\s*context_invalidate_state\(context, '
                     r'STATE_RENDER\(WINED3D_RS_POINTSPRITEENABLE\)\);', blit)
    # A changed render state reaches the command stream, which invalidates it;
    # handlers read the device's state, which the call already updated.
    render_state = function(device, 'void CDECL wined3d_device_set_render_state(')
    assert re.search(r'device->update_state->render_states\[state\] = value;[\s\S]*if \(value == old_value\)\s*\n'
                     r'.*\n\s*else\s*\n\s*wined3d_cs_emit_set_render_state\(device->cs, state, value\);', render_state)
    cs = (ROOT / 'vendor/wine9x/wined3d/cs.c').read_text()
    assert 'wined3d/cs.c' not in json.loads(MANIFEST.read_text())['files']
    assert 'device_invalidate_state(cs->device, STATE_RENDER(op->state));' in function(
        cs, 'static void wined3d_cs_exec_set_render_state(')
    assert 'const struct wined3d_state *state = &device->state;' in function(
        context, 'BOOL context_apply_draw_state(')

    code = (function(state, 'static void state_fillmode(') + function(state, 'static void state_pointsprite_draw(')
            + function(state, 'static void state_pointsprite_primitive('))
    assert 'context_apply_state(context, state, STATE_POINT_ENABLE);' in code
    header = (ROOT / 'vendor/wine9x/include/wine/wgl.h').read_text()
    values = dict(re.findall(r'^#define\s+(GL_[A-Z_0-9]+)\s+(0x[0-9a-fA-F]+|[0-9]+)\s*$', header, re.M))
    enums = sorted(set(re.findall(r'\bGL_[A-Z_0-9]+\b', code + TEST)))
    gl = '\n'.join('#define %s %s' % (e, values[e]) for e in enums)
    wined3d = (ROOT / 'vendor/wine9x/include/wine/wined3d.h').read_text()
    states = dict(re.findall(r'^\s*(WINED3D_(?:RS_[A-Z_0-9]+|FILL_[A-Z]+))\s*=\s*([0-9]+),', wined3d, re.M))
    names = ('WINED3D_RS_FILLMODE', 'WINED3D_RS_POINTSPRITEENABLE', 'WINED3D_FILL_POINT', 'WINED3D_FILL_WIREFRAME',
             'WINED3D_FILL_SOLID')
    d3d = 'enum wined3d_fill_mode { %s };\n' % ', '.join('%s = %s' % (n, states[n]) for n in names[2:])
    d3d += ''.join('#define %s %s\n' % (n, states[n]) for n in names[:2])
    assert int(states['WINED3D_RS_POINTSPRITEENABLE']) < 256
    (work / 'test.c').write_text(gl + '\n' + d3d + PREAMBLE + code + TEST)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer',
                    str(work / 'test.c'), '-o', str(work / 'test')], check=True)
    subprocess.run([str(work / 'test')], check=True)
