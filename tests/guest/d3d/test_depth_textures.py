#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Patches 0050 and 0051 on actual pinned Wine source: Direct3D depth formats
become textures exactly where a GeForce FX 5800 allowed them, and their
lookups compare in ARB fragment programs.

With DG_CAP2_GL_DEPTH_TEXTURE DreamGPU reports GL_ARB_depth_texture,
GL_ARB_shadow and GL_ARB_fragment_program_shadow. WineD3D then keeps the
TEXTURE flag of every depth format whose table entry has it, and
CheckDeviceFormat(D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE) accepts D16, D24X8
and D24S8, as NVIDIA's "hardware shadow maps", but also D16_LOCKABLE, D32 and
the INTZ FourCC, and plain depth cube textures, which an FX never had (0050).
The prepared format assignment (init_format_texture_info's per-entry flags and
depth rule) runs here on the actual depth entries of format_texture_info for
DreamGPU before and after the capability, then the actual
wined3d_check_device_format answers every depth format as a texture, a cube
texture and a depth/stencil surface. A depth texture's lookups compare
(WINED3DFMT_FLAG_SHADOW), and an ARB fragment program must name a SHADOW
target for that, which upstream's ARB backends never did (0051): the
fixed-function replacement keys programs by a per-stage shadow bit and the
pixel shader backend by ps_compile_args.shadow; both emit
OPTION ARB_fragment_program_shadow before any statement and SHADOW1D, SHADOW2D
or SHADOWRECT. The program text checks run on the prepared source; the
selection helper runs under ASan/UBSan. It fails without the patches.
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

# Direct3D depth formats and their WineD3D names.
DEPTH = {
    'D16_LOCKABLE': 'WINED3DFMT_D16_LOCKABLE', 'D32': 'WINED3DFMT_D32_UNORM',
    'D15S1': 'WINED3DFMT_S1_UINT_D15_UNORM', 'D24S8': 'WINED3DFMT_D24_UNORM_S8_UINT',
    'D24X8': 'WINED3DFMT_X8D24_UNORM', 'D24X4S4': 'WINED3DFMT_S4X4_UINT_D24_UNORM',
    'D16': 'WINED3DFMT_D16_UNORM', 'D32F_LOCKABLE': 'WINED3DFMT_D32_FLOAT',
    'D24FS8': 'WINED3DFMT_S8_UINT_D24_FLOAT', 'INTZ': 'WINED3DFMT_INTZ',
}
# What a GeForce FX 5800 accepted as a depth texture (NVIDIA hardware shadow maps).
FX_TEXTURES = {'D16', 'D24X8', 'D24S8'}
# DreamGPU's extensions as WineD3D sees them, before and after the capability.
BEFORE = ['WINED3D_GL_EXT_NONE', 'EXT_FRAMEBUFFER_OBJECT', 'EXT_PACKED_DEPTH_STENCIL', 'EXT_TEXTURE3D',
          'ARB_TEXTURE_CUBE_MAP', 'ARB_TEXTURE_RECTANGLE', 'ARB_FRAGMENT_PROGRAM', 'SGIS_GENERATE_MIPMAP']
AFTER = BEFORE + ['ARB_DEPTH_TEXTURE', 'ARB_SHADOW', 'ARB_FRAGMENT_PROGRAM_SHADOW']

PREAMBLE = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef int BOOL, HRESULT;
typedef unsigned int UINT, DWORD;
#define TRUE 1
#define FALSE 0
#define WINED3D_OK 0
#define WINED3DOK_NOAUTOGEN 1
#define WINED3DERR_INVALIDCALL -1
#define WINED3DERR_NOTAVAILABLE -2
#define TRACE(...) ((void)0)
#define WARN(...) ((void)0)
#define FIXME(...) ((void)0)
#define ERR(...) ((void)0)
#define CDECL
#define ORM_BACKBUFFER 0
#define ORM_FBO 1
enum wined3d_gl_resource_type { WINED3D_GL_RES_TYPE_TEX_1D, WINED3D_GL_RES_TYPE_TEX_2D, WINED3D_GL_RES_TYPE_TEX_3D,
        WINED3D_GL_RES_TYPE_TEX_CUBE, WINED3D_GL_RES_TYPE_TEX_RECT, WINED3D_GL_RES_TYPE_BUFFER,
        WINED3D_GL_RES_TYPE_RB, WINED3D_GL_RES_TYPE_COUNT };
