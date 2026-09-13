#!/usr/bin/env python3
"""Actual drawable predicate and pinned Wine front-coordinate path contracts."""
import os,pathlib,subprocess,tempfile
HERE=pathlib.Path(__file__).resolve().parent;ROOT=HERE.parents[2]
source=(ROOT/'vendor/wine9x/wined3d/surface.c').read_text()
start=source.index('void surface_translate_drawable_coords(');end=source.index('\n/* Context activation',start)
coordinates=source[start:end]
# Selection must remain after the existing CPU-only desktop guard. The
# read-only map path is deliberately not patched and still requests sysmem.
patch=(ROOT/'support/guest/d3d/patches/base/0001-dreamgpu-wined3d.patch').read_text()
assert 'if (!device->d3d_initialized)' in source
map_source=source[source.index('HRESULT CDECL wined3d_surface_map('):source.index('HRESULT CDECL wined3d_surface_getdc(')]
assert 'surface_load_location(surface, context2, surface->resource.map_binding);' in map_source
assert 'WINED3D_MAP_NO_DIRTY_UPDATE | WINED3D_MAP_READONLY' in map_source
assert 'dg_surface_blit_usage(dst_surface)' in patch
harness=r'''
#include <assert.h>
#include <stdio.h>
typedef int BOOL;typedef unsigned long DWORD;typedef unsigned int UINT;typedef void *HWND;
#define WINED3DUSAGE_RENDERTARGET 1u
#define WINED3D_LOCATION_SYSMEM 2u
#define WINED3D_LOCATION_TEXTURE_RGB 0x20u
#define WINED3D_LOCATION_DRAWABLE 0x80u
typedef struct {int x,y;} POINT;typedef struct {int left,top,right,bottom;} RECT;
struct texture;struct wined3d_swapchain {struct texture *front_buffer;struct texture **back_buffers;struct {unsigned int backbuffer_count;}desc;};
struct texture {struct wined3d_swapchain *swapchain;};
struct wined3d_surface {struct {DWORD usage;UINT height;}resource;struct texture *container;DWORD locations;};
static void ScreenToClient(HWND window,POINT*p){assert(window==(HWND)1);p->x-=100;p->y-=70;}
static void OffsetRect(RECT*r,int x,int y){r->left+=x;r->right+=x;r->top+=y;r->bottom+=y;}
static void GetClientRect(HWND window,RECT*r){assert(window==(HWND)1);*r=(RECT){0,0,640,480};}
#include "wine-blit-usage.h"
'''+coordinates+r'''
int main(void){
 struct texture front={0},back1={0},back2={0},unowned={0},offscreen={0};
 struct texture *backs[]={&back1,&back2};struct wined3d_swapchain swapchain={&front,backs,{2}};
 struct wined3d_surface s={{0,768},&offscreen,0};RECT r={120,90,220,190};
 front.swapchain=back1.swapchain=back2.swapchain=unowned.swapchain=&swapchain;
 assert(dg_surface_blit_usage(&s)==0);s.resource.usage=0x200;assert(dg_surface_blit_usage(&s)==0x200);
 s.container=&front;assert(dg_surface_blit_usage(&s)==0x201&&s.resource.usage==0x200);
 surface_translate_drawable_coords(&s,(HWND)1,&r);
 assert(r.left==20&&r.right==120&&r.top==460&&r.bottom==360);
 s.container=&back1;assert(dg_surface_blit_usage(&s)==0x201);
 s.container=&back2;assert(dg_surface_blit_usage(&s)==0x201);
 s.container=&unowned;assert(dg_surface_blit_usage(&s)==0x200);
 swapchain.back_buffers=0;assert(dg_surface_blit_usage(&s)==0x200);
 s.container=&back1;assert(dg_surface_blit_usage(&s)==0x200);
 s.container=&offscreen;r=(RECT){1,2,3,4};surface_translate_drawable_coords(&s,(HWND)1,&r);
 assert(r.left==1&&r.top==766&&r.bottom==764);
 {
  struct wined3d_surface dst=s;dst.locations=0x80;
  assert(!dg_surface_needs_cpu_upload(0,&dst));
  assert(!dg_surface_needs_cpu_upload(&s,0));
  assert(!dg_surface_needs_cpu_upload(0,0));
  s.locations=2;assert(dg_surface_needs_cpu_upload(&s,&dst));
  s.locations=0x82;assert(!dg_surface_needs_cpu_upload(&s,&dst));
  s.locations=0x22;assert(!dg_surface_needs_cpu_upload(&s,&dst));
  s.locations=0x80;assert(!dg_surface_needs_cpu_upload(&s,&dst));
  s.locations=2;assert(dg_surface_needs_cpu_upload(&s,&dst));
  dst.locations=2;assert(!dg_surface_needs_cpu_upload(&s,&dst));
 }
 puts("Wine actual swapchain predicate and front-coordinate translation PASS; CPU map path retained");return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='dg-wine-blit-') as temporary:
 p=pathlib.Path(temporary);(p/'test.c').write_text(harness)
 subprocess.run([os.environ.get('CC','cc'),'-std=c99','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(HERE.parents[2] / "guest/d3d"),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
