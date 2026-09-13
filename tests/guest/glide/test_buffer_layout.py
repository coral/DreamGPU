#!/usr/bin/env python3
"""Compile the patched donor's actual LFB allocation/initialization under ASan."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
from source import patched_source
source = patched_source('grguSstGlide.cpp')
allocation = source[source.index('#define PADDING'):source.index('    glGenTextures( 1, &Glide.LFBTexture )')]
start = source.index('    ZeroMemory( Glide.SrcBuffer.Address')
initialization = source[start:source.index('#ifdef OGL_DONE', start)]
harness = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <unistd.h>
using FxU32=uint32_t;using FxU16=uint16_t;
#define ZeroMemory(p,n) memset(p,0,n)
static unsigned int_log2(unsigned v){unsigned n=0;while(v>>=1)++n;return n;}
static void check(unsigned width,unsigned height) {
    struct {unsigned WindowTotalPixels;char *oneBuf;FxU32 *tmpBuf;} OpenGL={width*height,nullptr,nullptr};
    struct {unsigned WindowWidth,WindowHeight,WindowTotalPixels,LFBTextureSize;struct {FxU16 *Address;} DstBuffer,SrcBuffer;} Glide={width,height,width*height,0,{nullptr},{nullptr}};
''' + allocation + r'''
    uintptr_t base=(uintptr_t)OpenGL.oneBuf, end=base+OpenGL.WindowTotalPixels*10+PADDING;
    uintptr_t tmp=(uintptr_t)OpenGL.tmpBuf,dst=(uintptr_t)Glide.DstBuffer.Address,src=(uintptr_t)Glide.SrcBuffer.Address;
    assert(tmp>=base && tmp+OpenGL.WindowTotalPixels*4<=dst);
    assert(dst+OpenGL.WindowTotalPixels*4<=src);
    assert(src+OpenGL.WindowTotalPixels*2<=end);
    assert(!(tmp%page_size)&&!(dst%page_size)&&!(src%page_size));
    memset(OpenGL.tmpBuf,0x5a,OpenGL.WindowTotalPixels*4);
''' + initialization + r'''
    for(unsigned i=0;i<OpenGL.WindowTotalPixels;i++) {
        assert(OpenGL.tmpBuf[i]==0x5a5a5a5a);
        assert(Glide.DstBuffer.Address[i*2]==BLUE_SCREEN && Glide.DstBuffer.Address[i*2+1]==BLUE_SCREEN);
        assert(Glide.SrcBuffer.Address[i]==0);
    }
    delete[] OpenGL.oneBuf;
}
int main(){check(1,1);check(511,257);check(640,480);check(800,600);check(1024,768);check(1920,1200);}
'''
with tempfile.TemporaryDirectory(prefix='dg-glide-layout-') as directory:
    path=Path(directory);(path/'test.cpp').write_text(harness)
    subprocess.run(['clang++','-std=c++14','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer',str(path/'test.cpp'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
print('PASS actual Glide buffer allocation/initialization: alignment, bounds, non-overlap and pixels (ASan/UBSan)')
