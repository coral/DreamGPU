#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute the prepared surface_load_location for depth surfaces under ASan/UBSan.

An offscreen depth surface (for example an application's spare Z-buffer used as
a DirectDraw Blt target) has no GL storage without depth textures; only the
device-owned drawable has a native transfer path. Once discarded, mapping it
must define its CPU copy rather than fail, or depth Blts return INVALIDCALL.
With the patch removed, this test fails.
"""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
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
typedef unsigned DWORD;
typedef int BOOL;
typedef int HRESULT;
typedef unsigned char BYTE;
#define TRUE 1
#define FALSE 0
#define FAILED(x) ((x) < 0)
#define WINED3D_OK 0
#define WINED3DERR_INVALIDCALL ((HRESULT)0x8876086c)
#define E_OUTOFMEMORY ((HRESULT)0x8007000e)
#define TRACE(...) ((void)0)
#define WARN(...) ((void)0)
#define ERR(...) ((void)0)
#define FIXME(...) ((void)0)
#define WINED3D_LOCATION_DISCARDED 0x00000001
#define WINED3D_LOCATION_SYSMEM 0x00000002
#define WINED3D_LOCATION_USER_MEMORY 0x00000004
#define WINED3D_LOCATION_BUFFER 0x00000008
#define WINED3D_LOCATION_TEXTURE_RGB 0x00000010
#define WINED3D_LOCATION_TEXTURE_SRGB 0x00000020
#define WINED3D_LOCATION_DRAWABLE 0x00000040
#define WINED3D_LOCATION_RB_MULTISAMPLE 0x00000080
#define WINED3D_LOCATION_RB_RESOLVED 0x00000100
#define WINED3D_LOCATION_DIB 0x00000200
#define WINED3DUSAGE_DEPTHSTENCIL 0x00000002
#define ORM_BACKBUFFER 1
#define SFLAG_LOST 0x1
struct wined3d_context { int unused; };
struct wined3d_device { int owns_ds; };
struct wined3d_bo_address { unsigned buffer_object; BYTE *addr; };
struct wined3d_resource {
    DWORD usage, access_flags, size;
    struct wined3d_device *device;
    BYTE *heap_memory;
};
struct wined3d_surface {
    struct wined3d_resource resource;
    DWORD locations, flags;
    BYTE *user_memory;
};
static struct { int offscreen_rendering_mode; } wined3d_settings = {ORM_BACKBUFFER};
static const DWORD surface_simple_locations = WINED3D_LOCATION_SYSMEM | WINED3D_LOCATION_USER_MEMORY
        | WINED3D_LOCATION_DIB | WINED3D_LOCATION_BUFFER;
static unsigned transfers, ds_loads, prepared;
static BYTE heap[64], user[64];
static BOOL device_owns_backbuffer_ds(const struct wined3d_device *d, const struct wined3d_context *c,
        const struct wined3d_surface *s)
{ (void)c; (void)s; return d->owns_ds; }
static void surface_prepare_system_memory(struct wined3d_surface *s)
{ ++prepared; if (!s->resource.heap_memory) s->resource.heap_memory = heap; }
static void surface_get_memory(const struct wined3d_surface *s, struct wined3d_bo_address *m, DWORD location)
{
    m->buffer_object = location == WINED3D_LOCATION_BUFFER ? 7 : 0;
    m->addr = location == WINED3D_LOCATION_SYSMEM ? s->resource.heap_memory
            : location == WINED3D_LOCATION_USER_MEMORY ? s->user_memory : NULL;
}
static void surface_validate_location(struct wined3d_surface *s, DWORD l) { s->locations |= l; }
static HRESULT surface_copy_simple_location(struct wined3d_surface *s, DWORD l) { (void)s; (void)l; return WINED3D_OK; }
static HRESULT surface_transfer_depth(struct wined3d_surface *s, struct wined3d_context *c, DWORD l)
{
    (void)c;
    ++transfers;
    if (!device_owns_backbuffer_ds(s->resource.device, c, s))
        return WINED3DERR_INVALIDCALL;
    s->locations |= l;
    return WINED3D_OK;
}
static HRESULT surface_load_ds_location(struct wined3d_surface *s, struct wined3d_context *c, DWORD l)
{ (void)s; (void)c; (void)l; ++ds_loads; return WINED3D_OK; }
static DWORD resource_access_from_location(DWORD l) { (void)l; return 0; }
static HRESULT surface_load_sysmem(struct wined3d_surface *s, struct wined3d_context *c, DWORD l) { (void)s; (void)c; (void)l; return WINED3D_OK; }
static HRESULT surface_load_drawable(struct wined3d_surface *s, struct wined3d_context *c) { (void)s; (void)c; return WINED3D_OK; }
static void surface_multisample_resolve(struct wined3d_surface *s, struct wined3d_context *c) { (void)s; (void)c; }
static HRESULT surface_load_texture(struct wined3d_surface *s, struct wined3d_context *c, BOOL srgb) { (void)s; (void)c; (void)srgb; return WINED3D_OK; }
static void surface_evict_sysmem(struct wined3d_surface *s) { (void)s; }
'''
TEST = r'''
static void reset(void) { transfers = ds_loads = prepared = 0; memset(heap, 0xa5, sizeof(heap)); memset(user, 0x5a, sizeof(user)); }
static BOOL zero(const BYTE *p, unsigned n) { for (unsigned i = 0; i < n; ++i) if (p[i]) return FALSE; return TRUE; }
int main(void)
{
    struct wined3d_device offscreen = {0}, owner = {1};
    struct wined3d_context context = {0};
    struct wined3d_surface z;
    memset(&z, 0, sizeof(z));
    z.resource.usage = WINED3DUSAGE_DEPTHSTENCIL;
    z.resource.size = sizeof(heap);
    z.resource.device = &offscreen;
    /* A discarded offscreen depth surface gets a defined (zero) CPU copy with
     * no native transfer, so it can be mapped and used as a Blt target. */
    reset(); z.locations = WINED3D_LOCATION_DISCARDED;
    assert(surface_load_location(&z, &context, WINED3D_LOCATION_SYSMEM) == WINED3D_OK);
    assert(transfers == 0 && prepared == 1 && zero(heap, sizeof(heap)) && (z.locations & WINED3D_LOCATION_SYSMEM));
    /* No location at all is treated the same way. */
    reset(); z.locations = 0; z.resource.heap_memory = NULL;
    assert(surface_load_location(&z, &context, WINED3D_LOCATION_SYSMEM) == WINED3D_OK);
    assert(z.resource.heap_memory == heap && zero(heap, sizeof(heap)) && transfers == 0);
    /* User memory too. */
    reset(); z.locations = WINED3D_LOCATION_DISCARDED; z.user_memory = user;
    assert(surface_load_location(&z, &context, WINED3D_LOCATION_USER_MEMORY) == WINED3D_OK);
    assert(zero(user, sizeof(user)) && transfers == 0);
    /* Real contents keep the device transfer: drawable data is downloaded. */
    reset(); z.resource.device = &owner; z.locations = WINED3D_LOCATION_DRAWABLE;
    assert(surface_load_location(&z, &context, WINED3D_LOCATION_SYSMEM) == WINED3D_OK);
    assert(transfers == 1 && heap[0] == 0xa5);
    /* An offscreen surface with drawable-only data still fails as before. */
    reset(); z.resource.device = &offscreen; z.locations = WINED3D_LOCATION_DRAWABLE;
    assert(surface_load_location(&z, &context, WINED3D_LOCATION_SYSMEM) == WINED3DERR_INVALIDCALL);
    /* Buffer bindings never take the CPU definition path. */
    reset(); z.resource.device = &owner; z.locations = WINED3D_LOCATION_DISCARDED;
    assert(surface_load_location(&z, &context, WINED3D_LOCATION_BUFFER) == WINED3D_OK && transfers == 1);
    /* An already current location and GL-side loads are unchanged. */
    reset(); z.locations = WINED3D_LOCATION_SYSMEM;
    assert(surface_load_location(&z, &context, WINED3D_LOCATION_SYSMEM) == WINED3D_OK && !transfers && heap[0] == 0xa5);
    reset(); z.locations = WINED3D_LOCATION_DISCARDED;
    assert(surface_load_location(&z, &context, WINED3D_LOCATION_TEXTURE_RGB) == WINED3D_OK && ds_loads == 1);
    /* The drawable of a depth surface the device does not own stays rejected. */
    reset(); z.resource.device = &offscreen;
    assert(surface_load_location(&z, &context, WINED3D_LOCATION_DRAWABLE) == WINED3DERR_INVALIDCALL);
    puts("PASS prepared depth surface loads: discarded offscreen depth gets a defined CPU copy; "
         "real contents, buffers, GL locations and non-owned drawables unchanged");
    return 0;
}
'''


def function(source, signature):
    start = source.index(signature)
    end = source.index('\n}\n', start) + 3
    return source[start:end] + '\n'


with tempfile.TemporaryDirectory(prefix='dreamgpu-discarded-depth-') as temporary:
    work = Path(temporary)
    for name in json.loads(MANIFEST.read_text())['files']:
        p = work / name
        p.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'vendor/wine9x' / name, p)
    patches.apply(work, MANIFEST)
    surface = (work / 'wined3d/surface.c').read_text()
    helper = 'static HRESULT dg_surface_define_discarded_depth('
    body = (function(surface, helper) if helper in surface else '') + \
        function(surface, 'HRESULT surface_load_location(')
    (work / 'test.c').write_text(PREAMBLE + body + TEST)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', '-Wno-unused-function', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', str(work / 'test.c'), '-o', str(work / 'test')], check=True)
    sys.exit(subprocess.run([str(work / 'test')]).returncode)
