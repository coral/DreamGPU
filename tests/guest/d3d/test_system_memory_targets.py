#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run prepared Wine surface policy, render-target binding, and clear validation."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('checked_patches', ROOT / 'tests/guest/support/patches.py')
patches = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patches)
manifest = ROOT / 'support/guest/d3d/patches/base/manifest.json'
PREAMBLE = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
typedef uint32_t DWORD;
typedef int HRESULT, BOOL;
typedef struct { int left, top, right, bottom; } RECT;
#define CDECL
#define TRACE(...) ((void)0)
#define WARN(...) ((void)0)
#define WINED3D_OK 0
#define WINED3DERR_INVALIDCALL -1
#define WINED3DCLEAR_TARGET 1
#define WINED3DCLEAR_ZBUFFER 2
#define WINED3DCLEAR_STENCIL 4
#define DDSCAPS_3DDEVICE 0x2000
#define DDSCAPS_ZBUFFER 0x20000
#define DDSCAPS_SYSTEMMEMORY 0x800
#define DDSCAPS_VIDEOMEMORY 0x4000
#define DDSCAPS_LOCALVIDMEM 0x10000000
#define DDSCAPS_TEXTURE 0x1000
#define DDSCAPS_ALLOCONLOAD 0x04000000
#define DDSCAPS2_TEXTUREMANAGE 0x10
#define DDSCAPS2_D3DTEXTUREMANAGE 0x20000
#define DDSD_LPSURFACE 0x800
#define WINED3D_POOL_DEFAULT 0
#define WINED3D_POOL_SYSTEM_MEM 1
#define WINED3D_POOL_MANAGED 2
#define WINED3DUSAGE_DYNAMIC 1
#define WINED3DUSAGE_TEXTURE 2
#define WINED3DUSAGE_RENDERTARGET 4
#define WINED3DUSAGE_DEPTHSTENCIL 8
struct descriptor { struct { DWORD dwCaps, dwCaps2; } ddsCaps; DWORD dwFlags; void *lpSurface; };
struct resource_desc { unsigned pool, usage; };
struct viewport { unsigned x,y,width,height; float min_z,max_z; };
struct wined3d_state { struct viewport viewport; RECT scissor_rect; };
struct resource { DWORD usage; };
struct wined3d_rendertarget_view { struct resource *resource; unsigned width,height,refs; };
struct adapter { struct { struct { unsigned buffers; } limits; } gl_info; };
struct wined3d_device { struct adapter *adapter; struct wined3d_state state; struct { struct wined3d_rendertarget_view *render_targets[1],*depth_stencil; } fb; void *cs; };
struct wined3d_color { float r,g,b,a; };
static unsigned binds, clears, viewports, scissors;
static void wined3d_rendertarget_view_incref(struct wined3d_rendertarget_view *v) { ++v->refs; }
static void wined3d_rendertarget_view_decref(struct wined3d_rendertarget_view *v) { assert(v->refs); --v->refs; }
static void wined3d_cs_emit_set_viewport(void *cs,const struct viewport *v) { (void)cs; assert(v->width==32&&v->height==32); ++viewports; }
static void wined3d_cs_emit_set_scissor_rect(void *cs,const RECT *r) { (void)cs; assert(r->right==32&&r->bottom==32); ++scissors; }
static void wined3d_cs_emit_set_rendertarget_view(void *cs,unsigned i,struct wined3d_rendertarget_view *v) { (void)cs;(void)v;assert(!i);++binds; }
static void wined3d_cs_emit_clear(void *cs,DWORD n,const RECT *r,DWORD f,const struct wined3d_color *c,float d,DWORD s) { (void)cs;(void)n;(void)r;(void)f;(void)c;(void)d;(void)s;++clears; }
'''
TEST = r'''
int main(void) {
 struct descriptor d={0}; struct resource_desc out;
 for(unsigned kind=0;kind<4;++kind) {
  unsigned extra=kind==0?0:kind==1?DDSCAPS_3DDEVICE:kind==2?DDSCAPS_ZBUFFER:DDSCAPS_3DDEVICE|DDSCAPS_ZBUFFER;
  d=(struct descriptor){0}; d.ddsCaps.dwCaps=DDSCAPS_SYSTEMMEMORY|extra|0x40;
  DWORD caps=d.ddsCaps.dwCaps; out=policy(&d);
  assert(d.ddsCaps.dwCaps==caps);
  if(!kind) assert(out.pool==WINED3D_POOL_SYSTEM_MEM&&!out.usage);
  else assert(out.pool==WINED3D_POOL_DEFAULT && out.usage==(WINED3DUSAGE_DYNAMIC|(extra&DDSCAPS_ZBUFFER?WINED3DUSAGE_DEPTHSTENCIL:WINED3DUSAGE_RENDERTARGET)));
  // Real caller-owned pointers retain their pool and subsequent existing validation.
  d.dwFlags=DDSD_LPSURFACE;d.lpSurface=&d;out=policy(&d);
  assert(out.pool==WINED3D_POOL_SYSTEM_MEM&&!out.usage&&d.dwFlags==DDSD_LPSURFACE);
  // Ignored/null user pointers must not prevent owned 3D backing.
  d.lpSurface=NULL;out=policy(&d);assert(!d.dwFlags);
  assert(out.pool==(kind?WINED3D_POOL_DEFAULT:WINED3D_POOL_SYSTEM_MEM));
  d.dwFlags=DDSD_LPSURFACE;d.lpSurface=&d;d.ddsCaps.dwCaps|=DDSCAPS_ALLOCONLOAD;out=policy(&d);
  assert(!d.dwFlags&&out.pool==(kind?WINED3D_POOL_DEFAULT:WINED3D_POOL_SYSTEM_MEM));
 }
 d=(struct descriptor){0};d.ddsCaps.dwCaps=DDSCAPS_SYSTEMMEMORY|DDSCAPS_3DDEVICE|DDSCAPS_TEXTURE;
 out=policy(&d);assert(out.usage==(WINED3DUSAGE_DYNAMIC|WINED3DUSAGE_TEXTURE|WINED3DUSAGE_RENDERTARGET));
 d=(struct descriptor){0};d.ddsCaps.dwCaps=DDSCAPS_SYSTEMMEMORY|DDSCAPS_TEXTURE;
 out=policy(&d);assert(out.pool==WINED3D_POOL_SYSTEM_MEM&&!out.usage);
 d=(struct descriptor){0};d.ddsCaps.dwCaps=DDSCAPS_VIDEOMEMORY|DDSCAPS_3DDEVICE;
 out=policy(&d);assert(out.pool==WINED3D_POOL_DEFAULT&&out.usage==(WINED3DUSAGE_DYNAMIC|WINED3DUSAGE_RENDERTARGET));
 assert(d.ddsCaps.dwCaps&DDSCAPS_LOCALVIDMEM);
 d=(struct descriptor){0};d.ddsCaps.dwCaps=DDSCAPS_TEXTURE;d.ddsCaps.dwCaps2=DDSCAPS2_TEXTUREMANAGE;
 out=policy(&d);assert(out.pool==WINED3D_POOL_MANAGED&&out.usage==WINED3DUSAGE_TEXTURE&&(d.ddsCaps.dwCaps&DDSCAPS_SYSTEMMEMORY));
 // Feed the actual policy result into the actual bind function, then clear.
 d=(struct descriptor){0};d.ddsCaps.dwCaps=0x2840;out=policy(&d);
 struct resource resource={out.usage},invalid_resource={0};
 struct wined3d_rendertarget_view rt={&resource,32,32,1},bad={&invalid_resource,32,32,1},ds={0};ds.width=ds.height=32;
 struct adapter adapter={0};adapter.gl_info.limits.buffers=1;
 struct wined3d_device device={0};device.adapter=&adapter;device.fb.depth_stencil=&ds;
 struct wined3d_color color={1,0,0,1};RECT rect={0,0,32,32};
 assert(wined3d_device_clear(&device,1,&rect,3,&color,1,0)==WINED3DERR_INVALIDCALL&&!clears);
 assert(wined3d_device_set_rendertarget_view(&device,0,&bad,1)==WINED3DERR_INVALIDCALL&&!binds&&!device.fb.render_targets[0]);
 assert(wined3d_device_set_rendertarget_view(&device,1,&rt,0)==WINED3DERR_INVALIDCALL&&!binds);
 assert(wined3d_device_set_rendertarget_view(&device,0,&rt,1)==WINED3D_OK);
 assert(device.fb.render_targets[0]==&rt&&rt.refs==2&&binds==1&&viewports==1&&scissors==1);
 assert(wined3d_device_clear(&device,1,&rect,3,&color,1,0)==WINED3D_OK&&clears==1);
 assert(wined3d_device_set_rendertarget_view(&device,0,&bad,0)==WINED3DERR_INVALIDCALL&&device.fb.render_targets[0]==&rt&&rt.refs==2&&binds==1);
 assert(wined3d_device_set_rendertarget_view(&device,0,NULL,0)==WINED3D_OK&&rt.refs==1&&binds==2);
 assert(wined3d_device_clear(&device,1,&rect,1,&color,1,0)==WINED3DERR_INVALIDCALL&&clears==1);
 assert(wined3d_device_clear(&device,1,&rect,2,&color,1,0)==WINED3D_OK&&clears==2);
 device.fb.depth_stencil=NULL;
 assert(wined3d_device_clear(&device,1,&rect,2,&color,1,0)==WINED3DERR_INVALIDCALL&&clears==2);
 puts("PASS prepared Wine system-memory 3D backing, user-pointer/ordinary/managed preservation, actual binding and clear validation");
}
'''
with tempfile.TemporaryDirectory(prefix='dreamgpu-system-targets-') as directory:
    work = Path(directory)
    for name in json.loads(manifest.read_text())['files']:
        dest = work / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'vendor/wine9x' / name, dest)
    patches.apply(work, manifest)
    source = (work / 'ddraw/surface.c').read_text()
    start = source.index("    /* If the surface is of the 'ALLOCONLOAD' type")
    end = source.index('    if (desc->dwFlags & DDSD_LPSURFACE)', start)
    policy = 'static struct resource_desc policy(struct descriptor *desc) { struct resource_desc wined3d_desc={0};\n' + source[start:end] + '\nreturn wined3d_desc; }\n'
    source = (work / 'wined3d/device.c').read_text()
    functions = []
    for name in ['wined3d_device_set_rendertarget_view', 'wined3d_device_clear']:
        start = source.index('HRESULT CDECL ' + name + '(')
        end = source.index('\n}', start) + 2
        functions.append(source[start:end])
    test = work / 'test.c'
    test.write_text(PREAMBLE + policy + '\n'.join(functions) + TEST)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(test), '-o', str(work / 'test')], check=True)
    subprocess.run([str(work / 'test')], check=True)
