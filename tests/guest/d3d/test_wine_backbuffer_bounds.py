#!/usr/bin/env python3
"""Compile checked Wine bounds policy and donor GPU-copy implementation.

The framebuffer is deliberately smaller than the desktop primary. Exact pixels
must reach the primary's far right/bottom, with untouched surrounding texels.
"""
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('patches', ROOT / 'tests/guest/support/patches.py')
patches = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patches)
with tempfile.TemporaryDirectory(prefix='dg-wine-bounds-') as temporary:
    work = Path(temporary)
    source_dir = work / 'wine'
    shutil.copytree(ROOT / 'vendor/wine9x', source_dir, ignore=shutil.ignore_patterns('.git'))
    patches.apply(source_dir, ROOT / 'support/guest/d3d/patches/base/manifest.json')
    source = (source_dir / 'wined3d/surface.c').read_text()
    begin = source.index('static BOOL dg_surface_backbuffer_blit_fits(')
    bounds = source[begin:source.index('HRESULT CDECL wined3d_surface_blt(', begin)]
    begin = source.index('static void fb_copy_to_texture_direct(')
    copy = source[begin:source.index('/* Uses the hardware to stretch', begin)]
    assert 'if (!dg_surface_backbuffer_blit_fits(dst_surface))\n            goto fallback;' in source
    assert 'if (SUCCEEDED(surface_blt_special(' in source
    harness = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int BOOL; typedef unsigned int UINT, DWORD; typedef int LONG;