enum wined3d_resource_type { WINED3D_RTYPE_SURFACE = 1, WINED3D_RTYPE_VOLUME, WINED3D_RTYPE_TEXTURE,
        WINED3D_RTYPE_VOLUME_TEXTURE, WINED3D_RTYPE_CUBE_TEXTURE, WINED3D_RTYPE_BUFFER };
enum wined3d_device_type { WINED3D_DEVICE_TYPE_HAL = 1 };
enum wined3d_gl_extension { %(extensions)s, WINED3D_GL_EXT_COUNT };
enum wined3d_format_id { WINED3DFMT_X8R8G8B8 = 1, %(formats)s, WINED3DFMT_COUNT };
static struct { int offscreen_rendering_mode; } wined3d_settings;
struct wined3d_format { enum wined3d_format_id id; DWORD flags[WINED3D_GL_RES_TYPE_COUNT];
        UINT depth_size, stencil_size; };
struct wined3d_gl_info { BOOL supported[WINED3D_GL_EXT_COUNT]; struct wined3d_format formats[WINED3DFMT_COUNT]; };
struct wined3d_pixel_format { int unused; };
struct wined3d_adapter { struct wined3d_gl_info gl_info; UINT cfg_count; const struct wined3d_pixel_format *cfgs; };
struct wined3d { UINT adapter_count; DWORD flags; struct wined3d_adapter adapters[1]; };
struct format_texture_info { enum wined3d_format_id id; DWORD flags; enum wined3d_gl_extension extension; };
static const char *debug_d3ddevicetype(int v) { (void)v; return ""; }
static const char *debug_d3dformat(int v) { (void)v; return ""; }
static const char *debug_d3dusage(DWORD v) { (void)v; return ""; }
static const char *debug_d3dusagequery(DWORD v) { (void)v; return ""; }
static const char *debug_d3dresourcetype(int v) { (void)v; return ""; }
static const struct wined3d_format *wined3d_get_format(const struct wined3d_gl_info *gl_info,
        enum wined3d_format_id id)
{
    return &gl_info->formats[id];
}
/* Plain surfaces and render targets are not what changes here. */
static BOOL CheckSurfaceCapability(const struct wined3d_adapter *a, const struct wined3d_format *af,
        const struct wined3d_format *f, BOOL no3d)
{
    (void)a; (void)af; (void)f; (void)no3d;
    return TRUE;
}
static BOOL CheckRenderTargetCapability(const struct wined3d_adapter *a, const struct wined3d_format *af,
        const struct wined3d_format *f, enum wined3d_gl_resource_type t)
{
    (void)a; (void)af; (void)f; (void)t;
    return TRUE;
}
static BOOL wined3d_check_pixel_format_color(const struct wined3d_gl_info *g, const struct wined3d_pixel_format *c,
        const struct wined3d_format *f)
{
    (void)g; (void)c; (void)f;
    return FALSE;
}
static BOOL wined3d_check_pixel_format_depth(const struct wined3d_gl_info *g, const struct wined3d_pixel_format *c,
        const struct wined3d_format *f)
{
    (void)g; (void)c; (void)f;
    return FALSE;
}
'''

TEST = r'''
static const struct format_texture_info entries[] = {
%(entries)s
};
static const struct { enum wined3d_format_id id; UINT depth, stencil; const char *name; } sizes[] = {
%(sizes)s
};

/* init_format_texture_info's per-entry flag assignment and depth rule, in
 * table order, for the extensions in `supported`. */
