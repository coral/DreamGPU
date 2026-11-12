#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute the prepared ARB vertex program fog epilogue under table fog (0062).

With FOGTABLEMODE LINEAR/EXP/EXP2 and a vertex shader, NVIDIA (before 61.77)
and ATI drivers of 2003-2004 ran the fog coordinate a vs_1_x/vs_2_x shader
writes to oFog through the table fog equation with the application's
FOGSTART/FOGEND/FOGDENSITY; the reference rasterizer and later drivers use the
depth. Far Cry's NVIDIA path (Wine bug 25761) and Prince of Persia: The Sands
of Time (bug 17423) rely on the former, and DreamGPU reports a GeForce FX.
WineD3D wrote the vertex output z as the fog coordinate, so Far Cry's terrain,
water and boat were drawn in the fog colour.

The prepared vshader_add_footer, need_helper_const and arb_get_helper_value
generate the epilogue under ASan/UBSan for vs_1_1, vs_2_0 and vs_3_0 shaders
with and without oFog, built-in and declared varyings, and table or vertex
fog. A small ARB_vertex_program interpreter runs each generated epilogue on a
clip-space position and an oFog value; the prepared state_arbfp_fog and
get_fog_start_end choose the fog source and the GL start/end, and the
ARB_fog_linear/exp/exp2 equations give the fragment fog factor. It must be the
oFog value through the table equation, unclamped (start 0, end 4, oFog 2 is
half fogged), for vs_1_1/vs_2_0 shaders that write oFog; the vertex output z
for shaders that do not, for vs_3_0 and for declared varyings; and the clamped
oFog factor with FOGTABLEMODE NONE, as before. The position stays exact.
Mutations of the patch (and of the fog source it depends on) must fail.
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
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
typedef int BOOL;
typedef unsigned int DWORD, UINT;
typedef unsigned short WORD;
typedef unsigned char BYTE;
#define TRUE 1
#define FALSE 0
#define TRACE(...) ((void)0)
#define WARN(...) ((void)0)
#define FIXME(...) ((void)0)
#define ERR(...) (fprintf(stderr, __VA_ARGS__), assert(0))

enum wined3d_shader_type { WINED3D_SHADER_TYPE_PIXEL, WINED3D_SHADER_TYPE_VERTEX, WINED3D_SHADER_TYPE_GEOMETRY };
enum arb_helper_value { ARB_ZERO, ARB_ONE, ARB_TWO, ARB_0001, ARB_EPS, ARB_VS_REL_OFFSET };
enum fog_src_type { VS_FOG_Z = 0, VS_FOG_COORD = 1 };
enum fogsource { FOGSOURCE_FFP, FOGSOURCE_VS, FOGSOURCE_COORD };
enum { NV_VERTEX_PROGRAM, NV_VERTEX_PROGRAM2_OPTION, ARB_VERTEX_PROGRAM, EXTENSION_COUNT };
#define WINED3D_QUIRK_NV_CLIP_BROKEN 0x1
#define WINED3D_QUIRK_ARB_VS_OFFSET_LIMIT 0x2
#define WINED3D_QUIRK_SET_TEXCOORD_W 0x4
enum { WINED3D_RS_FOGENABLE = 28, WINED3D_RS_FOGCOLOR = 34, WINED3D_RS_FOGTABLEMODE = 35,
       WINED3D_RS_FOGSTART = 36, WINED3D_RS_FOGEND = 37, WINED3D_RS_FOGVERTEXMODE = 140 };
enum { WINED3D_FOG_NONE = 0, WINED3D_FOG_EXP = 1, WINED3D_FOG_EXP2 = 2, WINED3D_FOG_LINEAR = 3 };
#define STATE_RENDER(a) (a)
#define STATE_SHADER(a) (1000 + (a))

struct wined3d_string_buffer { char text[4096]; size_t length; };
static void shader_addline(struct wined3d_string_buffer *buffer, const char *format, ...)
{
    va_list args;
    int written;
    va_start(args, format);
    written = vsnprintf(buffer->text + buffer->length, sizeof(buffer->text) - buffer->length, format, args);
    va_end(args);
    assert(written >= 0 && (size_t)written < sizeof(buffer->text) - buffer->length);
    buffer->length += written;
}

