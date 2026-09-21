#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute prepared Wine depth conversions/transfers with sanitizer guard regions."""
import importlib.util,json,os,shutil,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
spec=importlib.util.spec_from_file_location('patches',ROOT/'tests/guest/support/patches.py')
patches=importlib.util.module_from_spec(spec);spec.loader.exec_module(patches)
MANIFEST=ROOT/'support/guest/d3d/patches/base/manifest.json'
PREAMBLE=r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef int BOOL,HRESULT,GLenum;
typedef uint32_t DWORD,GLuint,UINT;
typedef uint8_t BYTE;
typedef uint64_t ULONGLONG;
typedef size_t SIZE_T;
#define TRUE 1
#define FALSE 0
#define WINED3D_OK 0
#define WINED3DERR_INVALIDCALL -1
#define E_OUTOFMEMORY -2
#define FAILED(hr) ((hr)<0)
#define WINED3DCLEAR_ZBUFFER 1
#define WINED3DCLEAR_STENCIL 2
#define WINED3D_LOCATION_DISCARDED 32
#define WINED3D_LOCATION_DRAWABLE 16
#define WINED3D_LOCATION_SYSMEM 1
#define ORM_BACKBUFFER 1
#define WINED3DFMT_D16_LOCKABLE 1
#define WINED3DFMT_D16_UNORM 2
#define WINED3DFMT_D32_UNORM 3
#define WINED3DFMT_D24_UNORM_S8_UINT 4
#define WINED3DFMT_X8D24_UNORM 5
#define WINED3DFMT_S1_UINT_D15_UNORM 6
#define WINED3DFMT_S4X4_UINT_D24_UNORM 7
struct { int offscreen_rendering_mode; } wined3d_settings={ORM_BACKBUFFER};
static const DWORD surface_simple_locations=15;
struct format { unsigned id,byte_count,depth_size,stencil_size; };
struct wined3d_device;
struct resource { unsigned width,height;struct format *format;struct wined3d_device *device; };
struct wined3d_surface { struct resource resource;DWORD locations;struct {unsigned cx,cy;} ds_current_size; };
struct wined3d_gl_info {struct {struct {
 void (*noop)();
 void (*push)();
 void (*push_attrib)(unsigned);
 void (*pop)();
 void (*p_glReadPixels)(int,int,int,int,int,int,void*);
 void (*p_glDrawPixels)(int,int,int,int,const void*);
 GLenum (*p_glGetError)(void);
} gl;} gl_ops;};
struct wined3d_context {int valid;struct wined3d_surface *current_rt;const struct wined3d_gl_info *gl_info;BOOL render_offscreen;void *hdc,*glCtx;};
struct wined3d_device {struct wined3d_surface *onscreen_depth_stencil,*backbuffer_ds_rt;struct wined3d_context *backbuffer_ds_context;void *backbuffer_ds_hdc,*backbuffer_ds_glctx;};
struct wined3d_bo_address {void *addr;unsigned buffer_object;};
static struct wined3d_context *current;
static BYTE storage[40],gpu_stencil[6];
static GLuint gpu_depth[6];
static unsigned fail_alloc,fail_read,fail_write,error,pbo,missing,gl_calls;
static unsigned row_pitch,drawable_w=3,drawable_h=2;
static unsigned push_number,fail_push,stack_depth,transfers;
static void push(void) { ++gl_calls; if (++push_number==fail_push) error=1; else ++stack_depth; }
/* Match the shipped GL1.1 frontend, including rejection of Wine's modern
 * GL_ALL_ATTRIB_BITS value. A no-op GL mock hid this installed-provider bug. */