static void assign(struct wined3d_gl_info *gl_info)
{
    unsigned int i, n;
    memset(gl_info->formats, 0, sizeof(gl_info->formats));
    for (n = 0; n < sizeof(sizes) / sizeof(*sizes); ++n)
    {
        gl_info->formats[sizes[n].id].id = sizes[n].id;
        gl_info->formats[sizes[n].id].depth_size = sizes[n].depth;
        gl_info->formats[sizes[n].id].stencil_size = sizes[n].stencil;
    }
    for (i = 0; i < sizeof(entries) / sizeof(*entries); ++i)
    {
        const struct format_texture_info *format_texture_info = entries;
        struct wined3d_format *format;
        if (!gl_info->supported[entries[i].extension])
            continue;
        format = &gl_info->formats[entries[i].id];
%(assign)s
%(depth)s
    }
    /* check_fbo_compat's answers on DreamGPU: a depth format attaches as a
     * 2D texture, a rectangle and a renderbuffer. */
    for (n = 0; n < WINED3DFMT_COUNT; ++n)
    {
        struct wined3d_format *format = &gl_info->formats[n];
        unsigned int t;
        for (t = 0; t < WINED3D_GL_RES_TYPE_COUNT; ++t)
            if ((t == WINED3D_GL_RES_TYPE_TEX_2D || t == WINED3D_GL_RES_TYPE_TEX_RECT || t == WINED3D_GL_RES_TYPE_RB)
                    && format->flags[t] & (WINED3DFMT_FLAG_DEPTH | WINED3DFMT_FLAG_STENCIL))
                format->flags[t] |= WINED3DFMT_FLAG_FBO_ATTACHABLE;
    }
}

%(code)s

static struct wined3d d3d;

static HRESULT check(DWORD usage, enum wined3d_resource_type type, enum wined3d_format_id id)
{
    return wined3d_check_device_format(&d3d, 0, WINED3D_DEVICE_TYPE_HAL, WINED3DFMT_X8R8G8B8, usage, type, id);
}

int main(void)
{
    static const char *const extensions[2][16] = {{%(before)s, NULL}, {%(after)s, NULL}};
    struct wined3d_gl_info *gl_info = &d3d.adapters[0].gl_info;
    unsigned int pass, n, i;
    d3d.adapter_count = 1;
    wined3d_settings.offscreen_rendering_mode = ORM_FBO;
    for (pass = 0; pass < 2; ++pass)
    {
        memset(gl_info->supported, 0, sizeof(gl_info->supported));
        for (i = 0; extensions[pass][i]; ++i)
            gl_info->supported[%(lookup)s] = TRUE;
        assign(gl_info);
        printf("%%s ARB_depth_texture:\n", pass ? "with" : "without");
        for (n = 0; n < sizeof(sizes) / sizeof(*sizes); ++n)
        {
            const enum wined3d_format_id id = sizes[n].id;
            const struct wined3d_format *format = &gl_info->formats[id];
            const BOOL texture = check(WINED3DUSAGE_DEPTHSTENCIL, WINED3D_RTYPE_TEXTURE, id) == WINED3D_OK;
            const BOOL plain = check(0, WINED3D_RTYPE_TEXTURE, id) == WINED3D_OK;
            const BOOL cube = check(0, WINED3D_RTYPE_CUBE_TEXTURE, id) == WINED3D_OK;
            const BOOL volume = check(0, WINED3D_RTYPE_VOLUME_TEXTURE, id) == WINED3D_OK;
            const BOOL surface = check(WINED3DUSAGE_DEPTHSTENCIL, WINED3D_RTYPE_SURFACE, id) == WINED3D_OK;
            const BOOL fx = %(fx)s;
            printf("  %%-14s texture %%s, cube %%s, volume %%s, depth surface %%s%%s\n", sizes[n].name,
                    texture ? "yes" : "no ", cube ? "yes" : "no ", volume ? "yes" : "no ", surface ? "yes" : "no",
                    texture && format->flags[WINED3D_GL_RES_TYPE_TEX_2D] & WINED3DFMT_FLAG_SHADOW
                    ? " (shadow map: compares)" : "");
            /* Exactly the FX's three, and only once the capability exists; a
             * texture of them is a shadow map. Cube and volume depth textures
             * never exist; depth surfaces are unchanged by the capability. */
            assert(texture == (pass && fx));
            assert(plain == texture);
            assert(!cube && !volume);
            assert(!texture || format->flags[WINED3D_GL_RES_TYPE_TEX_2D] & WINED3DFMT_FLAG_SHADOW);
            assert(!texture || format->flags[WINED3D_GL_RES_TYPE_TEX_RECT] & WINED3DFMT_FLAG_TEXTURE);
            if (!pass)
                surface_before[n] = surface;
            else
                assert(surface == surface_before[n]);
        }
    }
    return 0;
}
'''

SHADOW_TEST = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef int BOOL;
#define TRUE 1
#define FALSE 0
enum wined3d_gl_resource_type { WINED3D_GL_RES_TYPE_TEX_1D, WINED3D_GL_RES_TYPE_TEX_2D, WINED3D_GL_RES_TYPE_TEX_3D,
        WINED3D_GL_RES_TYPE_TEX_CUBE, WINED3D_GL_RES_TYPE_TEX_RECT, WINED3D_GL_RES_TYPE_BUFFER,
        WINED3D_GL_RES_TYPE_RB, WINED3D_GL_RES_TYPE_COUNT };
enum wined3d_gl_extension { ARB_FRAGMENT_PROGRAM_SHADOW, WINED3D_GL_EXT_COUNT };
struct wined3d_gl_info { BOOL supported[WINED3D_GL_EXT_COUNT]; };
struct color_fixup_desc { unsigned short padding; };
%(op)s
%(target)s
%(shadow)s
int main(void)
{
    struct wined3d_gl_info gl_info = {{TRUE}}, without = {{FALSE}};
    struct texture_stage_op op;
    unsigned int type;
    memset(&op, 0, sizeof(op));
    for (type = 0; type < WINED3D_GL_RES_TYPE_COUNT; ++type)
    {
        op.tex_type = type;
        op.shadow = 0;
        assert(!arbfp_shadow(&op, &gl_info));
        op.shadow = 1;
        /* SHADOW1D, SHADOW2D and SHADOWRECT exist; no cube or 3D depth textures. */
        assert(arbfp_shadow(&op, &gl_info) == (type == WINED3D_GL_RES_TYPE_TEX_1D
                || type == WINED3D_GL_RES_TYPE_TEX_2D || type == WINED3D_GL_RES_TYPE_TEX_RECT));
        assert(!arbfp_shadow(&op, &without));
    }
    /* The bit lives in the key's former padding: the key keeps its size. */
    assert(sizeof(op) == %(size)u);
    puts("PASS arbfp shadow selection: SHADOW1D/2D/RECT only with ARB_fragment_program_shadow");
    return 0;
}
'''


