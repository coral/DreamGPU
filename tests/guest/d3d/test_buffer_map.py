#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute actual Wine buffer Lock boundaries with allocator/GL failures."""
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
#include <stdlib.h>
#include <string.h>
typedef unsigned UINT,DWORD,GLbitfield;typedef int HRESULT,LONG;typedef uint8_t BYTE;typedef uintptr_t DWORD_PTR;
#define CDECL
#define TRACE(...) ((void)0)
#define WARN(...) ((void)0)
#define WINED3D_OK 0
#define WINED3DERR_INVALIDCALL -1
#define E_OUTOFMEMORY -2
#define WINED3D_BUFFER_DISCARD 1
#define WINED3D_BUFFER_DOUBLEBUFFER 2
#define WINED3D_BUFFER_APPLESYNC 4
#define WINED3D_BUFFER_CREATEBO 8
#define WINED3D_BUFFER_SYNC 16
#define WINED3D_MAP_DISCARD 1
#define WINED3D_MAP_READONLY 2
#define WINED3D_MAP_NOOVERWRITE 4
#define WINED3DUSAGE_DYNAMIC 1
#define GL_ELEMENT_ARRAY_BUFFER_ARB 1
#define GL_READ_WRITE 2
#define STATE_INDEXBUFFER 1
#define STATE_STREAMSRC 2
#define RESOURCE_ALIGNMENT 16
#define ARB_MAP_BUFFER_RANGE 0
#define GL_MAP_READ_BIT 1
#define GL_MAP_WRITE_BIT 2
#define GL_MAP_FLUSH_EXPLICIT_BIT 4
#define GL_MAP_INVALIDATE_BUFFER_BIT 8
#define GL_MAP_UNSYNCHRONIZED_BIT 16
#define GL_EXTCALL(call) call
#define checkGLcall(...) ((void)0)
struct wined3d_device { int unused; };
struct resource { struct wined3d_device *device; UINT size,map_count,usage,bind_count; BYTE *heap_memory; };
struct wined3d_buffer { struct resource resource; UINT flags,buffer_object,buffer_type_hint; BYTE *map_ptr; DWORD map_access; };
struct wined3d_gl_info { unsigned supported[1]; };
struct wined3d_context { unsigned valid; struct wined3d_gl_info *gl_info; };
static struct wined3d_gl_info gl;
static struct wined3d_context context={1,&gl},*tls;
static unsigned acquire_mode,acquires,releases,map_fail,maps,dirty,dirty_offset,dirty_size,unaligned,unmaps,native_access;
static _Alignas(16) BYTE gpu[80],cpu[64];
static UINT wined3d_resource_sanitize_map_flags(struct resource *r,UINT f) { (void)r;return f; }
static struct wined3d_context *context_acquire(struct wined3d_device *d,void *rt)
{ assert(d&&!rt);++acquires;context.valid=acquire_mode!=2;tls=acquire_mode==3?NULL:&context;return acquire_mode==1?NULL:&context; }
static void context_release(struct wined3d_context *c) { assert(c==&context);++releases;tls=NULL; }
static struct wined3d_context *context_get_current(void) { return tls; }
static void context_invalidate_state(struct wined3d_context *c,UINT state) { assert(c==tls&&state==STATE_INDEXBUFFER); }
static void glBindBuffer(UINT type,UINT buffer) { assert(type==GL_ELEMENT_ARRAY_BUFFER_ARB&&buffer); }
static void *native_map(void) { ++maps;return map_fail?NULL:gpu+unaligned; }
static void *glMapBuffer(UINT type,UINT access)
{ assert(type==1&&access==GL_READ_WRITE);native_access=GL_MAP_READ_BIT|GL_MAP_WRITE_BIT;return native_map(); }
static void *glMapBufferRange(UINT type,UINT start,UINT size,UINT flags)
{ assert(type==1&&!start&&size==64);native_access=flags&(GL_MAP_READ_BIT|GL_MAP_WRITE_BIT);return native_map(); }
static void glUnmapBuffer(UINT type) { assert(type==1);++unmaps;native_access=0; }
static void buffer_sync_apple(struct wined3d_buffer *b,UINT f,const struct wined3d_gl_info *g) { (void)b;(void)f;(void)g; }
static void buffer_invalidate_bo_range(struct wined3d_buffer *b,UINT off,UINT size)
{ assert(b);++dirty;dirty_offset=off;dirty_size=size; }
'''
TEST = r'''
int main(void)
{
 struct wined3d_device device={0};
 struct wined3d_buffer b={{&device,64,0,0,0,cpu},0,0,1,NULL,0};BYTE *pointer=(BYTE *)(uintptr_t)0x1234;
 for(unsigned off=0;off<=80;++off)for(unsigned size=0;size<=80;++size) {
  HRESULT hr=wined3d_buffer_map(&b,off,size,&pointer,0);
  if(off<=64&&size<=64-off) { assert(!hr&&pointer==cpu+off&&b.resource.map_count==1);b.resource.map_count=0; }
  else assert(hr==WINED3DERR_INVALIDCALL&&!b.resource.map_count);
 }
 pointer=(BYTE *)(uintptr_t)0x1234;
 assert(wined3d_buffer_map(&b,UINT32_MAX,2,&pointer,0)==WINED3DERR_INVALIDCALL);
 assert(wined3d_buffer_map(&b,0,UINT32_MAX,&pointer,0)==WINED3DERR_INVALIDCALL);
 assert(wined3d_buffer_map(&b,0,0,NULL,0)==WINED3DERR_INVALIDCALL);
 assert(pointer==(BYTE *)(uintptr_t)0x1234&&!b.resource.map_count&&!dirty);
 b.resource.heap_memory=NULL;
 assert(wined3d_buffer_map(&b,8,8,&pointer,0)==E_OUTOFMEMORY&&!b.resource.map_count);
 assert(pointer==(BYTE *)(uintptr_t)0x1234);b.resource.heap_memory=cpu;
 b.buffer_object=7;
 for(unsigned mode=1;mode<=3;++mode) {
  acquire_mode=mode;acquires=releases=0;
  assert(wined3d_buffer_map(&b,4,8,&pointer,WINED3D_MAP_DISCARD)==WINED3DERR_INVALIDCALL);
  assert(!b.flags&&!b.resource.map_count&&!dirty&&!maps&&pointer==(BYTE *)(uintptr_t)0x1234);
  assert(acquires==1&&releases==(mode!=1));
 }
 acquire_mode=0;
 for(unsigned range=0;range<2;++range) {
  gl.supported[0]=range;map_fail=1;
  assert(wined3d_buffer_map(&b,4,8,&pointer,WINED3D_MAP_DISCARD)==E_OUTOFMEMORY);
  assert(!b.flags&&!b.resource.map_count&&!dirty&&pointer==(BYTE *)(uintptr_t)0x1234);
 }
 // Reject unaligned native storage without invoking the old allocation /
 // download / delete-BO fallback, for dynamic and ordinary buffers alike.
 map_fail=0;unaligned=1;memset(gpu,0xa7,sizeof(gpu));
 for(unsigned range=0;range<2;++range)for(unsigned dynamic=0;dynamic<2;++dynamic) {
  gl.supported[0]=range;b.resource.usage=dynamic?WINED3DUSAGE_DYNAMIC:0;
  unsigned old_unmaps=unmaps;unsigned old_releases=releases;
  assert(wined3d_buffer_map(&b,4,8,&pointer,0)==WINED3DERR_INVALIDCALL);
  assert(!b.resource.map_count&&!b.flags&&!b.map_ptr&&!dirty&&b.buffer_object==7);
  assert(b.resource.heap_memory==cpu&&pointer==(BYTE *)(uintptr_t)0x1234);
  assert(unmaps==old_unmaps+1&&releases==old_releases+1&&!native_access);
  for(unsigned i=0;i<sizeof(gpu);++i)assert(gpu[i]==0xa7);
 }
 unaligned=0;gl.supported[0]=1;
 assert(!wined3d_buffer_map(&b,4,8,&pointer,WINED3D_MAP_READONLY));
 assert(pointer==gpu+4&&b.resource.map_count==1&&!dirty&&b.map_access==GL_MAP_READ_BIT);
 unsigned old_maps=maps;BYTE *outer=pointer;
 assert(wined3d_buffer_map(&b,12,4,&pointer,WINED3D_MAP_NOOVERWRITE)==WINED3DERR_INVALIDCALL);
 assert(pointer==outer&&b.resource.map_count==1&&maps==old_maps&&!dirty);
 assert(!wined3d_buffer_map(&b,12,4,&pointer,WINED3D_MAP_READONLY));
 assert(pointer==gpu+12&&b.resource.map_count==2&&maps==old_maps&&!dirty);
 // A write-only outer range map must likewise reject nested read access.
 b.resource.map_count=0;b.map_ptr=NULL;b.flags=0;dirty=0;
 assert(!wined3d_buffer_map(&b,4,8,&pointer,WINED3D_MAP_NOOVERWRITE));
 assert(b.map_access==GL_MAP_WRITE_BIT&&native_access==GL_MAP_WRITE_BIT);outer=pointer;old_maps=maps;
 assert(wined3d_buffer_map(&b,12,4,&pointer,WINED3D_MAP_READONLY)==WINED3DERR_INVALIDCALL);
 assert(pointer==outer&&b.resource.map_count==1&&maps==old_maps&&dirty==1);
 assert(!wined3d_buffer_map(&b,12,4,&pointer,WINED3D_MAP_NOOVERWRITE));
 assert(pointer==gpu+12&&b.resource.map_count==2&&dirty==2&&dirty_offset==12&&dirty_size==4);
 // Legacy glMapBuffer always grants read/write, so compatible nesting remains.
 b.resource.map_count=0;b.map_ptr=NULL;b.flags=0;dirty=0;gl.supported[0]=0;
 assert(!wined3d_buffer_map(&b,4,8,&pointer,WINED3D_MAP_READONLY));
 assert(b.map_access==(GL_MAP_READ_BIT|GL_MAP_WRITE_BIT));old_maps=maps;
 assert(!wined3d_buffer_map(&b,12,4,&pointer,WINED3D_MAP_NOOVERWRITE));
 assert(pointer==gpu+12&&b.resource.map_count==2&&maps==old_maps&&dirty==1);
 // CPU-backed double buffers still record the complete zero-size lock range.
 b.resource.map_count=0;b.map_ptr=NULL;b.flags=WINED3D_BUFFER_DOUBLEBUFFER;dirty=0;
 assert(!wined3d_buffer_map(&b,12,0,&pointer,WINED3D_MAP_NOOVERWRITE));
 assert(pointer==cpu+12&&dirty_offset==12&&dirty_size==52&&dirty==1&&!(b.flags&WINED3D_BUFFER_SYNC));
 b.resource.map_count=0;dirty=0;
 assert(!wined3d_buffer_map(&b,12,4,&pointer,WINED3D_MAP_DISCARD));
 assert(dirty==1&&!dirty_offset&&!dirty_size&&(b.flags&WINED3D_BUFFER_DISCARD));
 puts("PASS prepared buffer maps: bounded offsets/sizes, null storage, failed context/native map, readonly, no-overwrite, discard, nested locks and retry");
}
'''
with tempfile.TemporaryDirectory(prefix='dreamgpu-buffer-map-') as directory:
    work = Path(directory)
    for name in json.loads(MANIFEST.read_text())['files']:
        path = work / name
        path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'vendor/wine9x' / name, path)
    patches.apply(work, MANIFEST)
    source = (work / 'wined3d/buffer.c').read_text()
    start = source.index('HRESULT CDECL wined3d_buffer_map(')
    end = source.index('\nvoid CDECL wined3d_buffer_unmap(', start)
    resource=(work / 'wined3d/resource.c').read_text()
    access_start=resource.index('GLbitfield wined3d_resource_gl_map_flags(')
    access_end=resource.index('\n}',access_start)+2
    (work / 'test.c').write_text(PREAMBLE + resource[access_start:access_end] + source[start:end] + TEST)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    str(work / 'test.c'), '-o', str(work / 'test')], check=True)
    subprocess.run([str(work / 'test')], check=True)
