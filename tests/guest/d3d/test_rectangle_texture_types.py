#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Patch 0049 on actual pinned Wine source: NPOT resources become rectangle
textures exactly where a rectangle can hold them.

With DG_CAP_GL_TEXTURE_RECTANGLE DreamGPU reports GL_ARB_texture_rectangle
without ARB_texture_non_power_of_two, so WineD3D reports what a GeForce FX
reaches through its NP2 quirk: TextureCaps POW2 | NONPOW2CONDITIONAL. Three
places then disagreed with the rectangle rules. init_format_texture_info gave
compressed formats rectangle flags, although ARB_texture_rectangle has no
compressed internal formats. resource_init (after 0042) skipped the 2D type of
every NPOT resource "to try texture rectangle", and because this tree disables
the texture usage check, a DXT texture took the rectangle anyway and a depth
buffer (no ARB_depth_texture) fell through to the renderbuffer 0042 removed.
surface_private_setup and wined3d_surface_update_desc unpadded every surface
whenever rectangles existed, also those of a padded 2D texture, whose
pow2_matrix texture_init computes for the padded size.

The prepared functions run here under ASan/UBSan: the TextureCaps computation
gives the FX answer for DreamGPU and for a real FX driver after the quirk;
NPOT colour textures, render targets and plain surfaces take an exact-size
rectangle, DXT and depth keep the padded 2D texture, a format the rectangle
cannot attach keeps 2D, and surface sizes follow the container's target. It
fails without the patch.
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
typedef int BOOL, HRESULT, LONG;
typedef unsigned int UINT, DWORD;
#define TRUE 1
#define FALSE 0
#define NULL ((void *)0)
#define WINED3D_OK 0
#define WINED3DERR_INVALIDCALL -1
#define WINED3DERR_OUTOFVIDEOMEMORY -2
#define E_OUTOFMEMORY -3
#define WINED3DERR_NOTAVAILABLE -4
#define TRACE(...) ((void)0)
#define WARN(...) ((void)0)
#define FIXME(...) ((void)0)
#define ERR(...) ((void)0)
#define WARN_(channel) WARN
#define ORM_BACKBUFFER 0
#define ORM_FBO 1
#define GL_TEXTURE_2D 0x0de1
#define GL_TEXTURE_RECTANGLE_ARB 0x84f5
enum { ARB_TEXTURE_NON_POWER_OF_TWO, ARB_TEXTURE_RECTANGLE, ARB_FRAMEBUFFER_OBJECT, EXT_FRAMEBUFFER_OBJECT,
        WINED3D_GL_NORMALIZED_TEXRECT, ARB_DEPTH_TEXTURE, EXT_TEXTURE3D, ARB_TEXTURE_CUBE_MAP, EXTENSIONS };
enum wined3d_resource_type { WINED3D_RTYPE_SURFACE = 1, WINED3D_RTYPE_VOLUME, WINED3D_RTYPE_TEXTURE,
        WINED3D_RTYPE_VOLUME_TEXTURE, WINED3D_RTYPE_CUBE_TEXTURE, WINED3D_RTYPE_BUFFER };
enum wined3d_pool { WINED3D_POOL_DEFAULT, WINED3D_POOL_MANAGED, WINED3D_POOL_SYSTEM_MEM, WINED3D_POOL_SCRATCH };
enum wined3d_multisample_type { WINED3D_MULTISAMPLE_NONE };
/* The D3D depth formats a GeForce FX samples (0050), apart from the ids here. */
enum { WINED3DFMT_D16_UNORM = 1001, WINED3DFMT_X8D24_UNORM, WINED3DFMT_D24_UNORM_S8_UINT };
enum wined3d_gl_resource_type { WINED3D_GL_RES_TYPE_TEX_1D, WINED3D_GL_RES_TYPE_TEX_2D, WINED3D_GL_RES_TYPE_TEX_3D,
        WINED3D_GL_RES_TYPE_TEX_CUBE, WINED3D_GL_RES_TYPE_TEX_RECT, WINED3D_GL_RES_TYPE_BUFFER,
        WINED3D_GL_RES_TYPE_RB, WINED3D_GL_RES_TYPE_COUNT };