def block(text, start):
    """text[start:] up to the brace closing its first opening brace."""
    depth, at = 0, text.index('{', start)
    while True:
        if text[at] == '{':
            depth += 1
        elif text[at] == '}':
            depth -= 1
            if not depth:
                return text[start:at + 1]
        at += 1


def function(source, prefix):
    return block(source, source.index(prefix)) + '\n'


def split_entries(table):
    """The brace-delimited entries of a C array initializer."""
    table = re.sub(r'/\*.*?\*/', '', table, flags=re.S)
    table = table[table.index('{') + 1:table.rindex('}')]
    out, depth, start = [], 0, None
    for at, c in enumerate(table):
        if c == '{':
            depth += 1
            if depth == 1:
                start = at + 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                out.append(table[start:at])
    return out


def prepared():
    temporary = tempfile.TemporaryDirectory(prefix='dreamgpu-depth-textures-')
    work = Path(temporary.name)
    for name in json.loads(MANIFEST.read_text())['files']:
        p = work / name
        p.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'vendor/wine9x' / name, p)
    patches.apply(work, MANIFEST)
    return temporary, work


def compile_and_run(work, name, source):
    (work / (name + '.c')).write_text(source)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                    '-Wno-unused-function', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-fno-omit-frame-pointer', str(work / (name + '.c')), '-o', str(work / name)], check=True)
    return subprocess.run([str(work / name)], check=True, capture_output=True, text=True).stdout


