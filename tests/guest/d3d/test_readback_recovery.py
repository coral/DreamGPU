#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run the prepared readback pipeline against real success/failure boundaries."""
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
typedef int HRESULT,BOOL,GLenum;typedef unsigned DWORD,UINT;typedef uint8_t BYTE;
#define TRUE 1
#define FALSE 0
#define WINED3D_OK 0
#define WINED3DERR_INVALIDCALL -1
#define E_OUTOFMEMORY -2
#define FAILED(hr) ((hr)<0)
#define TRACE(...) ((void)0)
#define WARN(...) ((void)0)
#define ERR(...) ((void)0)
#define FIXME(...) ((void)0)
#define checkGLcall(...) ((void)0)
#define GL_EXTCALL(call) ((void)gl_info, call)
#define WINED3D_LOCATION_SYSMEM 1
#define WINED3D_LOCATION_DIB 2
#define WINED3D_LOCATION_USER_MEMORY 4
#define WINED3D_LOCATION_BUFFER 8
#define WINED3D_LOCATION_TEXTURE_RGB 16
#define WINED3D_LOCATION_TEXTURE_SRGB 32
#define WINED3D_LOCATION_DRAWABLE 64
#define WINED3D_LOCATION_RB_MULTISAMPLE 128
#define WINED3D_LOCATION_RB_RESOLVED 256
#define WINED3D_LOCATION_DISCARDED 512
#define WINED3DUSAGE_DEPTHSTENCIL 1
#define ORM_BACKBUFFER 1
#define ORM_FBO 2
static struct { unsigned offscreen_rendering_mode; } wined3d_settings={ORM_BACKBUFFER};
#define WINED3D_RESOURCE_ACCESS_CPU 1
#define WINED3D_RESOURCE_ACCESS_GPU 2
#define SFLAG_LOST 1
#define GL_PIXEL_PACK_BUFFER 1
#define GL_PIXEL_UNPACK_BUFFER 2
#define GL_PACK_ROW_LENGTH 3
#define GL_READ_WRITE 4
static const unsigned surface_simple_locations=15;
struct wined3d_device { unsigned offscreenBuffer; };
struct format { unsigned byte_count,glFormat,glType; };
struct resource { struct wined3d_device *device;unsigned size,width,height,usage,access_flags,draw_binding;struct format *format; };
struct wined3d_texture { struct resource resource; };
struct wined3d_surface { struct resource resource;struct wined3d_texture *container;unsigned locations,flags; };
struct wined3d_gl_info { struct { struct {
 void (*p_glReadBuffer)(unsigned);
 void (*p_glPixelStorei)(unsigned,unsigned);
 void (*p_glReadPixels)(unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,void *);
} gl; } gl_ops; };
struct wined3d_context { BOOL valid;struct wined3d_surface *current_rt;const struct wined3d_gl_info *gl_info; };
struct wined3d_bo_address { unsigned buffer_object;void *addr; };
static BYTE cpu_src[16],cpu_dst[16];
static struct wined3d_context *tls,*parent;
static struct wined3d_context nested;
static unsigned acquire_mode,acquires,releases,restores,gl_calls,downloads,validations,evictions;
static unsigned use_pbo,missing_memory,offscreen=1,fail_alloc;
static unsigned depth_owner_valid=1,depth_owner_checks;
static BOOL device_owns_backbuffer_ds(const struct wined3d_device *d,
        const struct wined3d_context *c,const struct wined3d_surface *s)
{ ++depth_owner_checks;return depth_owner_valid&&c&&c->valid&&c==parent&&s->resource.device==d; }
static struct wined3d_context *context_get_current(void) { return tls; }
static struct wined3d_context *context_acquire(struct wined3d_device *d,struct wined3d_surface *s)
{
 assert(d);++acquires;
 if(acquire_mode==1)return NULL;
 nested.current_rt=s;nested.gl_info=parent->gl_info;nested.valid=acquire_mode!=2;
 tls=acquire_mode==3?NULL:&nested;return &nested;
}
static void context_release(struct wined3d_context *c) { assert(c);++releases;tls=parent; }
static void context_restore(struct wined3d_context *c,struct wined3d_surface *r)
{ assert(c&&r);++restores;tls=parent; }
static void context_apply_blit_state(struct wined3d_context *c,struct wined3d_device *d)
{ assert(c&&c->valid&&tls==c&&d);++gl_calls; }
static void surface_get_memory(struct wined3d_surface *s,struct wined3d_bo_address *out,unsigned location)
{ (void)s;out->buffer_object=location==WINED3D_LOCATION_BUFFER&&use_pbo;out->addr=missing_memory?NULL:location==WINED3D_LOCATION_DIB?cpu_src:cpu_dst; }
static unsigned wined3d_surface_get_pitch(struct wined3d_surface *s) { return s->resource.width*s->resource.format->byte_count; }
static BOOL wined3d_resource_is_offscreen(struct resource *r) { assert(r);return offscreen; }
static unsigned surface_get_gl_buffer(struct wined3d_surface *s) { assert(s);return 4; }
static void read_buffer(unsigned b) { (void)b;++gl_calls; }
static void store(unsigned p,unsigned v) { assert(p==GL_PACK_ROW_LENGTH);(void)v;++gl_calls; }
static void pixels(unsigned x,unsigned y,unsigned w,unsigned h,unsigned f,unsigned t,void *p)
{ assert(!x&&!y&&w==2&&h==2);(void)f;(void)t;assert(p);memset(p,0x75,16);++gl_calls; }
static void glBindBuffer(unsigned type,unsigned buffer) { (void)type;(void)buffer;++gl_calls; }
static void glBufferSubData(unsigned type,unsigned offset,unsigned size,void *p)
{ (void)type;assert(!offset&&size==16&&p);++gl_calls; }
static void glGetBufferSubData(unsigned type,unsigned offset,unsigned size,void *p)
{ (void)type;assert(!offset&&size==16&&p);memset(p,0x76,16);++gl_calls; }
static void *glMapBuffer(unsigned type,unsigned access) { (void)type;(void)access;++gl_calls;return cpu_dst; }
static void glUnmapBuffer(unsigned type) { (void)type;++gl_calls; }
static void wined3d_texture_bind_and_dirtify(struct wined3d_texture *t,struct wined3d_context *c,BOOL srgb)
{ assert(t&&c&&c->valid&&c==tls);(void)srgb;++gl_calls; }
static void surface_download_data(struct wined3d_surface *s,const struct wined3d_gl_info *g,DWORD loc)
{ assert(s&&g);(void)loc;memset(cpu_dst,0x77,16);++downloads; }
static HRESULT surface_load_ds_location(struct wined3d_surface *s,struct wined3d_context *c,DWORD loc)
{ (void)s;(void)c;(void)loc;assert(0);return WINED3DERR_INVALIDCALL; }
static HRESULT surface_transfer_depth(struct wined3d_surface *s,struct wined3d_context *c,DWORD loc)
{ (void)s;(void)c;(void)loc;assert(0);return WINED3DERR_INVALIDCALL; }
static HRESULT surface_load_drawable(struct wined3d_surface *s,struct wined3d_context *c)
{ (void)s;(void)c;assert(0);return -1; }
static void surface_multisample_resolve(struct wined3d_surface *s,struct wined3d_context *c)
{ (void)s;(void)c;assert(0); }
static HRESULT surface_load_texture(struct wined3d_surface *s,struct wined3d_context *c,BOOL srgb)
{ (void)s;(void)c;(void)srgb;return WINED3DERR_INVALIDCALL; }
static void surface_validate_location(struct wined3d_surface *s,DWORD loc) { s->locations|=loc;++validations; }
static void surface_evict_sysmem(struct wined3d_surface *s) { (void)s;++evictions; }
static HRESULT surface_load_location(struct wined3d_surface *,struct wined3d_context *,DWORD);
static void *test_malloc(size_t size) { return fail_alloc?NULL:malloc(size); }
#define malloc test_malloc
'''
TEST=r'''
#undef malloc
static void reset(struct wined3d_surface *s,struct wined3d_context *c,unsigned locations)
{
 s->locations=locations;c->valid=1;tls=parent=c;
 acquire_mode=acquires=releases=restores=gl_calls=downloads=validations=evictions=0;
 use_pbo=missing_memory=fail_alloc=depth_owner_checks=0;offscreen=depth_owner_valid=1;
 memset(cpu_dst,0xaa,16);memset(cpu_src,0x55,16);
}
int main(void)
{
 struct wined3d_device device={0};struct format format={4,1,2};
 struct wined3d_texture texture={0};struct wined3d_surface target={0},other={0};
 struct wined3d_gl_info gl={{{read_buffer,store,pixels}}};
 struct wined3d_context context={1,&target,&gl};
 target.container=&texture;target.resource=(struct resource){&device,16,2,2,0,3,0,&format};
 // A missing/invalid GL binding must never enter the fog wrapper or validate a
 // CPU location that still contains its old sentinel bytes.
 for(unsigned mode=0;mode<3;++mode) {
  reset(&target,&context,WINED3D_LOCATION_DRAWABLE);
  if(mode==1)context.valid=0;if(mode==2)tls=NULL;
  assert(surface_load_location(&target,mode?&context:NULL,WINED3D_LOCATION_SYSMEM)==WINED3DERR_INVALIDCALL);
  assert(target.locations==WINED3D_LOCATION_DRAWABLE&&!validations&&!gl_calls&&cpu_dst[0]==0xaa);
 }
 // Nested target acquisition can fail even when the caller owns a valid context.
 context.current_rt=&other;
 for(unsigned mode=1;mode<=3;++mode) {
  reset(&target,&context,WINED3D_LOCATION_DRAWABLE);acquire_mode=mode;
  assert(surface_load_location(&target,&context,WINED3D_LOCATION_SYSMEM)==WINED3DERR_INVALIDCALL);
  assert(acquires==1&&restores==(mode!=1)&&!gl_calls&&!validations&&cpu_dst[0]==0xaa);
  assert(target.locations==WINED3D_LOCATION_DRAWABLE);
 }
 // A later successful acquisition reads pixels and only then validates them.
 reset(&target,&context,WINED3D_LOCATION_DRAWABLE);
 assert(surface_load_location(&target,&context,WINED3D_LOCATION_SYSMEM)==WINED3D_OK);
 assert(acquires==1&&restores==1&&validations==1&&cpu_dst[0]==0x75&&cpu_dst[15]==0x75);
 assert(target.locations==(WINED3D_LOCATION_DRAWABLE|WINED3D_LOCATION_SYSMEM));
 // CPU-only copies work without any GL, while missing backing memory fails.
 reset(&target,&context,WINED3D_LOCATION_DIB);
 assert(surface_load_location(&target,NULL,WINED3D_LOCATION_SYSMEM)==WINED3D_OK);
 assert(!gl_calls&&!acquires&&cpu_dst[0]==0x55&&cpu_dst[15]==0x55&&validations==1);
 reset(&target,&context,WINED3D_LOCATION_DIB);missing_memory=1;
 assert(surface_load_location(&target,NULL,WINED3D_LOCATION_SYSMEM)==WINED3DERR_INVALIDCALL&&!validations);
 // PBO downloads need their own valid binding; failure leaves locations intact.
 reset(&target,&context,WINED3D_LOCATION_BUFFER);use_pbo=1;acquire_mode=2;
 assert(surface_load_location(&target,&context,WINED3D_LOCATION_SYSMEM)==WINED3DERR_INVALIDCALL);
 assert(releases==1&&!gl_calls&&!validations&&target.locations==WINED3D_LOCATION_BUFFER);
 // Texture download requires a current context too; same verified contents can
 // still be returned without GL when the requested location is already valid.
 context.current_rt=&target;reset(&target,&context,WINED3D_LOCATION_TEXTURE_RGB);context.valid=0;
 assert(surface_load_location(&target,&context,WINED3D_LOCATION_SYSMEM)==WINED3DERR_INVALIDCALL&&!downloads);
 target.locations|=WINED3D_LOCATION_SYSMEM;
 assert(surface_load_location(&target,NULL,WINED3D_LOCATION_SYSMEM)==WINED3D_OK);
 // Failure resolving a renderbuffer also propagates without claiming a CPU copy.
 reset(&target,&context,WINED3D_LOCATION_RB_MULTISAMPLE);
 assert(surface_load_location(&target,&context,WINED3D_LOCATION_SYSMEM)==WINED3DERR_INVALIDCALL&&!validations);
 // Failure allocating the onscreen row-flip scratch does not validate readback.
 reset(&target,&context,WINED3D_LOCATION_DRAWABLE);offscreen=0;fail_alloc=1;
 assert(surface_load_location(&target,&context,WINED3D_LOCATION_SYSMEM)==E_OUTOFMEMORY&&!validations);
 assert(target.locations==WINED3D_LOCATION_DRAWABLE);
 // An already-current depth DRAWABLE flag does not bypass its physical owner.
 target.resource.usage=WINED3DUSAGE_DEPTHSTENCIL;
 reset(&target,&context,WINED3D_LOCATION_DRAWABLE);depth_owner_valid=0;
 assert(surface_load_location(&target,&context,WINED3D_LOCATION_DRAWABLE)==WINED3DERR_INVALIDCALL);
 assert(depth_owner_checks==1&&!gl_calls&&!validations&&target.locations==WINED3D_LOCATION_DRAWABLE);
 assert(cpu_dst[0]==0xaa&&cpu_dst[15]==0xaa);
 // A proven owner can use the current GPU copy without an unnecessary transfer.
 depth_owner_valid=1;
 assert(surface_load_location(&target,&context,WINED3D_LOCATION_DRAWABLE)==WINED3D_OK);
 assert(depth_owner_checks==2&&!gl_calls&&!validations);
 // CPU-authoritative depth remains readable/copyable without a matching drawable.
 reset(&target,&context,WINED3D_LOCATION_SYSMEM);depth_owner_valid=0;
 assert(surface_load_location(&target,NULL,WINED3D_LOCATION_SYSMEM)==WINED3D_OK);
 assert(!depth_owner_checks&&!gl_calls&&!validations);
 reset(&target,&context,WINED3D_LOCATION_DIB);depth_owner_valid=0;
 assert(surface_load_location(&target,NULL,WINED3D_LOCATION_SYSMEM)==WINED3D_OK);
 assert(!depth_owner_checks&&!gl_calls&&validations==1&&cpu_dst[0]==0x55&&cpu_dst[15]==0x55);
 puts("PASS prepared readback pipeline: failed context/target/PBO/resolve/OOM preserves location validity, retry succeeds, CPU copies need no GL, depth DRAWABLE fast path requires physical ownership");
}
'''
def function(source,prefix):
 start=source.index(prefix);return source[start:source.index('\n}',start)+2]
with tempfile.TemporaryDirectory(prefix='dreamgpu-readback-recovery-') as directory:
 work=Path(directory)
 for name in json.loads(MANIFEST.read_text())['files']:
  p=work/name;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/'vendor/wine9x'/name,p)
 patches.apply(work,MANIFEST);source=(work/'wined3d/surface.c').read_text()
 code=PREAMBLE+'\n'.join(function(source,p) for p in ['static HRESULT read_from_framebuffer(', 'static DWORD resource_access_from_location(', 'static HRESULT surface_copy_simple_location(', 'static HRESULT surface_load_sysmem(', 'HRESULT surface_load_location('])+TEST
 p=work/'test.c';p.write_text(code)
 subprocess.run([os.environ.get('CC','cc'),'-std=c99','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-sign-compare','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(p),'-o',str(work/'test')],check=True)
 subprocess.run([str(work/'test')],check=True)