static struct { int offscreen_rendering_mode; } wined3d_settings;
struct wined3d_format { int id; DWORD flags[WINED3D_GL_RES_TYPE_COUNT]; UINT block_width, block_height; };
struct wined3d_gl_limits { UINT texture_size; };
struct wined3d_gl_info { BOOL supported[EXTENSIONS]; struct wined3d_gl_limits limits; };
struct wined3d_adapter { struct wined3d_gl_info gl_info; };
struct wined3d { DWORD flags; };
struct wined3d_device { const struct wined3d *wined3d; struct wined3d_adapter *adapter; };
struct wined3d_parent_ops;
struct wined3d_resource_ops;
struct wined3d_resource {
    LONG ref;
    struct wined3d_device *device;
    enum wined3d_resource_type type;
    enum wined3d_gl_resource_type gl_type;
    const struct wined3d_format *format;
    DWORD format_flags;
    enum wined3d_multisample_type multisample_type;
    UINT multisample_quality;
    DWORD usage;
    enum wined3d_pool pool;
    DWORD access_flags;
    UINT width, height, depth, size;
    DWORD priority;
    void *parent;
    const struct wined3d_parent_ops *parent_ops;
    const struct wined3d_resource_ops *resource_ops;
    void *heap_memory;
    DWORD map_binding;
};
struct wined3d_texture { struct wined3d_resource resource; };
struct wined3d_surface {
    struct wined3d_resource resource;
    struct wined3d_texture *container;
    DWORD flags, locations;
    UINT pow2Width, pow2Height;
    unsigned int texture_target;
};
/* The texture info fields init_format_texture_info's flag assignment reads. */
struct format_texture_info { int id; DWORD flags; };
static const char *debug_d3dformat(int id) { (void)id; return ""; }
static BOOL wined3d_resource_allocate_sysmem(struct wined3d_resource *r) { (void)r; return TRUE; }
static void wined3d_resource_free_sysmem(struct wined3d_resource *r) { (void)r; }
static UINT wined3d_device_get_available_texture_mem(const struct wined3d_device *d) { (void)d; return ~0u; }
static void adapter_adjust_memory(struct wined3d_adapter *a, UINT size) { (void)a; (void)size; }
static void device_resource_add(struct wined3d_device *d, struct wined3d_resource *r) { (void)d; (void)r; }
static BOOL surface_use_pbo(const struct wined3d_surface *s) { (void)s; return FALSE; }
'''

TEST = r'''
/* init_format_texture_info's per-format flag assignment for one info entry,
 * including the ARB_depth_texture rule of the same loop. */
static void assign(struct wined3d_format *format, const struct format_texture_info *info,
        const struct wined3d_gl_info *gl_info)
{
    const struct format_texture_info *format_texture_info = info;
    const unsigned int i = 0;
    memset(format->flags, 0, sizeof(format->flags));
%(assign)s
%(depth)s
}

/* The TextureCaps computation of wined3d_get_device_caps. */
static DWORD texture_caps(const struct wined3d_gl_info *gl_info)
{
    struct { DWORD TextureCaps; } caps_storage, *caps = &caps_storage;
%(caps)s
    return caps->TextureCaps;
}

static DWORD d3d9_texture_caps(DWORD wined3d_caps)
{
    struct { DWORD TextureCaps; } caps_storage, *pCaps = &caps_storage;
    pCaps->TextureCaps = wined3d_caps;
%(d3d9_mask)s
    return pCaps->TextureCaps;
}

static struct wined3d d3d;
static struct wined3d_adapter adapter;
static struct wined3d_device device = {&d3d, &adapter};

static int create(const struct wined3d_format *format, DWORD usage, UINT width, UINT height)
{
    struct wined3d_resource resource;
    memset(&resource, 0, sizeof(resource));
    if (resource_init(&resource, &device, WINED3D_RTYPE_TEXTURE, format, WINED3D_MULTISAMPLE_NONE, 0, usage,
            WINED3D_POOL_DEFAULT, width, height, 1, 0, NULL, NULL, NULL) != WINED3D_OK)
        return -1;
    assert(resource.format_flags == format->flags[resource.gl_type]);
    return resource.gl_type;
}

/* surface_init's order: the container's target, then the private setup. */
static HRESULT setup(struct wined3d_surface *surface, DWORD container_flags, unsigned int target, UINT width,
        UINT height)
{
    static struct wined3d_texture container;
    memset(surface, 0, sizeof(*surface));
    memset(&container, 0, sizeof(container));
    container.resource.format_flags = container_flags;
    surface->container = &container;
    surface->resource.device = &device;
    surface->resource.width = width;
    surface->resource.height = height;
    surface->texture_target = target;
    return surface_private_setup(surface);
}

