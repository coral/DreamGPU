#!/usr/bin/env python3
"""Actual diagnostic NT format: strict bounds and initialized descriptor bytes."""
import os
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[3]
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
#include "icd-pixel-format.h"
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
}
'''
with tempfile.TemporaryDirectory(prefix="dreamgpu-pixel-format-") as temporary:
    root=Path(temporary)
    (root/"test.cpp").write_text(source)
    subprocess.run([os.environ.get("CXX","c++"),"-std=c++23","-O1","-Wall","-Wextra","-Werror",
                    "-fsanitize=address,undefined","-I"+str(ROOT/"guest/nt/display"),
                    str(root/"test.cpp"),"-o",str(root/"test")],check=True)
    subprocess.run([str(root/"test")],check=True)
print("PASS diagnostic NT pixel descriptor; bounds, initialized fields, tail preservation")
