#!/usr/bin/env python3
"""Actual NT ICD DDIs: primary admission, bounds, and initialized descriptor bytes."""
import os
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[3]
ddi=(ROOT/'guest/nt/display/icd.cpp').read_text()
assert ddi.count('#include "framebuf.h"') == 1
ddi=ddi.replace('#include "framebuf.h"', '')
source = r'''
#include <cstdint>
#include <cstring>
#include <cassert>
using LONG=int32_t; using ULONG=uint32_t;
struct PIXELFORMATDESCRIPTOR {
 uint16_t nSize,nVersion;uint32_t dwFlags;
 uint8_t iPixelType,cColorBits,cRedBits,cRedShift,cGreenBits,cGreenShift,cBlueBits,cBlueShift;
 uint8_t cAlphaBits,cAlphaShift,cAccumBits,cAccumRedBits,cAccumGreenBits,cAccumBlueBits,cAccumAlphaBits;
 uint8_t cDepthBits,cStencilBits,cAuxBuffers,iLayerType,bReserved;
 uint32_t dwLayerMask,dwVisibleMask,dwDamageMask;
};
static_assert(sizeof(PIXELFORMATDESCRIPTOR)==40);
#define PFD_DRAW_TO_WINDOW 4
#define PFD_SUPPORT_OPENGL 32
#define PFD_DOUBLEBUFFER 1
#define PFD_TYPE_RGBA 0
#define PFD_MAIN_PLANE 0
using BOOL=int; using HWND=void*; using DHPDEV=void*;
#define TRUE 1
#define FALSE 0
#define APIENTRY
struct PDEV { unsigned char BitsPerPixel; void* hSurfEng; };
using PPDEV=PDEV*;
struct SURFOBJ { DHPDEV dhpdev; void* hsurf; };
''' + ddi + r'''
int main(){
 struct {PIXELFORMATDESCRIPTOR format;uint32_t guard;} output;
 memset(&output,0xa5,sizeof(output));
 assert(DgIcdDescribePixelFormat(0,0,nullptr)==1);
 assert(!DgIcdDescribePixelFormat(0,40,&output.format));
 assert(!DgIcdDescribePixelFormat(1,39,&output.format));
 assert(output.format.nSize==0xa5a5&&output.guard==0xa5a5a5a5);
 assert(DgIcdDescribePixelFormat(1,44,&output.format)==1);
 assert(output.guard==0xa5a5a5a5);
 auto &f=output.format;
 assert(f.nSize==40&&f.nVersion==1&&f.dwFlags==37);
 assert(f.cColorBits==32&&f.cAlphaBits==8&&f.cDepthBits==24&&f.cStencilBits==8);
 assert(!f.cAuxBuffers&&!f.cAccumBits&&!f.bReserved&&!f.dwLayerMask&&!f.dwDamageMask);
 PDEV dev{};dev.hSurfEng=&dev;
 SURFOBJ surface{&dev,dev.hSurfEng};HWND window=&surface;
 assert(!DrvDescribePixelFormat(nullptr,0,0,nullptr));
 assert(!DrvSetPixelFormat(nullptr,1,window));
 assert(!DrvSetPixelFormat(&surface,1,nullptr));
 for(unsigned bpp=0;bpp<256;++bpp){
  dev.BitsPerPixel=bpp;
  const bool admitted=bpp==16||bpp==32;
  memset(&output,0xa5,sizeof(output));
  assert(DrvDescribePixelFormat(&dev,0,0,nullptr)==admitted);
  assert(DrvDescribePixelFormat(&dev,1,sizeof(output.format),&output.format)==admitted);
  assert(output.guard==0xa5a5a5a5);
  if(admitted){
   assert(output.format.cColorBits==32&&output.format.cRedBits==8&&output.format.cDepthBits==24);
   assert(!DrvDescribePixelFormat(&dev,2,sizeof(output.format),&output.format));
   assert(!DrvDescribePixelFormat(&dev,1,sizeof(output.format)-1,&output.format));
  }else assert(output.format.nSize==0xa5a5);
  assert(DrvSetPixelFormat(&surface,1,window)==admitted);
  assert(!DrvSetPixelFormat(&surface,0,window));
  assert(!DrvSetPixelFormat(&surface,2,window));
  surface.hsurf=nullptr;assert(!DrvSetPixelFormat(&surface,1,window));surface.hsurf=dev.hSurfEng;
 }
 surface.dhpdev=nullptr;assert(!DrvSetPixelFormat(&surface,1,window));
}
'''
with tempfile.TemporaryDirectory(prefix="dreamgpu-pixel-format-") as temporary:
    root=Path(temporary)
    (root/"test.cpp").write_text(source)
    subprocess.run([os.environ.get("CXX","c++"),"-std=c++23","-O1","-Wall","-Wextra","-Werror",
                    "-fsanitize=address,undefined","-I"+str(ROOT/"guest/nt/display"),
                    str(root/"test.cpp"),"-o",str(root/"test")],check=True)
    subprocess.run([str(root/"test")],check=True)
print("PASS actual NT ICD DDIs: RGB565/32-bit primary admission, invalid surfaces/formats, truthful RGBA8, descriptor bounds/tail")
