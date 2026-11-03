#!/usr/bin/env python3
"""Patch 0029 on actual pinned Wine source: with ARB_fragment_program WineD3D
selects its ARB shader backend, arbfp fragment pipeline and arbfp blitter.
Check the prepared source keeps indexed textures CPU-expanded with identity
fixups and alpha-zero keys, never drives vertex programs DreamGPU lacks,
emits grammar-valid fixed-function programs, keeps NP2 fixups out of the
application's constants and keeps depth uploads independent of programs."""
import importlib.util
from pathlib import Path
import re
import shutil
import tempfile

ROOT = Path(__file__).resolve().parents[3]
HERE = ROOT / 'tests/guest/support'
spec = importlib.util.spec_from_file_location('dreamgpu_wine_prepare', HERE / 'wine_prepare.py')
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)


def function(text, signature):
    """The body of the first function whose definition starts with signature."""
    start = text.index(signature)
    open_brace = text.index('\n{', start)
    assert ';' not in text[start:open_brace], signature
    depth, at = 0, open_brace + 1
    while True:
        if text[at] == '{':
            depth += 1
        elif text[at] == '}':
            depth -= 1
            if not depth:
                return text[open_brace:at + 1]
        at += 1


with tempfile.TemporaryDirectory(prefix='dreamgpu-arbfp-') as temporary:
    work = Path(temporary) / 'wine'
    output = Path(temporary) / 'output'
    shutil.copytree(ROOT / 'vendor/wine9x', work, ignore=shutil.ignore_patterns('.git'))
    output.mkdir()
    prepare.prepare(work, output, False)
    arb = (work / 'wined3d/arb_program_shader.c').read_text()
    utils = (work / 'wined3d/utils.c').read_text()
    texture = (work / 'wined3d/texture.c').read_text()
    surface = (work / 'wined3d/surface.c').read_text()

    # Fixed-function replacement programs: every OPTION precedes the first
    # PARAM/TEMP/ATTRIB statement (colour key plus fog used to fail to parse).
    ffp = function(arb, 'static GLuint gen_arbfp_ffp_shader(')
    options = [m.start() for m in re.finditer(r'"OPTION ARB_fog_', ffp)]
    statements = [m.start() for m in re.finditer(r'"(?:PARAM|TEMP|ATTRIB) ', ffp)]
    assert options and statements and max(options) < min(statements), (options, statements[:3])
    assert 'color_key_low = program.env' in ffp[max(options):]
    # gen_color_correction() swizzles sign-fixed sources (V8U8 bump maps) through TA.
    assert 'sign_fixup_src = "TA";' in function(arb, 'static void gen_color_correction(')
    assert '"TEMP TA;\\n"' in ffp
    # Projected bump mapping multiplies and adds: a three-operand MUL is invalid.
    assert not re.search(r'"MUL[^"]*,[^"]*,[^"]*,[^"]*;', arb)
    assert 'MAD ret.xyz, ret, fragment.texcoord[%u].w, fragment.texcoord[%u];' in arb

    # NP2 texture-coordinate fixups are program locals on both sides; program
    # environment parameters are the application's c# constants.
    load = function(arb, 'static void shader_arb_load_np2fixup_constants(')
    assert 'glProgramLocalParameter4fvARB' in load and 'glProgramEnvParameter4fvARB' not in load
    assert 'PARAM np2fixup[%u] = { program.local[%u..%u] };' in arb
    assert 'program.env[%u..%u] };' not in arb.split('np2fixup', 1)[1].split('\n', 40)[0]

    # Vertex shaders exist (VertexShaderVersion 0) but never reach GL without
    # ARB_vertex_program: no vertex program binds, enables or constant loads.
    select = function(arb, 'static void shader_arb_select(void *shader_priv, struct wined3d_context *context,\n        const struct wined3d_state *state)\n{')
    assert 'if (use_vs(state) && gl_info->supported[ARB_VERTEX_PROGRAM])' in select
    constants = function(arb, 'static void shader_arb_load_constants(void *shader_priv')
    assert 'use_vs(state) && context->gl_info->supported[ARB_VERTEX_PROGRAM]' in constants
    for line in arb.splitlines():
        if 'use_vs(state)' in line and 'BOOL' not in line and 'if' in line:
            assert 'ARB_VERTEX_PROGRAM' in line, line

    # Indexed surfaces, the front buffer included, always expand to BGRA on
    # the CPU with an identity fixup, so P8 textures stay advertised.
    assert 'create_complex_fixup_desc(COMPLEX_FIXUP_P8)' not in utils
    conversion = function(utils, 'const struct wined3d_color_key_conversion * wined3d_format_get_color_key_conversion(')
    assert re.search(r'if \(format->id == WINED3DFMT_P8_UINT\)\s+return &convert_p8;', conversion)
    assert re.search(r'\n    gl_info->formats\[getFmtIdx\(WINED3DFMT_P8_UINT\)\]\.flags\[WINED3D_GL_RES_TYPE_TEX_2D\]'
                     r'\s+\|= WINED3DFMT_FLAG_TEXTURE \| WINED3DFMT_FLAG_FILTERING;', utils)
    # Their keys become alpha zero on the CPU, even with shader colour keying,
    # and the shader key matches exactly those texels.
    assert texture.count('(!d3d_info->shader_color_key || texture->resource.format->id == WINED3DFMT_P8_UINT)') == 2
    key = function(utils, 'void wined3d_format_get_float_color_key(')
    p8 = key[key.index('case WINED3DFMT_P8_UINT:'):]
    p8 = p8[:p8.index('break;')]
    assert 'float_colors[0].a = 0.0f;' in p8 and 'float_colors[1].a = 0.5f / 255.0f;' in p8
    assert 'float_colors[1].r = 2.0f;' in p8

    # Native ARB temporaries can exceed the D3D9 PS 2.0 range.
    directx = (work / 'wined3d/directx.c').read_text()
    assert re.search(r'temp_count = min\(WINED3DPS20_MAX_NUMTEMPS,\s+max\(12, adapter->gl_info\.limits\.arb_ps_temps\)\);', directx)

    # CPU depth uploads draw pixels with any program left by the last draw disabled.
    transfer = function(surface, 'static HRESULT surface_transfer_depth(')
    upload = transfer[transfer.index('if (upload)'):transfer.index('glDrawPixels')]
    assert 'p_glDisable(GL_FRAGMENT_PROGRAM_ARB)' in upload
    assert 'GL_ENABLE_BIT' in transfer
print('PASS arbfp adaptations: P8 expansion and keys, vertex-program guards, grammar, NP2 locals, depth uploads')
