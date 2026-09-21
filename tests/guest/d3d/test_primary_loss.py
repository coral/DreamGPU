#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute prepared DirectDraw scanout loss and restoration under ASan/UBSan."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[3]
spec=importlib.util.spec_from_file_location('patches',ROOT/'tests/guest/support/patches.py')
patches=importlib.util.module_from_spec(spec);spec.loader.exec_module(patches)
MANIFEST=ROOT/'support/guest/d3d/patches/base/manifest.json'
PREAMBLE=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int BOOL,HRESULT;typedef int32_t LONG;typedef unsigned DWORD;
typedef void *HWND,*HANDLE,*HMONITOR;
typedef struct {int left,top,right,bottom;}RECT;
typedef struct {unsigned cbSize;RECT rcMonitor;}MONITORINFO,*LPMONITORINFO;
typedef HMONITOR (*MonitorFromWindow_func)(HWND,DWORD);
typedef BOOL (*GetMonitorInfoA_func)(HMONITOR,LPMONITORINFO);
#define CALLBACK
#define CDECL
#define WINAPI
#define TRUE 1
#define FALSE 0
#define TRACE(...) ((void)0)
#define ERR(...) ((void)0)
#define FAILED(x) ((x)<0)
#define DD_OK 0
#define WINED3D_OK 0
#define WINED3DERR_DEVICELOST -1
#define DDERR_SURFACELOST -2
#define DDERR_IMPLICITLYCREATED -3
#define DDERR_NOEXCLUSIVEMODE -4
#define DDERR_WRONGMODE -5
#define DDSCL_EXCLUSIVE 1
#define WINED3DADAPTER_DEFAULT 0
#define DDRAW_DEVICE_STATE_OK 0
#define DDRAW_DEVICE_STATE_LOST 1
#define MONITOR_DEFAULTTOPRIMARY 1
#define HWND_TOP NULL
#define SWP_SHOWWINDOW 1
#define SWP_NOACTIVATE 2
struct wined3d_device_parent {int unused;};
struct wined3d_surface {BOOL lost;unsigned pixels;HRESULT restore_error;};
struct ddraw_surface;
struct ddraw {struct wined3d_device_parent parent;LONG primary_loss_epoch,device_state;unsigned cooperative_level;
 struct ddraw_surface *primary;HWND swapchain_window;void *wined3d;};
struct ddraw_surface {struct ddraw *ddraw;struct wined3d_surface *wined3d_surface;
 BOOL primary_chain,is_complex_root;LONG restored_primary_epoch;struct ddraw_surface *complex_array[6];
 struct {unsigned dwWidth,dwHeight;struct {unsigned ddpfPixelFormat;}u4;}surface_desc;};
typedef struct ddraw_surface IDirectDrawSurface7;
typedef struct ddraw IDirectDraw7;typedef struct {int unused;} DDSURFACEDESC2;
#define DDENUMRET_OK 1
#define DDENUMRET_CANCEL 0
#define DDENUMSURFACES_ALL 1
#define DDENUMSURFACES_DOESEXIST 2
#define IDirectDrawSurface7_Restore ddraw_surface7_Restore
static unsigned enum_releases;
static struct ddraw_surface *enum_surfaces[3];
static void IDirectDrawSurface7_Release(IDirectDrawSurface7*s){assert(s);++enum_releases;}
static HRESULT IDirectDraw7_EnumSurfaces(IDirectDraw7*d,unsigned flags,void*desc,void*context,
 HRESULT(*callback)(IDirectDrawSurface7*,DDSURFACEDESC2*,void*))
{assert(d&&flags==3&&!desc);for(unsigned i=0;i<3;++i)if(callback(enum_surfaces[i],NULL,context)==DDENUMRET_CANCEL)break;return 0;}

