#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute the actual private-provider atlas allocation and publication body."""
from pathlib import Path
import subprocess,sys,tempfile

source_path=Path(sys.argv[1])
source=source_path.read_text()
ledger=(source_path.parents[3]/'src/gallium/auxiliary/util/u_helpers.c').read_text()
ledger=ledger[ledger.index('/* Border CPU images and GPU atlases'): ]
start=source.index('static bool\nfinalize_border_1d(')
end=source.index('\nGLboolean\nst_finalize_texture(',start)
body=source[start:end]
prefix=r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include <pthread.h>
typedef pthread_mutex_t simple_mtx_t;
#define SIMPLE_MTX_INITIALIZER PTHREAD_MUTEX_INITIALIZER
#define simple_mtx_lock pthread_mutex_lock
#define simple_mtx_unlock pthread_mutex_unlock
#define p_atomic_read(p) __atomic_load_n(p,__ATOMIC_SEQ_CST)
#define p_atomic_inc(p) ((void)__atomic_fetch_add(p,1,__ATOMIC_SEQ_CST))
#define p_atomic_dec(p) ((void)__atomic_fetch_sub(p,1,__ATOMIC_SEQ_CST))
void *util_border_resource_detach(uintptr_t resource);
void util_border_resource_retire(void *token);
#define MAX_TEXTURE_LEVELS 16
#define MAX2(a,b) ((a)>(b)?(a):(b))
#define PIPE_TEXTURE_2D 2
#define PIPE_BIND_SAMPLER_VIEW 1
#define PIPE_COMPRESSION_FIXED_RATE_NONE 0
#define GL_OUT_OF_MEMORY 0x505
enum pipe_format { RGBA8=1 };
struct pipe_resource {unsigned refs,w,h;uint8_t pixels[128];};
struct gl_texture_image {unsigned Width,Width2,Border,TexFormat;uint8_t *border_data;};
struct gl_texture_object {struct {unsigned BaseLevel;} Attrib;unsigned lastLevel;
 struct gl_texture_image *Image[1][16]; struct pipe_resource *pt;
 bool needs_validation;unsigned validated_first_level,validated_last_level;};
struct pipe_box {unsigned x,y,width,height;};
struct pipe_context {void (*texture_subdata)(struct pipe_context*,struct pipe_resource*,unsigned,unsigned,const struct pipe_box*,const void*,unsigned,unsigned);};
struct st_context {int unused;};struct gl_context {struct st_context st;};
static bool fail_alloc;static unsigned allocations,frees,uploads,invalidations,errors;
static unsigned u_minify(unsigned x,unsigned l){return MAX2(x>>l,1);}
static struct st_context *st_context(struct gl_context *c){return &c->st;}
static enum pipe_format st_mesa_format_to_pipe_format(struct st_context *s,unsigned f){(void)s;return f;}
static unsigned _mesa_get_format_bytes(unsigned f){assert(f==RGBA8);return 4;}
static void _mesa_error(struct gl_context*c,unsigned e,const char*m){(void)c;(void)m;assert(e==GL_OUT_OF_MEMORY);errors++;}
static struct pipe_resource *st_texture_create(struct st_context*s,unsigned target,enum pipe_format fmt,unsigned last,unsigned w,unsigned h,unsigned d,unsigned layers,unsigned samples,unsigned flags,unsigned bind,bool sparse,unsigned compression){
 (void)s;assert(target==PIPE_TEXTURE_2D&&fmt==RGBA8&&!last&&d==1&&layers==1&&!samples&&!flags&&bind==1&&!sparse&&!compression);
 if(fail_alloc)return NULL;assert(w*h*4<=128);struct pipe_resource*r=calloc(1,sizeof*r);assert(r);r->refs=1;r->w=w;r->h=h;allocations++;return r;}
static void pipe_resource_reference(struct pipe_resource **to,struct pipe_resource *from){
 if(from)from->refs++;struct pipe_resource *old=*to;*to=from;if(old&&!--old->refs){void *token=util_border_resource_detach((uintptr_t)old);free(old);frees++;util_border_resource_retire(token);}}
