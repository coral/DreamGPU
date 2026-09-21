#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute prepared Lock/GetDC/ReleaseDC across failed transfers and retries."""
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
#include <string.h>
typedef uint32_t DWORD; typedef unsigned UINT; typedef unsigned char BYTE;
typedef int HRESULT, BOOL; typedef void *HDC;
#define CDECL
#define TRACE(...) ((void)0)
#define WARN(...) ((void)0)
#define WARN_(channel) WARN
#define ERR(...) ((void)0)
#define FALSE 0
#define FAILED(hr) ((hr)<0)
#define WINED3D_OK 0
#define WINED3DERR_INVALIDCALL -1
#define E_OUTOFMEMORY -2
#define WINEDDERR_DCALREADYCREATED -3
#define WINEDDERR_NODC -4
#define WINED3DFMT_FLAG_BLOCKS 1
#define WINED3DFMT_FLAG_BROKEN_PITCH 2
#define WINED3D_POOL_DEFAULT 0
#define WINED3D_RESOURCE_ACCESS_CPU 1
#define WINED3D_TEXTURE_DYNAMIC_MAP 1
#define WINED3D_TEXTURE_PIN_SYSMEM 2
#define WINED3DUSAGE_DYNAMIC 1
#define WINED3DUSAGE_DEPTHSTENCIL 2
#define WINED3D_LOCATION_SYSMEM 1
#define WINED3D_LOCATION_USER_MEMORY 2
#define WINED3D_LOCATION_DIB 4
#define WINED3D_LOCATION_BUFFER 8
#define WINED3D_LOCATION_DRAWABLE 16
#define WINED3D_MAP_DISCARD 1
#define WINED3D_MAP_NO_DIRTY_UPDATE 2
#define WINED3D_MAP_READONLY 4
#define SFLAG_DCINUSE 1
#define SFLAG_CLIENT 2
#define MAXLOCKCOUNT 50
#define GL_PIXEL_UNPACK_BUFFER 1
#define GL_READ_WRITE 2
#define GL_EXTCALL(call) ((void)gl_info, call)
#define checkGLcall(...) ((void)0)
struct wined3d_format { unsigned byte_count,block_width,block_height,block_byte_count; };
struct wined3d_device { BOOL d3d_initialized; };
struct resource { struct wined3d_format *format; struct wined3d_device *device;
 unsigned map_count,pool,access_flags,map_binding,width,height,usage,format_flags; BYTE *heap_memory; };
struct wined3d_texture { struct resource resource; unsigned flags; };
struct wined3d_box { unsigned left,top,right,bottom,front,back; };
struct wined3d_map_desc { unsigned row_pitch,slice_pitch; void *data; };
struct wined3d_surface { struct resource resource; struct wined3d_texture *container;
 unsigned lockCount,locations,flags,pbo; BYTE *user_memory; struct { BYTE *bitmap_data; } dib;
 struct { unsigned left,top,right,bottom; } lockedRect; HDC hDC; };
