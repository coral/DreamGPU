#!/usr/bin/env python3
"""Actual pixel state wrappers: typed bits, bounds, and failure-atomic output."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[3]
api=(ROOT/'crates/dreamgpu-host/src/gl_api.rs').read_text()
constants={name:value for name,value in re.findall(r'pub const (GL_\w+): [^=]+ = (\d+);',api)}
needed={'GL_PIXEL_MAP_R_TO_R','GL_PIXEL_MAP_S_TO_S'} | set(re.findall(r'\bGL_[A-Z0-9_]+\b',(ROOT/'guest/opengl/icd-pixels.inc').read_text()))
assert not needed-constants.keys(),needed-constants.keys()
source=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#define APIENTRY
using ULONG=uint32_t;using GLenum=uint32_t;using GLint=int32_t;using GLsizei=int32_t;
using GLfloat=float;using GLuint=uint32_t;using GLushort=uint16_t;
constexpr unsigned DG_GL_RESULT_INT=2,DG_GL_RESULT_FLOAT=3;
#include "gl-funcs.h"
'''
source+='\n'.join(f'constexpr unsigned {name}={constants[name]};' for name in sorted(needed))
source+=r'''
static bool ready=true,query_ok=true,bad_bytes=false,bad_integer=false,bad_short=false;
static unsigned error=0,calls=0,queries=0,function=0,size=0,words[5],maximum=256,current_size=4;
static unsigned char payload[1024];
static bool JglReady(){if(!ready)error=GL_INVALID_OPERATION;return ready;}
static void JglSetError(unsigned e){error=e;}
static void JglScalarVector(unsigned f,unsigned n,const void*p){assert(n==2||n==5);++calls;function=f;memcpy(words,p,n*4);}
static bool JglData(unsigned f,const ULONG*a,unsigned n,const void*p,unsigned bytes){
 assert(n==2&&bytes<=sizeof(payload));++calls;function=f;size=bytes;memcpy(words,a,8);memcpy(payload,p,bytes);return true;}
static bool JglQuery(unsigned f,const ULONG*a,unsigned kind,void*out,unsigned cap,ULONG*bytes){
 ++queries;if(!query_ok)return false;
 if(f==FEnum_glGetIntegerv){assert(kind==DG_GL_RESULT_INT&&cap==4);ULONG value=a[0]==GL_MAX_PIXEL_MAP_TABLE?maximum:current_size;memcpy(out,&value,4);*bytes=bad_integer?3:4;return true;}
 assert(a[1]==current_size&&cap==current_size*4&&a[2]==0);
 for(unsigned i=0;i<current_size;++i){ULONG value=0;
  if(f==FEnum_glGetPixelMapfv){assert(kind==DG_GL_RESULT_FLOAT);float x=float(i)*.25f;memcpy(&value,&x,4);}
  else if(f==FEnum_glGetPixelMapuiv)value=0xffffffffu-i;
  else {assert(f==FEnum_glGetPixelMapusv);value=bad_short&&i==current_size-1?65536:65535-i;}
  memcpy((char*)out+i*4,&value,4);
 }
 *bytes=bad_bytes?cap-4:cap;return true;
}
#include "icd-pixels.inc"
int main(){
 AliasCopyPixels(-7,-9,64,32,GL_COLOR);assert(function==FEnum_glCopyPixels&&words[0]==(ULONG)-7&&words[1]==(ULONG)-9&&words[4]==GL_COLOR);
 unsigned initial=calls;AliasCopyPixels(0,0,-1,8,GL_COLOR);assert(calls==initial&&error==GL_INVALID_VALUE);
 AliasCopyPixels(0,0,1,1,0xdead);assert(calls==initial&&error==GL_INVALID_ENUM);
 AliasCopyPixels(0,0,0,32,GL_DEPTH);assert(calls==initial);
 AliasCopyPixels((-2147483647-1),2147483647,1,1,GL_STENCIL);assert(words[0]==0x80000000&&words[1]==2147483647);
 AliasPixelZoom(-0.0f,2.5f);assert(function==FEnum_glPixelZoom&&words[0]==0x80000000);
 AliasPixelTransferf(GL_RED_SCALE,-0.25f);float f;memcpy(&f,words+1,4);assert(f==-.25f&&words[0]==GL_RED_SCALE);
 AliasPixelTransferi(GL_INDEX_OFFSET,(-2147483647-1));assert(function==FEnum_glPixelTransferi&&words[1]==0x80000000);
 unsigned before=calls;AliasPixelTransferi(0xdead,1);assert(calls==before&&error==GL_INVALID_ENUM);
 GLfloat fv[]={-0.0f,.25f,.75f,1};AliasPixelMapfv(GL_PIXEL_MAP_R_TO_R,4,fv);assert(size==16&&!memcmp(fv,payload,16));
 GLuint uv[]={0xffffffff,0x80000000,0x01000001,0};AliasPixelMapuiv(GL_PIXEL_MAP_I_TO_I,4,uv);assert(size==16&&!memcmp(uv,payload,16));
 GLushort sv[]={0,255,32768,65535};AliasPixelMapusv(GL_PIXEL_MAP_S_TO_S,4,sv);assert(size==8&&!memcmp(sv,payload,8));
 before=calls;unsigned oldqueries=queries;
 AliasPixelMapfv(GL_PIXEL_MAP_I_TO_I,3,(GLfloat*)1);assert(calls==before&&queries==oldqueries&&error==GL_INVALID_VALUE);
 AliasPixelMapfv(GL_PIXEL_MAP_R_TO_R,257,(GLfloat*)1);assert(calls==before&&queries==oldqueries);
 AliasPixelMapfv(0xdead,4,(GLfloat*)1);assert(error==GL_INVALID_ENUM&&calls==before);
 maximum=32;AliasPixelMapuiv(GL_PIXEL_MAP_I_TO_I,64,(GLuint*)1);assert(error==GL_INVALID_VALUE&&calls==before);
 maximum=256;AliasPixelMapfv(GL_PIXEL_MAP_R_TO_R,3,fv);assert(size==12);
 GLuint big[256]={};AliasPixelMapuiv(GL_PIXEL_MAP_R_TO_R,256,big);assert(size==1024);
 struct{GLfloat values[4];unsigned tail;} floats{{-1,-1,-1,-1},0xa5a5a5a5};
 AliasGetPixelMapfv(GL_PIXEL_MAP_R_TO_R,floats.values);assert(floats.values[3]==.75f&&floats.tail==0xa5a5a5a5);
 struct{GLuint values[4];unsigned tail;} integers{{0,0,0,0},0xa5a5a5a5};
 AliasGetPixelMapuiv(GL_PIXEL_MAP_I_TO_I,integers.values);assert(integers.values[0]==0xffffffff&&integers.values[3]==0xfffffffc&&integers.tail==0xa5a5a5a5);
 struct{GLushort values[4];unsigned tail;} shorts{{7,7,7,7},0xa5a5a5a5};
 AliasGetPixelMapusv(GL_PIXEL_MAP_S_TO_S,shorts.values);assert(shorts.values[0]==65535&&shorts.values[3]==65532&&shorts.tail==0xa5a5a5a5);
 bad_short=true;memset(shorts.values,0x5a,sizeof(shorts.values));AliasGetPixelMapusv(GL_PIXEL_MAP_S_TO_S,shorts.values);
 assert(error==GL_INVALID_OPERATION&&shorts.values[0]==0x5a5a&&shorts.values[3]==0x5a5a);
 bad_short=false;query_ok=false;AliasGetPixelMapfv(GL_PIXEL_MAP_R_TO_R,floats.values);assert(floats.values[3]==.75f);
 query_ok=true;bad_bytes=true;for(float &v:floats.values)v=-1;
 AliasGetPixelMapfv(GL_PIXEL_MAP_R_TO_R,floats.values);assert(error==GL_INVALID_OPERATION&&floats.values[0]==-1&&floats.values[3]==-1);
 bad_bytes=false;bad_integer=true;AliasGetPixelMapfv(GL_PIXEL_MAP_R_TO_R,floats.values);assert(floats.values[0]==-1);bad_integer=false;
 bad_bytes=false;current_size=257;AliasGetPixelMapuiv(GL_PIXEL_MAP_I_TO_I,integers.values);assert(error==GL_INVALID_OPERATION&&integers.values[0]==0xffffffff);
 ready=false;before=calls;oldqueries=queries;AliasPixelMapfv(GL_PIXEL_MAP_R_TO_R,4,(GLfloat*)1);AliasGetPixelMapfv(GL_PIXEL_MAP_R_TO_R,(GLfloat*)1);
 assert(calls==before&&queries==oldqueries&&error==GL_INVALID_OPERATION);
}
'''
with tempfile.TemporaryDirectory(prefix='dreamgpu-icd-pixels-') as temporary:
    path=Path(temporary);(path/'test.cpp').write_text(source)
    subprocess.run([os.environ.get('CXX','clang++'),'-std=c++23','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined','-fno-omit-frame-pointer','-I',str(ROOT/'guest/opengl'),
                    '-I',str(ROOT/'guest/include'),str(path/'test.cpp'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
print('PASS pixel state9+CopyPixels: exact typed maps, bounded sizes/caps, Begin guards, atomic query output')