struct wined3d_display_mode {unsigned width,height,format_id;};
static struct wined3d_display_mode mode={800,600,16};
static HRESULT mode_error;
static HRESULT wined3d_get_adapter_display_mode(void*d,unsigned adapter,struct wined3d_display_mode*m,void*rotation)
{(void)d;assert(!adapter&&!rotation);*m=mode;return mode_error;}
static unsigned wined3dformat_from_ddrawformat(const unsigned*f){return *f;}
static struct ddraw owner;
static unsigned restores,lock_depth;
static BOOL race_mode;
static LONG InterlockedCompareExchange(LONG*p,LONG value,LONG compare){LONG old=*p;if(old==compare)*p=value;return old;}
static LONG InterlockedIncrement(LONG*p){return ++*p;}
static struct ddraw *ddraw_from_device_parent(struct wined3d_device_parent*p){assert(p==&owner.parent);return &owner;}
static struct ddraw_surface *impl_from_IDirectDrawSurface7(IDirectDrawSurface7*p){return p;}
static void wined3d_mutex_lock(void){assert(!lock_depth);++lock_depth;}
static void wined3d_mutex_unlock(void){assert(lock_depth==1);--lock_depth;}
static HRESULT wined3d_surface_is_lost(struct wined3d_surface*s){return s->lost?WINED3DERR_DEVICELOST:0;}
static HRESULT wined3d_surface_restore(struct wined3d_surface*s)
{assert(lock_depth);++restores;if(s->restore_error)return s->restore_error;s->lost=0;
 if(race_mode){++owner.primary_loss_epoch;race_mode=0;}return 0;}