static unsigned attrib_mask;
static void push_attrib(unsigned mask)
{
 if(mask & ~0x000fffffu){error=0x0501;return;}
 attrib_mask=mask;push();
}
static void pop(void) { ++gl_calls; assert(stack_depth); --stack_depth; }
static void noop(void) {++gl_calls;}
static struct wined3d_context *context_get_current(void) {return current;}
static UINT wined3d_surface_get_pitch(struct wined3d_surface *s) {(void)s;return row_pitch;}
static void surface_get_drawable_size(struct wined3d_surface *s,struct wined3d_context *c,UINT *w,UINT *h)
{assert(s&&c);*w=drawable_w;*h=drawable_h;}
static void surface_get_memory(struct wined3d_surface *s,struct wined3d_bo_address *m,DWORD loc)
{assert(s&&loc);m->addr=missing?NULL:storage+4;m->buffer_object=pbo;}
static void surface_validate_location(struct wined3d_surface *s,DWORD loc) {s->locations|=loc;}
static void *allocate(size_t size) {return fail_alloc?NULL:malloc(size);}
#define malloc allocate
'''
CLEAR_PREAMBLE=r'''
typedef struct {int left,top,right,bottom;} RECT;
static unsigned ds_loads,ds_fail;
static HRESULT surface_load_ds_location(struct wined3d_surface *s,struct wined3d_context *c,DWORD loc)
{ (void)c;(void)loc;++ds_loads;if(ds_fail)return -1;s->ds_current_size.cx=s->resource.width;s->ds_current_size.cy=s->resource.height;return 0; }
static void SetRect(RECT *r,int l,int t,int x,int y){*r=(RECT){l,t,x,y};}
static void SetRectEmpty(RECT *r){SetRect(r,0,0,0,0);}
static int EqualRect(const RECT *a,const RECT *b){return !memcmp(a,b,sizeof(*a));}
static void IntersectRect(RECT *r,const RECT *a,const RECT *b)
{r->left=a->left>b->left?a->left:b->left;r->top=a->top>b->top?a->top:b->top;r->right=a->right<b->right?a->right:b->right;r->bottom=a->bottom<b->bottom?a->bottom:b->bottom;if(r->right<=r->left||r->bottom<=r->top)SetRectEmpty(r);}
'''
TEST=r'''
#undef malloc
static void read_pixels(int x,int y,int w,int h,int format,int type,void *data)
{
 assert(!x&&!y&&w==3&&h==2);++gl_calls;++transfers;
 if(fail_read){error=1;memset(data,0xcc,format==GL_DEPTH_COMPONENT?24:6);return;}
 if(format==GL_DEPTH_COMPONENT){assert(type==GL_UNSIGNED_INT);memcpy(data,gpu_depth,24);}
 else {assert(format==GL_STENCIL_INDEX&&type==GL_UNSIGNED_BYTE);memcpy(data,gpu_stencil,6);}
}
static void draw_pixels(int w,int h,int format,int type,const void *data)
{
 assert(w==3&&h==2);++gl_calls;++transfers;
 if(fail_write){error=1;return;}
 if(format==GL_DEPTH_COMPONENT){assert(type==GL_UNSIGNED_INT);memcpy(gpu_depth,data,24);}
 else {assert(format==GL_STENCIL_INDEX&&type==GL_UNSIGNED_BYTE);memcpy(gpu_stencil,data,6);}
}
static GLenum get_error(void) {unsigned e=error;error=0;return e;}
int main(void)
{
 struct format format={1,2,16,0};
 struct wined3d_device device={0};
 struct wined3d_surface surface={{3,2,&format,&device},16,{0,0}};
 struct wined3d_gl_info gl={{{noop,push,push_attrib,pop,read_pixels,draw_pixels,get_error}}};
 struct wined3d_context context={1,&surface,&gl,1,&device,&surface};
 device.onscreen_depth_stencil=&surface;device.backbuffer_ds_rt=&surface;device.backbuffer_ds_context=&context;
 device.backbuffer_ds_hdc=context.hdc;device.backbuffer_ds_glctx=context.glCtx;
 BYTE saved[40];DWORD mask,smask,value;unsigned i,j,flip,id;
 current=&context;
 // The pinned Wine header uses the later all-ones token. GL1.1 must reject it;
 // the prepared depth helper must instead select the exact affected groups.
 assert(GL_ALL_ATTRIB_BITS==0xffffffffu);
 push_attrib(GL_ALL_ATTRIB_BITS);
 assert(get_error()==0x0501&&!stack_depth&&!push_number);
 for(id=1;id<=7;++id) for(flip=0;flip<2;++flip)
 {
  format.id=id;format.byte_count=(id==1||id==2||id==6)?2:4;
  row_pitch=3*format.byte_count+2;context.render_offscreen=!flip;
  assert(surface_depth_layout(&surface,&mask,&smask));
  memset(storage,0xa5,sizeof(storage));
  for(i=0;i<6;++i){gpu_depth[i]=(i==0?0:i==5?0xffffffffu:i*0x22222222u);gpu_stencil[i]=i+1;}
  surface.locations=16;
  assert(surface_transfer_depth(&surface,&context,1)==0&&surface.locations==17);
  assert(attrib_mask==(GL_CURRENT_BIT|GL_PIXEL_MODE_BIT|GL_DEPTH_BUFFER_BIT
         |GL_STENCIL_BUFFER_BIT|GL_ENABLE_BIT|GL_COLOR_BUFFER_BIT|GL_VIEWPORT_BIT|GL_TRANSFORM_BIT));
  for(i=0;i<2;++i)for(j=0;j<3;++j)
  {
   unsigned index=(flip?1-i:i)*3+j;value=0;
   memcpy(&value,storage+4+i*row_pitch+j*format.byte_count,format.byte_count);
   DWORD expected=(DWORD)(((ULONGLONG)gpu_depth[index]*mask+0x7fffffff)/0xffffffffu);
   assert((value&mask)==expected);
   if(smask)assert((value>>(mask==0x7fff?15:24))==(gpu_stencil[index]&smask));
  }
  assert(storage[0]==0xa5&&storage[3]==0xa5&&storage[4+3*format.byte_count]==0xa5);
  assert(storage[4+row_pitch+3*format.byte_count]==0xa5&&storage[39]==0xa5);
  // CPU editing + writeback must use normalized depth and preserve stencil.
  value=mask|(smask<<(mask==0x7fff?15:24));memcpy(storage+4,&value,format.byte_count);
  surface.locations=1;memcpy(saved,storage,sizeof(storage));
  assert(surface_transfer_depth(&surface,&context,16)==0&&surface.locations==17);
  assert(gpu_depth[flip?3:0]==0xffffffffu);
  if(smask)assert(gpu_stencil[flip?3:0]==smask);
  assert(!memcmp(saved,storage,sizeof(storage)));
 }
 // Null private-provider exports are detected before allocation, pushes or
 // readback. A missing upload-only operation must not block a CPU download.
 {
  struct wined3d_gl_info missing_gl=gl;
  unsigned before=gl_calls;
  context.gl_info=&missing_gl;
  for(i=0;i<7;i++)
  {
   missing_gl=gl;
   if(i==0)missing_gl.gl_ops.gl.noop=NULL;
   if(i==1)missing_gl.gl_ops.gl.push=NULL;
   if(i==2)missing_gl.gl_ops.gl.push_attrib=NULL;
   if(i==3)missing_gl.gl_ops.gl.pop=NULL;
   if(i==4)missing_gl.gl_ops.gl.p_glGetError=NULL;
   if(i==5)missing_gl.gl_ops.gl.p_glReadPixels=NULL;
   if(i==6)context.gl_info=NULL;
   surface.locations=16;memcpy(saved,storage,40);
   assert(surface_transfer_depth(&surface,&context,1)<0);
   assert(surface.locations==16&&!memcmp(saved,storage,40)&&gl_calls==before);
  }
  context.gl_info=&missing_gl;missing_gl=gl;missing_gl.gl_ops.gl.p_glDrawPixels=NULL;
  surface.locations=1;
  assert(surface_transfer_depth(&surface,&context,16)<0&&surface.locations==1&&gl_calls==before);
  surface.locations=16;assert(!surface_transfer_depth(&surface,&context,1));
  context.gl_info=&gl;
 }
 // The physical drawable belongs to exactly one depth/context/target tuple.
 // A stale surface flag alone can never authorize reading another target's depth.
 for(i=0;i<5;++i)
 {
  unsigned before=gl_calls;surface.locations=16;memcpy(saved,storage,sizeof(storage));
  device.onscreen_depth_stencil=i==0?NULL:&surface;
  device.backbuffer_ds_rt=i==1?NULL:&surface;
  device.backbuffer_ds_context=i==2?NULL:&context;
  device.backbuffer_ds_hdc=i==3?NULL:context.hdc;
  device.backbuffer_ds_glctx=i==4?NULL:context.glCtx;
  assert(surface_transfer_depth(&surface,&context,1)<0&&surface.locations==16);
  assert(gl_calls==before&&!memcmp(saved,storage,sizeof(storage)));
  surface.locations=1;assert(surface_transfer_depth(&surface,&context,16)<0&&surface.locations==1);
 }
 device.backbuffer_ds_glctx=context.glCtx;
 // Overflow each state stack independently. No application frame is popped,
 // no pixels are transferred and authoritative locations remain unchanged.
 for(i=1;i<=4;++i)
 {
  unsigned before=transfers;
  surface.locations=1;push_number=0;fail_push=i;stack_depth=7;
  assert(surface_transfer_depth(&surface,&context,16)<0);
  assert(surface.locations==1&&stack_depth==7&&transfers==before);
 }
 for(i=1;i<=2;++i)
 {
  unsigned before=transfers;
  surface.locations=16;push_number=0;fail_push=i;stack_depth=7;
  memcpy(saved,storage,sizeof(storage));
  assert(surface_transfer_depth(&surface,&context,1)<0);
  assert(surface.locations==16&&stack_depth==7&&transfers==before&&!memcmp(saved,storage,40));
 }
 fail_push=0;push_number=0;stack_depth=0;
 // Failed download must not publish partially copied bytes; failed upload must
 // preserve CPU authority. Both retry after the transient boundary recovers.
 surface.locations=16;memcpy(saved,storage,sizeof(storage));fail_read=1;
 assert(surface_transfer_depth(&surface,&context,1)<0&&surface.locations==16&&!memcmp(saved,storage,40));
 fail_read=0;assert(!surface_transfer_depth(&surface,&context,1));
 surface.locations=1;fail_write=1;
 assert(surface_transfer_depth(&surface,&context,16)<0&&surface.locations==1);
 fail_write=0;assert(!surface_transfer_depth(&surface,&context,16));
 surface.locations=16;
 for(i=0;i<7;++i)
 {
  unsigned before=gl_calls;
  fail_alloc=i==0;context.valid=i!=1;current=i==2?NULL:&context;
  missing=i==3;pbo=i==4;drawable_w=i==5?2:3;format.id=i==6?99:7;
  assert(surface_transfer_depth(&surface,&context,1)<0&&surface.locations==16&&gl_calls==before);
 }
 // Execute the actual clear preparation policy: even a full depth clear must
 // preserve stencil, and a full stencil clear must preserve depth.
 {RECT full={0,0,3,2},partial={0,0,1,2},out={0};
  surface.locations=1;format.depth_size=24;format.stencil_size=8;
  assert(!prepare_ds_clear(&surface,&context,16,1,&full,1,&full,&out)&&ds_loads==1);
  assert(!prepare_ds_clear(&surface,&context,16,2,&full,1,&full,&out)&&ds_loads==2);
  ds_fail=1;
  assert(prepare_ds_clear(&surface,&context,16,1,&full,1,&full,&out)<0);
  assert(prepare_ds_clear(&surface,&context,16,3,&full,1,&partial,&out)<0);
  ds_loads=0;
  assert(!prepare_ds_clear(&surface,&context,16,3,&full,1,&full,&out)&&!ds_loads);
 }
 puts("PASS prepared depth pipeline: real Wine GL constants and strict GL1.1 attribute masks, seven normalized layouts, stencil, padded pitch, orientation, guard bytes, CPU edits, private export coverage, null entrypoints, allocation/binding/read/write failures and retries");
}
'''
def function(source,prefix):
 start=source.index(prefix);return source[start:source.index('\n}',start)+2]
with tempfile.TemporaryDirectory(prefix='dreamgpu-depth-') as directory:
 work=Path(directory)
 for name in json.loads(MANIFEST.read_text())['files']:
  p=work/name;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/'vendor/wine9x'/name,p)
 patches.apply(work,MANIFEST);source=(work/'wined3d/surface.c').read_text()
 owner=function((work/'wined3d/device.c').read_text(),'BOOL device_owns_backbuffer_ds(')
 code=owner+'\n'+'\n'.join(function(source,p) for p in ['static BOOL surface_depth_layout(', 'static void surface_depth_convert(', 'static BOOL surface_depth_transfer_available(', 'static HRESULT surface_transfer_depth('])
 import re
 transfer=function(source,'static HRESULT surface_transfer_depth(')
 available=function(source,'static BOOL surface_depth_transfer_available(')
 called=set(re.findall(r'\.p_(gl\w+)\(',transfer))
 guarded=set(re.findall(r'\.p_(gl\w+)',available))
 assert called==guarded, ('depth entrypoint preflight drift',called-guarded,guarded-called)
 exports=set(re.findall(r'^\s+(gl\w+)=',(ROOT/'guest/opengl/frontend.def').read_text(),re.M))
 assert called<=exports, ('depth functions absent from actual private-provider exports',called-exports)
 enums=sorted(set(re.findall(r'\bGL_[A-Z_0-9]+\b',code))|{'GL_ALL_ATTRIB_BITS'})
 # Preserve actual donor values: GL_ALL_ATTRIB_BITS differs from the shipped
 # GL1.1 SDK. Synthetic sequential enums cannot expose cross-layer mask errors.
 header=(ROOT/'vendor/wine9x/include/wine/wgl.h').read_text()
 constants=dict(re.findall(r'^#define\s+(GL_[A-Z_0-9]+)\s+(0x[0-9a-fA-F]+|[0-9]+)\s*$',header,re.M))
 definitions='\n'.join('#define %s %s'%(e,constants[e]) for e in enums)
 names=sorted(set(re.findall(r'p_gl\w+',code))-{'p_glReadPixels','p_glDrawPixels','p_glGetError'})
 definitions+='\n'+'\n'.join('#define %s %s'%(n,'push_attrib' if n=='p_glPushAttrib' else 'push' if 'Push' in n else 'pop' if 'Pop' in n else 'noop') for n in names)
 clear=function((work/'wined3d/device.c').read_text(),'static HRESULT prepare_ds_clear(')
 p=work/'test.c';p.write_text(PREAMBLE+definitions+'\n'+code+CLEAR_PREAMBLE+clear+TEST)
 subprocess.run([os.environ.get('CC','cc'),'-std=c99','-Wall','-Wextra','-Werror','-Wno-deprecated-non-prototype','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(p),'-o',str(work/'test')],check=True)
 subprocess.run([str(work/'test')],check=True)