int main(void)
{
    const DWORD color_info = WINED3DFMT_FLAG_TEXTURE | WINED3DFMT_FLAG_RENDERTARGET | WINED3DFMT_FLAG_FILTERING;
    const DWORD dxt_info = WINED3DFMT_FLAG_TEXTURE | WINED3DFMT_FLAG_FILTERING | WINED3DFMT_FLAG_COMPRESSED;
    const DWORD depth_info = WINED3DFMT_FLAG_TEXTURE | WINED3DFMT_FLAG_DEPTH | WINED3DFMT_FLAG_STENCIL;
    const struct format_texture_info color_entry = {1, color_info}, dxt_entry = {2, dxt_info},
            depth_entry = {3, depth_info};
    struct wined3d_format color = {1, {0}, 1, 1}, dxt = {2, {0}, 4, 4}, depth = {3, {0}, 1, 1};
    struct wined3d_format unattachable = {4, {0}, 1, 1};
    struct wined3d_gl_info *gl_info = &adapter.gl_info, real_fx;
    const DWORD conditional = WINED3DPTEXTURECAPS_POW2 | WINED3DPTEXTURECAPS_NONPOW2CONDITIONAL;
    const DWORD usages[] = {WINED3DUSAGE_RENDERTARGET, WINED3DUSAGE_TEXTURE,
            WINED3DUSAGE_TEXTURE | WINED3DUSAGE_RENDERTARGET, WINED3DUSAGE_DYNAMIC, 0};
    DWORD before, after;
    struct wined3d_surface surface;
    unsigned int i;

    /* Caps. DreamGPU before: 3D and cube maps, no rectangles, no NPOT. */
    gl_info->supported[EXT_TEXTURE3D] = gl_info->supported[ARB_TEXTURE_CUBE_MAP] = TRUE;
    before = texture_caps(gl_info);
    assert((before & (conditional | WINED3DPTEXTURECAPS_SQUAREONLY)) == WINED3DPTEXTURECAPS_POW2);
    /* With rectangles: POW2 | NONPOW2CONDITIONAL and nothing else changes. */
    gl_info->supported[ARB_TEXTURE_RECTANGLE] = TRUE;
    after = texture_caps(gl_info);
    assert(after == (before | WINED3DPTEXTURECAPS_NONPOW2CONDITIONAL));
    assert(after & WINED3DPTEXTURECAPS_CUBEMAP_POW2 && after & WINED3DPTEXTURECAPS_VOLUMEMAP_POW2);
    /* A real GeForce FX: NPOT through GL 2.0 and rectangles; detection hides
     * rectangles behind NPOT, the FX quirk (0034) trades NPOT back for them. */
    memset(&real_fx, 0, sizeof(real_fx));
    real_fx.supported[EXT_TEXTURE3D] = real_fx.supported[ARB_TEXTURE_CUBE_MAP] = TRUE;
    real_fx.supported[ARB_TEXTURE_NON_POWER_OF_TWO] = real_fx.supported[ARB_TEXTURE_RECTANGLE] = TRUE;
    real_fx.texture_rectangle_hidden = FALSE;
%(hide)s
    quirk_no_np2(&real_fx);
    assert(texture_caps(&real_fx) == after);
    /* DreamGPU's detection and quirk keep its rectangles. */
    {
        struct wined3d_gl_info dreamgpu = *gl_info;
%(hide_dreamgpu)s
        quirk_no_np2(&dreamgpu);
        assert(dreamgpu.supported[ARB_TEXTURE_RECTANGLE] && !dreamgpu.supported[ARB_TEXTURE_NON_POWER_OF_TWO]);
        assert(texture_caps(&dreamgpu) == after);
    }
    printf("PASS TextureCaps: before %%#x (D3D9 %%#x), with rectangles %%#x (D3D9 %%#x), real FX after quirk %%#x\n",
            before, d3d9_texture_caps(before), after, d3d9_texture_caps(after), texture_caps(&real_fx));

    /* Format flags with rectangles and without ARB_depth_texture. */
    assign(&color, &color_entry, gl_info);
    assign(&dxt, &dxt_entry, gl_info);
    assign(&depth, &depth_entry, gl_info);
    assert(color.flags[WINED3D_GL_RES_TYPE_TEX_RECT] & WINED3DFMT_FLAG_TEXTURE);
    assert(dxt.flags[WINED3D_GL_RES_TYPE_TEX_2D] & WINED3DFMT_FLAG_TEXTURE);
    assert(!dxt.flags[WINED3D_GL_RES_TYPE_TEX_RECT]);
    assert(!(depth.flags[WINED3D_GL_RES_TYPE_TEX_RECT] & WINED3DFMT_FLAG_TEXTURE));
    /* check_fbo_compat's answers on DreamGPU: colour attaches as 2D, rectangle
     * and renderbuffer; depth as 2D and renderbuffer (no depth rectangles). */
    for (i = 0; i < WINED3D_GL_RES_TYPE_COUNT; ++i)
        if (i == WINED3D_GL_RES_TYPE_TEX_2D || i == WINED3D_GL_RES_TYPE_TEX_RECT || i == WINED3D_GL_RES_TYPE_RB)
            color.flags[i] |= WINED3DFMT_FLAG_FBO_ATTACHABLE;
    depth.flags[WINED3D_GL_RES_TYPE_TEX_2D] |= WINED3DFMT_FLAG_FBO_ATTACHABLE;
    depth.flags[WINED3D_GL_RES_TYPE_RB] |= WINED3DFMT_FLAG_FBO_ATTACHABLE;
    unattachable = color;
    unattachable.flags[WINED3D_GL_RES_TYPE_TEX_RECT] &= ~(WINED3DFMT_FLAG_RENDERTARGET | WINED3DFMT_FLAG_FBO_ATTACHABLE);

    /* Resource types under framebuffer objects. */
    wined3d_settings.offscreen_rendering_mode = ORM_FBO;
    gl_info->supported[EXT_FRAMEBUFFER_OBJECT] = TRUE;
    for (i = 0; i < sizeof(usages) / sizeof(*usages); ++i)
    {
        assert(create(&color, usages[i], 640, 480) == WINED3D_GL_RES_TYPE_TEX_RECT);
        assert(create(&color, usages[i], 800, 600) == WINED3D_GL_RES_TYPE_TEX_RECT);
        assert(create(&color, usages[i], 512, 256) == WINED3D_GL_RES_TYPE_TEX_2D);
    }
    assert(create(&dxt, 0, 640, 480) == WINED3D_GL_RES_TYPE_TEX_2D);
    assert(create(&dxt, WINED3DUSAGE_TEXTURE, 640, 480) == WINED3D_GL_RES_TYPE_TEX_2D);
    assert(create(&depth, WINED3DUSAGE_DEPTHSTENCIL, 800, 600) == WINED3D_GL_RES_TYPE_TEX_2D);
    assert(create(&depth, WINED3DUSAGE_DEPTHSTENCIL, 1024, 1024) == WINED3D_GL_RES_TYPE_TEX_2D);
    assert(create(&unattachable, WINED3DUSAGE_RENDERTARGET, 800, 600) == WINED3D_GL_RES_TYPE_TEX_2D);
    assert(create(&unattachable, 0, 800, 600) == WINED3D_GL_RES_TYPE_TEX_RECT);
    /* Without framebuffer objects attachability is not asked. */
    wined3d_settings.offscreen_rendering_mode = ORM_BACKBUFFER;
    gl_info->supported[EXT_FRAMEBUFFER_OBJECT] = FALSE;
    assert(create(&color, WINED3DUSAGE_RENDERTARGET, 800, 600) == WINED3D_GL_RES_TYPE_TEX_RECT);
    assert(create(&unattachable, WINED3DUSAGE_RENDERTARGET, 800, 600) == WINED3D_GL_RES_TYPE_TEX_2D);
    assert(create(&depth, WINED3DUSAGE_DEPTHSTENCIL, 800, 600) == WINED3D_GL_RES_TYPE_TEX_2D);
    wined3d_settings.offscreen_rendering_mode = ORM_FBO;
    gl_info->supported[EXT_FRAMEBUFFER_OBJECT] = TRUE;
    puts("PASS resource types: NPOT colour textures, targets and plain surfaces take rectangles; DXT, depth and "
         "rectangle-unattachable targets keep padded 2D");

    /* With ARB_depth_texture (0050) the GeForce FX's depth formats are
     * textures a rectangle can hold, and check_fbo_compat attaches them as
     * rectangles: NPOT depth-stencil surfaces become exact-size rectangles like
     * colour targets. Other depth formats (here D32-like) keep padded 2D. */
    {
        struct wined3d_format fx_depth = {WINED3DFMT_D24_UNORM_S8_UINT, {0}, 1, 1}, other_depth = {5, {0}, 1, 1};
        const struct format_texture_info fx_entry = {WINED3DFMT_D24_UNORM_S8_UINT, depth_info},
                other_entry = {5, depth_info};
        gl_info->supported[ARB_DEPTH_TEXTURE] = TRUE;
        assign(&fx_depth, &fx_entry, gl_info);
        assign(&other_depth, &other_entry, gl_info);
        assert(fx_depth.flags[WINED3D_GL_RES_TYPE_TEX_RECT] & WINED3DFMT_FLAG_TEXTURE);
        assert(!(other_depth.flags[WINED3D_GL_RES_TYPE_TEX_RECT] & WINED3DFMT_FLAG_TEXTURE));
        assert(!(fx_depth.flags[WINED3D_GL_RES_TYPE_TEX_CUBE] & WINED3DFMT_FLAG_TEXTURE));
        for (i = 0; i < WINED3D_GL_RES_TYPE_COUNT; ++i)
            if (i == WINED3D_GL_RES_TYPE_TEX_2D || i == WINED3D_GL_RES_TYPE_TEX_RECT || i == WINED3D_GL_RES_TYPE_RB)
            {
                fx_depth.flags[i] |= WINED3DFMT_FLAG_FBO_ATTACHABLE;
                other_depth.flags[i] |= WINED3DFMT_FLAG_FBO_ATTACHABLE;
            }
        assert(create(&fx_depth, WINED3DUSAGE_DEPTHSTENCIL, 800, 600) == WINED3D_GL_RES_TYPE_TEX_RECT);
        assert(create(&fx_depth, WINED3DUSAGE_DEPTHSTENCIL, 1024, 1024) == WINED3D_GL_RES_TYPE_TEX_2D);
        assert(create(&other_depth, WINED3DUSAGE_DEPTHSTENCIL, 800, 600) == WINED3D_GL_RES_TYPE_TEX_2D);
        gl_info->supported[ARB_DEPTH_TEXTURE] = FALSE;
        puts("PASS with ARB_depth_texture: NPOT D16/D24X8/D24S8 depth-stencil surfaces take exact-size rectangles, "
             "other depth formats keep padded 2D");
    }

    /* Surface sizes follow the container's target. */
    gl_info->limits.texture_size = 2048;
    assert(setup(&surface, 0, GL_TEXTURE_RECTANGLE_ARB, 800, 600) == WINED3D_OK);
    assert(surface.pow2Width == 800 && surface.pow2Height == 600 && !(surface.flags & SFLAG_NONPOW2));
    assert(setup(&surface, 0, GL_TEXTURE_2D, 800, 600) == WINED3D_OK);
    assert(surface.pow2Width == 1024 && surface.pow2Height == 1024 && surface.flags & SFLAG_NONPOW2);
    assert(setup(&surface, 0, GL_TEXTURE_2D, 1600, 1200) == WINED3D_OK);
    assert(surface.pow2Width == 2048 && surface.pow2Height == 2048);
    assert(setup(&surface, 0, GL_TEXTURE_RECTANGLE_ARB, 1600, 1200) == WINED3D_OK);
    assert(surface.pow2Width == 1600 && surface.pow2Height == 1200);
    /* A padded compressed level is unavailable, as without rectangles. */
    assert(setup(&surface, WINED3DFMT_FLAG_COMPRESSED, GL_TEXTURE_2D, 640, 480) == WINED3DERR_NOTAVAILABLE);
    assert(setup(&surface, WINED3DFMT_FLAG_COMPRESSED, GL_TEXTURE_2D, 512, 256) == WINED3D_OK);
    /* Full NPOT support unpads 2D textures. */
    gl_info->supported[ARB_TEXTURE_NON_POWER_OF_TWO] = TRUE;
    assert(setup(&surface, 0, GL_TEXTURE_2D, 800, 600) == WINED3D_OK);
    assert(surface.pow2Width == 800 && surface.pow2Height == 600);
    puts("PASS surface sizes: rectangles exact (800x600 stays 800x600), 2D textures padded unless NPOT");
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


with tempfile.TemporaryDirectory(prefix='dreamgpu-rect-types-') as temporary:
    work = Path(temporary)
    for name in json.loads(MANIFEST.read_text())['files']:
        p = work / name
        p.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'vendor/wine9x' / name, p)
    patches.apply(work, MANIFEST)
    wined3d = work / 'wined3d'
    resource = (wined3d / 'resource.c').read_text()
    surface = (wined3d / 'surface.c').read_text()
    utils = (wined3d / 'utils.c').read_text()
    directx = (wined3d / 'directx.c').read_text()
    d3d9 = (ROOT / 'vendor/wine9x/d3d9/directx.c').read_text()  # not patched

    code = ''.join(function(resource, prefix) for prefix in (
        'static DWORD resource_access_from_pool(', 'static void resource_check_usage(',
        'static BOOL resource_rect_usable(', 'HRESULT resource_init('))
    code += function(surface, 'static HRESULT surface_private_setup(')
    quirk = function(directx, 'static void quirk_no_np2(')
    # 0034's bookkeeping where full NPOT support hides rectangles.
    init = function(directx, 'static BOOL wined3d_adapter_init_gl_caps(')
    hide = init[init.index('    gl_info->texture_rectangle_hidden = FALSE;\n'):]
    hide = hide[:hide.index('\n    }\n') + len('\n    }')]
    # init_format_texture_info: the per-type flag assignment and the depth rule.
    start = utils.index('        format->flags[WINED3D_GL_RES_TYPE_TEX_1D] |= format_texture_info[i].flags;')
    end = utils.index('format->flags[WINED3D_GL_RES_TYPE_RB] &= ~WINED3DFMT_FLAG_TEXTURE;\n', start)
    assign = utils[start:end + len('format->flags[WINED3D_GL_RES_TYPE_RB] &= ~WINED3DFMT_FLAG_TEXTURE;')]
    # The depth rule (0050: no cube or volume depth textures; without
    # ARB_depth_texture no depth textures at all) is two statements.
    rule = utils.index('            if (format_texture_info[i].flags & (WINED3DFMT_FLAG_DEPTH', start)
    depth_rule = block(utils, rule)
    depth_rule += '\n' + block(utils, utils.index('            if (format_texture_info[i].flags', rule + 1))
    # wined3d_get_device_caps: the TextureCaps statements.
    caps_start = directx.index('    caps->TextureCaps   =')
    caps = directx[caps_start:directx.index('    caps->TextureFilterCaps', caps_start)]
    mask_start = d3d9.index('    pCaps->TextureCaps &=')
    d3d9_mask = d3d9[mask_start:d3d9.index(';', mask_start) + 1].replace('D3DPTEXTURECAPS_', 'WINED3DPTEXTURECAPS_')

    # Real flag values from the pinned headers.
    headers = (wined3d / 'wined3d_private.h').read_text() + (ROOT / 'vendor/wine9x/include/wine/wined3d.h').read_text()
    body = code + quirk + TEST
    names = set(re.findall(r'\b(WINED3D(?:USAGE|FMT_FLAG|PTEXTURECAPS)_\w+|WINED3D_RESOURCE_ACCESS_\w+|'
                           r'WINED3D_VIDMEM_ACCOUNTING|SFLAG_\w+|WINED3D_LOCATION_\w+)\b',
                           body + assign + depth_rule + caps + d3d9_mask))
    values = dict(re.findall(r'^#define\s+((?:WINED3D|SFLAG_)\w+)\s+(0x[0-9a-fA-F]+|\d+)\b', headers, re.M))
    defines = '\n'.join('#define %s %s' % (n, values[n]) for n in sorted(names))
    preamble = PREAMBLE.replace('struct wined3d_gl_info { BOOL supported[EXTENSIONS]; struct wined3d_gl_limits limits; };',
                                'struct wined3d_gl_info { BOOL supported[EXTENSIONS]; struct wined3d_gl_limits limits;\n'
                                '        BOOL texture_rectangle_hidden; };')
    test = TEST % {'assign': assign, 'depth': depth_rule, 'caps': caps, 'd3d9_mask': d3d9_mask,
                   'hide': hide.replace('gl_info->', 'real_fx.'),
                   'hide_dreamgpu': hide.replace('gl_info->', 'dreamgpu.')}
    (work / 'test.c').write_text(preamble + defines + '\n' + code + quirk + test)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                    '-Wno-unused-function', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-fno-omit-frame-pointer', str(work / 'test.c'), '-o', str(work / 'test')], check=True)
    subprocess.run([str(work / 'test')], check=True)
print('PASS prepared rectangle texture types (0049)')
