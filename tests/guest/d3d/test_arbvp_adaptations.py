#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Patch 0033 on actual pinned Wine source: with GL_ARB_vertex_program WineD3D's
ARB backend compiles vertex shaders (VertexShaderVersion 1.1, 2.0 with 256
native vertex env parameters). Check the prepared source maps the SHORT2
generic attribute entry point, clamps VS 2.0 NumTemps, sizes points per vertex
only for shaders writing oPts, keeps CPU depth uploads independent of vertex
programs, and executes the zero-attribute client array Wine binds without
ARB_vertex_buffer_object under ASan/UBSan. The line templates of the generated
vs_1_1/vs_2_0 programs the native QEMU test parses on Mesa
(tests/native_qemu/vertex_program.rs) must still be the generator's."""
import importlib.util
import os
from pathlib import Path
import re
import shutil
import subprocess
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
                return text[start:at + 1]
        at += 1


TEST = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned int GLenum, GLuint;
typedef unsigned char GLubyte;
typedef float GLfloat;
typedef short GLshort;
typedef void GLvoid;
#define GL_BYTE 0x1400
#define GL_UNSIGNED_BYTE 0x1401
#define GL_SHORT 0x1402
#define GL_UNSIGNED_SHORT 0x1403
#define GL_FLOAT 0x1406
#define max(a, b) ((a) > (b) ? (a) : (b))
#define ERR(...) fprintf(stderr, __VA_ARGS__)
%s
int main(void)
{
    const GLfloat zero[4] = {0, 0, 0, 0}, ones[4] = {1, 1, 1, 1};
    const GLshort pair[2] = {-3, 7};
    const GLfloat *f;
    const GLshort *s;
    unsigned int i;
    /* At least 65536 values cover every 16-bit index of a small draw. */
    f = zv_client_array(GL_FLOAT, 6, 4, zero);
    assert(f);
    for (i = 0; i < 65536 * 4; ++i)
        assert(f[i] == 0);
    /* The same value reuses the array; another value refills it. */
    assert(zv_client_array(GL_FLOAT, 3, 4, zero) == (const GLvoid *)f);
    f = zv_client_array(GL_FLOAT, 6, 4, ones);
    for (i = 0; i < 65536 * 4; ++i)
        assert(f[i] == 1);
    /* Larger draws grow it; other element types repeat their exact bytes. */
    f = zv_client_array(GL_FLOAT, 70000, 3, ones);
    for (i = 0; i < 70000 * 3; ++i)
        assert(f[i] == 1);
    s = zv_client_array(GL_SHORT, 10, 2, pair);
    for (i = 0; i < 65536; ++i)
        assert(s[2 * i] == -3 && s[2 * i + 1] == 7);
    /* Impossible requests have no array (the draw then fails as a GL error). */
    assert(!zv_client_array(GL_FLOAT, 6, 5, zero));
    assert(!zv_client_array(0x140a, 6, 1, zero));
    assert(!zv_client_array(GL_FLOAT, 0x01000000, 4, zero));
    puts("PASS prepared zero-attribute client array: values, reuse, growth, types and bounds");
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='dreamgpu-arbvp-') as temporary:
    work = Path(temporary) / 'wine'
    output = Path(temporary) / 'output'
    shutil.copytree(ROOT / 'vendor/wine9x', work, ignore=shutil.ignore_patterns('.git'))
    output.mkdir()
    prepare.prepare(work, output, False)
    arb = (work / 'wined3d/arb_program_shader.c').read_text()
    directx = (work / 'wined3d/directx.c').read_text()
    device = (work / 'wined3d/device.c').read_text()
    surface = (work / 'wined3d/surface.c').read_text()

    # The SHORT2 attribute entry point exists only under its ARB name: the
    # core-name pointer stayed NULL and stride-zero or immediate SHORT2
    # attributes called it.
    assert 'MAP_GL_FUNCTION(glVertexAttrib2sv, glVertexAttrib2svARB);' in directx
    assert 'USE_GL_FUNC(glVertexAttrib2svARB)' in directx
    # Native ARB temporaries can exceed the D3D9 VS 2.0 range.
    assert re.search(r'VS20Caps\.temp_count = min\(WINED3DVS20_MAX_NUMTEMPS,\s+max\(12, adapter->gl_info\.limits\.arb_vs_temps\)\);',
                     directx)

    # Vertex shaders are live: no guard hides them once ARB_vertex_program
    # exists, and the program size follows oPts.
    select = function(arb, 'static void shader_arb_select(void *shader_priv, struct wined3d_context *context,\n        const struct wined3d_state *state)\n{')
    vertex = select[select.index('if (use_vs(state) && gl_info->supported[ARB_VERTEX_PROGRAM])'):]
    vertex = vertex[:vertex.index('\n    else\n')]
    assert 'p_glEnable(GL_VERTEX_PROGRAM_ARB)' in vertex
    assert re.search(r'if \(vs->reg_maps\.point_size\)\s+gl_info->gl_ops\.gl\.p_glEnable\(GL_VERTEX_PROGRAM_POINT_SIZE_ARB\);'
                     r'\s+else\s+gl_info->gl_ops\.gl\.p_glDisable\(GL_VERTEX_PROGRAM_POINT_SIZE_ARB\);', vertex)
    caps = function(arb, 'static void shader_arb_get_caps(')
    assert 'else if (vs_consts >= 256)' in caps and 'vs_version = 2;' in caps

    # CPU depth uploads draw pixels at a raster position no vertex program
    # transforms; ENABLE_BIT restores the mode.
    transfer = function(surface, 'static HRESULT surface_transfer_depth(')
    upload = transfer[transfer.index('if (upload)'):transfer.index('glRasterPos3f')]
    assert 'p_glDisable(GL_VERTEX_PROGRAM_ARB)' in upload
    assert 'p_glDisable(GL_FRAGMENT_PROGRAM_ARB)' in upload
    assert 'GL_ENABLE_BIT' in transfer

    # Without VBOs the zero attribute is a client array; VBO hosts keep
    # their buffer path.
    bind = function(device, 'void zv_bind(struct wined3d_context *context,')
    client = bind[bind.index('if (!gl_info->supported[ARB_VERTEX_BUFFER_OBJECT])'):bind.index('else')]
    assert 'zv_client_array(' in client and 'glBindBufferARB' not in client
    assert 'glBindBufferARB' in bind[bind.index('else'):]
    body = (function(device, 'static GLuint zv_value_el_size(') + '\n'
            + function(device, 'static const GLvoid *zv_client_array('))
    (work / 'test.c').write_text(TEST % body)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-O1', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    str(work / 'test.c'), '-o', str(work / 'test')], check=True)
    subprocess.run([str(work / 'test')], check=True)

    # The generator still emits every line template of the vs_1_1 and vs_2_0
    # programs that tests/native_qemu/vertex_program.rs loads on Mesa.
    generate = function(arb, 'static GLuint shader_arb_generate_vshader(')
    footer = function(arb, 'static void vshader_add_footer(')
    declarations = function(arb, 'static void shader_generate_arb_declarations(')
    for template, where in [
        ('"!!ARBvp1.0\\n"', generate),
        ('"TEMP TMP_OUT;\\n"', generate),
        ('"PARAM helper_const = { 0.0, 1.0, 2.0, %s};\\n"', generate),
        ('"TEMP TA;\\n"', generate),
        ('"TEMP TB;\\n"', generate),
        ('"PARAM posFixup = program.local[%u];\\n"', generate),
        ('"MOV result.color.secondary, %s;\\n"', generate),
        ('"TEMP R%u;\\n"', declarations),
        ('"PARAM C%d = program.env[%d];\\n"', declarations),
        ('"ADD result.fogcoord, posFixup.x, -posFixup.x;\\n"', footer),
        ('"MOV TA, %s;\\n"', footer),
        ('"MOV result.texcoord[%u], TA;\\n"', footer),
        ('"MUL TA, posFixup, TMP_OUT.w;\\n"', footer),
        ('"ADD TMP_OUT.x, TMP_OUT.x, TA.z;\\n"', footer),
        ('"MAD TMP_OUT.y, TMP_OUT.y, posFixup.y, TA.w;\\n"', footer),
        ('"MAD TMP_OUT.z, TMP_OUT.z, %s, -TMP_OUT.w;\\n"', footer),
        ('"MOV result.position, TMP_OUT;\\n"', footer),
    ]:
        assert template in where, template
    assert 'sprintf(register_name, "vertex.attrib[%u]", reg->idx[0].offset);' in arb
    assert 'const char *swizzle_chars = fixup ? "zyxw" : "xyzw";' in arb
    native = (ROOT / 'tests/native_qemu/vertex_program.rs').read_text()
    for line in ('MOV result.color.primary, vertex.attrib[1].zyxw;',
                 'MOV result.texcoord[0], vertex.attrib[2];', 'MUL result.color.primary, vertex.attrib[1].zyxw, C4;',
                 'PARAM helper_const = { 0.0, 1.0, 2.0, 9.99999994e-09};', 'MOV result.texcoord[7], TA;'):
        assert line in native, line
print('PASS arbvp adaptations: SHORT2 entry point, VS 2.0 NumTemps, per-vertex point size, '
      'depth uploads, zero-attribute client array, generated program templates')