struct wined3d_gl_info { BOOL supported[EXTENSION_COUNT]; DWORD quirks; struct { UINT clipplanes; } limits; };
struct wined3d_shader_reg_maps
{
    struct { enum wined3d_shader_type type; BYTE major, minor; } shader_version;
    DWORD fog : 1, usesnrm : 1, usespow : 1, usesmova : 1;
};
struct arb_vshader_private { int rel_offset; };
struct vs_compile_args { BYTE fog_src; BYTE clip_enabled : 1; };
struct arb_vs_compile_args
{
    struct vs_compile_args super;
    union { struct { WORD bools; unsigned char clip_texcoord, clipplane_mask; } boolclip; DWORD boolclip_compare; } clip;
};
struct shader_arb_ctx_priv
{
    enum { ARB, NV2, NV3 } target_version;
    DWORD vs_clipplanes;
    BOOL footer_written;
    const char *fog_output;
};

static BOOL need_rel_addr_const(const struct arb_vshader_private *shader_data,
        const struct wined3d_shader_reg_maps *reg_maps, const struct wined3d_gl_info *gl_info)
{
    return shader_data->rel_offset || reg_maps->usesmova;
}

struct wined3d_state { DWORD render_states[256]; BOOL vertex_shader; };
struct wined3d_context { const struct wined3d_gl_info *gl_info; BOOL last_was_rhw; enum fogsource fog_source; };
#define use_vs(state) ((state)->vertex_shader)
#define isStateDirty(context, id) ((id) == STATE_SHADER(WINED3D_SHADER_TYPE_PIXEL))
static void fragment_prog_arbfp(struct wined3d_context *context, const struct wined3d_state *state, DWORD state_id)
{
    assert(0);
}
static int fog_updates;
static float gl_fog_start, gl_fog_end;
void get_fog_start_end(const struct wined3d_context *context, const struct wined3d_state *state,
        float *start, float *end);
static void state_fogstartend(struct wined3d_context *context, const struct wined3d_state *state, DWORD state_id)
{
    assert(state_id == STATE_RENDER(WINED3D_RS_FOGSTART));
    get_fog_start_end(context, state, &gl_fog_start, &gl_fog_end);
    ++fog_updates;
}
'''

TEST = r'''
static DWORD bits(float value)
{
    DWORD result;
    memcpy(&result, &value, sizeof(result));
    return result;
}

/* name, shader model, writes oFog, fog output register, fog_src. */
static void footer(const char *name, BYTE major, BOOL fog, const char *fog_output, BYTE fog_src)
{
    struct wined3d_gl_info gl_info;
    struct wined3d_shader_reg_maps reg_maps;
    struct arb_vshader_private shader_data;
    struct arb_vs_compile_args args;
    struct shader_arb_ctx_priv priv;
    struct wined3d_string_buffer buffer;

    memset(&gl_info, 0, sizeof(gl_info));
    memset(&reg_maps, 0, sizeof(reg_maps));
    memset(&shader_data, 0, sizeof(shader_data));
    memset(&priv, 0, sizeof(priv));
    memset(&buffer, 0, sizeof(buffer));
    gl_info.limits.clipplanes = 8;
    gl_info.supported[ARB_VERTEX_PROGRAM] = TRUE;
    reg_maps.shader_version.type = WINED3D_SHADER_TYPE_VERTEX;
    reg_maps.shader_version.major = major;
    reg_maps.shader_version.minor = major == 1 ? 1 : 0;
    reg_maps.fog = fog;
    priv.target_version = ARB;
    priv.fog_output = fog_output;
    memset(&args, 0, sizeof(args));
    args.super.fog_src = fog_src;
    vshader_add_footer(&priv, &shader_data, &args, &reg_maps, &gl_info, &buffer);
    assert(priv.footer_written);
    printf("FOOTER %s %u %d %d\n%sEND\n", name, major, fog, fog_src, buffer.text);
}

