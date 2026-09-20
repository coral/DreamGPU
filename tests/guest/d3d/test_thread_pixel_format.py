#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the prepared native Wine pixel-format reuse path."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('checked_patches', ROOT / 'tests/guest/support/patches.py')
patches = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patches)
manifest = ROOT / 'support/guest/d3d/patches/base/manifest.json'
PREAMBLE = r'''
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
typedef int BOOL, GLint;
typedef void *HDC,*HWND;
#define TRUE 1
#define FALSE 0
#define ERR(...) ((void)0)
#define WARN(...) log_warn(__VA_ARGS__)
static void log_warn(const char *fmt,...) { (void)fmt; }
#define WARN_ON(...) 0
#define TRACE(...) ((void)0)
#define USE_WIN32_OPENGL
#define PFD_DRAW_TO_WINDOW 4
#define PFD_SUPPORT_OPENGL 32
#define PFD_DOUBLEBUFFER 1
#define PFD_TYPE_RGBA 0
#define PFD_MAIN_PLANE 0
#define WGL_WINE_PIXEL_FORMAT_PASSTHROUGH 0
#define GL_EXTCALL(call) call
typedef struct { unsigned short nSize,nVersion; unsigned dwFlags; unsigned char iPixelType,cColorBits,cRedBits,cRedShift,cGreenBits,cGreenShift,cBlueBits,cBlueShift,cAlphaBits,cAlphaShift,cAccumBits,cAccumRedBits,cAccumGreenBits,cAccumBlueBits,cAccumAlphaBits,cDepthBits,cStencilBits,cAuxBuffers,iLayerType,bReserved; unsigned dwLayerMask,dwVisibleMask,dwDamageMask; } PIXELFORMATDESCRIPTOR;
struct wined3d_gl_info { BOOL supported[1]; struct { struct { int (*p_wglGetPixelFormat)(HDC); } wgl; struct { void (*p_glFlush)(void); } gl; } gl_ops; };
struct wined3d_context { struct wined3d_gl_info *gl_info; void *glCtx; unsigned level, valid, needs_set; void *restore_ctx, *restore_dc; HDC hdc; BOOL hdc_is_private,hdc_has_format; int restore_pf; HWND restore_pf_win; };
struct device { unsigned context_count; struct wined3d_context **contexts; };
static int share_result=1,share_calls,deletes;
static BOOL wglShareLists(void *src,void *dst) { assert(src&&dst&&src!=dst);++share_calls;return share_result; }
static void context_release(struct wined3d_context *c) { assert(c->level==1);--c->level; }
static BOOL wglDeleteContext(void *c) { assert(c);++deletes;return TRUE; }
static int current_format, choose_result, set_calls, choose_calls, release_calls;
static unsigned GetLastError(void) { return 6; }
static HWND WindowFromDC(HDC dc) { return dc; }
static BOOL context_restore_pixel_format(struct wined3d_context *c) { c->restore_pf=0;return TRUE; }
static BOOL wglSetPixelFormatWINE(HDC dc,int format) { assert(dc);current_format=format;return TRUE; }
static int get_format(HDC dc) { assert(dc);return current_format; }
static BOOL wglMakeCurrent(HDC dc,void *ctx) { assert(dc&&!ctx);++release_calls;return TRUE; }
static int ChoosePixelFormat(HDC dc,const PIXELFORMATDESCRIPTOR *p) { assert(dc&&p);++choose_calls;return choose_result; }
static BOOL SetPixelFormat(HDC dc,int format,const PIXELFORMATDESCRIPTOR *p) { (void)p;assert(dc);++set_calls;if(current_format)return FALSE;current_format=format;return TRUE; }
'''
TEST = r'''
int main(void) {
 struct wined3d_gl_info gl={0};gl.gl_ops.wgl.p_wglGetPixelFormat=get_format;
 struct wined3d_context context={0};context.gl_info=&gl;HDC dc=&context;
 choose_result=1;
 assert(context_set_pixel_format_legacy_native(&context,dc,FALSE,1));
 assert(current_format==1&&set_calls==1&&choose_calls==1&&release_calls==1);
 // A new context on this window (e.g. another rendering thread) must reuse it.
 struct wined3d_context worker={0};worker.gl_info=&gl;
 assert(context_set_pixel_format_legacy_native(&worker,dc,FALSE,1));
 assert(set_calls==1&&choose_calls==1&&release_calls==1);
 // Matching private backup windows are reusable as well.
 assert(context_set_pixel_format_legacy_native(&worker,dc,TRUE,1)&&set_calls==1);
 // An incompatible existing format must still fail, rather than replace it.
 current_format=2;
 assert(!context_set_pixel_format_legacy_native(&worker,dc,FALSE,1));
 assert(current_format==2&&set_calls==2);
 current_format=0;choose_result=0;
 assert(!context_set_pixel_format_legacy_native(&worker,dc,FALSE,1)&&set_calls==2);
 // The production MinGW configuration uses the original non-WIN32_NATIVE helper.
 current_format=0;set_calls=choose_calls=release_calls=0;
 assert(context_set_pixel_format(&context,dc,FALSE,1)&&current_format==1&&set_calls==1);
 assert(context_set_pixel_format(&worker,dc,FALSE,1)&&set_calls==1&&!choose_calls&&!release_calls);
 current_format=2;
 assert(context_set_pixel_format(&worker,dc,FALSE,1)&&current_format==2&&set_calls==1);
 struct wined3d_context *contexts[]={&context};context.glCtx=&context;
 struct device device={0,contexts};
 assert(share_context(&device,&worker,&worker)&&!share_calls&&!deletes);
 device.context_count=1;
 assert(share_context(&device,&worker,&worker)&&share_calls==1&&!deletes);
 share_result=0;worker.level=1;
 assert(!share_context(&device,&worker,&worker)&&share_calls==2&&worker.level==0&&deletes==1);
 puts("PASS prepared Wine actual-MinGW and legacy-native pixel-format reuse and sharing cleanup");
}
'''
with tempfile.TemporaryDirectory(prefix='dreamgpu-thread-pixel-format-') as directory:
    work = Path(directory)
    for name in json.loads(manifest.read_text())['files']:
        dest = work / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'vendor/wine9x' / name, dest)
    patches.apply(work, manifest)
    source = (work / 'wined3d/context.c').read_text()
    first = source.index('static BOOL context_set_pixel_format(')
    start = source.index('static BOOL context_set_pixel_format(', first + 1)
    end = source.index('\n}', start) + 2
    test = work / 'test.c'
    share_start = source.index('    /* All thread contexts of a device use the same texture/list namespace. */')
    share_end = source.index('\n#endif', share_start)
    share = ('static BOOL share_context(struct device *device,struct wined3d_context *ret,void *ctx) {\n'
             + source[share_start:share_end] + '\nreturn TRUE; out: return FALSE; }\n')
    original_end = source.index('\n}', first) + 2
    native = source[start:end].replace('context_set_pixel_format(', 'context_set_pixel_format_legacy_native(', 1)
    test.write_text(PREAMBLE + source[first:original_end] + native + share + TEST)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(test), '-o', str(work / 'test')], check=True)
    subprocess.run([str(work / 'test')], check=True)
