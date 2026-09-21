#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual installed fullscreen probe against coherent and deliberately stale APIs."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
HARNESS = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
using DWORD=uint32_t; using UINT=unsigned; using WORD=uint16_t; using BYTE=uint8_t;
using BOOL=bool; using HRESULT=int32_t; using COLORREF=DWORD; using HBRUSH=COLORREF*;
constexpr BOOL TRUE=true,FALSE=false;
constexpr DWORD DDLOCK_WAIT=1,DDLOCK_READONLY=2,DDSCL_EXCLUSIVE=4,DDSCL_FULLSCREEN=8,
 DDSCL_NORMAL=16,DDSD_CAPS=1,DDSD_WIDTH=2,DDSD_HEIGHT=4,DDSD_PIXELFORMAT=8,
 DDSCAPS_PRIMARYSURFACE=1,DDSCAPS_OFFSCREENPLAIN=2,DDSCAPS_SYSTEMMEMORY=4,
 DDCKEY_SRCBLT=1,DDBLT_WAIT=1,DDBLT_KEYSRC=2,ENUM_CURRENT_SETTINGS=0,
 SM_CXSCREEN=1,SM_CYSCREEN=2,SWP_NOZORDER=1,SWP_NOACTIVATE=2,WHITENESS=0xff0062;
struct RECT { int left,top,right,bottom; };
struct DDSCAPS2 { DWORD dwCaps; };
struct DDPIXELFORMAT { DWORD dwRGBBitCount,dwRBitMask,dwGBitMask,dwBBitMask; };
struct DDSURFACEDESC2 { DWORD dwSize,dwFlags,dwWidth,dwHeight; DDSCAPS2 ddsCaps;
 DDPIXELFORMAT ddpfPixelFormat; int lPitch; void *lpSurface; };
