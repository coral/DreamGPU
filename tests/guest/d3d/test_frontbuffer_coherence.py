#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run checked primary/GDI and FFP preservation boundaries with exact pixels.

Fake native operations model independent desktop, logical primary and GL_FRONT
storage, including whole-drawable publication after a partial native draw.
"""
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

DDRAW = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int HRESULT,BOOL;typedef unsigned DWORD,UINT;typedef int32_t LONG;
typedef struct {int left,top,right,bottom;} RECT;
#define TRUE 1
#define FALSE 0
#define DD_OK 0
#define E_FAIL -1
#define DDERR_SURFACELOST -10
#define FAILED(x) ((x)<0)
#define WINED3D_TEXF_POINT 1
#define SRCCOPY 0xcc0020
struct wined3d_surface {uint32_t bits[48],native[48];int acquired;};
typedef struct wined3d_surface *HDC;
struct ddraw_palette {void *wineD3DPalette;};
struct ddraw {void *swapchain_window;struct wined3d_surface *wined3d_frontbuffer;};
struct ddraw_surface {struct ddraw *ddraw;struct wined3d_surface *wined3d_surface;
 struct ddraw_palette *palette;struct {int dwWidth,dwHeight;}surface_desc;};
static BOOL ddraw_surface_is_lost(const struct ddraw_surface*s){assert(s);return 0;}
static struct wined3d_surface screen,logical,front;
static int dc_gets,releases,copies,blits,fail_get,fail_release,fail_screen,fail_copy;
static HRESULT wined3d_surface_getdc(struct wined3d_surface *s,HDC *dc)
{++dc_gets;if(fail_get)return -3;assert(!s->acquired);s->acquired=1;*dc=s;return 0;}
static HRESULT wined3d_surface_releasedc(struct wined3d_surface *s,HDC dc)
{assert(s==dc&&s->acquired);++releases;s->acquired=0;return fail_release?-4:0;}
static void wined3d_palette_apply_to_dc(void *p,HDC dc){assert(p&&dc);}
static HDC GetDC(void *w){assert(!w);return fail_screen?NULL:&screen;}
static void ReleaseDC(void *w,HDC dc){assert(!w&&dc==&screen);}
static BOOL BitBlt(HDC dst,int x,int y,int w,int h,HDC src,int sx,int sy,unsigned rop)
{
 ++copies;assert(rop==SRCCOPY&&x>=0&&y>=0&&x+w<=8&&y+h<=6);
 if(fail_copy)return 0;
 for(int j=0;j<h;++j)for(int i=0;i<w;++i)dst->bits[(y+j)*8+x+i]=src->bits[(sy+j)*8+sx+i];
 return 1;
}
static HRESULT wined3d_surface_blt(struct wined3d_surface *dst,const RECT *dr,
 struct wined3d_surface *src,const RECT *sr,unsigned flags,void *fx,int filter)
{
 assert(dst==&front&&src==&logical&&!flags&&!fx&&filter==WINED3D_TEXF_POINT);
 ++blits;RECT d=dr?*dr:(RECT){0,0,8,6},s=sr?*sr:(RECT){0,0,8,6};
 /* The FFP path loads preserved DIB pixels before its draw. Publication itself
  * replaces all desktop pixels covered by the GL window. */
 if(d.left||d.top||d.right!=8||d.bottom!=6)memcpy(dst->native,dst->bits,sizeof(dst->bits));
 for(int y=d.top;y<d.bottom;++y)for(int x=d.left;x<d.right;++x)
  dst->native[y*8+x]=src->bits[(s.top+y-d.top)*8+s.left+x-d.left];
 memcpy(screen.bits,dst->native,sizeof(screen.bits));return 0;
}
'''
DDRAW_TEST = r'''
static void reset(void)
{
 memset(&screen,0,sizeof(screen));memset(&logical,0,sizeof(logical));memset(&front,0,sizeof(front));
 for(unsigned i=0;i<48;++i){screen.bits[i]=0x120000+i;logical.bits[i]=0x340000+i;front.native[i]=0x560000+i;}
 dc_gets=releases=copies=blits=fail_get=fail_release=fail_screen=fail_copy=0;
}
int main(void)
{
 struct ddraw owner={(void*)1,&front};struct ddraw_surface s={&owner,&logical,NULL,{8,6}};
 RECT r={2,1,6,4};
 reset();assert(!ddraw_surface_update_frontbuffer(&s,&r,TRUE));
 assert(copies==1&&dc_gets==1&&releases==1&&!blits);
 for(int y=0;y<6;++y)for(int x=0;x<8;++x)
  assert(logical.bits[y*8+x]==(unsigned)((x>=2&&x<6&&y>=1&&y<4?0x120000:0x340000)+y*8+x));
 reset();assert(!ddraw_surface_update_frontbuffer(&s,&r,FALSE));
 assert(copies==1&&dc_gets==1&&releases==1&&blits==1);
 for(int y=0;y<6;++y)for(int x=0;x<8;++x)
  assert(screen.bits[y*8+x]==(unsigned)((x>=2&&x<6&&y>=1&&y<4?0x340000:0x120000)+y*8+x));
 // A complete replacement needs no preservation/readback.
 reset();assert(!ddraw_surface_update_frontbuffer(&s,NULL,FALSE));
 assert(!copies&&!dc_gets&&blits==1&&!memcmp(screen.bits,logical.bits,sizeof(screen.bits)));
 for(int mode=0;mode<4;++mode){
  reset();if(mode==0)fail_get=1;else if(mode==1)fail_release=1;else if(mode==2)fail_screen=1;else fail_copy=1;
  assert(ddraw_surface_update_frontbuffer(&s,&r,FALSE)<0&&!blits);
  for(unsigned i=0;i<48;++i)assert(screen.bits[i]==0x120000+i);
 }
 reset();owner.swapchain_window=NULL;
 assert(!ddraw_surface_update_frontbuffer(&s,&r,FALSE)&&copies==1&&!blits);
 for(int y=0;y<6;++y)for(int x=0;x<8;++x)
  assert(screen.bits[y*8+x]==(unsigned)((x>=2&&x<6&&y>=1&&y<4?0x340000:0x120000)+y*8+x));
 assert(ddraw_surface_update_frontbuffer(NULL,NULL,TRUE)==E_FAIL);
 puts("PASS actual primary update: GDI reads with live swapchain, partial publication preserves desktop, full fastpath, failure isolation");
}
'''
FFP = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
typedef int HRESULT,BOOL;typedef unsigned DWORD;typedef struct {int left,top,right,bottom;} RECT;
enum wined3d_texture_filter_type {WINED3D_TEXF_POINT=1};enum wined3d_blit_op {COPY};
#define TRUE 1
#define FALSE 0
#define FAILED(x) ((x)<0)
#define WINED3D_OK 0
#define WINED3DERR_INVALIDCALL -1
#define WINED3D_LOCATION_TEXTURE_RGB 1
#define WINED3D_LOCATION_DRAWABLE 2
#define WINED3D_LOCATION_DIB 4
#define WINED3D_TEXTURE_CONVERTED 1
#define WINED3D_CKEY_SRC_BLT 1
#define WINED3DFMT_P8_UINT 1
#define GL_ALPHA_TEST 1
#define GL_NOTEQUAL 2
#define TRACE(...) ((void)0)
#define ERR(...) ((void)0)
#define ORM_FBO 7
#define checkGLcall(...) ((void)0)
struct wined3d_context;struct wined3d_device;struct texture;
struct wined3d_color_key {unsigned color_space_low_value,color_space_high_value;};
struct resource {unsigned width,height,draw_binding;int offscreen;struct {int id;}*format;struct wined3d_device *device;};
struct swapchain {struct texture *front_buffer;};
struct texture {struct resource resource;struct swapchain *swapchain;unsigned flags;
 struct {struct wined3d_color_key src_blt_color_key;unsigned color_key_flags;}async;};
struct wined3d_surface {struct resource resource;struct texture *container;unsigned locations;uint32_t cpu[16],tex[16];};
struct wined3d_gl_info {struct {struct {void(*p_glEnable)(int),(*p_glDisable)(int),(*p_glAlphaFunc)(int,float),(*p_glFlush)(void);}gl;}gl_ops;};
struct wined3d_context {BOOL valid;struct wined3d_surface *current_rt;const struct wined3d_gl_info *gl_info;void *win_handle;};
struct blit_shader {HRESULT(*set_shader)(void*,struct wined3d_context*,struct wined3d_surface*,void*);
 void(*unset_shader)(const struct wined3d_gl_info*);};
struct wined3d_device {const struct blit_shader *blitter;void *blit_priv;};
static struct {BOOL strict_draw_ordering;int offscreen_rendering_mode;}wined3d_settings;
static struct wined3d_surface src,dst;static struct texture st,dt;static uint32_t framebuffer[16];
static struct wined3d_context ctx,*current;static int keyed,draws,dest_loads,source_loads,releases,fail_source,fail_dest,fail_shader,fail_context;
static void enable(int cap){assert(cap==GL_ALPHA_TEST);keyed=1;}
static void disable(int cap){assert(cap==GL_ALPHA_TEST);keyed=0;}
static void alpha_func(int fn,float value){assert(fn==GL_NOTEQUAL&&value==0);}
static void flush(void){}
static struct wined3d_gl_info gl={{{enable,disable,alpha_func,flush}}};
static HRESULT set_shader(void *p,struct wined3d_context *c,struct wined3d_surface *s,void *key)
{assert(!p&&c==&ctx&&s&&!key);return fail_shader?-4:0;}
static void unset_shader(const struct wined3d_gl_info *g){assert(g==&gl);}
static struct blit_shader shader={set_shader,unset_shader};static struct wined3d_device device={&shader,NULL};
static struct wined3d_context *context_get_current(void){return current;}
static struct wined3d_context *context_acquire(const struct wined3d_device*d,struct wined3d_surface*s)
{assert(d==&device&&s==&dst);if(fail_context)return NULL;ctx.current_rt=s;current=&ctx;return &ctx;}
static void context_release(struct wined3d_context*c){assert(c==&ctx);++releases;}
static void context_restore(struct wined3d_context*c,struct wined3d_surface*s){assert(c==&ctx);c->current_rt=s;}
static BOOL surface_is_full_rect(struct wined3d_surface*s,const RECT*r)
{return abs(r->right-r->left)==(int)s->resource.width&&abs(r->bottom-r->top)==(int)s->resource.height;}
static HRESULT surface_load_drawable(struct wined3d_surface*,struct wined3d_context*);
static void surface_get_rect(struct wined3d_surface*s,void*unused,RECT*r){assert(!unused);*r=(RECT){0,0,(int)s->resource.width,(int)s->resource.height};}
static HRESULT surface_load_location(struct wined3d_surface*s,struct wined3d_context*c,DWORD loc)
{
 assert(c==&ctx);
 if(loc==WINED3D_LOCATION_TEXTURE_RGB){++source_loads;if(fail_source)return -2;
  if(!(s->locations&loc))memcpy(s->tex,s->locations&WINED3D_LOCATION_DRAWABLE?framebuffer:s->cpu,sizeof(s->tex));}
 else {assert(s==&dst&&loc==WINED3D_LOCATION_DRAWABLE);++dest_loads;if(fail_dest)return -3;
  if(!(s->locations&loc)){HRESULT hr=surface_load_drawable(s,c);if(FAILED(hr))return hr;}}
 s->locations|=loc;return 0;
}
static void wined3d_texture_load(struct texture*t,struct wined3d_context*c,BOOL srgb)
{assert((t==&st||t==&dt)&&c==&ctx&&!srgb);}
static BOOL wined3d_resource_is_offscreen(struct resource*r){return r->offscreen;}
static void context_apply_blit_state(struct wined3d_context*c,const struct wined3d_device*d){assert(c==&ctx&&d==&device);}
static void surface_translate_drawable_coords(struct wined3d_surface*s,void*w,RECT*r){(void)s;(void)w;(void)r;}
static void draw_textured_quad(struct wined3d_surface*s,struct wined3d_context*c,const RECT*sr,const RECT*dr,int filter)
{
 assert(c==&ctx&&filter==WINED3D_TEXF_POINT);++draws;
 for(int y=dr->top;y<dr->bottom;++y)for(int x=dr->left;x<dr->right;++x){
 uint32_t v=s->tex[(sr->top+y-dr->top)*4+sr->left+x-dr->left];if(!keyed||v)framebuffer[y*4+x]=v;}
}
static void wined3d_texture_set_color_key(struct texture*t,unsigned flags,const struct wined3d_color_key*k)
{assert(t==&st&&flags==WINED3D_CKEY_SRC_BLT);if(k){t->async.color_key_flags|=flags;t->async.src_blt_color_key=*k;}else t->async.color_key_flags&=~flags;}
static void surface_validate_location(struct wined3d_surface*s,DWORD loc){s->locations|=loc;}
static void surface_invalidate_location(struct wined3d_surface*s,DWORD loc){s->locations&=~loc;}
'''
FFP_TEST=r'''
static void reset(void)
{
 memset(&src,0,sizeof(src));memset(&dst,0,sizeof(dst));memset(&st,0,sizeof(st));memset(&dt,0,sizeof(dt));
 src.resource.width=dst.resource.width=src.resource.height=dst.resource.height=4;
 src.resource.device=dst.resource.device=&device;src.container=&st;dst.container=&dt;src.locations=WINED3D_LOCATION_DRAWABLE;dst.locations=WINED3D_LOCATION_DIB;
 static struct {int id;}fmt={2};src.resource.format=(void*)&fmt;dst.resource.format=(void*)&fmt;
 st.resource.offscreen=dt.resource.offscreen=1;dt.resource.draw_binding=WINED3D_LOCATION_DRAWABLE;
 ctx=(struct wined3d_context){1,&dst,&gl,NULL};current=&ctx;
 for(unsigned i=0;i<16;++i){framebuffer[i]=0x700+i;dst.cpu[i]=0x300+i;}
 keyed=draws=dest_loads=source_loads=releases=fail_source=fail_dest=fail_shader=fail_context=0;
}
int main(void)
{
 RECT full={0,0,4,4},part={1,1,3,3};struct wined3d_color_key key={0,0};
 reset();assert(!ffp_blit_blit_surface(&device,COPY,1,&src,&part,&dst,&part,NULL));
 assert(draws==2&&source_loads==2&&dest_loads==1&&dst.locations==WINED3D_LOCATION_DRAWABLE);
 for(int y=0;y<4;++y)for(int x=0;x<4;++x)assert(framebuffer[y*4+x]==(unsigned)((x>=1&&x<3&&y>=1&&y<3?0x700:0x300)+y*4+x));
 // Keyed full writes preserve holes too, and save the source before the
 // destination replaces their borrowed physical backbuffer.
 reset();framebuffer[5]=0;
 assert(!ffp_blit_blit_surface(&device,COPY,1,&src,&full,&dst,&full,&key));
 assert(framebuffer[5]==0x305&&framebuffer[6]==0x706&&dest_loads==1&&!st.async.color_key_flags);
 reset();assert(!ffp_blit_blit_surface(&device,COPY,1,&src,&full,&dst,&full,NULL));
 assert(!dest_loads&&draws==1);
 for(int failure=0;failure<4;++failure){
 reset();if(failure==0)fail_source=1;else if(failure==1)fail_dest=1;else if(failure==2)fail_shader=1;else fail_context=1;
 assert(ffp_blit_blit_surface(&device,COPY,1,&src,&part,&dst,&part,&key)<0&&!draws);
 assert(dst.locations&WINED3D_LOCATION_DIB);assert(!st.async.color_key_flags);
 assert(releases==(failure==3?0:1));
 }
 puts("PASS actual FFP blit: source-before-destination, partial and keyed pixel preservation, whole fastpath, failure authority and color-key restoration");
}
'''
with tempfile.TemporaryDirectory(prefix='dreamgpu-front-coherence-') as temporary:
    work=Path(temporary)
    for name in json.loads(MANIFEST.read_text())['files']:
        p=work/name;p.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(ROOT/'vendor/wine9x'/name,p)
    patches.apply(work,MANIFEST)
    dd=(work/'ddraw/surface.c').read_text();surface=(work/'wined3d/surface.c').read_text()
    start=dd.index('static HRESULT ddraw_surface_copy_screen(')
    primary=dd[start:dd.index('\n/*****************************************************************************',start)]
    start=surface.index('static HRESULT surface_blt_to_drawable(')
    helper=surface[start:surface.index('\nHRESULT surface_color_fill(',start)]
    start=surface.index('static HRESULT ffp_blit_blit_surface(')
    wrapper=surface[start:surface.index('\nconst struct blit_shader ffp_blit',start)]
    # These gates sit inside much larger API dispatchers; native pixel behavior
    # is executed above, while dispatch selection is checked against prepared code.
    clip=dd[dd.index('static HRESULT ddraw_surface_blt_clipped('):]
    keyed=clip[:clip.index('    if (!dst_surface->clipper)')]
    assert 'WINEDDBLT_KEYSRC | WINEDDBLT_KEYSRCOVERRIDE' in keyed
    assert 'ddraw_surface_update_frontbuffer(dst_surface, dst_rect_in, TRUE)' in keyed
    assert '&& !src_rect.left && !src_rect.top && surface_is_full_rect(src_surface, &src_rect)' in surface
    assert '&& !dst_rect.left && !dst_rect.top && surface_is_full_rect(dst_surface, &dst_rect)' in surface
    assert 'return blitter->blit_surface(device, blit_op, filter,' in surface
    start=surface.index('static HRESULT surface_load_drawable(')
    load_drawable=surface[start:surface.index('\nstatic HRESULT surface_load_texture(',start)]
    for name,code in [('primary',DDRAW+primary+DDRAW_TEST),('ffp',FFP+helper+load_drawable+wrapper+FFP_TEST)]:
        source=work/(name+'.c');binary=work/name;source.write_text(code)
        subprocess.run([os.environ.get('CC','cc'),'-std=c99','-Wall','-Wextra','-Werror',
                        '-Wno-unused-parameter','-fsanitize=address,undefined','-fno-omit-frame-pointer',
                        str(source),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
