#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Patch 0042 on actual pinned Wine source: NPOT resources stay padded 2D textures with FBOs.

resource_init() skips the 2D texture type of an NPOT resource without
ARB_texture_non_power_of_two "to try texture rectangle". DreamGPU reports no
rectangle textures, so with EXT_framebuffer_object the next candidate was a
renderbuffer, and because this tree disables the texture usage check every NPOT
resource took it: backbuffers, depth buffers, offscreen plain surfaces and
textures. A renderbuffer draw binding (WINED3D_LOCATION_RB_RESOLVED) is only
ever loaded by resolving a multisampled renderbuffer, and swapchain_blit()
presents from the texture, so CPU uploads, partial blits and presents of those
surfaces were lost. The prepared resource_init() runs here under ASan/UBSan:
with framebuffer objects and no rectangles an NPOT resource keeps its padded
2D texture (as without framebuffer objects, where patch 0001 skips
renderbuffers), a renderbuffer remains the fallback for a format whose 2D
texture cannot be attached, and rectangle and NPOT implementations are
unchanged. It fails without the patch.
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
#define TRACE(...) ((void)0)
#define WARN(...) ((void)0)
#define FIXME(...) ((void)0)
#define ERR(...) ((void)0)
#define WARN_(channel) WARN
#define ORM_BACKBUFFER 0
#define ORM_FBO 1
enum { ARB_TEXTURE_NON_POWER_OF_TWO, ARB_TEXTURE_RECTANGLE, ARB_FRAMEBUFFER_OBJECT, EXT_FRAMEBUFFER_OBJECT,
        WINED3D_GL_NORMALIZED_TEXRECT, EXTENSIONS };
enum wined3d_resource_type { WINED3D_RTYPE_SURFACE = 1, WINED3D_RTYPE_VOLUME, WINED3D_RTYPE_TEXTURE,
        WINED3D_RTYPE_VOLUME_TEXTURE, WINED3D_RTYPE_CUBE_TEXTURE, WINED3D_RTYPE_BUFFER };
enum wined3d_pool { WINED3D_POOL_DEFAULT, WINED3D_POOL_MANAGED, WINED3D_POOL_SYSTEM_MEM, WINED3D_POOL_SCRATCH };
enum wined3d_multisample_type { WINED3D_MULTISAMPLE_NONE };
enum wined3d_gl_resource_type { WINED3D_GL_RES_TYPE_TEX_1D, WINED3D_GL_RES_TYPE_TEX_2D, WINED3D_GL_RES_TYPE_TEX_3D,
        WINED3D_GL_RES_TYPE_TEX_CUBE, WINED3D_GL_RES_TYPE_TEX_RECT, WINED3D_GL_RES_TYPE_BUFFER,
        WINED3D_GL_RES_TYPE_RB, WINED3D_GL_RES_TYPE_COUNT };
static struct { int offscreen_rendering_mode; } wined3d_settings;
struct wined3d_format { int id; DWORD flags[WINED3D_GL_RES_TYPE_COUNT]; UINT block_width, block_height; };
struct wined3d_gl_info { BOOL supported[EXTENSIONS]; };
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
};
static const char *debug_d3dformat(int id) { (void)id; return ""; }
static BOOL wined3d_resource_allocate_sysmem(struct wined3d_resource *r) { (void)r; return TRUE; }
static void wined3d_resource_free_sysmem(struct wined3d_resource *r) { (void)r; }
static UINT wined3d_device_get_available_texture_mem(const struct wined3d_device *d) { (void)d; return ~0u; }
static void adapter_adjust_memory(struct wined3d_adapter *a, UINT size) { (void)a; (void)size; }
static void device_resource_add(struct wined3d_device *d, struct wined3d_resource *r) { (void)d; (void)r; }
'''

TEST = r'''
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