static void st_texture_release_all_sampler_views(struct st_context*s,struct gl_texture_object*o){(void)s;(void)o;invalidations++;}
static void u_box_2d(unsigned x,unsigned y,unsigned w,unsigned h,struct pipe_box*b){*b=(struct pipe_box){x,y,w,h};}
static void upload(struct pipe_context*p,struct pipe_resource*r,unsigned l,unsigned use,const struct pipe_box*b,const void*src,unsigned stride,unsigned layer){
 (void)p;assert(!l&&!use&&!layer&&b->height==1&&b->y<r->h&&b->x<=r->w&&b->width<=r->w-b->x&&stride==b->width*4);
 memcpy(r->pixels+4*(r->w*b->y+b->x),src,4*b->width);uploads++;}
'''
tests=r'''
int main(void){
 struct gl_context ctx={0};struct pipe_context pipe={upload};struct gl_texture_object o={0};
 uint8_t a[24],b[16],c[12];for(unsigned i=0;i<24;i++)a[i]=i;for(unsigned i=0;i<16;i++)b[i]=40+i;for(unsigned i=0;i<12;i++)c[i]=80+i;
 struct gl_texture_image ims[3]={{6,4,1,RGBA8,a},{4,2,1,RGBA8,b},{3,1,1,RGBA8,c}};
 for(unsigned i=0;i<3;i++)o.Image[0][i]=&ims[i];o.lastLevel=2;o.needs_validation=true;
 o.pt=calloc(1,sizeof*o.pt);assert(o.pt);o.pt->refs=1;struct pipe_resource *old=o.pt;
 fail_alloc=true;assert(!finalize_border_1d(&ctx,&pipe,&o));assert(o.pt==old&&o.needs_validation&&errors==1&&!frees&&!uploads&&!invalidations);
 fail_alloc=false;ims[1].border_data=NULL;assert(!finalize_border_1d(&ctx,&pipe,&o));assert(o.pt==old&&!allocations&&!frees);ims[1].border_data=b;
 ims[1].Width=5;assert(!finalize_border_1d(&ctx,&pipe,&o));assert(!allocations);ims[1].Width=4;
 o.lastLevel=MAX_TEXTURE_LEVELS;assert(!finalize_border_1d(&ctx,&pipe,&o));assert(!allocations);o.lastLevel=2;
 assert(finalize_border_1d(&ctx,&pipe,&o));assert(o.pt!=old&&allocations==1&&frees==1&&uploads==9&&invalidations==1&&!o.needs_validation);
 assert(o.pt->refs==1&&o.pt->w==4&&o.pt->h==6&&o.validated_first_level==0&&o.validated_last_level==2);
 for(unsigned l=0;l<3;l++) {struct gl_texture_image*im=&ims[l];uint8_t *row=o.pt->pixels+4*(o.pt->w*2*l);
  assert(!memcmp(row,im->border_data+4,im->Width2*4));row+=4*o.pt->w;
  assert(!memcmp(row,im->border_data,4));assert(!memcmp(row+4,im->border_data+4*(im->Width-1),4));}
 // Failed replacement must leave both the old atlas and credit unchanged.
 size_t live=border_storage_bytes;
 fail_alloc=true;assert(!finalize_border_1d(&ctx,&pipe,&o));fail_alloc=false;
 assert(border_storage_bytes==live&&frees==1);
 // Both old/new allocations remain charged while another context holds a view.
 struct pipe_resource *view=NULL;pipe_resource_reference(&view,o.pt);
 assert(finalize_border_1d(&ctx,&pipe,&o));assert(border_storage_bytes==2*live&&frees==1);
 pipe_resource_reference(&view,NULL);assert(border_storage_bytes==live&&frees==2);
 assert(util_border_storage_reserve(BORDER_STORAGE_LIMIT-live));
 unsigned prior=allocations;assert(!finalize_border_1d(&ctx,&pipe,&o));assert(allocations==prior);
 util_border_storage_release(BORDER_STORAGE_LIMIT-live);
 pipe_resource_reference(&o.pt,NULL);assert(frees==3&&!border_storage_bytes&&!border_resource_count);
 puts("PASS actual atlas ownership: allocation rollback, preflight, exact mip/border upload, atomic publication, final release");
}
'''
with tempfile.TemporaryDirectory(prefix='dg-border-storage-') as d:
    p=Path(d);(p/'test.c').write_text(prefix+ledger+body+tests)
    subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-misleading-indentation','-fsanitize=address,undefined','-pthread',str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
