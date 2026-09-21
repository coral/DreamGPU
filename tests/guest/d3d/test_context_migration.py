#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute native Wine acquire/release/restore and swapchain selection boundaries."""
import importlib.util,json,os,shutil,subprocess,tempfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
spec=importlib.util.spec_from_file_location('checked_patches',ROOT/'tests/guest/support/patches.py')
patches=importlib.util.module_from_spec(spec);spec.loader.exec_module(patches)
manifest=ROOT/'support/guest/d3d/patches/base/manifest.json'
PREAMBLE=r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#define USE_WIN32_OPENGL
#define TRACE(...) ((void)0)
#define WARN(...) ((void)0)
#define FIXME(...) ((void)0)
#define ERR(...) log_error(__VA_ARGS__)
static void log_error(const char *fmt,...) { (void)fmt; }
#define WARN_ON(...) 0
#define TRUE 1
#define FALSE 0
typedef int BOOL;typedef unsigned DWORD,UINT;typedef void *HGLRC,*HDC;
struct wined3d_context;struct wined3d_device;struct wined3d_swapchain;
struct wined3d_gl_info { struct { struct { int (*p_wglGetPixelFormat)(HDC); } wgl; } gl_ops; };
struct texture { struct wined3d_swapchain *swapchain; void *surface; };
struct wined3d_surface { struct texture *container; int id; };
struct wined3d_swapchain { struct wined3d_device *device; unsigned num_contexts;struct wined3d_context **context;struct texture **back_buffers,*front_buffer; };
struct wined3d_device { struct wined3d_swapchain **swapchains;unsigned context_count;struct wined3d_context **contexts; };
struct wined3d_context { struct wined3d_gl_info *gl_info;struct wined3d_swapchain *swapchain;struct wined3d_surface *current_rt;unsigned level,tid,current,valid,destroyed,needs_set,hdc_is_private,hdc_has_format;int pixel_format;HGLRC glCtx,restore_ctx;HDC hdc,restore_dc;struct wined3d_context *restore_wined3d; };
static unsigned thread=1,binds,flushes,setup_calls,creates,fail_bind,fail_unbind;
static struct wined3d_context *tls;
static HGLRC current_gl;static HDC current_dc;
static unsigned wined3d_context_tls_idx;
static DWORD GetCurrentThreadId(void) { return thread; }
static DWORD GetLastError(void) { return 6; }
static HGLRC wglGetCurrentContext(void) { return current_gl; }
static HDC wglGetCurrentDC(void) { return current_dc; }
static struct wined3d_context *context_get_current(void) { return tls; }
static BOOL TlsSetValue(unsigned idx,void *value) { (void)idx;tls=value;return TRUE; }
static BOOL wglMakeCurrent(HDC dc,HGLRC gl) {
 if(current_gl)++flushes;
 current_gl=NULL;current_dc=NULL;
 if((gl&&fail_bind)||(!gl&&fail_unbind)) { fail_bind=fail_unbind=0;return FALSE; }
 current_gl=gl;current_dc=dc;if(gl)++binds;return TRUE;
}
// Exercise the production binding helper, including failed backup and recovery.
static BOOL context_set_current(struct wined3d_context *c);
static BOOL context_set_pixel_format(struct wined3d_context *c,HDC dc,BOOL priv,int format) {
 (void)c;(void)priv;assert(dc&&format==1);return TRUE;
}
static HDC swapchain_get_backup_dc(struct wined3d_swapchain *s) { assert(s);return NULL; }
static void context_destroy_gl_resources(struct wined3d_context *c) { (void)c;assert(0); }
static BOOL context_restore_pixel_format(struct wined3d_context *c) { (void)c;return FALSE; }
static int get_format(HDC dc) { assert(dc);return 1; }
static void context_update_window(struct wined3d_context *c) { assert(c); }
static void context_setup_target(struct wined3d_context *c,struct wined3d_surface *target) {
 // Target switches may copy the old framebuffer: the intended GL must be bound first.
 assert(current_gl==c->glCtx&&c->current&&c->tid==thread);++setup_calls;c->current_rt=target;
}
static struct wined3d_context *swapchain_create_context(struct wined3d_swapchain *s) { (void)s;++creates;return NULL; }
static void *wined3d_texture_get_sub_resource(struct texture *t,unsigned i) { assert(!i);return t->surface; }
static struct wined3d_surface *surface_from_resource(void *p) { return p; }
'''
TEST=r'''
int main(void) {
 struct wined3d_gl_info gl={0};gl.gl_ops.wgl.p_wglGetPixelFormat=get_format;
 struct wined3d_device d1={0},d2={0};struct wined3d_swapchain s1={0},s2={0};
 struct wined3d_context c1={0},c2={0};struct wined3d_context *cs1[]={&c1},*cs2[]={&c2};
 struct wined3d_swapchain *ss1[]={&s1},*ss2[]={&s2};
 struct texture front1={&s1,NULL},front2={&s2,NULL},off1={NULL,NULL},off2={NULL,NULL};
 struct wined3d_surface f1={&front1,1},f2={&front2,2},t1={&off1,3},t2={&off2,4};
 front1.surface=&f1;front2.surface=&f2;off1.surface=&t1;off2.surface=&t2;
 d1.swapchains=ss1;d1.context_count=1;d1.contexts=cs1;d2.swapchains=ss2;d2.context_count=1;d2.contexts=cs2;
 s1.device=&d1;s1.num_contexts=1;s1.context=cs1;s1.front_buffer=&front1;
 s2.device=&d2;s2.num_contexts=1;s2.context=cs2;s2.front_buffer=&front2;
 c1.gl_info=c2.gl_info=&gl;c1.swapchain=&s1;c2.swapchain=&s2;
 c1.glCtx=&c1;c2.glCtx=&c2;c1.hdc=&s1;c2.hdc=&s2;c1.valid=c2.valid=1;
 c1.pixel_format=c2.pixel_format=1;c1.current_rt=&f1;c2.current_rt=&f2;
 // Sequential worker handoff retains exactly the same context/drawable identity.
 assert(context_acquire(&d1,&t1)==&c1&&current_gl==&c1&&c1.tid==1&&c1.level==1);
 context_release(&c1);assert(!tls&&!current_gl&&!c1.current&&!c1.level&&flushes==1);
 thread=2;assert(context_acquire(&d1,NULL)==&c1&&c1.tid==2&&!creates&&c1.current_rt==&t1);
 assert(context_acquire(&d1,&t1)==&c1&&c1.level==2);
 unsigned before=flushes;context_release(&c1);assert(current_gl==&c1&&c1.level==1&&flushes==before);
 context_release(&c1);assert(!current_gl&&!tls&&flushes==before+1);
 thread=1;assert(context_acquire(&d1,&f1)==&c1&&c1.tid==1);
 context_release(&c1);assert(!current_gl&&!tls&&!creates);
 // Nested context from another Wine device restores exact prior TLS and owner.
 assert(context_acquire(&d1,&t1)==&c1);
 assert(context_acquire(&d2,&t2)==&c2&&c2.restore_ctx==&c1&&c2.restore_wined3d==&c1);
 context_release(&c2);assert(current_gl==&c1&&tls==&c1&&c1.current&&!c2.current&&c1.level==1);
 assert(!c2.restore_ctx&&!c2.restore_wined3d);
 context_release(&c1);assert(!current_gl&&!tls&&!c1.current);
 // Another swapchain in the same device remains distinct and restores correctly.
 struct wined3d_context *both[]={&c1,&c2};d1.context_count=2;d1.contexts=both;s2.device=&d1;
 assert(context_acquire(&d1,&f1)==&c1);assert(context_acquire(&d1,&f2)==&c2);
 context_release(&c2);assert(current_gl==&c1&&tls==&c1);context_release(&c1);
 assert(!current_gl&&!tls&&!creates);
 // Foreign application GL is restored without being immediately unbound again.
 int foreign;current_gl=&foreign;current_dc=&foreign;
 assert(context_acquire(&d1,&t1)==&c1&&c1.restore_ctx==&foreign&&!c1.restore_wined3d);
 context_release(&c1);assert(current_gl==&foreign&&!tls&&!c1.current);
 current_gl=NULL;current_dc=NULL;
 // Rebinding remains necessary when nested window/DC state changes.
 assert(context_acquire(&d1,&t1)==&c1);c1.needs_set=1;before=binds;
 assert(context_acquire(&d1,&t1)==&c1&&binds==before+1&&!c1.needs_set);
 context_release(&c1);assert(current_gl==&c1);context_release(&c1);
 // Applications may change WGL directly while Wine TLS still names this context.
 tls=&c1;c1.current=1;current_gl=&foreign;current_dc=&foreign;before=binds;
 assert(context_acquire(&d1,&t1)==&c1&&binds==before+1&&current_gl==&c1);
 assert(c1.restore_ctx==&foreign&&!c1.restore_wined3d);
 context_release(&c1);assert(current_gl==&foreign&&!tls&&!c1.current);
 current_gl=NULL;current_dc=NULL;
 // Failed activation cannot publish new ownership or run target copy operations.
 before=setup_calls;unsigned old_tid=c1.tid;thread=3;fail_bind=1;
 assert(context_acquire(&d1,&t1)==&c1&&!c1.valid&&c1.tid==old_tid&&setup_calls==before);
 context_release(&c1);assert(!tls&&!current_gl&&!c1.current&&!c1.level);
 assert(context_acquire(&d1,&t1)==&c1&&c1.valid&&c1.current&&c1.tid==3);
 // Failed unbind follows WGL failure semantics and invalidates context for reuse.
 fail_unbind=1;context_release(&c1);assert(!c1.valid&&!tls&&!current_gl&&!c1.current);
 thread=1;assert(context_acquire(&d1,&t1)==&c1&&c1.valid&&c1.current);
 assert(context_acquire(&d2,&f2)==&c2);fail_bind=1;context_release(&c2);
 assert(!current_gl&&!tls&&!c2.current);context_release(&c1);
 assert(!current_gl&&!tls&&!c1.current);
 // Same TLS identity is not proof that a previously failed binding recovered.
 tls=&c1;c1.current=0;c1.valid=0;before=binds;
 assert(context_acquire(&d1,&t1)==&c1&&c1.valid&&c1.current&&binds==before+1);
 context_release(&c1);assert(!tls&&!current_gl&&!c1.current&&!c1.level);
 // Direct activation must retry an invalid TLS-identical context too.
 tls=&c1;c1.current=0;c1.valid=0;before=binds;
 assert(context_set_current(&c1)&&c1.valid&&c1.current&&binds==before+1);
 assert(context_set_current(NULL)&&!tls&&!current_gl&&!c1.current);
 // Destruction is permanent: neither acquire nor direct activation may revive it.
 c1.destroyed=1;before=binds;unsigned old_setup=setup_calls;
 assert(!context_set_current(&c1)&&binds==before&&!tls&&!current_gl);
 assert(context_acquire(&d1,&t1)==&c1&&!c1.valid&&setup_calls==old_setup&&binds==before);
 context_release(&c1);assert(!tls&&!current_gl&&!c1.level);
 // Even a stale TLS pointer cannot make a destroyed context appear usable.
 tls=&c1;assert(!context_set_current(&c1)&&binds==before&&!current_gl);
 tls=NULL;c1.destroyed=0;
 before=flushes;context_release(&c1);context_release(NULL);assert(!c1.level&&flushes==before);
 struct wined3d_swapchain empty={0};assert(!swapchain_get_context(&empty)&&creates==1);
 puts("PASS prepared native Wine migration: same drawable across threads, target-copy activation order, nested levels, multiple swapchains/devices, foreign restoration, failed-bind retry, stale TLS recovery and destroyed-context rejection");
}
'''
with tempfile.TemporaryDirectory(prefix='dreamgpu-context-migration-') as directory:
    work=Path(directory)
    for name in json.loads(manifest.read_text())['files']:
        dest=work/name;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/'vendor/wine9x'/name,dest)
    patches.apply(work,manifest)
    # Match the actual bundled MinGW build, which does not define WIN32_NATIVE.
    assert '-DUSE_WIN32_OPENGL' in (ROOT/'vendor/wine9x/Makefile').read_text()
    source=(work/'wined3d/context.c').read_text();swap=(work/'wined3d/swapchain.c').read_text()
    code=[]
    for text,prefix in [(source,'static BOOL context_set_gl_context('),(source,'BOOL context_set_current('),(source,'static void context_restore_gl_context('),(source,'void context_release('),(source,'static void context_enter('),(swap,'struct wined3d_context *swapchain_get_context('),(source,'struct wined3d_context *context_acquire(')]:
        start=text.index(prefix);end=text.index('\n}',start)+2;code.append(text[start:end])
    test=work/'test.c';test.write_text(PREAMBLE+'\n'.join(code)+TEST)
    subprocess.run([os.environ.get('CC','cc'),'-std=c99','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(test),'-o',str(work/'test')],check=True)
    subprocess.run([str(work/'test')],check=True)