temporary, work = prepared()
with temporary:
    wined3d = work / 'wined3d'
    utils = (wined3d / 'utils.c').read_text()
    directx = (wined3d / 'directx.c').read_text()
    arb = (wined3d / 'arb_program_shader.c').read_text()
    private = (wined3d / 'wined3d_private.h').read_text()
    gl_h = (wined3d / 'wined3d_gl.h').read_text()
    headers = private + (ROOT / 'vendor/wine9x/include/wine/wined3d.h').read_text()

    # The actual depth entries of format_texture_info and their sizes.
    table = utils[utils.index('static const struct wined3d_format_texture_info format_texture_info[] =\n{'):]
    table = table[table.index('{'):table.index('\n};\n') + 2]
    entries = []
    for entry in split_entries(table):
        fields = [f.strip() for f in entry.split(',')]
        if fields[0] in DEPTH.values():
            entries.append((fields[0], fields[7], fields[8]))
    assert {e[0] for e in entries} == set(DEPTH.values()), entries
    channels = utils[utils.index('static const struct wined3d_format_channels formats[] =\n{'):]
    channels = channels[channels.index('{'):channels.index('\n};\n') + 2]
    sizes = {}
    for entry in split_entries(channels):
        fields = [f.strip() for f in entry.split(',')]
        if fields[0] in DEPTH.values():
            sizes[fields[0]] = (int(fields[10]), int(fields[11]))
    assert set(sizes) == set(DEPTH.values())

    # init_format_texture_info: the per-type assignment and the depth rule.
    start = utils.index('        format->flags[WINED3D_GL_RES_TYPE_TEX_1D] |= format_texture_info[i].flags;')
    end = utils.index('format->flags[WINED3D_GL_RES_TYPE_RB] &= ~WINED3DFMT_FLAG_TEXTURE;\n', start)
    assign = utils[start:end + len('format->flags[WINED3D_GL_RES_TYPE_RB] &= ~WINED3DFMT_FLAG_TEXTURE;')]
    rule = utils.index('            /* DreamGPU: with ARB_depth_texture, a GeForce FX samples the D3D', start)
    first = utils.index('            if (format_texture_info[i].flags', rule)
    second = utils.index('            if (format_texture_info[i].flags', first + 1)
    depth_rule = block(utils, first) + '\n' + block(utils, second)
    assert 'ARB_DEPTH_TEXTURE' in depth_rule and 'WINED3DFMT_D24_UNORM_S8_UINT' in depth_rule

    code = function(directx, 'static BOOL CheckDepthStencilCapability(')
    code += function(directx, 'HRESULT CDECL wined3d_check_device_format(')

    extensions = sorted({e[2] for e in entries} | set(AFTER))
    formats = list(DEPTH.values())
    body = TEST % {
        'entries': ',\n'.join('    {%s, %s, %s}' % e for e in entries),
        'sizes': ',\n'.join('    {%s, %u, %u, "%s"}' % (DEPTH[k], *sizes[DEPTH[k]], k) for k in DEPTH),
        'assign': assign, 'depth': depth_rule, 'code': code,
        'before': ', '.join('"%s"' % e for e in BEFORE), 'after': ', '.join('"%s"' % e for e in AFTER),
        'lookup': 'extension(extensions[pass][i])',
        'fx': ' || '.join('id == %s' % DEPTH[k] for k in sorted(FX_TEXTURES)),
    }
    body = body.replace('int main(void)\n{', 'static BOOL surface_before[%u];\n'
                        'static enum wined3d_gl_extension extension(const char *name)\n{\n'
                        '    static const char *const names[] = {%s};\n'
                        '    unsigned int i;\n'
                        '    for (i = 0; strcmp(names[i], name); ++i) {}\n'
                        '    return (enum wined3d_gl_extension)i;\n}\n\nint main(void)\n{'
                        % (len(DEPTH), ', '.join('"%s"' % e for e in extensions)), 1)
    names = set(re.findall(r'\b(WINED3D(?:USAGE|FMT_FLAG)_\w+|WINED3D_NO3D)\b', body))
    values = dict(re.findall(r'^#define\s+(WINED3D\w+)\s+(0x[0-9a-fA-F]+|\d+)\b', headers, re.M))
    defines = '\n'.join('#define %s %s' % (n, values[n]) for n in sorted(names))
    preamble = PREAMBLE % {'extensions': ', '.join(extensions), 'formats': ', '.join(formats)}
    print(compile_and_run(work, 'formats', preamble + defines + '\n' + body), end='')
    print('PASS depth formats (0050): D16, D24X8 and D24S8 textures (shadow maps) only with ARB_depth_texture; '
          'no depth cube or volume textures; depth surfaces unchanged')

    # 0051: the extension is known, and its programs name SHADOW targets.
    assert '    ARB_FRAGMENT_PROGRAM_SHADOW,\n' in gl_h
    assert '{"GL_ARB_fragment_program_shadow",      ARB_FRAGMENT_PROGRAM_SHADOW   },' in directx
    op = private[private.index('struct texture_stage_op\n{'):]
    op = op[:op.index('};') + 2]
    # 0064 takes one more padding bit (vs_np2_fixup); the key stays 12 bytes.
    assert 'unsigned                shadow : 1;' in op and 'unsigned                padding : 8;' in op
    assert 'unsigned                vs_np2_fixup : 1;' in op
    target = function(arb, 'static const char *arbfp_texture_target(')
    shadow = function(arb, 'static BOOL arbfp_shadow(')
    # The key's size: three words of ops, the fixup, and one word of bits.
    original_op = (ROOT / 'vendor/wine9x/wined3d/wined3d_private.h').read_text()
    original_op = original_op[original_op.index('struct texture_stage_op\n{'):]
    assert 'padding : 10;' in original_op[:original_op.index('};')]
    print(compile_and_run(work, 'shadow', SHADOW_TEST % {'op': op, 'target': target, 'shadow': shadow,
                                                          'size': 12}), end='')

    # gen_ffp_frag_op keys every stage by its shadow bit, the disabled stage
    # included (the key is compared with memcmp).
    gen = function(utils, 'void gen_ffp_frag_op(')
    disabled = gen[gen.index('if (state->texture_states[i][WINED3D_TSS_COLOR_OP] == WINED3D_TOP_DISABLE)'):]
    assert 'settings->op[i].shadow = 0;' in disabled[:disabled.index('break;')]
    assert re.search(r'settings->op\[i\]\.shadow = \(texture = state->textures\[i\]\) && !ignore_textype\s+'
                     r'&& \(texture->resource\.format_flags & WINED3DFMT_FLAG_SHADOW\);', gen)
    # The fixed-function replacement: OPTION after the textures read are
    # known and before every statement; SHADOW targets for those stages.
    ffp = function(arb, 'static GLuint gen_arbfp_ffp_shader(')
    option = ffp.index('"OPTION ARB_fragment_program_shadow;\\n"')
    statements = [m.start() for m in re.finditer(r'"(?:PARAM|TEMP|ATTRIB|MOV|KIL) ', ffp)]
    assert ffp.index('lowest_disabled_stage = stage;') < option < min(statements)
    assert re.search(r'if \(tex_read\[stage\] && arbfp_shadow\(&settings->op\[stage\], gl_info\)\)', ffp)
    sampling = ffp[ffp.index('textype = arbfp_texture_target(settings->op[stage].tex_type);'):]
    sampling = sampling[:sampling.index('if(settings->op[stage].projected == proj_none)')]
    assert '"SHADOW1D"' in sampling and '"SHADOW2D"' in sampling and '"SHADOWRECT"' in sampling
    # The pixel shader backend: ps_compile_args.shadow (part of the compared
    # key) selects the OPTION and the targets of the sampled shadow textures.
    generate = function(arb, 'static GLuint shader_arb_generate_pshader(')
    option = generate.index('"OPTION ARB_fragment_program_shadow;\\n"')
    assert 'if (args->super.shadow && gl_info->supported[ARB_FRAGMENT_PROGRAM_SHADOW])' in generate
    assert generate.index('"!!ARBfp1.0\\n"') < option < generate.index('"TEMP TA;\\n"')
    sample = function(arb, 'static void shader_hw_sample(')
    mapping = sample[sample.index('if (pshader && (priv->cur_ps_args->super.shadow & (1u << sampler_idx))'):]
    mapping = mapping[:mapping.index('/* TEX, TXL, TXD and TXP')]
    for plain, shadow_target in (('1D', 'SHADOW1D'), ('2D', 'SHADOW2D'), ('RECT', 'SHADOWRECT')):
        assert re.search(r'if \(!strcmp\(tex_type, "%s"\)\)\s+tex_type = "%s";' % (plain, shadow_target), mapping)
    assert 'CUBE' not in mapping and '3D"' not in mapping
    assert sample.index('switch (resource_type)') < sample.index(mapping)
    find = function(arb, 'static struct arb_ps_compiled_shader *find_arb_pshader(')
    assert 'memcmp(&shader_data->gl_shaders[i].args, args, sizeof(*args))' in find
print('PASS prepared depth textures (0050, 0051)')
