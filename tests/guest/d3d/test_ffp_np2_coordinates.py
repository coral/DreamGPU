#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute the prepared fixed-function fragment programs for NP2 textures under
a vertex shader (0064).

DreamGPU reports a GeForce FX without ARB_texture_non_power_of_two, so WineD3D
creates an NPOT texture, such as an 800x600 render target, as a rectangle
(texel coordinates) or a padded 2D texture, and get_texture_matrix() scales the
application's normalized coordinates by its pow2_matrix. transform_texture()
skips the texture matrix under a vertex shader, so a D3D vertex shader feeding
fixed-function texture stages sampled a rectangle at texels 0..1: Thief: Deadly
Shadows draws its 800x600 scene to the back buffer that way and showed one
texel of it, a uniform near-black screen.

The prepared gen_arbfp_ffp_shader generates WineD3D's fixed-function
replacement programs under ASan/UBSan, and arbfp_load_np2_fixup loads their
program locals from prepared texture state. A small ARB_fragment_program
interpreter samples a texture that reports where it was hit. A stage keyed
vs_np2_fixup must reach the D3D coordinate on the rectangle (and on a padded
texture) for plain, projected (count3 and count4) and bump-mapped lookups and
on a later stage; a stage without it (the fixed-function vertex pipeline,
whose texture matrix already scaled) must stay unscaled. gen_ffp_frag_op keys
the bit by the texture matrix's own condition; the program binding and the
constant reload after sampler() load the scales. Mutations must fail.
"""
import importlib.util
import json
import math
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
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef int BOOL;
typedef unsigned int DWORD, UINT, GLuint, GLenum;
typedef unsigned short WORD;
typedef float GLfloat;
#define TRUE 1
#define FALSE 0
static void TRACE(const char *format, ...) { (void)format; }
static void FIXME(const char *format, ...) { (void)format; }
#define ERR(...) (fprintf(stderr, __VA_ARGS__), assert(0))
#define checkGLcall(...) ((void)0)
#define MAX_TEXTURES 8
#define GL_FRAGMENT_PROGRAM_ARB 0x8804
#define WINED3D_QUIRK_BROKEN_ARB_FOG 0x00000200
enum wined3d_gl_extension { ARB_FRAGMENT_PROGRAM_SHADOW, EXTENSION_COUNT };
struct wined3d_gl_info { BOOL supported[EXTENSION_COUNT]; DWORD quirks; };
static const float wined3d_srgb_const0[] = {0.41666f, 1.055f, 0.055f, 12.92f};
static const float wined3d_srgb_const1[] = {0.0031308f, 0.0f, 0.0f, 0.0f};
static void wined3d_ftoa(float value, char *s) { sprintf(s, "%.8e", value); }

struct wined3d_string_buffer { char *buffer; size_t size, content_size; };
static BOOL string_buffer_init(struct wined3d_string_buffer *buffer)
{
    buffer->size = 1 << 16;
    buffer->content_size = 0;
    return !!(buffer->buffer = calloc(1, buffer->size));
}
static void string_buffer_free(struct wined3d_string_buffer *buffer) { free(buffer->buffer); }
static void shader_addline(struct wined3d_string_buffer *buffer, const char *format, ...)
{
    va_list args;
    int written;
    va_start(args, format);
    written = vsnprintf(buffer->buffer + buffer->content_size, buffer->size - buffer->content_size, format, args);
    va_end(args);
    assert(written >= 0 && (size_t)written < buffer->size - buffer->content_size);
    buffer->content_size += written;
}

/* GL: the generated program goes to stdout; locals are recorded. */
static void glGenProgramsARB(int n, GLuint *names) { assert(n == 1); *names = 40; }
static void glBindProgramARB(GLenum target, GLuint name) { assert(target == GL_FRAGMENT_PROGRAM_ARB); }
static void glProgramLocalParameter4fvARB(GLenum target, GLuint index, const GLfloat *v)
{
    assert(target == GL_FRAGMENT_PROGRAM_ARB);
    printf("LOCAL %u %.9g %.9g %.9g %.9g\n", index, v[0], v[1], v[2], v[3]);
}
#define GL_EXTCALL(call) call
static void shader_arb_compile(const struct wined3d_gl_info *gl_info, GLenum target, const char *text)
{
    printf("PROGRAM\n%sEOP\n", text);
}

/* Prepared texture state for arbfp_load_np2_fixup. */
struct wined3d_texture { float pow2_matrix[16]; };
struct wined3d_state { const struct wined3d_texture *textures[MAX_TEXTURES]; };
'''

TEST = r'''
#define T(arg) (WINED3DTA_TEXTURE | (arg))
static struct ffp_frag_settings plain(void)
{
    struct ffp_frag_settings settings;
    unsigned int i;

    memset(&settings, 0, sizeof(settings));
    for (i = 0; i < MAX_TEXTURES; ++i)
        memset(&settings.op[i], 0xff, sizeof(settings.op[i]));
    settings.op[0].cop = settings.op[0].aop = WINED3D_TOP_DISABLE;
    settings.op[0].padding = 0;
    settings.fog = WINED3D_FFP_PS_FOG_OFF;
    settings.texcoords_initialized = 0xff;
    return settings;
}

/* Stage i samples a texture of the given type and selects it. */
static void stage(struct ffp_frag_settings *settings, unsigned int i, DWORD op, DWORD arg1, DWORD arg2,
        unsigned int tex_type, unsigned int projected, BOOL fixup)
{
    struct texture_stage_op *s = &settings->op[i];

    s->cop = s->aop = op;
    s->carg1 = s->aarg1 = arg1;
    s->carg2 = s->aarg2 = arg2;
    s->carg0 = s->aarg0 = ARG_UNUSED;
    s->color_fixup = COLOR_FIXUP_IDENTITY;
    s->tex_type = tex_type;
    s->dst = resultreg;
    s->projected = projected;
    s->shadow = 0;
    s->vs_np2_fixup = fixup;
    s->padding = 0;
    settings->op[i + 1].cop = settings->op[i + 1].aop = WINED3D_TOP_DISABLE;
    settings->op[i + 1].padding = 0;
}

static void emit(const char *name, const struct ffp_frag_settings *settings,
        const struct wined3d_texture *t0, const struct wined3d_texture *t1)
{
    struct wined3d_gl_info gl_info;
    struct wined3d_state state;
    WORD stages = 0;
    unsigned int i;

    memset(&gl_info, 0, sizeof(gl_info));
    memset(&state, 0, sizeof(state));
    state.textures[0] = t0;
    state.textures[1] = t1;
    printf("CASE %s\n", name);
    gen_arbfp_ffp_shader(settings, &gl_info);
    /* As fragment_prog_arbfp after binding the program. */
    for (i = 0; i < MAX_TEXTURES && settings->op[i].cop != WINED3D_TOP_DISABLE; ++i)
        if (settings->op[i].vs_np2_fixup)
            stages |= 1u << i;
    arbfp_load_np2_fixup(&gl_info, &state, stages);
    printf("END CASE\n");
}

static struct wined3d_texture texture(float x, float y)
{
    struct wined3d_texture t;
    memset(&t, 0, sizeof(t));
    t.pow2_matrix[0] = x;
    t.pow2_matrix[5] = y;
    t.pow2_matrix[10] = t.pow2_matrix[15] = 1.0f;
    return t;
}

int main(void)
{
    /* An 800x600 rectangle, an 800x600 render target padded to 1024x1024 and
     * a power-of-two bump map. */
    struct wined3d_texture rect = texture(800.0f, 600.0f), padded = texture(800.0f / 1024.0f, 600.0f / 1024.0f);
    struct wined3d_texture bump = texture(1.0f, 1.0f);
    struct ffp_frag_settings s;

    setvbuf(stdout, NULL, _IONBF, 0);
    s = plain(); stage(&s, 0, WINED3D_TOP_SELECT_ARG1, T(0), WINED3DTA_CURRENT, WINED3D_GL_RES_TYPE_TEX_RECT, proj_none, TRUE);
    emit("rect-vs", &s, &rect, NULL);
    s = plain(); stage(&s, 0, WINED3D_TOP_SELECT_ARG1, T(0), WINED3DTA_CURRENT, WINED3D_GL_RES_TYPE_TEX_RECT, proj_none, FALSE);
    emit("rect-ffp", &s, &rect, NULL);
    s = plain(); stage(&s, 0, WINED3D_TOP_MODULATE, T(0), WINED3DTA_DIFFUSE, WINED3D_GL_RES_TYPE_TEX_2D, proj_none, TRUE);
    emit("padded-vs", &s, &padded, NULL);
    s = plain(); stage(&s, 0, WINED3D_TOP_SELECT_ARG1, T(0), WINED3DTA_CURRENT, WINED3D_GL_RES_TYPE_TEX_RECT, proj_count4, TRUE);
    emit("rect-vs-proj4", &s, &rect, NULL);
    s = plain(); stage(&s, 0, WINED3D_TOP_SELECT_ARG1, T(0), WINED3DTA_CURRENT, WINED3D_GL_RES_TYPE_TEX_RECT, proj_count3, TRUE);
    emit("rect-vs-proj3", &s, &rect, NULL);
    s = plain(); stage(&s, 0, WINED3D_TOP_BUMPENVMAP, T(0), WINED3DTA_CURRENT, WINED3D_GL_RES_TYPE_TEX_2D, proj_none, FALSE);
    stage(&s, 1, WINED3D_TOP_SELECT_ARG1, T(0), WINED3DTA_CURRENT, WINED3D_GL_RES_TYPE_TEX_RECT, proj_none, TRUE);
    emit("bump-rect-vs", &s, &bump, &rect);
    s = plain(); stage(&s, 0, WINED3D_TOP_SELECT_ARG1, T(0), WINED3DTA_CURRENT, WINED3D_GL_RES_TYPE_TEX_2D, proj_none, FALSE);
    stage(&s, 1, WINED3D_TOP_ADD, T(0), WINED3DTA_CURRENT, WINED3D_GL_RES_TYPE_TEX_RECT, proj_none, TRUE);
    emit("second-stage-vs", &s, &bump, &rect);
    return 0;
}
'''

# A small ARB_fragment_program interpreter for the statements these programs use.
COORD = (0.3125, 0.375, 0.5, 1.0)      # the D3D normalized coordinate of every stage
Q = 2.0                                # projected cases: the vertex shader writes coord * Q, w = Q
BUMP = (0.25, -0.5)                    # bump map texel (du, dv) in tex0.xy
BUMPMAT = (0.5, 0.0, 0.0, 0.5)         # bumpmat00, 01, 10, 11
SIZES = {'rect': (800.0, 600.0), 'padded': (800.0 / 1024.0, 600.0 / 1024.0)}


def run(program, locals_, texcoords, textures):
    """textures[unit] = (kind, scale): a RECT lookup at texel (x, y) of an
    800x600 rectangle reports the normalized (x / 800, y / 600); a 2D lookup
    of a padded texture reports the image coordinate (s / ratio). A bump map
    reports BUMP."""
    registers = {}
    params = {}

    def value(text):
        negate = text.startswith('-')
        text = text.lstrip('-')
        match = re.fullmatch(r'([\w.\[\]]+?)(?:\.([xyzw]{1,4}|[rgba]{1,4}))?', text)
        name, swizzle = match[1], match[2] or 'xyzw'
        swizzle = swizzle.translate(str.maketrans('rgba', 'xyzw'))
        if re.fullmatch(r'-?[\d.]+', name):
            vector = [float(name)] * 4
        elif name in registers:
            vector = registers[name]
        elif name in params:
            vector = params[name]
        elif m := re.fullmatch(r'fragment\.texcoord\[(\d)\]', name):
            vector = list(texcoords[int(m[1])])
        elif name == 'fragment.color.primary':
            vector = [1.0, 1.0, 1.0, 1.0]
        elif name == 'fragment.color.secondary':
            vector = [0.0, 0.0, 0.0, 0.0]
        else:
            raise AssertionError(f'read of unknown register {name!r}')
        swizzle = (swizzle * 4)[:4] if len(swizzle) == 1 else swizzle
        out = [vector['xyzw'.index(c)] for c in swizzle]
        return [-v for v in out] if negate else out

    def sample(coordinate, unit, target, projected):
        kind, scale = textures[unit]
        x, y = coordinate[0], coordinate[1]
        if projected:
            x, y = x / coordinate[3], y / coordinate[3]
        if kind == 'bump':
            assert target == '2D'
            return [BUMP[0], BUMP[1], 0.0, 1.0]
        if kind == 'rect':
            assert target == 'RECT', target
            return [x / scale[0], y / scale[1], 0.0, 1.0]
        assert target == '2D', target
        return [x / scale[0], y / scale[1], 0.0, 1.0]

    lines = program.strip().splitlines()
    assert lines[0] == '!!ARBfp1.0' and lines[-1] == 'END', program
    for line in lines[1:-1]:
        if line.startswith('OPTION '):
            continue
        if m := re.fullmatch(r'TEMP (\w+);', line):
            # Unwritten components read as zero, as on period drivers: the
            # bump path's ADD reads ret.zw before writing them (upstream).
            registers[m[1]] = [0.0] * 4
            continue
        if m := re.fullmatch(r'PARAM (\w+) = (.+);', line):
            source = m[2]
            if c := re.fullmatch(r'\{([^}]*)\}', source):
                params[m[1]] = [float(v) for v in c[1].split(',')]
            elif c := re.fullmatch(r'program\.local\[(\d+)\]', source):
                params[m[1]] = list(locals_.get(int(c[1]), [0.0, 0.0, 0.0, 0.0]))
            elif c := re.fullmatch(r'program\.env\[(\d+)\]', source):
                index = int(c[1])
                params[m[1]] = list(BUMPMAT) if 12 <= index < 20 else [0.0] * 4
            else:
                raise AssertionError(line)
            continue
        m = re.fullmatch(r'(\w+) ([^,]+), (.+);', line)
        assert m, line
        op, destination, operands = m[1], m[2], [s.strip() for s in m[3].split(',')]
        name, _, mask = destination.rpartition('.') if re.search(r'\.[xyzw]{1,4}$', destination) \
            else (destination, '', 'xyzw')
        saturate = op.endswith('_SAT')
        op = op.removesuffix('_SAT')
        if op in ('TEX', 'TXP'):
            unit = int(re.fullmatch(r'texture\[(\d)\]', operands[1])[1])
            result = sample(value(operands[0]), unit, operands[2], op == 'TXP')
        elif op == 'SWZ':
            source = value(operands[0])
            result = [float(c) if c in '01' else source['xyzw'.index(c)] for c in operands[1:]]
        else:
            values = [value(s) for s in operands]
            if op == 'MOV':
                result = values[0]
            elif op == 'ADD':
                result = [a + b for a, b in zip(*values)]
            elif op == 'SUB':
                result = [a - b for a, b in zip(*values)]
            elif op == 'MUL':
                result = [a * b for a, b in zip(*values)]
            elif op == 'MAD':
                result = [a * b + c for a, b, c in zip(*values)]
            elif op == 'DP3':
                result = [sum(a * b for a, b in zip(values[0][:3], values[1][:3]))] * 4
            else:
                raise AssertionError(f'unexpected instruction {line!r}')
        if saturate:
            result = [min(max(v, 0.0), 1.0) for v in result]
        if name == 'result.color':
            registers.setdefault(name, [math.nan] * 4)
        elif name not in registers:
            raise AssertionError(f'write to undeclared {name!r}\n{program}')
        for c in mask:
            registers[name]['xyzw'.index(c)] = result['xyzw'.index(c)]
    return registers['result.color']


def close(a, b):
    return all(math.isclose(x, y, abs_tol=1e-5) for x, y in zip(a, b))


def check(output):
    cases = {}
    for block in re.finditer(r'CASE ([\w-]+)\nPROGRAM\n(.*?)EOP\n(.*?)END CASE\n', output, re.S):
        locals_ = {int(m[1]): [float(v) for v in m[2].split()]
                   for m in re.finditer(r'LOCAL (\d+) (.+)', block[3])}
        cases[block[1]] = (block[2], locals_)
    assert len(cases) == 7, output
    expected = list(COORD[:2])
    checked = 0

    def color(name, texcoords, textures):
        nonlocal checked
        program, locals_ = cases[name]
        checked += 1
        return run(program, locals_, texcoords, textures)

    rect, padded = ('rect', SIZES['rect']), ('padded', SIZES['padded'])
    # The fixed-function vertex pipeline's texture matrix already scaled.
    scaled = (COORD[0] * 800.0, COORD[1] * 600.0, COORD[2], COORD[3])
    out = color('rect-ffp', [scaled] * 8, {0: rect})
    assert close(out[:2], expected), f'rect-ffp: sampled image point {out[:2]}, expected {expected}'
    assert 'np2coord' not in cases['rect-ffp'][0] and not cases['rect-ffp'][1]
    # A vertex shader's normalized coordinate reaches the same image point
    # (the Thief: Deadly Shadows post pass: the whole scene, not texel 0..1).
    for name, texture in (('rect-vs', rect), ('padded-vs', padded)):
        out = color(name, [COORD] * 8, {0: texture})
        assert close(out[:2], expected), f'{name}: sampled image point {out[:2]}, expected {expected}\n{cases[name][0]}'
    # Projected lookups divide the scaled coordinate.
    projected4 = (COORD[0] * Q, COORD[1] * Q, COORD[2] * Q, Q)
    out = color('rect-vs-proj4', [projected4] * 8, {0: rect})
    assert close(out[:2], expected), f'rect-vs-proj4: {out[:2]}\n{cases["rect-vs-proj4"][0]}'
    projected3 = (COORD[0] * Q, COORD[1] * Q, Q, 7.0)   # count3: z is the divisor, w junk
    out = color('rect-vs-proj3', [projected3] * 8, {0: rect})
    assert close(out[:2], expected), f'rect-vs-proj3: {out[:2]}\n{cases["rect-vs-proj3"][0]}'
    # Bump mapping: the displacement is in normalized units too.
    bumped = [COORD[0] + BUMPMAT[0] * BUMP[0] + BUMPMAT[2] * BUMP[1],
              COORD[1] + BUMPMAT[1] * BUMP[0] + BUMPMAT[3] * BUMP[1]]
    out = color('bump-rect-vs', [COORD] * 8, {0: ('bump', None), 1: rect})
    assert close(out[:2], bumped), f'bump-rect-vs: {out[:2]}, expected {bumped}\n{cases["bump-rect-vs"][0]}'
    # A later stage scales by its own local; stage 0 (power of two) is untouched.
    out = color('second-stage-vs', [COORD] * 8, {0: ('padded', (1.0, 1.0)), 1: rect})
    assert close(out[:2], [2 * expected[0], 2 * expected[1]]), (
        f'second-stage-vs: {out[:2]}\n{cases["second-stage-vs"][0]}')
    assert set(cases['second-stage-vs'][1]) == {1}, cases['second-stage-vs'][1]
    return checked


def function(source, prefix):
    start = source.index(prefix)
    return source[start:source.index('\n}', start) + 2]


def block(source, prefix):
    start = source.index(prefix)
    return source[start:source.index('\n};', start) + 3]


def preamble(private, wined3d_h):
    texture_op = block(wined3d_h, 'enum wined3d_texture_op\n{')
    ta = '\n'.join(re.findall(r'#define WINED3DTA_\w+\s+0x[0-9a-f]+', wined3d_h))
    fixup = private[private.index('enum fixup_channel_source\n{'):private.index('static inline struct color_fixup_desc create_color_fixup_desc(')]
    fixup = fixup.replace('#include <pshpack2.h>\n', '').replace('#include <poppack.h>\n', '')
    complex_ = function(private, 'static inline BOOL is_complex_fixup(') + function(
        private, 'static inline enum complex_fixup get_complex_fixup(')
    masks = '\n'.join(re.findall(r'#define WINED3DSP_WRITEMASK_\w+\s+0x[0-9a-f]+u', private))
    types = [block(private, 'enum wined3d_gl_resource_type\n{'), block(private, 'enum wined3d_ffp_ps_fog_mode\n{'),
             block(private, 'enum projection_types\n{'), block(private, 'enum dst_arg\n{'),
             '#define ARG_UNUSED          0xff\n', block(private, 'struct texture_stage_op\n{'),
             block(private, 'struct ffp_frag_settings\n{')]
    return '\n'.join([PREAMBLE, texture_op, ta, fixup, complex_, masks, *types])


ARB_FUNCTIONS = ('static void shader_arb_append_imm_vec4(', 'static const char *shader_arb_get_fixup_swizzle(',
                 'struct color_fixup_masks\n{', 'static struct color_fixup_masks calc_color_correction(',
                 'static void gen_color_correction(', 'static void arbfp_add_sRGB_correction(',
                 'static void arbfp_add_linear_fog(', 'static const char *get_argreg(', 'static void gen_ffp_instr(',
                 'static const char *arbfp_texture_target(', 'static BOOL arbfp_shadow(',
                 'static void arbfp_load_np2_fixup(', 'static GLuint gen_arbfp_ffp_shader(')


def build(work, name, arb, head, expect_pass):
    pieces = [head, '\n'.join(re.findall(r'#define ARB_FFP_CONST_\w+.*', arb))]
    for prefix in ARB_FUNCTIONS:
        pieces.append(block(arb, prefix) if prefix.endswith('{') else function(arb, prefix))
    path = work / (name + '.c')
    path.write_text('\n'.join(pieces) + TEST)
    executable = work / name
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    '-Wno-unused-parameter', '-Wno-sign-compare', '-Wno-missing-field-initializers',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-g',
                    str(path), '-o', str(executable), '-lm'], check=True)
    result = subprocess.run([str(executable)], capture_output=True, text=True, check=True)
    try:
        checked = check(result.stdout)
    except AssertionError as error:
        if expect_pass:
            raise
        return (str(error).strip().splitlines() or ['(assertion)'])[0]
    assert expect_pass, (name, 'not rejected')
    return checked


PLAIN_FIXUP = ('        } else if (settings->op[stage].vs_np2_fixup) {\n'
               '            shader_addline(&buffer, "MUL ret, fragment.texcoord[%u], np2coord%u;\\n", stage, stage);\n'
               '            shader_addline(&buffer, "%s tex%u, ret, texture[%u], %s;\\n",\n'
               '                            instr, stage, stage, textype);\n')
MUTATIONS = {
    # Before 0064: the vertex shader's coordinate went straight to the lookup.
    'unscaled': ('arb', PLAIN_FIXUP, ''),
    'unscaled-projected': ('arb', '            shader_addline(&buffer, "MOV ret.w, ret.z;\\n");\n'
                           '            if (settings->op[stage].vs_np2_fixup)\n'
                           '                shader_addline(&buffer, "MUL ret.xy, ret, np2coord%u;\\n", stage);\n',
                           '            shader_addline(&buffer, "MOV ret.w, ret.z;\\n");\n'),
    'unscaled-bump': ('arb', '            }\n            if (settings->op[stage].vs_np2_fixup)\n'
                      '                shader_addline(&buffer, "MUL ret.xy, ret, np2coord%u;\\n", stage);\n',
                      '            }\n'),
    # Scaling every rectangle double-scales the texture matrix's coordinates.
    'every-rectangle': ('arb', '        } else if (settings->op[stage].vs_np2_fixup) {\n',
                        '        } else if (settings->op[stage].tex_type == WINED3D_GL_RES_TYPE_TEX_RECT) {\n'),
    # As the texture matrix does: only the coordinate scaled, not the displacement.
    'unscaled-displacement': ('arb', '                shader_addline(&buffer, "ADD ret, ret, fragment.texcoord[%u];\\n", stage);\n'
                              '            }\n            if (settings->op[stage].vs_np2_fixup)\n'
                              '                shader_addline(&buffer, "MUL ret.xy, ret, np2coord%u;\\n", stage);\n',
                              '                shader_addline(&buffer, settings->op[stage].vs_np2_fixup\n'
                              '                        ? "MAD ret, fragment.texcoord[%u], np2coord%u, ret;\\n"\n'
                              '                        : "ADD ret, ret, fragment.texcoord[%u];\\n", stage, stage);\n'
                              '            }\n'),
    'scaled-w': ('arb', '        scale[3] = 1.0f;\n', '        scale[3] = scale[0];\n'),
    'texture-height': ('arb', '        scale[1] = texture->pow2_matrix[5];\n', '        scale[1] = texture->pow2_matrix[1];\n'),
    'first-local': ('arb', '"PARAM np2coord%u = program.local[%u];\\n", stage, stage);',
                    '"PARAM np2coord%u = program.local[%u];\\n", stage, 0u);'),
}


def gen_ffp_frag_op_contract(utils):
    gen = function(utils, 'void gen_ffp_frag_op(')
    # The texture matrix's condition (get_texture_matrix / transform_texture):
    # a pow2_matrix scale exists and the texture matrix is skipped.
    assert re.search(r'settings->op\[i\]\.vs_np2_fixup = texture && !ignore_textype && use_vs\(state\)\s+'
                     r'&& !\(texture->flags & WINED3D_TEXTURE_POW2_MAT_IDENT\);', gen), gen
    disabled = gen[gen.index('if (state->texture_states[i][WINED3D_TSS_COLOR_OP] == WINED3D_TOP_DISABLE)'):]
    assert 'settings->op[i].vs_np2_fixup = 0;' in disabled[:disabled.index('break;')]
    matrix = function(utils, 'void get_texture_matrix(')
    assert 'multiply_matrix(mat, mat, (struct wined3d_matrix *)state->textures[tex]->pow2_matrix);' in matrix
    assert 'if (!use_ps(state))' in matrix


def loading_contract(arb, state_c):
    # The VS toggle already reselects the fixed-function program (vertexdeclaration
    # applies FOGVERTEXMODE, whose arbfp handler is state_arbfp_fog).
    declaration = function(state_c, 'static void vertexdeclaration(')
    assert 'context_apply_state(context, state, STATE_RENDER(WINED3D_RS_FOGVERTEXMODE));' in declaration
    assert re.search(r'if \(context->last_was_vshader\)\s+\{\s+updateFog = TRUE;', declaration)
    assert re.search(r'if\(!context->last_was_vshader\) \{', declaration)
    assert '{STATE_RENDER(WINED3D_RS_FOGVERTEXMODE),              { STATE_RENDER(WINED3D_RS_FOGENABLE),' in arb
    fog = function(arb, 'static void state_arbfp_fog(')
    assert 'fragment_prog_arbfp(context, state, state_id);' in fog
    # Binding a program loads its scales; the stage mask covers enabled stages.
    select = function(arb, 'static void fragment_prog_arbfp(')
    bind = select.index('GL_EXTCALL(glBindProgramARB(GL_FRAGMENT_PROGRAM_ARB, desc->shader));')
    load = select.index('arbfp_load_np2_fixup(gl_info, state, priv->ffp_np2_fixup);')
    assert bind < select.index('priv->ffp_np2_fixup = 0;') < load
    assert 'if (desc->parent.settings.op[i].vs_np2_fixup)\n                priv->ffp_np2_fixup |= 1u << i;' in select
    # sampler() flags NP2 textures for a constant reload; without a pixel
    # shader the reload goes to the bound fixed-function program.
    sampler = function(state_c, 'static void sampler(')
    assert re.search(r'if \(!\(texture->flags & WINED3D_TEXTURE_POW2_MAT_IDENT\)\)\s+'
                     r'context->constant_update_mask \|= WINED3D_SHADER_CONST_PS_NP2_FIXUP;', sampler)
    constants = function(arb, 'static void shader_arb_load_constants_internal(')
    tail = constants[constants.index('    if (usePixelShader)\n'):]
    assert re.search(r'\n    else if \(priv->ffp_np2_fixup && \(context->constant_update_mask & '
                     r'WINED3D_SHADER_CONST_PS_NP2_FIXUP\)\)\n    \{\n.*?'
                     r'arbfp_load_np2_fixup\(gl_info, state, priv->ffp_np2_fixup\);', tail, re.S), tail
    # The application's constants stay in program.env: the scales are locals.
    loader = function(arb, 'static void arbfp_load_np2_fixup(')
    assert 'glProgramEnvParameter' not in loader


with tempfile.TemporaryDirectory(prefix='dreamgpu-ffp-np2-') as directory:
    work = Path(directory)
    for name in json.loads(MANIFEST.read_text())['files']:
        destination = work / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'vendor/wine9x' / name, destination)
    patches.apply(work, MANIFEST)
    arb = (work / 'wined3d/arb_program_shader.c').read_text()
    utils = (work / 'wined3d/utils.c').read_text()
    private = (work / 'wined3d/wined3d_private.h').read_text()
    state_c = (ROOT / 'vendor/wine9x/wined3d/state.c').read_text()
    if (work / 'wined3d/state.c').exists():
        state_c = (work / 'wined3d/state.c').read_text()
    wined3d_h = (ROOT / 'vendor/wine9x/include/wine/wined3d.h').read_text()

    gen_ffp_frag_op_contract(utils)
    loading_contract(arb, state_c)
    head = preamble(private, wined3d_h)
    checked = build(work, 'fixed', arb, head, True)
    print(f'PASS prepared fixed-function NP2 coordinates: vertex shader coordinates reach the rectangle and padded '
          f'image points, projected, bump-mapped and later stages included; texture-matrix coordinates unscaled '
          f'({checked} programs)')
    for name, (_, old, new) in MUTATIONS.items():
        assert arb.count(old) == 1, name
        print(name, 'rejected:', build(work, name, arb.replace(old, new), head, False))
    for name, (old, new) in {
        'any-vertex-pipeline': ('use_vs(state)\n                && !(texture->flags & WINED3D_TEXTURE_POW2_MAT_IDENT);',
                                'TRUE\n                && !(texture->flags & WINED3D_TEXTURE_POW2_MAT_IDENT);'),
        'rectangles-only': ('&& !(texture->flags & WINED3D_TEXTURE_POW2_MAT_IDENT);',
                            '&& texture->target == GL_TEXTURE_RECTANGLE_ARB;'),
    }.items():
        assert utils.count(old) == 1, name
        try:
            gen_ffp_frag_op_contract(utils.replace(old, new))
        except AssertionError:
            print(name, 'rejected: gen_ffp_frag_op key')
        else:
            raise AssertionError((name, 'not rejected'))
    print(f'PASS all {len(MUTATIONS) + 2} mutations are rejected')