struct DEVMODEA { DWORD dmSize,dmPelsWidth,dmPelsHeight,dmBitsPerPel; };
struct DDCOLORKEY { DWORD low,high; };
struct IDirectDraw4 {};
struct IDirectDrawSurface4 { bool primary; UINT width,height; std::vector<WORD> pixels; };
using HDC=IDirectDrawSurface4*;
static IDirectDraw4 dd,*draw=&dd;
static void *window=reinterpret_cast<void*>(1);
static IDirectDrawSurface4 screen;
static bool failed,mode_set,exclusive;
static HDC pending_dc;
static void finish_batch(){if(pending_dc){for(int y=40;y<48;++y)for(int x=40;x<48;++x)screen.pixels[y*640+x]=0xffff;pending_dc=nullptr;}}
static unsigned scenario,surfaces,locks,releases,source_reads,keyed_writes,partial_writes,dc_releases;
static std::string reason;
static COLORREF RGB(unsigned r,unsigned g,unsigned b) {return r|(g<<8)|(b<<16);}
static WORD pack(COLORREF c) {return WORD(((c&255)>>3)<<11|(((c>>8)&255)>>2)<<5|(((c>>16)&255)>>3));}
static DWORD GetLastError(){return 0;}
static void Log(const char*){}
static void Number(const char*,DWORD){}
static BOOL Check(BOOL okay,const char *stage,DWORD) {if(!okay){failed=true;reason=stage;}return okay;}
static BOOL HR(HRESULT result,const char *stage){return Check(result>=0,stage,DWORD(result));}
static std::vector<WORD>& data(IDirectDrawSurface4 *s){return s->primary?screen.pixels:s->pixels;}
static HRESULT IDirectDraw4_SetCooperativeLevel(IDirectDraw4*,void*,DWORD flags){exclusive=flags!=DDSCL_NORMAL;return 0;}
static HRESULT IDirectDraw4_SetDisplayMode(IDirectDraw4*,DWORD w,DWORD h,DWORD b,DWORD,DWORD){assert(w==640&&h==480&&b==16);mode_set=true;return 0;}
static HRESULT IDirectDraw4_RestoreDisplayMode(IDirectDraw4*){mode_set=false;return 0;}
static BOOL EnumDisplaySettingsA(void*,DWORD,DEVMODEA *d){d->dmPelsWidth=scenario==1?1280:640;d->dmPelsHeight=480;d->dmBitsPerPel=16;return true;}
static BOOL GetClientRect(void*,RECT *r){*r={0,0,640,480};return true;}
static BOOL GetWindowRect(void*,RECT *r){*r={64,64,404,324};return true;}
static BOOL SetWindowPos(void*,void*,int x,int y,int w,int h,DWORD){assert(!exclusive&&!mode_set&&x==64&&y==64&&w==340&&h==260);return true;}
static int GetSystemMetrics(int n){return n==SM_CXSCREEN?640:480;}
static HRESULT IDirectDraw4_GetDisplayMode(IDirectDraw4*,DDSURFACEDESC2 *d){d->dwWidth=640;d->dwHeight=480;d->ddpfPixelFormat={16,0xf800,0x7e0,31};return 0;}
static HRESULT IDirectDraw4_CreateSurface(IDirectDraw4*,DDSURFACEDESC2 *d,IDirectDrawSurface4 **out,void*){
 bool primary=d->ddsCaps.dwCaps==DDSCAPS_PRIMARYSURFACE;
 *out=new IDirectDrawSurface4{primary,primary?640:d->dwWidth,primary?480:d->dwHeight,{}};
 if(!primary)(*out)->pixels.resize(d->dwWidth*d->dwHeight);++surfaces;return 0;
}
static HRESULT IDirectDrawSurface4_GetSurfaceDesc(IDirectDrawSurface4 *s,DDSURFACEDESC2 *d){d->dwWidth=s->width;d->dwHeight=s->height;d->ddpfPixelFormat={16,0xf800,0x7e0,31};return 0;}
static HRESULT IDirectDrawSurface4_Lock(IDirectDrawSurface4 *s,RECT *r,DDSURFACEDESC2 *d,DWORD,void*){
 ++locks;d->lPitch=int(s->width*2);d->ddpfPixelFormat={16,0xf800,0x7e0,31};
 d->lpSurface=data(s).data()+(r?r->top*s->width+r->left:0);
 if(scenario==2&&s->primary)screen.pixels.assign(screen.pixels.size(),0);
 return 0;
}
static HRESULT IDirectDrawSurface4_Unlock(IDirectDrawSurface4*,RECT*){assert(locks);--locks;return 0;}
static HRESULT IDirectDrawSurface4_GetDC(IDirectDrawSurface4 *s,HDC *dc){*dc=s;if(scenario==7)data(s).assign(data(s).size(),0);return 0;}
static HRESULT IDirectDrawSurface4_ReleaseDC(IDirectDrawSurface4 *s,HDC dc){assert(s==dc);if(scenario!=8)finish_batch();if(scenario==4&&++dc_releases==2)data(s).assign(data(s).size(),0);return 0;}
static HRESULT IDirectDrawSurface4_SetColorKey(IDirectDrawSurface4*,DWORD,DDCOLORKEY*){return 0;}
static HRESULT IDirectDrawSurface4_Blt(IDirectDrawSurface4 *dst,RECT *r,IDirectDrawSurface4 *src,RECT *q,DWORD flags,void*){
 if(src->primary)++source_reads;
 if(flags&DDBLT_KEYSRC)++keyed_writes;
 if(dst->primary&&!(flags&DDBLT_KEYSRC)){++partial_writes;if(scenario==6)data(dst).assign(data(dst).size(),0);}
 for(int y=0;y<r->bottom-r->top;++y)for(int x=0;x<r->right-r->left;++x){
 WORD value=data(src)[(q->top+y)*src->width+q->left+x];
 if(src->primary&&scenario==3)value=0;
 if(!(flags&DDBLT_KEYSRC)||value||scenario==5)data(dst)[(r->top+y)*dst->width+r->left+x]=value;
 }return 0;
}
static void IDirectDrawSurface4_Release(IDirectDrawSurface4 *s){++releases;delete s;}
static HDC GetDC(void*){return &screen;}
static int ReleaseDC(void*,HDC){return 1;}
static HBRUSH CreateSolidBrush(COLORREF c){return new COLORREF(c);}
static BOOL DeleteObject(HBRUSH b){delete b;return true;}
static BOOL FillRect(HDC dc,const RECT *r,HBRUSH b){for(int y=r->top;y<r->bottom;++y)for(int x=r->left;x<r->right;++x)data(dc)[y*dc->width+x]=pack(*b);return true;}
static BOOL PatBlt(HDC dc,int x,int y,int w,int h,DWORD rop){assert(x==40&&y==40&&w==8&&h==8&&rop==WHITENESS&&!pending_dc);pending_dc=dc;return true;}
static BOOL GdiFlush(){finish_batch();return true;}
static COLORREF GetPixel(HDC dc,int x,int y){WORD p=data(dc)[y*dc->width+x];return RGB(((p>>11)&31)*255/31,((p>>5)&63)*255/63,(p&31)*255/31);}
#include "fullscreen-probe.inc"
int main(){
 for(scenario=0;scenario<9;++scenario){
 failed=mode_set=exclusive=false;surfaces=locks=releases=source_reads=keyed_writes=partial_writes=dc_releases=0;
 pending_dc=nullptr;screen={true,640,480,std::vector<WORD>(640*480,0)};reason.clear();
 assert(FullscreenProbe()==(scenario==0));
 assert(!exclusive&&!mode_set&&!locks&&surfaces==releases);
 if(!scenario)assert(source_reads==1&&keyed_writes==1&&partial_writes==1);
 else assert(!reason.empty());
 }
 puts("PASS actual fullscreen probe: initial mode and GDI/source/DC/keyed/partial preservation and batched ReleaseDC oracles, cleanup on every failure");
}
'''
with tempfile.TemporaryDirectory(prefix='dg-fullscreen-probe-') as directory:
    path = Path(directory)
    (path / 'test.cpp').write_text(HARNESS)
    subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    '-Wno-misleading-indentation', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    '-I' + str(ROOT / 'tools/d3d'), str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