int main(void)
{
    const DWORD rt = WINED3DFMT_FLAG_RENDERTARGET | WINED3DFMT_FLAG_FBO_ATTACHABLE;
    const DWORD ds = WINED3DFMT_FLAG_DEPTH | WINED3DFMT_FLAG_STENCIL | WINED3DFMT_FLAG_FBO_ATTACHABLE;
    struct wined3d_format color = {1, {0}, 1, 1}, depth = {2, {0}, 1, 1}, rb_only = {3, {0}, 1, 1};
    BOOL *supported = adapter.gl_info.supported;
    const DWORD usages[] = {WINED3DUSAGE_RENDERTARGET, WINED3DUSAGE_TEXTURE,
            WINED3DUSAGE_TEXTURE | WINED3DUSAGE_RENDERTARGET, WINED3DUSAGE_DYNAMIC, 0};
    unsigned i;

    color.flags[WINED3D_GL_RES_TYPE_TEX_2D] = WINED3DFMT_FLAG_TEXTURE | rt;
    color.flags[WINED3D_GL_RES_TYPE_TEX_RECT] = WINED3DFMT_FLAG_TEXTURE | rt;
    color.flags[WINED3D_GL_RES_TYPE_RB] = rt;
    depth.flags[WINED3D_GL_RES_TYPE_TEX_2D] = ds;
    depth.flags[WINED3D_GL_RES_TYPE_RB] = ds;
    rb_only.flags[WINED3D_GL_RES_TYPE_TEX_2D] = WINED3DFMT_FLAG_TEXTURE | WINED3DFMT_FLAG_RENDERTARGET;
    rb_only.flags[WINED3D_GL_RES_TYPE_RB] = rt;

    /* DreamGPU: EXT framebuffer objects, no NPOT, no rectangles. */
    wined3d_settings.offscreen_rendering_mode = ORM_FBO;
    supported[EXT_FRAMEBUFFER_OBJECT] = TRUE;
    for (i = 0; i < sizeof(usages) / sizeof(*usages); ++i)
    {
        assert(create(&color, usages[i], 640, 480) == WINED3D_GL_RES_TYPE_TEX_2D);
        assert(create(&color, usages[i], 800, 600) == WINED3D_GL_RES_TYPE_TEX_2D);
        assert(create(&color, usages[i], 512, 256) == WINED3D_GL_RES_TYPE_TEX_2D);
    }
    assert(create(&depth, WINED3DUSAGE_DEPTHSTENCIL, 640, 480) == WINED3D_GL_RES_TYPE_TEX_2D);
    assert(create(&depth, WINED3DUSAGE_DEPTHSTENCIL, 1024, 1024) == WINED3D_GL_RES_TYPE_TEX_2D);
    /* A format whose 2D texture the FBO cannot attach keeps its renderbuffer. */
    assert(create(&rb_only, WINED3DUSAGE_RENDERTARGET, 640, 480) == WINED3D_GL_RES_TYPE_RB);
    assert(create(&rb_only, WINED3DUSAGE_RENDERTARGET, 512, 512) == WINED3D_GL_RES_TYPE_RB);
    rb_only.flags[WINED3D_GL_RES_TYPE_RB] = 0;
    assert(create(&rb_only, WINED3DUSAGE_RENDERTARGET, 640, 480) == -1);

    /* Rectangle textures are still tried first for NPOT resources. */
    supported[ARB_TEXTURE_RECTANGLE] = TRUE;
    assert(create(&color, WINED3DUSAGE_RENDERTARGET, 640, 480) == WINED3D_GL_RES_TYPE_TEX_RECT);
    assert(create(&color, WINED3DUSAGE_RENDERTARGET, 512, 512) == WINED3D_GL_RES_TYPE_TEX_2D);
    supported[ARB_TEXTURE_RECTANGLE] = FALSE;
    /* Full NPOT support uses 2D textures directly. */
    supported[ARB_TEXTURE_NON_POWER_OF_TWO] = TRUE;
    assert(create(&color, WINED3DUSAGE_RENDERTARGET, 640, 480) == WINED3D_GL_RES_TYPE_TEX_2D);
    supported[ARB_TEXTURE_NON_POWER_OF_TWO] = FALSE;
    /* Without framebuffer objects (0001 skips renderbuffers): padded 2D as before. */
    wined3d_settings.offscreen_rendering_mode = ORM_BACKBUFFER;
    supported[EXT_FRAMEBUFFER_OBJECT] = FALSE;
    assert(create(&color, WINED3DUSAGE_RENDERTARGET, 640, 480) == WINED3D_GL_RES_TYPE_TEX_2D);
    assert(create(&depth, WINED3DUSAGE_DEPTHSTENCIL, 640, 480) == WINED3D_GL_RES_TYPE_TEX_2D);
    puts("PASS prepared FBO resource types: NPOT targets, depth buffers, plain surfaces and textures keep padded "
         "2D textures without rectangles; renderbuffer fallback, rectangle, NPOT and non-FBO selection unchanged");
    return 0;
}
'''


def function(source, prefix):
    start = source.index(prefix)
    return source[start:source.index('\n}', start) + 2] + '\n'


with tempfile.TemporaryDirectory(prefix='dreamgpu-fbo-types-') as temporary:
    work = Path(temporary)
    for name in json.loads(MANIFEST.read_text())['files']:
        p = work / name
        p.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'vendor/wine9x' / name, p)
    patches.apply(work, MANIFEST)
    resource = (work / 'wined3d/resource.c').read_text()
    code = ''.join(function(resource, prefix) for prefix in (
        'static DWORD resource_access_from_pool(', 'static void resource_check_usage(',
        'static BOOL resource_rect_usable(', 'HRESULT resource_init('))
    # Real flag values from the pinned headers.
    headers = (work / 'wined3d/wined3d_private.h').read_text() + (ROOT / 'vendor/wine9x/include/wine/wined3d.h').read_text()
    names = set(re.findall(r'\b(WINED3D(?:USAGE|FMT_FLAG)_\w+|WINED3D_RESOURCE_ACCESS_\w+|WINED3D_VIDMEM_ACCOUNTING)\b', code + TEST))
    values = dict(re.findall(r'^#define\s+(WINED3D\w+)\s+(0x[0-9a-fA-F]+|\d+)\b', headers, re.M))
    defines = '\n'.join('#define %s %s' % (n, values[n]) for n in sorted(names))
    (work / 'test.c').write_text(PREAMBLE + defines + '\n' + code + TEST)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                    '-Wno-unused-function', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-fno-omit-frame-pointer', str(work / 'test.c'), '-o', str(work / 'test')], check=True)
    subprocess.run([str(work / 'test')], check=True)
