#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute prepared D3D2/3 immediate drawing and allocation failures under ASan."""
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
typedef uint32_t DWORD; typedef uint16_t WORD; typedef uint8_t BYTE;
typedef int HRESULT,BOOL,D3DPRIMITIVETYPE,D3DVERTEXTYPE;
#define WINAPI
#define TRACE(...) ((void)0)
#define ERR(...) ((void)0)
#define FALSE 0
#define TRUE 1
#define FAILED(hr) ((hr)<0)
#define D3D_OK 0
#define DDERR_INVALIDPARAMS -1
#define E_OUTOFMEMORY -2
#define D3DERR_INBEGIN -3
#define D3DERR_NOTINBEGIN -4
#define D3DPT_POINTLIST 1
#define D3DPT_TRIANGLEFAN 6
#define D3DVT_VERTEX 1
#define D3DVT_LVERTEX 2
#define D3DVT_TLVERTEX 3
#define D3DFVF_VERTEX 10
#define D3DFVF_LVERTEX 20
#define D3DFVF_TLVERTEX 30
struct d3d_device;
typedef struct { struct d3d_device *owner; } IDirect3DDevice2,IDirect3DDevice3;
struct d3d_device {
 IDirect3DDevice3 IDirect3DDevice3_iface;
 IDirect3DDevice2 IDirect3DDevice2_iface;
 IMMEDIATE_FIELDS
};
static struct d3d_device *impl_from_IDirect3DDevice3(IDirect3DDevice3 *p) { return p->owner; }
static struct d3d_device *impl_from_IDirect3DDevice2(IDirect3DDevice2 *p) { return p->owner; }
static DWORD get_flexible_vertex_size_ddraw(DWORD fvf) { return fvf==10||fvf==20||fvf==30?32:fvf==40?12:0; }
static unsigned held,draws,alloc_fail;
static HRESULT draw_result;
static DWORD drawn_type,drawn_fvf,drawn_flags,drawn_count;
static BYTE drawn[4096];
static void wined3d_mutex_lock(void) { ++held; }
static void wined3d_mutex_unlock(void) { assert(held);--held; }
static HRESULT d3d_device3_DrawPrimitive(IDirect3DDevice3 *iface,D3DPRIMITIVETYPE type,
 DWORD fvf,void *vertices,DWORD count,DWORD flags)
{
 DWORD stride=get_flexible_vertex_size_ddraw(fvf);
 assert(iface&&held&&vertices&&count*stride<=sizeof(drawn));++draws;
 drawn_type=type;drawn_fvf=fvf;drawn_flags=flags;drawn_count=count;memcpy(drawn,vertices,count*stride);return draw_result;
}
static void *allocate(size_t bytes) { return alloc_fail?NULL:malloc(bytes); }
static void *resize(void *old,size_t bytes) { return alloc_fail?NULL:realloc(old,bytes); }
#define malloc allocate
#define realloc resize
'''
TEST = r'''
int main(void)
{
 struct d3d_device d={0};d.IDirect3DDevice3_iface.owner=&d;d.IDirect3DDevice2_iface.owner=&d;
 IDirect3DDevice3 *three=&d.IDirect3DDevice3_iface;IDirect3DDevice2 *two=&d.IDirect3DDevice2_iface;
 BYTE vertices[3][32],saved[3][32];for(unsigned i=0;i<sizeof(vertices);++i)((BYTE *)vertices)[i]=i;
 memcpy(saved,vertices,sizeof(vertices));
 assert(d3d_device3_Index(three,0)==D3DERR_NOTINBEGIN);
 assert(d3d_device3_Vertex(three,vertices[0])==D3DERR_NOTINBEGIN);
 assert(d3d_device3_End(three,0)==D3DERR_NOTINBEGIN);
 assert(d3d_device3_BeginIndexed(three,4,30,NULL,3,0)==DDERR_INVALIDPARAMS);
 assert(d3d_device3_BeginIndexed(three,4,30,vertices,UINT32_MAX,0)==DDERR_INVALIDPARAMS);
 alloc_fail=1;assert(d3d_device3_BeginIndexed(three,4,30,vertices,3,0)==E_OUTOFMEMORY&&!d.immediate_active);
 alloc_fail=0;
 assert(d3d_device3_BeginIndexed(three,4,30,vertices,0,0)==DDERR_INVALIDPARAMS);
 assert(d3d_device3_BeginIndexed(three,4,0,vertices,3,0)==DDERR_INVALIDPARAMS);
 assert(d3d_device3_Begin(three,0,30,0)==DDERR_INVALIDPARAMS);
 assert(d3d_device3_Begin(three,1,0,0)==DDERR_INVALIDPARAMS);
 // First append allocation failure must preserve an empty, usable block.
 assert(!d3d_device3_BeginIndexed(three,4,30,saved,3,0));
 alloc_fail=1;assert(d3d_device3_Index(three,0)==E_OUTOFMEMORY);
 assert(d.immediate_active&&!d.nb_vertices&&!d.sysmem_vertex_buffer&&!d.buffer_size);
 alloc_fail=0;assert(!d3d_device3_Index(three,0));assert(!d3d_device3_End(three,0));
 // D3D2 forwards every legacy vertex type; all topologies preserve index order.
 for(unsigned type=1;type<=6;++type)for(unsigned fvf=1;fvf<=3;++fvf) {
  memcpy(vertices,saved,sizeof(vertices));
  assert(!d3d_device2_BeginIndexed(two,type,fvf,vertices,3,0x51));
  assert(d.immediate_active&&d.immediate_vertex_count==3);
  memset(vertices,0,sizeof(vertices)); // Caller storage never backs queued vertices.
  assert(d3d_device3_Begin(three,1,10,0)==D3DERR_INBEGIN);
  assert(d3d_device3_BeginIndexed(three,1,10,saved,3,0)==D3DERR_INBEGIN);
  assert(d3d_device3_Vertex(three,saved[0])==DDERR_INVALIDPARAMS);
  assert(d3d_device2_Index(two,3)==DDERR_INVALIDPARAMS&&!d.nb_vertices);
  unsigned order[]={2,0,2,1};
  for(unsigned i=0;i<4;++i)assert(!d3d_device2_Index(two,order[i]));
  assert(!d3d_device2_End(two,0));
  assert(drawn_type==type&&drawn_fvf==fvf*10&&drawn_flags==0x51&&drawn_count==4);
  for(unsigned i=0;i<4;++i)assert(!memcmp(drawn+i*32,saved[order[i]],32));
  assert(!d.immediate_active&&!d.immediate_vertices&&!d.nb_vertices&&!held);
 }
 // Buffer growth failure retains the old bytes and count, permitting retry.
 assert(!d3d_device3_BeginIndexed(three,4,30,saved,3,0));
 for(unsigned i=0;i<4;++i)assert(!d3d_device3_Index(three,i%3));
 BYTE *old=d.sysmem_vertex_buffer;DWORD capacity=d.buffer_size;
 alloc_fail=1;assert(d3d_device3_Index(three,2)==E_OUTOFMEMORY);
 assert(d.nb_vertices==4&&d.sysmem_vertex_buffer==old&&d.buffer_size==capacity&&!memcmp(old,saved[0],32));
 alloc_fail=0;assert(!d3d_device3_Index(three,2));draw_result=-17;
 assert(d3d_device3_End(three,0)==-17&&!d.immediate_active&&!d.immediate_vertices);
 draw_result=0;
 // Ordinary Begin/Vertex remains valid; wrong-mode Index has no side effects.
 assert(!d3d_device2_Begin(two,4,3,0));assert(d3d_device2_Index(two,0)==DDERR_INVALIDPARAMS);
 assert(!d3d_device2_Vertex(two,saved[1]));assert(!d3d_device2_Vertex(two,saved[2]));
 assert(!d3d_device2_End(two,0)&&drawn_count==2&&!memcmp(drawn,saved[1],64));
 unsigned before=draws;assert(!d3d_device3_Begin(three,1,10,0));assert(!d3d_device3_End(three,0)&&draws==before);
 assert(d3d_device2_BeginIndexed(two,4,9,saved,3,0)==DDERR_INVALIDPARAMS);
 assert(d3d_device3_BeginIndexed(three,7,30,saved,3,0)==DDERR_INVALIDPARAMS&&!d.immediate_active);
 // Device state and owned arrays remain isolated with interleaved immediate blocks.
 struct d3d_device other={0};other.IDirect3DDevice3_iface.owner=&other;
 assert(!d3d_device3_BeginIndexed(three,4,30,saved,3,0x12));
 assert(!d3d_device3_BeginIndexed(&other.IDirect3DDevice3_iface,2,30,saved,3,0x34));
 assert(!d3d_device3_Index(three,2));
 assert(!d3d_device3_Index(&other.IDirect3DDevice3_iface,1));
 assert(!d3d_device3_End(three,0)&&drawn_flags==0x12&&!memcmp(drawn,saved[2],32));
 assert(other.immediate_active&&other.nb_vertices==1);
 assert(!d3d_device3_End(&other.IDirect3DDevice3_iface,0)&&drawn_flags==0x34&&!memcmp(drawn,saved[1],32));
 free(other.sysmem_vertex_buffer);
 // FVF changes between blocks honor the new stride despite retained capacity.
 BYTE packed[3][12];memcpy(packed,saved,sizeof(packed));
 assert(!d3d_device3_BeginIndexed(three,2,40,packed,3,0));
 assert(!d3d_device3_Index(three,2));assert(!d3d_device3_Index(three,0));
 assert(!d3d_device3_End(three,0)&&drawn_count==2);
 assert(!memcmp(drawn,packed[2],12)&&!memcmp(drawn+12,packed[0],12));
 before=draws;assert(!d3d_device3_BeginIndexed(three,1,40,packed,3,0));
 assert(!d3d_device3_End(three,0)&&draws==before&&!d.immediate_vertices);
 // The arithmetic guard is exercised without creating a huge allocation.
 assert(!d3d_device3_Begin(three,1,30,0));d.nb_vertices=UINT32_MAX/32;
 assert(d3d_device3_Vertex(three,saved)==E_OUTOFMEMORY);d.nb_vertices=0;
 assert(!d3d_device3_End(three,0));free(d.sysmem_vertex_buffer);assert(!held);
 puts("PASS prepared D3D2/3 Begin/BeginIndexed/Vertex/Index/End: topology, owned vertices, bounds, OOM retry, nesting, variable stride, device isolation, failure propagation and reuse");
}
'''

def function(source, prefix):
    start = source.index(prefix)
    return source[start:source.index('\n}', start) + 2]

with tempfile.TemporaryDirectory(prefix='dreamgpu-indexed-') as directory:
    work = Path(directory)
    for name in json.loads(MANIFEST.read_text())['files']:
        p = work / name
        p.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'vendor/wine9x' / name, p)
    patches.apply(work, MANIFEST)
    source = (work / 'ddraw/device.c').read_text()
    header = (work / 'ddraw/ddraw_private.h').read_text()
    start = header.index('    D3DPRIMITIVETYPE primitive_type;')
    fields = header[start:header.index('    /* Handle management */', start)]
    names = ['d3d_device3_Begin', 'd3d_device2_Begin', 'd3d_device3_BeginIndexed',
             'd3d_device2_BeginIndexed', 'd3d_device_append_vertex', 'd3d_device3_Vertex',
             'd3d_device2_Vertex', 'd3d_device3_Index', 'd3d_device2_Index',
             'd3d_device3_End', 'd3d_device2_End']
    code = PREAMBLE.replace('IMMEDIATE_FIELDS', fields) + '\n'.join(
        function(source, 'static HRESULT ' + ('' if name == 'd3d_device_append_vertex' else 'WINAPI ') + name + '(')
        for name in names) + TEST
    (work / 'test.c').write_text(code)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-parameter', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    str(work / 'test.c'), '-o', str(work / 'test')], check=True)
    subprocess.run([str(work / 'test')], check=True)
