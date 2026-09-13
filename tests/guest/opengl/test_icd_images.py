#!/usr/bin/env python3
"""Actual immutable image guest packer; reusable fixture for adversarial tests."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[3]
def run(body):
    api=(ROOT/'crates/dreamgpu-host/src/gl_api.rs').read_text()
    constants={name:value for name,value in re.findall(r'pub const (GL_\w+): [^=]+ = (\d+);',api)}
    source=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <vector>
#include <climits>
#define APIENTRY
#define CopyMemory memcpy
using ULONG=uint32_t;using ULONG_PTR=uintptr_t;using BYTE=uint8_t;
using GLenum=uint32_t;using GLint=int32_t;using GLsizei=int32_t;
using GLfloat=float;using GLubyte=uint8_t;using GLvoid=void;
#include "gl-funcs.h"
'''
    source+='\n'.join(f'#define {name} {value}' for name,value in constants.items())
    source+=r'''
struct JGL_UNPACK {GLint Alignment,RowLength,SkipRows,SkipPixels,SwapBytes,LsbFirst;};
struct JGL_ARRAY_STATE {BYTE Scratch[65536];};
static JGL_UNPACK unpack{4,0,0,0,0,0};static JGL_ARRAY_STATE arrays{};
static unsigned error=0,capacity=65536,identity=0,attempts=0,fail_at=0;
static bool ready=true;
struct Record {ULONG function;std::vector<ULONG> args;std::vector<BYTE> payload;};
static std::vector<Record> records;
static bool JglReady(){if(!ready)error=GL_INVALID_OPERATION;return ready;}
static void JglSetError(unsigned e){error=e;}
static JGL_UNPACK* JglUnpack(){return &unpack;}
static JGL_ARRAY_STATE* JglArrays(){return &arrays;}
static ULONG JglMaxDataBytes(ULONG n){assert(n==8);return capacity;}
static ULONG JglNextImageId(){if(identity==0xffffffff){error=GL_OUT_OF_MEMORY;return 0;}return ++identity;}
static bool JglData(ULONG f,const ULONG*a,ULONG n,const void*p,ULONG bytes){
 assert(n==8);if(++attempts==fail_at)return false;
 records.push_back({f,{a,a+n},{(const BYTE*)p,(const BYTE*)p+bytes}});return true;
}
static void Reset(){unpack={4,0,0,0,0,0};error=0;capacity=65536;identity=0;attempts=0;fail_at=0;ready=true;records.clear();}
#include "icd-images.inc"
'''
    source+='\nint main(){\n'+body+'\n}\n'
    with tempfile.TemporaryDirectory(prefix='dreamgpu-icd-images-') as temp:
        p=Path(temp);(p/'test.cpp').write_text(source)
        subprocess.run([os.environ.get('CXX','c++'),'-std=c++23','-O1','-Wall','-Wextra','-Werror','-fno-exceptions','-fno-rtti','-fsanitize=address,undefined','-g','-I'+str(ROOT/'guest/opengl'),'-I'+str(ROOT/'guest/include'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
        subprocess.run([str(p/'test')],check=True)
if __name__=='__main__':
    run(r'''
 Reset();std::vector<BYTE> input(640*480*4);for(unsigned i=0;i<input.size();++i)input[i]=i*37;
 AliasDrawPixels(640,480,GL_RGBA,GL_UNSIGNED_BYTE,input.data());
 unsigned offset=0;assert(records.size()>1);std::vector<BYTE> joined;
 for(auto&r:records){assert(r.args[4]==input.size()&&r.args[5]==offset&&r.args[7]==1);offset+=r.payload.size();joined.insert(joined.end(),r.payload.begin(),r.payload.end());}
 assert(records.front().args[6]==1&&records.back().args[6]==2&&joined==input);
 Reset();capacity=19;unpack={1,0,0,0,0,1};BYTE bits[]={0x69,0x05,0x9a,0x03};
 AliasBitmap(11,2,-.25f,1.5f,-3.25f,.125f,bits);assert(records.size()==2&&records[0].payload.size()==19&&records[1].payload.size()==1);
 float floats[4];memcpy(floats,records[0].payload.data(),16);assert(floats[0]==-.25f&&floats[2]==-3.25f);
 assert(records[0].payload[16]==0x96&&records[0].payload[17]==0xa0&&records[0].payload[18]==0x59&&records[1].payload[0]==0xc0);
 assert(records[0].args[4]==4&&records[1].args[5]==3);
 Reset();AliasBitmap(0,100,0,0,.5f,-.25f,nullptr);assert(records.size()==1&&records[0].payload.size()==16&&records[0].args[4]==0&&records[0].args[6]==3);
 Reset();unpack={8,4,1,1,1,0};BYTE src[32]={};src[10]=0x12;src[11]=0x34;src[12]=0xab;src[13]=0xcd;
 AliasDrawPixels(2,1,GL_RED,GL_UNSIGNED_SHORT,src);assert(records[0].payload==std::vector<BYTE>({0x34,0x12,0xcd,0xab}));
 Reset();AliasDrawPixels(8192,8193,GL_STENCIL_INDEX,GL_BITMAP,(void*)1);
 assert(error==GL_OUT_OF_MEMORY&&records.empty()&&!attempts&&!identity);
 Reset();AliasDrawPixels(8192,8193,GL_COLOR_INDEX,GL_BITMAP,(void*)1);
 assert(error==GL_OUT_OF_MEMORY&&records.empty()&&!attempts&&!identity);
 // The exact expanded limit is admitted; fail first queue to avoid a full draw.
 Reset();fail_at=1;std::vector<BYTE> packed(8192*8192/8);
 AliasDrawPixels(8192,8192,GL_STENCIL_INDEX,GL_BITMAP,packed.data());
 assert(attempts==1&&identity==1&&!error);
 Reset();AliasDrawPixels(0,INT_MAX,GL_STENCIL_INDEX,GL_BITMAP,nullptr);
 assert(records.size()==1&&records[0].args[4]==0&&records[0].args[6]==3);
 // glBitmap never expands indices, so larger pixel dimensions remain legal.
 Reset();fail_at=1;AliasBitmap(8192,8193,0,0,0,0,packed.data());
 assert(attempts==1&&identity==1&&!error);
 puts("PASS immutable image packer: multichunk RGBA, odd MSB/LSB bitmap, float metadata, zero-area movement, typed unpack");
''')