struct wined3d_gl_info { int unused; };
struct wined3d_context { BOOL valid; const struct wined3d_gl_info *gl_info; };
static struct wined3d_gl_info gl;
static struct wined3d_context ctx={1,&gl},*tls;
static unsigned acquire_mode,acquires,releases,loads,invalidations,validations,client_releases,gl_calls;
static HRESULT load_error,dib_error;
static BOOL pbo_fail;
static BYTE cpu[64],dib[64],user[64];
static struct wined3d_context *context_acquire(struct wined3d_device *d,void *target)
{ assert(d&&!target);++acquires;ctx.valid=acquire_mode!=2;tls=acquire_mode==3?NULL:&ctx;return acquire_mode==1?NULL:&ctx; }
static void context_release(struct wined3d_context *c) { assert(c==&ctx);++releases;tls=NULL; }
static struct wined3d_context *context_get_current(void) { return tls; }
static BOOL surface_check_block_align(struct wined3d_surface *s,const struct wined3d_box *b)
{ return !(b->left%s->resource.format->block_width || b->top%s->resource.format->block_height); }
static void surface_prepare_map_memory(struct wined3d_surface *s) { (void)s; }
static HRESULT surface_load_location(struct wined3d_surface *s,struct wined3d_context *c,DWORD loc)
{
 ++loads;if(load_error)return load_error;
 if(s->locations&loc)return 0;
 if(s->locations&WINED3D_LOCATION_DRAWABLE) {
  if(!c||!c->valid||tls!=c)return WINED3DERR_INVALIDCALL;
  memset(loc==WINED3D_LOCATION_DIB?dib:cpu,0x75,64);
 } else {
  const BYTE *src=s->locations&WINED3D_LOCATION_DIB?dib:cpu;
  BYTE *dst=loc==WINED3D_LOCATION_DIB?dib:loc==WINED3D_LOCATION_USER_MEMORY?user:cpu;
  memcpy(dst,src,64);
 }
 s->locations|=loc;return 0;
}
static void surface_validate_location(struct wined3d_surface *s,DWORD loc) { ++validations;s->locations|=loc; }
static void surface_invalidate_location(struct wined3d_surface *s,DWORD loc) { ++invalidations;s->locations&=~loc; }
static UINT wined3d_surface_get_pitch(struct wined3d_surface *s) { return s->resource.width*s->resource.format->byte_count; }
static void glBindBuffer(unsigned type,unsigned name) { assert(type==GL_PIXEL_UNPACK_BUFFER);(void)name;++gl_calls; }
static void *glMapBuffer(unsigned type,unsigned access) { assert(type==GL_PIXEL_UNPACK_BUFFER&&access==GL_READ_WRITE);++gl_calls;return pbo_fail?NULL:cpu; }
static HRESULT surface_create_dib_section(struct wined3d_surface *s) { if(dib_error)return dib_error;s->hDC=dib;s->dib.bitmap_data=dib;return 0; }
static void surface_release_client_storage(struct wined3d_surface *s) { ++client_releases;s->flags&=~SFLAG_CLIENT; }
#include "wine-map-policy.h"
'''
TEST = r'''
static struct wined3d_format format={4,4,4,16};
static struct wined3d_device device={1};
static struct wined3d_texture texture;
static struct wined3d_surface surface;
static struct wined3d_map_desc map;
static void reset(void)
{
 memset(&surface,0,sizeof(surface));memset(&texture,0,sizeof(texture));
 surface.resource=(struct resource){&format,&device,0,0,1,WINED3D_LOCATION_SYSMEM,4,4,0,0,cpu};
 surface.container=&texture;surface.locations=WINED3D_LOCATION_DRAWABLE;
 surface.user_memory=user;surface.dib.bitmap_data=dib;surface.pbo=7;
 map=(struct wined3d_map_desc){99,98,(void *)(uintptr_t)0x1234};
 acquire_mode=acquires=releases=loads=invalidations=validations=client_releases=gl_calls=0;
 load_error=dib_error=0;pbo_fail=0;device.d3d_initialized=1;
 memset(cpu,0xaa,64);memset(dib,0xbb,64);memset(user,0xcc,64);
}
static void failed_map(HRESULT expected,unsigned flags)
{
 struct wined3d_map_desc saved=map;unsigned locations=surface.locations;
 assert(wined3d_surface_map(&surface,&map,NULL,flags)==expected);
 assert(!memcmp(&saved,&map,sizeof(map))&&!surface.resource.map_count);
 assert(surface.locations==locations&&!invalidations&&!validations&&cpu[0]==0xaa);
}
int main(void)
{
 // Rectangle validation is shared with D3D9, whose wrapper does not perform
 // DirectDraw's signed RECT checks. Failure must precede all side effects.
 const struct wined3d_box invalid_boxes[]={
  {UINT32_MAX,0,4,4,0,1},{0,UINT32_MAX,4,4,0,1},
  {0,0,5,4,0,1},{0,0,4,5,0,1},{2,0,1,4,0,1},
  {0,2,4,1,0,1},{0,0,0,4,0,1},{0,0,4,0,0,1},
  {0,0,4,4,1,2},{0,0,4,4,0,2},{0,0,UINT32_MAX,UINT32_MAX,0,1}
 };
 for(unsigned i=0;i<sizeof(invalid_boxes)/sizeof(*invalid_boxes);++i) {
  reset();struct wined3d_map_desc saved=map;
  assert(wined3d_surface_map(&surface,&map,&invalid_boxes[i],0)==WINED3DERR_INVALIDCALL);
  assert(!memcmp(&saved,&map,sizeof(map))&&!surface.resource.map_count);
  assert(surface.locations==WINED3D_LOCATION_DRAWABLE&&!loads&&!acquires&&!invalidations&&!validations);
 }
 reset();assert(wined3d_surface_map(&surface,NULL,NULL,0)==WINED3DERR_INVALIDCALL);
 assert(!surface.resource.map_count&&!loads&&!acquires&&!invalidations);
 reset();struct wined3d_box last_pixel={3,3,4,4,0,1};
 assert(!wined3d_surface_map(&surface,&map,&last_pixel,WINED3D_MAP_READONLY)&&map.data==cpu+60);
 // Failed depth/color transfers must not publish a pointer, dirty the GPU
 // copy, or leave the surface locked. Successful retry reads real bytes.
 for(unsigned usage=0;usage<=WINED3DUSAGE_DEPTHSTENCIL;usage+=WINED3DUSAGE_DEPTHSTENCIL) {
  for(unsigned flags=0;flags<=WINED3D_MAP_READONLY;flags+=WINED3D_MAP_READONLY) {
   reset();surface.resource.usage=usage;load_error=-17;
   failed_map(-17,flags);assert(loads==1&&acquires==1&&releases==1);
   load_error=0;assert(!wined3d_surface_map(&surface,&map,NULL,flags));
   assert(map.data==cpu&&cpu[0]==0x75&&map.row_pitch==16&&map.slice_pitch==64);
   assert(surface.resource.map_count==1);
   assert(surface.locations==(flags?WINED3D_LOCATION_DRAWABLE|WINED3D_LOCATION_SYSMEM:WINED3D_LOCATION_SYSMEM));
  }
 }
 for(unsigned mode=1;mode<=3;++mode) {
  reset();acquire_mode=mode;failed_map(WINED3DERR_INVALIDCALL,0);
  assert(releases==(mode!=1));
  reset();surface.resource.map_binding=WINED3D_LOCATION_BUFFER;acquire_mode=mode;
  failed_map(WINED3DERR_INVALIDCALL,WINED3D_MAP_DISCARD);assert(!gl_calls&&releases==(mode!=1));
 }
 reset();surface.resource.heap_memory=NULL;failed_map(E_OUTOFMEMORY,0);assert(!loads&&!acquires);
 reset();surface.resource.map_binding=WINED3D_LOCATION_BUFFER;pbo_fail=1;
 failed_map(E_OUTOFMEMORY,WINED3D_MAP_DISCARD);assert(gl_calls==3);
 reset();assert(!wined3d_surface_map(&surface,&map,NULL,WINED3D_MAP_DISCARD));
 assert(!loads&&!acquires&&surface.locations==WINED3D_LOCATION_SYSMEM);
 // Read-only current CPU storage avoids GL; CPU-only DIB copies cannot be skipped.
 reset();surface.locations=WINED3D_LOCATION_SYSMEM;
 assert(!wined3d_surface_map(&surface,&map,NULL,WINED3D_MAP_READONLY)&&!acquires&&!invalidations);
 reset();device.d3d_initialized=0;surface.locations=WINED3D_LOCATION_DIB;
 struct wined3d_box box={1,2,3,4,0,1};
 assert(!wined3d_surface_map(&surface,&map,&box,WINED3D_MAP_READONLY));
 assert(!acquires&&loads==1&&map.data==cpu+36&&cpu[0]==0xbb&&cpu[63]==0xbb);
 // Failed GetDC does not release client storage, expose a DC, or invalidate.
 for(unsigned client=0;client<2;++client) {
  reset();surface.flags=client?SFLAG_CLIENT:0;load_error=-17;HDC dc=(void *)(uintptr_t)0x1234;
  assert(wined3d_surface_getdc(&surface,&dc)==-17&&dc==(void *)(uintptr_t)0x1234);
  assert(!client_releases&&!surface.resource.map_count&&!(surface.flags&SFLAG_DCINUSE));
  assert(surface.locations==WINED3D_LOCATION_DRAWABLE&&!invalidations&&releases==1);
  load_error=0;assert(!wined3d_surface_getdc(&surface,&dc)&&dc==dib);
  assert(surface.resource.map_count==1&&(surface.flags&SFLAG_DCINUSE)&&dib[0]==0x75);
 }
 reset();dib_error=E_OUTOFMEMORY;HDC dc=NULL;
 assert(wined3d_surface_getdc(&surface,&dc)==E_OUTOFMEMORY&&!dc&&!invalidations&&releases==1);
 // Preserve the acquired DC and last correct DIB on failed writeback. Retry
 // copies the CPU edit to caller memory before relinquishing DIB authority.
 reset();surface.resource.map_binding=WINED3D_LOCATION_USER_MEMORY;
 assert(!wined3d_surface_getdc(&surface,&dc));dib[19]=0x37;load_error=-17;
 assert(wined3d_surface_releasedc(&surface,dc)==-17);
 assert(surface.resource.map_count==1&&(surface.flags&SFLAG_DCINUSE));
 assert(surface.locations==WINED3D_LOCATION_DIB&&dib[19]==0x37&&user[19]==0xcc);
 load_error=0;assert(!wined3d_surface_releasedc(&surface,dc));
 assert(!surface.resource.map_count&&!(surface.flags&SFLAG_DCINUSE));
 assert(surface.locations==WINED3D_LOCATION_USER_MEMORY&&user[19]==0x37&&user[18]==0x75);
 assert(wined3d_surface_releasedc(&surface,dc)==WINEDDERR_NODC);
 puts("PASS prepared Lock/GetDC/ReleaseDC: transfer/context/allocation failure, authority, balanced lifetime, CPU copies, offsets, read-only/discard and retry");
}
'''
with tempfile.TemporaryDirectory(prefix='dreamgpu-map-dc-') as directory:
    work = Path(directory)
    for name in json.loads(MANIFEST.read_text())['files']:
        p = work / name
        p.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'vendor/wine9x' / name, p)
    patches.apply(work, MANIFEST)
    source = (work / 'wined3d/surface.c').read_text()
    start = source.index('HRESULT CDECL wined3d_surface_map(')
    end = source.index('\nstatic HRESULT read_from_framebuffer(', start)
    (work / 'test.c').write_text(PREAMBLE + source[start:end] + TEST)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-I' + str(ROOT / 'guest/d3d'), str(work / 'test.c'),
                    '-o', str(work / 'test')], check=True)
    subprocess.run([str(work / 'test')], check=True)
