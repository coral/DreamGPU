#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run prepared depth-owner switching and context target preservation under ASan."""
import importlib.util
import json
import os
from pathlib import Path
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
#include <stdint.h>
#include <stdio.h>
typedef int BOOL,HRESULT;typedef unsigned DWORD,UINT;
#define TRUE 1
#define FALSE 0
#define FAILED(hr) ((hr)<0)
#define WINED3D_OK 0
#define WINED3DERR_INVALIDCALL -1
#define WINED3D_LOCATION_SYSMEM 1
#define WINED3D_LOCATION_TEXTURE_RGB 2
#define WINED3D_LOCATION_DRAWABLE 4
#define ORM_BACKBUFFER 1
#define ORM_FBO 2
#define STATE_RENDER(x) (x)
#define WINED3D_RS_ALPHABLENDENABLE 1
#define WINED3D_RS_SRGBWRITEENABLE 2
#define WINED3DFMT_FLAG_POSTPIXELSHADER_BLENDING 1
#define WINED3DFMT_FLAG_SRGB_WRITE 2
#define WARN(...) ((void)0)
struct {unsigned offscreen_rendering_mode;} wined3d_settings={ORM_BACKBUFFER};
struct wined3d_format {unsigned id,alpha_size;};
struct wined3d_device;struct wined3d_context;struct wined3d_surface;
struct resource {unsigned offscreen,format_flags,map_binding;struct wined3d_format *format;struct wined3d_device *device;};
struct wined3d_texture {struct resource resource;struct {unsigned name;} texture_srgb;unsigned refs;};
struct wined3d_surface {struct resource resource;struct wined3d_texture *container;DWORD locations;struct {unsigned cx,cy;} ds_current_size;unsigned cpu;};
struct swapchain {struct wined3d_device *device;};
struct wined3d_context {BOOL valid;struct wined3d_surface *current_rt;void *hdc,*glCtx;BOOL render_offscreen;struct swapchain *swapchain;unsigned gpu;};
struct wined3d_device {struct wined3d_surface *onscreen_depth_stencil,*backbuffer_ds_rt;struct wined3d_context *backbuffer_ds_context;void *backbuffer_ds_hdc,*backbuffer_ds_glctx;};
static unsigned downloads,uploads,fail_transfer;
static BOOL device_owns_backbuffer_ds(const struct wined3d_device *,const struct wined3d_context *,const struct wined3d_surface *);
static void surface_prepare_map_memory(struct wined3d_surface *s){assert(s->resource.map_binding==1);}
static HRESULT surface_load_location(struct wined3d_surface *s,struct wined3d_context *c,DWORD loc) {
 if(s->locations&loc)return 0;
 if(!device_owns_backbuffer_ds(s->resource.device,c,s)||fail_transfer)return -1;
 if(loc==1){++downloads;s->cpu=c->gpu;}else{assert(loc==4);++uploads;c->gpu=s->cpu;}
 s->locations|=loc;return 0;
}
static void surface_modify_ds_location(struct wined3d_surface *s,DWORD loc,unsigned x,unsigned y){s->locations=loc;s->ds_current_size.cx=x;s->ds_current_size.cy=y;}
static void wined3d_texture_incref(struct wined3d_texture *t){assert(t->refs);++t->refs;}
static void wined3d_texture_decref(struct wined3d_texture *t){assert(t->refs);--t->refs;}
static BOOL wined3d_resource_is_offscreen(const struct resource *r){return r->offscreen;}
static void context_invalidate_state(struct wined3d_context *c,unsigned state){(void)c;(void)state;}
static void context_set_render_offscreen(struct wined3d_context *c,BOOL off){c->render_offscreen=off;}
static void wined3d_texture_load(struct wined3d_texture *t,struct wined3d_context *c,BOOL srgb){(void)t;(void)c;(void)srgb;}
static void surface_invalidate_location(struct wined3d_surface *s,DWORD loc){s->locations&=~loc;}
'''
TEST = r'''
int main(void) {
 struct wined3d_format format={1,0};struct wined3d_device device={0};
 struct wined3d_texture ta={{1,0,1,&format,&device},{0},1},tb=ta,tda=ta,tdb=ta;
 struct wined3d_surface a={ta.resource,&ta,4,{2,2},0},b={tb.resource,&tb,4,{2,2},0};
 struct wined3d_surface da={tda.resource,&tda,1,{2,2},11},db={tdb.resource,&tdb,1,{2,2},22};
 struct swapchain swap={&device};
 struct wined3d_context context={1,&a,&a,&device,1,&swap,0},other=context;
 other.hdc=&b;other.glCtx=&db;
 assert(!device_switch_onscreen_ds(&device,&context,&da));
 assert(tda.refs==2&&device_owns_backbuffer_ds(&device,&context,&da));
 assert(!surface_load_location(&da,&context,4)&&context.gpu==11);
 surface_modify_ds_location(&da,4,2,2);context.gpu=31;
 unsigned before=downloads;
 assert(!device_switch_onscreen_ds(&device,&context,&da)&&downloads==before&&tda.refs==2);
 // Switching offscreen RTs must save depth before current_rt changes identity.
 context_setup_target(&context,&b);
 assert(da.locations==1&&da.cpu==31&&downloads==before+1);
 assert(!device_owns_backbuffer_ds(&device,&context,&da));
 assert(!device_switch_onscreen_ds(&device,&context,&db)&&tda.refs==1&&tdb.refs==2);
 assert(!surface_load_location(&db,&context,4)&&context.gpu==22);
 surface_modify_ds_location(&db,4,2,2);context.gpu=42;
 context_setup_target(&context,&a);
 assert(db.locations==1&&db.cpu==42);
 assert(!device_switch_onscreen_ds(&device,&context,&da));
 assert(!surface_load_location(&da,&context,4)&&context.gpu==31);
 // Same depth surface can change targets after preserving its CPU authority.
 surface_modify_ds_location(&da,4,2,2);context.gpu=51;
 context_setup_target(&context,&b);assert(da.cpu==51&&da.locations==1);
 assert(!device_switch_onscreen_ds(&device,&context,&da)&&tda.refs==2);
 assert(!surface_load_location(&da,&context,4)&&context.gpu==51);
 // Failed preservation neither retires the old reference nor attributes new pixels to it.
 surface_modify_ds_location(&da,4,2,2);context.gpu=61;fail_transfer=1;
 context_setup_target(&context,&a);assert(da.locations==4&&da.cpu==51);
 assert(device_switch_onscreen_ds(&device,&context,&db)<0);
 assert(device.onscreen_depth_stencil==&da&&tda.refs==2&&tdb.refs==1);
 fail_transfer=0;assert(device_switch_onscreen_ds(&device,&context,&db)<0);
 // Returning to the recorded drawable permits retry without losing the last good copy.
 context_setup_target(&context,&b);assert(!device_preserve_backbuffer_ds(&device,&context));
 assert(da.locations==1&&da.cpu==61);
 // Different context/DC may rebind CPU-authoritative depth, but not read another drawable.
 assert(!device_switch_onscreen_ds(&device,&other,&db));
 assert(!surface_load_location(&db,&other,4));surface_modify_ds_location(&db,4,2,2);other.gpu=71;
 assert(device_switch_onscreen_ds(&device,&context,&da)<0&&device.onscreen_depth_stencil==&db);
 assert(!device_preserve_backbuffer_ds(&device,&other)&&db.cpu==71);
 assert(!device_switch_onscreen_ds(&device,&context,&da));
 // Handle rebinding invalidates ownership even when object and target pointers match.
 void *dc=context.hdc;context.hdc=&db;assert(!device_owns_backbuffer_ds(&device,&context,&da));context.hdc=dc;
 void *gl=context.glCtx;context.glCtx=&db;assert(!device_owns_backbuffer_ds(&device,&context,&da));context.glCtx=gl;
 assert(device_owns_backbuffer_ds(&device,&context,&da));
 wined3d_texture_decref(device.onscreen_depth_stencil->container);assert(tda.refs==1&&tdb.refs==1);
 puts("PASS prepared depth ownership: A/B/A preservation, same-DS targets, rejected stale/context/DC reads, retry and balanced references");
}
'''

def function(source, prefix):
    start = source.index(prefix)
    return source[start:source.index('\n}', start) + 2]

with tempfile.TemporaryDirectory(prefix='dreamgpu-depth-owner-') as directory:
    work = Path(directory)
    for name in json.loads(MANIFEST.read_text())['files']:
        p = work / name
        p.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'vendor/wine9x' / name, p)
    patches.apply(work, MANIFEST)
    device = (work / 'wined3d/device.c').read_text()
    context = (work / 'wined3d/context.c').read_text()
    code = PREAMBLE + '\n'.join(function(device, prefix) for prefix in (
        'BOOL device_owns_backbuffer_ds(', 'HRESULT device_preserve_backbuffer_ds(',
        'HRESULT device_switch_onscreen_ds('))
    code += function(context, 'static void context_setup_target(') + TEST
    (work / 'test.c').write_text(code)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(work / 'test.c'),
                    '-o', str(work / 'test')], check=True)
    subprocess.run([str(work / 'test')], check=True)