static void source(DWORD table, float start, float end)
{
    struct wined3d_gl_info gl_info;
    struct wined3d_context context;
    struct wined3d_state state;

    memset(&gl_info, 0, sizeof(gl_info));
    gl_info.supported[ARB_VERTEX_PROGRAM] = TRUE;
    context.gl_info = &gl_info;
    context.last_was_rhw = FALSE;
    context.fog_source = (enum fogsource)99;
    memset(&state, 0, sizeof(state));
    state.vertex_shader = TRUE;
    state.render_states[WINED3D_RS_FOGENABLE] = TRUE;
    state.render_states[WINED3D_RS_FOGTABLEMODE] = table;
    state.render_states[WINED3D_RS_FOGVERTEXMODE] = WINED3D_FOG_NONE;
    state.render_states[WINED3D_RS_FOGSTART] = bits(start);
    state.render_states[WINED3D_RS_FOGEND] = bits(end);
    fog_updates = 0;
    state_arbfp_fog(&context, &state, STATE_RENDER(WINED3D_RS_FOGENABLE));
    assert(fog_updates == 1);
    printf("SOURCE %u %.9g %.9g %d %.9g %.9g\n", table, start, end, context.fog_source, gl_fog_start, gl_fog_end);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    for (BYTE major = 1; major <= 3; ++major)
        for (BOOL fog = FALSE; fog <= TRUE; ++fog)
            for (BYTE fog_src = VS_FOG_Z; fog_src <= VS_FOG_COORD; ++fog_src)
            {
                footer("builtin", major, fog, "TMP_FOGCOORD", fog_src);
                footer("declared", major, fog, "result.texcoord[7]", fog_src);
            }
    for (DWORD table = WINED3D_FOG_NONE; table <= WINED3D_FOG_LINEAR; ++table)
    {
        source(table, 0.0f, 1.0f);
        source(table, 0.0f, 4.0f);
        source(table, 250.0f, 900.0f);
    }
    return 0;
}
'''

# A tiny ARB_vertex_program interpreter for the statements the epilogue emits.
WRITTEN_FOG = (7.0, 8.0, 9.0)  # oFog lands in .x; the other components hold junk


def evaluate(program, position, fog_value, writes_fog):
    registers = {
        'TMP_OUT': list(position),
        'posFixup': [1.0, -1.0, 0.0009613037, 0.0012817383],
        'helper_const': [0.0, 1.0, 2.0, 1e-8],
        'TA': [float('nan')] * 4,
        'result.fogcoord': [float('nan')] * 4,
        'result.position': [float('nan')] * 4,
        'result.texcoord[7]': [float('nan')] * 4,
    }
    if writes_fog:
        registers['TMP_FOGCOORD'] = [fog_value, *WRITTEN_FOG]

    def source(text):
        negate = text.startswith('-')
        text = text.lstrip('-')
        name, _, swizzle = text.rpartition('.') if re.search(r'\.[xyzw]{1,4}$', text) else (text, '', 'xyzw')
        if name not in registers:
            raise AssertionError(f'read of undeclared or unknown register {name!r}')
        value = registers[name]
        swizzle = (swizzle * 4)[:4] if len(swizzle) == 1 else swizzle
        out = [value['xyzw'.index(c)] for c in swizzle]
        return [-v for v in out] if negate else out

    for line in program.strip().splitlines():
        match = re.fullmatch(r'(\w+) ([^,]+), (.+);', line)
        assert match, line
        op, destination, operands = match[1], match[2], [s.strip() for s in match[3].split(',')]
        name, _, mask = destination.rpartition('.') if re.search(r'\.[xyzw]{1,4}$', destination) else (destination, '', 'xyzw')
        if name not in registers:
            raise AssertionError(f'write to unknown register {name!r}')
        values = [source(s) for s in operands]
        if op == 'MOV':
            result = values[0]
        elif op == 'ADD':
            result = [a + b for a, b in zip(*values)]
        elif op == 'MUL':
            result = [a * b for a, b in zip(*values)]
        elif op == 'MAD':
            result = [a * b + c for a, b, c in zip(*values)]
        elif op == 'MIN':
            result = [min(a, b) for a, b in zip(*values)]
        elif op == 'MAX':
            result = [max(a, b) for a, b in zip(*values)]
        else:
            raise AssertionError(f'unexpected instruction {line!r}')
        for c in mask:
            registers[name]['xyzw'.index(c)] = result['xyzw'.index(c)]
    return registers


def fragment_fog(mode, coordinate, start, end, density):
    """ARB_fog_linear/exp/exp2 (GL fog factor, 1 = unfogged)."""
    if mode == 'linear':
        factor = (end - coordinate) / (end - start)
    elif mode == 'exp':
        factor = math.exp(-density * coordinate)
    else:
        factor = math.exp(-(density * coordinate) ** 2)
    return min(max(factor, 0.0), 1.0)


TABLE = {0: None, 1: 'exp', 2: 'exp2', 3: 'linear'}
POSITION = (0.3, -0.2, 0.6, 1.5)  # clip space: vertex output z 0.6, z/w 0.4


def check(output):
    footers, sources = {}, {}
    for block in re.finditer(r'FOOTER (\w+) (\d) (\d) (\d)\n(.*?)END\n', output, re.S):
        footers[(block[1], int(block[2]), bool(int(block[3])), int(block[4]))] = block[5]
    for line in re.finditer(r'SOURCE (\d+) (\S+) (\S+) (\d+) (\S+) (\S+)', output):
        sources[(int(line[1]), float(line[2]), float(line[3]))] = (int(line[4]), float(line[5]), float(line[6]))
    assert len(footers) == 24 and len(sources) == 12, output

    checked = 0
    for (table, start, end), (fog_source, gl_start, gl_end) in sources.items():
        if TABLE[table]:
            # Table fog: the application's start and end (FOGSOURCE_FFP).
            assert (fog_source, gl_start, gl_end) == (0, start, end), (
                f'table {table}: fog source {fog_source}, GL start {gl_start} end {gl_end}, expected FFP {start} {end}')
        else:
            assert (fog_source, gl_start, gl_end) == (1, 1.0, 0.0), (table, fog_source, gl_start, gl_end)
        mode = TABLE[table] or 'linear'
        fog_src = 0 if TABLE[table] else 1  # find_vs_compile_args
        for (kind, major, writes_fog, src), program in footers.items():
            if src != fog_src:
                continue
            for value in (0.0, 0.25, 1.0, 2.0, -0.5):
                for density in (0.5, 1.0):
                    registers = evaluate(program, POSITION, value, writes_fog)
                    x, y, z, w = POSITION
                    expected_position = [x + w * 0.0009613037, -y + w * 0.0012817383, 2 * z - w, w]
                    assert all(math.isclose(a, b, abs_tol=1e-6) for a, b in
                               zip(registers['result.position'], expected_position)), (kind, major, program)
                    actual = fragment_fog(mode, registers['result.fogcoord'][0], gl_start, gl_end, density)
                    if not TABLE[table]:
                        # Vertex fog (unchanged): oFog is the factor, clamped;
                        # without oFog fully fogged. vs_3_0 and declared
                        # varyings write oFog elsewhere and are not modelled.
                        if writes_fog and major < 3 and kind == 'builtin':
                            expected = min(max(value, 0.0), 1.0)
                        elif not writes_fog:
                            expected = 0.0
                        else:
                            continue
                    elif writes_fog and major < 3 and kind == 'builtin':
                        # Period drivers: oFog through the table equation, unclamped.
                        expected = fragment_fog(mode, value, start, end, density)
                    else:
                        # No oFog, vs_3_0 or declared varyings: the vertex output z.
                        expected = fragment_fog(mode, POSITION[2], start, end, density)
                    assert math.isclose(actual, expected, abs_tol=1e-6), (
                        f'table {table} start {start} end {end} density {density} {kind} vs_{major} '
                        f'oFog {writes_fog}={value}: fog factor {actual}, expected {expected}\n{program}')
                    checked += 1
    # The probe's cases: oFog 1 fully fogged, 0 clear, under LINEAR 0..1.
    program = footers[('builtin', 1, True, 0)]
    for value, factor in ((1.0, 0.0), (0.0, 1.0), (0.25, 0.75)):
        coordinate = evaluate(program, POSITION, value, True)['result.fogcoord'][0]
        assert math.isclose(fragment_fog('linear', coordinate, 0.0, 1.0, 1.0), factor), (value, coordinate)
    return checked


def function(source, prefix):
    start = source.index(prefix)
    return source[start:source.index('\n}', start) + 2]


FOOTER_FOG = ('        if (reg_maps->fog && reg_maps->shader_version.major < 3\n'
              '                && !strcmp(priv_ctx->fog_output, "TMP_FOGCOORD"))\n'
              '            shader_addline(buffer, "MOV result.fogcoord, TMP_FOGCOORD.x;\\n");\n'
              '        else\n'
              '            shader_addline(buffer, "MOV result.fogcoord, TMP_OUT.z;\\n");\n')
MUTATIONS = {
    # Before 0062: the vertex output z whatever the shader wrote.
    'vertex-output-z': ('arb', FOOTER_FOG, '        shader_addline(buffer, "MOV result.fogcoord, TMP_OUT.z;\\n");\n'),
    'clamped-coordinate': ('arb', '            shader_addline(buffer, "MOV result.fogcoord, TMP_FOGCOORD.x;\\n");\n',
                           '        {\n            shader_addline(buffer, "MIN TMP_FOGCOORD.x, TMP_FOGCOORD.x, helper_const.y;\\n");\n'
                           '            shader_addline(buffer, "MAX result.fogcoord.x, TMP_FOGCOORD.x, helper_const.x;\\n");\n        }\n'),
    'any-shader': ('arb', 'if (reg_maps->fog && reg_maps->shader_version.major < 3\n',
                   'if (reg_maps->shader_version.major < 3\n'),
    'shader-model-3': ('arb', 'if (reg_maps->fog && reg_maps->shader_version.major < 3\n', 'if (reg_maps->fog\n'),
    'declared-varyings': ('arb', '\n                && !strcmp(priv_ctx->fog_output, "TMP_FOGCOORD"))', ')'),
    'wrong-component': ('arb', '"MOV result.fogcoord, TMP_FOGCOORD.x;\\n"', '"MOV result.fogcoord, TMP_FOGCOORD.w;\\n"'),
    'shader-fog-source': ('arb', '    else\n    {\n        new_source = FOGSOURCE_FFP;\n    }\n',
                          '    else\n    {\n        new_source = use_vs(state) ? FOGSOURCE_VS : FOGSOURCE_FFP;\n    }\n'),
}


def build(work, name, arb, utils, expect_pass):
    program = (PREAMBLE + function(arb, 'static const char *arb_get_helper_value(')
               + function(arb, 'static inline BOOL use_nv_clip(')
               + function(arb, 'static BOOL need_helper_const(')
               + function(arb, 'static void vshader_add_footer(')
               + function(utils, 'void get_fog_start_end(')
               + function(arb, 'static void state_arbfp_fog(') + TEST)
    path = work / (name + '.c')
    path.write_text(program)
    executable = work / name
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    '-Wno-unused-parameter', '-Wno-sign-compare', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-g', str(path), '-o', str(executable),
                    '-lm'], check=True)
    result = subprocess.run([str(executable)], capture_output=True, text=True, check=True)
    try:
        checked = check(result.stdout)
    except AssertionError as error:
        if expect_pass:
            raise
        return (str(error).strip().splitlines() or ['(assertion)'])[0]
    assert expect_pass, (name, 'not rejected')
    return checked


with tempfile.TemporaryDirectory(prefix='dreamgpu-table-fog-') as directory:
    work = Path(directory)
    for name in json.loads(MANIFEST.read_text())['files']:
        destination = work / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'vendor/wine9x' / name, destination)
    patches.apply(work, MANIFEST)
    arb = (work / 'wined3d/arb_program_shader.c').read_text()
    utils = (work / 'wined3d/utils.c').read_text()
    shader = (ROOT / 'vendor/wine9x/wined3d/shader.c').read_text()

    # Table fog selects VS_FOG_Z for vertex shaders (find_vs_compile_args,
    # unpatched), and the fog coordinate is written before the position fixup.
    compile_args = function(shader, 'void find_vs_compile_args(')
    assert re.search(r'args->fog_src = state->render_states\[WINED3D_RS_FOGTABLEMODE\]\s*'
                     r'== WINED3D_FOG_NONE \? VS_FOG_COORD : VS_FOG_Z;', compile_args)
    footer_source = function(arb, 'static void vshader_add_footer(')
    assert footer_source.index('TMP_FOGCOORD.x;') < footer_source.index('MOV result.position, TMP_OUT;')
    # oFog of vs_1_x/vs_2_x shaders lands in TMP_FOGCOORD with built-in varyings.
    assert 'rastout_reg_names[] = {"TMP_OUT", "TMP_FOGCOORD", "result.pointsize"}' in arb
    assert 'priv_ctx->fog_output = "TMP_FOGCOORD";' in arb

    checked = build(work, 'fixed', arb, utils, True)
    print(f'PASS prepared table fog: oFog of vs_1_x/vs_2_x through the table equation unclamped, vertex output z '
          f'otherwise, vertex fog clamped, position exact ({checked} cases)')
    for name, (_, old, new) in MUTATIONS.items():
        assert arb.count(old) == 1, name
        print(name, 'rejected:', build(work, name, arb.replace(old, new), utils, False))
    print(f'PASS all {len(MUTATIONS)} mutations are rejected')