static HANDLE GetModuleHandle(const char*n){assert(n);return NULL;}
static void *GetProcAddress(HANDLE h,const char*n){assert(h&&n);return NULL;}
static BOOL SetWindowPos(HWND w,HWND after,int x,int y,int width,int height,unsigned flags)
{(void)w;(void)after;(void)x;(void)y;(void)width;(void)height;(void)flags;return 1;}
'''
TEST=r'''
int main(void)
{
 struct wined3d_surface memory[5]={{0,0xabcdef,0},{0,0x123456,0},{0,0xff00ff,0},{0,0x765432,0},{0,0x44ee55,0}};
 struct ddraw_surface primary={&owner,&memory[0],1,1,0,{0}};
 struct ddraw_surface back1={&owner,&memory[1],1,0,0,{0}},back2={&owner,&memory[2],1,0,0,{0}};
 struct ddraw_surface system={&owner,&memory[3],0,1,0,{0}},texture={&owner,&memory[4],0,1,0,{0}};
 primary.complex_array[0]=&back1;back1.complex_array[0]=&back2;back2.complex_array[0]=&primary;
 primary.surface_desc.dwWidth=800;primary.surface_desc.dwHeight=600;primary.surface_desc.u4.ddpfPixelFormat=16;
 owner.primary=&primary;owner.cooperative_level=DDSCL_EXCLUSIVE;
 enum_surfaces[0]=&back1;enum_surfaces[1]=&primary;enum_surfaces[2]=&texture;
 assert(!ddraw_surface7_IsLost(&primary));
 device_parent_activate(&owner.parent,FALSE);
 assert(owner.primary_loss_epoch==1&&owner.device_state==DDRAW_DEVICE_STATE_LOST);
 device_parent_activate(&owner.parent,FALSE);assert(owner.primary_loss_epoch==1);
 assert(ddraw_surface7_IsLost(&primary)==DDERR_SURFACELOST);
 assert(ddraw_surface7_IsLost(&back1)==DDERR_SURFACELOST);
 assert(!ddraw_surface7_IsLost(&system)&&!ddraw_surface7_IsLost(&texture));
 assert(ddraw_surface7_Restore(&primary)==DDERR_NOEXCLUSIVEMODE&&!restores);
 assert(ddraw7_RestoreAllSurfaces(&owner)==DDERR_NOEXCLUSIVEMODE&&!restores&&enum_releases==2);
 device_parent_activate(&owner.parent,TRUE);
 assert(owner.device_state==DDRAW_DEVICE_STATE_OK&&ddraw_surface7_IsLost(&primary)==DDERR_SURFACELOST);
 assert(ddraw_surface7_Restore(&back1)==DDERR_IMPLICITLYCREATED&&!restores);
 memory[2].restore_error=-9;
 assert(ddraw_surface7_Restore(&primary)==-9);
 assert(ddraw_surface7_IsLost(&primary)==DDERR_SURFACELOST&&ddraw_surface7_IsLost(&back1)==DDERR_SURFACELOST);
 memory[2].restore_error=0;assert(!ddraw_surface7_Restore(&primary));
 assert(!ddraw_surface7_IsLost(&primary)&&!ddraw_surface7_IsLost(&back1)&&!ddraw_surface7_IsLost(&back2));
 // Mode notification invalidates scanout while retaining system and texture data.
 device_parent_mode_changed(&owner.parent);assert(owner.primary_loss_epoch==2);
 assert(ddraw_surface7_IsLost(&primary)==DDERR_SURFACELOST);
 assert(!ddraw_surface7_IsLost(&system)&&!ddraw_surface7_IsLost(&texture));
 assert(memory[3].pixels==0x765432&&memory[4].pixels==0x44ee55);
 // Restoring into a different display mode is rejected without clearing loss.
 unsigned old_restores=restores;
 mode.width=1024;assert(ddraw_surface7_Restore(&primary)==DDERR_WRONGMODE&&restores==old_restores);
 mode.width=800;mode.format_id=32;assert(ddraw_surface7_Restore(&primary)==DDERR_WRONGMODE);
 mode.format_id=16;mode_error=-7;assert(ddraw_surface7_Restore(&primary)==-7);mode_error=0;
 // A concurrent mode change must not accidentally clear the later loss.
 race_mode=1;assert(ddraw_surface7_Restore(&primary)==DDERR_WRONGMODE);
 assert(ddraw_surface7_IsLost(&primary)==DDERR_SURFACELOST);
 assert(!ddraw7_RestoreAllSurfaces(&owner));
 // Windowed focus changes do not lose the primary; actual mode changes do.
 owner.cooperative_level=0;LONG epoch=owner.primary_loss_epoch;
 device_parent_activate(&owner.parent,FALSE);assert(owner.primary_loss_epoch==epoch);
 assert(!ddraw_surface7_IsLost(&primary));
 device_parent_mode_changed(&owner.parent);assert(ddraw_surface7_IsLost(&primary)==DDERR_SURFACELOST);
 assert(!ddraw_surface7_Restore(&primary));
 // Preserve existing Wine resource-loss handling on independent allocations.
 memory[4].lost=1;assert(ddraw_surface7_IsLost(&texture)==DDERR_SURFACELOST);
 assert(!ddraw_surface7_Restore(&texture)&&!ddraw_surface7_IsLost(&texture));
 assert(!lock_depth);
 puts("PASS prepared DirectDraw loss: exclusive focus persistence, display-mode loss, implicit flip-ring restoration, retained system/offscreen pixels, failure/race isolation");
}
'''
def function(source,signature):
    start=source.index(signature);end=source.index('\n}',start)+2
    return source[start:end]+'\n'
with tempfile.TemporaryDirectory(prefix='dreamgpu-primary-loss-') as temporary:
    work=Path(temporary)
    for name in json.loads(MANIFEST.read_text())['files']:
        p=work/name;p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/'vendor/wine9x'/name,p)
    patches.apply(work,MANIFEST)
    surface=(work/'ddraw/surface.c').read_text();ddraw=(work/'ddraw/ddraw.c').read_text()
    code=PREAMBLE
    for signature in ['static BOOL ddraw_surface_is_lost(', 'static HRESULT WINAPI ddraw_surface7_IsLost(',
                      'static HRESULT ddraw_surface_restore(', 'static HRESULT WINAPI ddraw_surface7_Restore(']:
        code+=function(surface,signature)
    for signature in ['static void CDECL device_parent_mode_changed(', 'static void CDECL device_parent_activate(']:
        code+=function(ddraw,signature)
    for signature in ['static HRESULT CALLBACK restore_callback(', 'static HRESULT WINAPI ddraw7_RestoreAllSurfaces(']:
        code+=function(ddraw,signature)
    # Public entrypoints must reject lost storage before pixel access. Existing
    # primary coherence tests execute those transfers independently of lifecycle.
    for signature,operation in [
        ('static HRESULT surface_lock(', 'wined3d_surface_map('),
        ('ddraw_surface7_GetDC(IDirectDrawSurface7', 'ddraw_surface_update_frontbuffer('),
        ('ddraw_surface7_Blt(IDirectDrawSurface7', 'ddraw_surface_blt_clipped('),
        ('ddraw_surface7_BltFast(IDirectDrawSurface7', 'wined3d_surface_blt('),
        ('HRESULT ddraw_surface_update_frontbuffer(', 'ddraw_surface_copy_screen('),
    ]:
        body=function(surface,signature)
        assert body.index('ddraw_surface_is_lost(')<body.index(operation)
        assert 'return DDERR_SURFACELOST;' in body
    assert 'last->primary_chain = root->primary_chain;' in surface
    assert 'IDirectDrawSurface7_IsLost(iface) == DDERR_SURFACELOST' in function(surface,'ddraw_surface7_Flip(IDirectDrawSurface7')
    (work/'test.c').write_text(code+TEST)
    subprocess.run([os.environ.get('CC','cc'),'-std=c99','-Wall','-Wextra','-Werror',
                    '-Wno-unused-parameter','-Wno-missing-field-initializers','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(work/'test.c'),'-o',str(work/'test')],check=True)
    subprocess.run([str(work/'test')],check=True)