typedef struct {int left,top,right,bottom;} RECT;
#define TRUE 1
#define FALSE 0
#define ORM_BACKBUFFER 1
#define ORM_FBO 2
#define WINED3D_LOCATION_TEXTURE_RGB 1u
#define GL_TEXTURE_2D 0xde1
#define TRACE(...) ((void)0)
#define FIXME(...) ((void)0)
#define ERR(...) ((void)0)
#define checkGLcall(...) ((void)0)
static const float eps=0.00001f;
enum wined3d_texture_filter_type {WINED3D_TEXF_NONE,WINED3D_TEXF_POINT};
struct wined3d_device {int offscreenBuffer;};
struct resource {UINT width,height;struct wined3d_device *device;int offscreen;};
struct texture {struct resource resource;int target;struct {int name;}texture_rgb;};
struct wined3d_surface {struct resource resource;struct texture *container;int texture_target,texture_level;DWORD locations;};
struct wined3d_gl_info {struct {struct {void (*p_glReadBuffer)(int);void (*p_glCopyTexSubImage2D)(int,int,int,int,int,int,int,int);}gl;}gl_ops;};
struct wined3d_context {BOOL valid;void *win_handle;const struct wined3d_gl_info *gl_info;};
static struct {int offscreen_rendering_mode;}wined3d_settings;
static unsigned acquired,released,copies; static BOOL missing,rect_ok=TRUE;
static RECT client={0,0,640,480};
static uint32_t framebuffer[480][640], texture[1024][1024];
static void read_buffer(int b){assert(b==7);}
static void copy_pixels(int target,int level,int dx,int dy,int sx,int sy,int w,int h){
 assert(target==GL_TEXTURE_2D && level==0);
 assert(dx>=0&&dy>=0&&dx+w<=1024&&dy+h<=1024);
 assert(sx>=0&&sy>=0&&sx+w<=640&&sy+h<=480);
 for(int y=0;y<h;++y)memcpy(&texture[dy+y][dx],&framebuffer[sy+y][sx],(size_t)w*4);
 ++copies;
}
static struct wined3d_gl_info gl={{{read_buffer,copy_pixels}}};
static struct wined3d_context ctx={TRUE,(void*)1,&gl};
static struct wined3d_context *context_acquire(struct wined3d_device*d,struct wined3d_surface*s){(void)d;(void)s;++acquired;return missing?NULL:&ctx;}
static void context_release(struct wined3d_context*c){assert(c==&ctx);++released;}
static BOOL GetClientRect(void*w,RECT*r){assert(w==(void*)1);*r=client;return rect_ok;}
static BOOL wined3d_resource_is_offscreen(const struct resource*r){return r->offscreen;}
static void context_apply_blit_state(struct wined3d_context*c,struct wined3d_device*d){assert(c==&ctx);(void)d;}
static void wined3d_texture_load(struct texture*t,struct wined3d_context*c,BOOL b){(void)t;assert(c==&ctx&&!b);}
static void context_bind_texture(struct wined3d_context*c,int target,int name){assert(c==&ctx&&target==GL_TEXTURE_2D&&name==3);}
static int surface_get_gl_buffer(struct wined3d_surface*s){(void)s;return 7;}
static void surface_validate_location(struct wined3d_surface*s,DWORD mask){s->locations|=mask;}
static void surface_invalidate_location(struct wined3d_surface*s,DWORD mask){s->locations&=~mask;}
''' + bounds + copy + r'''
int main(void){
 struct wined3d_device dev={7};struct texture primary={{1024,768,&dev,1},GL_TEXTURE_2D,{3}};
 struct texture back={{640,480,&dev,1},GL_TEXTURE_2D,{2}};
 struct wined3d_surface dst={{1024,768,&dev,1},&primary,GL_TEXTURE_2D,0,0x80};
 struct wined3d_surface src={{640,480,&dev,1},&back,GL_TEXTURE_2D,0,0x80};
 wined3d_settings.offscreen_rendering_mode=ORM_BACKBUFFER;
 assert(!dg_surface_backbuffer_blit_fits(&dst)); assert(acquired==released);
 assert(dg_surface_backbuffer_blit_fits(&src));
 dst.resource.width=640;assert(!dg_surface_backbuffer_blit_fits(&dst));
 dst.resource.height=480;assert(dg_surface_backbuffer_blit_fits(&dst));
 dst.resource.width=641;assert(!dg_surface_backbuffer_blit_fits(&dst));
 dst.resource.width=1024;dst.resource.height=768;
 primary.resource.offscreen=0;unsigned old=acquired;assert(dg_surface_backbuffer_blit_fits(&dst)&&acquired==old);
 primary.resource.offscreen=1;wined3d_settings.offscreen_rendering_mode=ORM_FBO;
 assert(dg_surface_backbuffer_blit_fits(&dst)&&acquired==old);
 wined3d_settings.offscreen_rendering_mode=ORM_BACKBUFFER;
 client=(RECT){0,0,1024,768};assert(dg_surface_backbuffer_blit_fits(&dst));
 rect_ok=FALSE;assert(!dg_surface_backbuffer_blit_fits(&dst));rect_ok=TRUE;
 ctx.valid=FALSE;assert(!dg_surface_backbuffer_blit_fits(&dst));ctx.valid=TRUE;
 missing=TRUE;assert(!dg_surface_backbuffer_blit_fits(&dst));missing=FALSE;
 assert(acquired==released+1);
 client=(RECT){0,0,640,480};
 for(int y=0;y<480;++y)for(int x=0;x<640;++x)framebuffer[y][x]=0xff000000u|((unsigned)y<<10)|(unsigned)x;
 for(int y=0;y<1024;++y)for(int x=0;x<1024;++x)texture[y][x]=0x12345678;
 RECT sr={0,0,640,480},dr={158,178,798,658};
 fb_copy_to_texture_direct(&dst,&src,&sr,&dr,WINED3D_TEXF_POINT);
 assert(copies==1 && dst.locations==WINED3D_LOCATION_TEXTURE_RGB);
 for(int y=0;y<1024;++y)for(int x=0;x<1024;++x){
  uint32_t expected=x>=158&&x<798&&y>=178&&y<658?framebuffer[y-178][x-158]:0x12345678;
  assert(texture[y][x]==expected);
 }
 /* A clipped subrectangle crosses both physical drawable edges in desktop
  * coordinates; its source remains bounded in the original framebuffer. */
 sr=(RECT){17,29,209,173};dr=(RECT){600,430,792,574};
 fb_copy_to_texture_direct(&dst,&src,&sr,&dr,WINED3D_TEXF_POINT);
 assert(copies==2);
 for(int y=0;y<144;++y)for(int x=0;x<192;++x)assert(texture[430+y][600+x]==framebuffer[307+y][17+x]);
 puts("PASS checked Wine backing bounds and actual GPU copy: 1048576 exact primary texels, clipped edge copy, location ownership");
}
'''
    (work / 'test.c').write_text(harness)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', str(work / 'test.c'), '-o', str(work / 'test')], check=True)
    subprocess.run([str(work / 'test')], check=True)
