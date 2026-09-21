#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute prepared fullscreen ownership/mode code, including defect controls."""
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
#include <limits.h>
#include <stdio.h>
#include <string.h>
typedef int BOOL,HRESULT,LONG;typedef unsigned UINT,DWORD;
#define TRUE 1
#define FALSE 0
#define CDECL
#define TRACE(...) ((void)0)
#define WARN(...) ((void)0)
#define ERR(...) ((void)0)
#define FAILED(x) ((x)<0)
#define WINED3D_OK 0
#define WINED3DERR_INVALIDCALL -1
#define WINED3DERR_NOTAVAILABLE -2
#define ENUM_CURRENT_SETTINGS 1
#define ENUM_REGISTRY_SETTINGS 2
#define DM_BITSPERPEL 1
#define DM_PELSWIDTH 2
#define DM_PELSHEIGHT 4
#define DM_DISPLAYFREQUENCY 8
#define DM_DISPLAYFLAGS 16
#define DM_INTERLACED 1
#define DUMMYACCESS1(a,b,c) ((a).c)
#define CDS_FULLSCREEN 4
#define DISP_CHANGE_SUCCESSFUL 0
#define WINED3D_SCANLINE_ORDERING_UNKNOWN 0
#define WINED3D_SCANLINE_ORDERING_INTERLACED 1
#define WINED3D_FOCUS_MESSAGES 1
#define WINED3D_RESTORE_MODE_ON_ACTIVATE 2
#define WINED3DCREATE_NOWINDOWCHANGES 4
#define MEM_COMMIT 1
#define PAGE_NOACCESS 2
#define SWP_NOACTIVATE 1
#define SWP_NOZORDER 2
#define SW_MINIMIZE 3
struct {BOOL force32bit;} wined3d_settings;
enum wined3d_format_id {FMT_16=16,FMT_32=32};
struct wined3d_format {unsigned byte_count;};
struct wined3d_display_mode {UINT width,height,refresh_rate;enum wined3d_format_id format_id;UINT scanline_ordering;};
struct wined3d_adapter {char DeviceName[8];int gl_info;UINT ordinal;enum wined3d_format_id screen_format;};
struct wined3d {UINT adapter_count,flags;struct wined3d_adapter *adapters;};
struct wined3d_device {struct wined3d *wined3d;struct wined3d_adapter *adapter;BOOL filter_messages;struct {UINT flags;} create_parms;};
struct wined3d_swapchain {struct wined3d_device *device;struct {BOOL windowed;UINT backbuffer_width,backbuffer_height;} desc;void *device_window;struct wined3d_display_mode d3d_mode;BOOL reapply_mode;};
typedef struct {UINT dmSize,dmFields,dmBitsPerPel,dmPelsWidth,dmPelsHeight,dmDisplayFrequency,dmDisplayFlags;} DEVMODEA;
typedef struct {int left,top,right,bottom;} RECT;
typedef struct {UINT State,Protect;} MEMORY_BASIC_INFORMATION;
static DEVMODEA current,registry;
static unsigned native_calls,clips,positions,shows,queries,fail_changes,fail_registry,fail_current,first_fields,last_fields;
static const struct wined3d_format *wined3d_get_format(const int *gl,enum wined3d_format_id id){static struct wined3d_format f;(void)gl;f.byte_count=id/8;return &f;}
static enum wined3d_format_id pixelformat_for_depth(UINT depth){return (enum wined3d_format_id)depth;}
static BOOL EnumDisplaySettingsA95(const char *name,UINT which,DEVMODEA *out){assert(!strcmp(name,"screen"));++queries;if(which==ENUM_REGISTRY_SETTINGS){if(fail_registry)return FALSE;*out=registry;}else{if(fail_current)return FALSE;*out=current;}return TRUE;}
static LONG ChangeDisplaySettingsExA95(const char *name,DEVMODEA *mode,void *window,UINT flags,void *extra){assert(!strcmp(name,"screen")&&!window&&!extra&&flags==CDS_FULLSCREEN);if(!native_calls)first_fields=mode->dmFields;++native_calls;last_fields=mode->dmFields;if(fail_changes){--fail_changes;return -1;}current=*mode;return DISP_CHANGE_SUCCESSFUL;}
static void SetRect(RECT *r,int x,int y,int w,int h){*r=(RECT){x,y,w,h};}
static void ClipCursor(const RECT *r){assert(r->right==(int)current.dmPelsWidth&&r->bottom==(int)current.dmPelsHeight);++clips;}
static unsigned VirtualQuery(void *p,MEMORY_BASIC_INFORMATION *info,unsigned size){assert(p&&size==sizeof(struct wined3d_device));info->State=MEM_COMMIT;info->Protect=0;return 1;}
static void SetWindowPos(void *w,void *after,int x,int y,UINT width,UINT height,UINT flags){assert(w&&!after&&!x&&!y&&width==800&&height==600&&flags==(SWP_NOACTIVATE|SWP_NOZORDER));++positions;}
static BOOL IsWindowVisible(void *w){assert(w);return TRUE;}
static void ShowWindow(void *w,int how){assert(w&&how==SW_MINIMIZE);++shows;}
static void clear_counts(void){native_calls=clips=positions=shows=queries=fail_changes=fail_registry=fail_current=first_fields=last_fields=0;}
'''
TEST = r'''
int main(void){
 struct wined3d_adapter adapter={"screen",0,0,FMT_32};
 struct wined3d api={1,WINED3D_RESTORE_MODE_ON_ACTIVATE,&adapter};
 struct wined3d_device device={&api,&adapter,FALSE,{0}};
 struct wined3d_swapchain chain={&device,{TRUE,800,600},&device,{800,600,0,FMT_16,0},FALSE};
 registry=(DEVMODEA){sizeof(DEVMODEA),DM_BITSPERPEL|DM_PELSWIDTH|DM_PELSHEIGHT,32,1280,1024,60,0};
 current=(DEVMODEA){sizeof(DEVMODEA),DM_BITSPERPEL|DM_PELSWIDTH|DM_PELSHEIGHT,16,800,600,60,0};
 // A windowed launcher's late activation/deactivation cannot mutate its successor.
 for(unsigned filter=0;filter<2;++filter){device.filter_messages=filter;for(unsigned active=0;active<2;++active){
  clear_counts();wined3d_swapchain_activate(&chain,active);
  assert(!native_calls&&!clips&&!positions&&!shows&&!queries&&!chain.reapply_mode);
  assert(device.filter_messages==(BOOL)filter&&adapter.screen_format==FMT_32&&current.dmPelsWidth==800);
 }}
 // Explicit same-mode requests must reach Windows: process ownership is part of this operation.
 clear_counts();assert(!wined3d_set_adapter_display_mode(&api,0,&chain.d3d_mode));
 assert(native_calls==1&&clips==1&&adapter.screen_format==FMT_16&&current.dmPelsWidth==800);
 // Fullscreen Alt+Tab still restores the desktop and re-applies its rendering mode.
 chain.desc.windowed=FALSE;
 for(unsigned filter=0;filter<2;++filter){device.filter_messages=filter;clear_counts();
  wined3d_swapchain_activate(&chain,FALSE);
  assert(native_calls==1&&clips==1&&shows==1&&!positions&&chain.reapply_mode);
  assert(adapter.screen_format==FMT_32&&current.dmPelsWidth==1280&&device.filter_messages==(BOOL)filter);
  clear_counts();wined3d_swapchain_activate(&chain,TRUE);
  assert(native_calls==1&&clips==1&&positions==1&&!shows&&adapter.screen_format==FMT_16);
  assert(current.dmPelsWidth==800&&device.filter_messages==(BOOL)filter);
 }
 // Same-mode restoration avoids unnecessary native work, but repairs cached format.
 current=registry;adapter.screen_format=FMT_16;clear_counts();
 assert(!wined3d_set_adapter_display_mode(&api,0,NULL));
 assert(!native_calls&&!clips&&adapter.screen_format==FMT_32);
 // Failed requests never publish their new format; refresh-rate retry preserves the contract.
 clear_counts();fail_changes=1;assert(wined3d_set_adapter_display_mode(&api,0,&chain.d3d_mode)==WINED3DERR_NOTAVAILABLE);
 assert(native_calls==1&&!clips&&adapter.screen_format==FMT_32&&current.dmPelsWidth==1280);
 chain.d3d_mode.refresh_rate=75;clear_counts();fail_changes=1;
 assert(!wined3d_set_adapter_display_mode(&api,0,&chain.d3d_mode));
 assert(native_calls==2&&clips==1&&(first_fields&DM_DISPLAYFREQUENCY)&&!(last_fields&DM_DISPLAYFREQUENCY)&&adapter.screen_format==FMT_16);
 clear_counts();fail_changes=2;adapter.screen_format=FMT_32;
 assert(wined3d_set_adapter_display_mode(&api,0,&chain.d3d_mode)==WINED3DERR_NOTAVAILABLE);
 assert(native_calls==2&&!clips&&adapter.screen_format==FMT_32);
 clear_counts();fail_registry=1;assert(wined3d_set_adapter_display_mode(&api,0,NULL)==WINED3DERR_NOTAVAILABLE);
 assert(!native_calls&&!clips&&adapter.screen_format==FMT_32);
 clear_counts();assert(wined3d_set_adapter_display_mode(&api,1,&chain.d3d_mode)==WINED3DERR_INVALIDCALL);assert(!queries&&!native_calls);
 clear_counts();fail_current=1;assert(!wined3d_set_adapter_display_mode(&api,0,&chain.d3d_mode));assert(native_calls==1&&clips==1);
 puts("PASS prepared fullscreen: windowed non-interference, same-mode native ownership, Alt+Tab, cached format, failed requests and refresh fallback");
}
'''

def function(source, prefix):
    start = source.index(prefix)
    return source[start:source.index('\n}', start) + 2]

with tempfile.TemporaryDirectory(prefix='dreamgpu-fullscreen-activation-') as directory:
    work = Path(directory)
    for name in json.loads(MANIFEST.read_text())['files']:
        path = work / name
        path.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'vendor/wine9x' / name, path)
    patches.apply(work, MANIFEST)
    mode = function((work / 'wined3d/directx.c').read_text(), 'HRESULT CDECL wined3d_set_adapter_display_mode(')
    activate = function((work / 'wined3d/swapchain.c').read_text(), 'void wined3d_swapchain_activate(')
    guard = '    if (swapchain->desc.windowed)\n        return;'
    same_mode = 'else if (!mode && current_mode.dmPelsWidth'
    assert guard in activate and same_mode in mode
    cases = {
        'fixed': mode + activate,
        'windowed-defect': mode + activate.replace(guard, ''),
        'ownership-defect': mode.replace(same_mode, 'else if (current_mode.dmPelsWidth') + activate,
    }
    for name, code in cases.items():
        source = work / (name + '.c')
        executable = work / name
        source.write_text(PREAMBLE + code + TEST)
        subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(source),
                        '-o', str(executable)], check=True)
        result = subprocess.run([str(executable)], capture_output=name != 'fixed')
        if name == 'fixed':
            result.check_returncode()
        else:
            assert result.returncode != 0 and b'Assertion' in result.stderr, (name, result)
    print('PASS both historical defect controls are rejected by the runtime assertions')
