#!/usr/bin/env python3
"""Actual texture aliases: native result handling and immutable request packing."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[3]
constants=dict(re.findall(r'pub const (GL_\w+): [^=]+ = (\d+);',(ROOT/'crates/dreamgpu-host/src/gl_api.rs').read_text()))
source=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
#include <cstdio>
#define APIENTRY
#define CopyMemory memcpy
using ULONG=uint32_t;using ULONG_PTR=uintptr_t;using BYTE=uint8_t;using GLenum=uint32_t;
using GLint=int32_t;using GLsizei=int32_t;using GLuint=uint32_t;using GLboolean=uint8_t;using GLclampf=float;
#include "gl.h"
#include "gl-funcs.h"
'''
source+='\n'.join(f'#define {k} {v}' for k,v in constants.items())
source+=r'''
struct JGL_ARRAY_STATE{BYTE Scratch[65536];};static JGL_ARRAY_STATE arrays;
static ULONG error,capacity=65536,calls,queries,fail,malformed,fn,scalars[8];static bool ready=true,all=true,valid=true; static GLint extent=10,border=1;
static std::vector<ULONG> last;static std::vector<BYTE> packed;
static bool JglReady(){if(!ready)error=GL_INVALID_OPERATION;return ready;}
static void JglSetError(ULONG e){error=e;}
static GLboolean glIsTexture(GLuint n){return n>0&&n<100;}
static JGL_ARRAY_STATE* JglArrays(){return &arrays;}
static ULONG JglMaxDataBytes(ULONG n){assert(n==1);return capacity;}
static void JglScalarVector(ULONG f,ULONG n,const void*p){fn=f;memcpy(scalars,p,n*4);++calls;}
static bool JglData(ULONG f,const ULONG*a,ULONG n,const void*p,ULONG bytes){
 assert(f==FEnum_glPrioritizeTextures&&n==1&&bytes==a[0]*8);++calls;
 if(calls==fail)return false;packed.insert(packed.end(),(const BYTE*)p,(const BYTE*)p+bytes);return true;}
static bool JglQuery(ULONG f,const ULONG*a,ULONG kind,void*out,ULONG cap,ULONG*bytes){
 if(f==FEnum_glGetTexLevelParameteriv){
  assert(kind==DG_GL_RESULT_INT&&cap==4);++queries;
  if(queries==fail)return false;
  GLint value=a[2]==GL_TEXTURE_WIDTH?extent:border;memcpy(out,&value,4);*bytes=queries==malformed?3:4;return true;
 }
 assert(f==FEnum_glAreTexturesResident&&kind==DG_GL_RESULT_BOOL&&cap==5);++queries;
 last.assign(a,a+3);if(queries==fail)return false;
 BYTE result[]={BYTE(valid),BYTE(all),1,1,1};
 if(!all)for(int i=0;i<3;++i)result[2+i]=a[i]!=2;
 if(queries==malformed)result[4]=2;
 memcpy(out,result,5);*bytes=5;return true;
}
#include "icd-textures.inc"
static void Reset(){error=calls=queries=fail=malformed=fn=0;capacity=65536;extent=10;border=1;ready=all=valid=true;last.clear();packed.clear();}
int main(){
 Reset();GLuint names[]={1,2,3,4};BYTE output[]={9,9,9,9};
 assert(AliasAreTexturesResident(4,names,output)&&queries==2&&output[0]==9&&output[3]==9);
 assert(last==std::vector<ULONG>({4,4,4}));
 Reset();all=false;assert(!AliasAreTexturesResident(4,names,output));assert(!error&&output[0]==1&&output[1]==0&&output[2]==1&&output[3]==1);
 Reset();memset(output,9,4);all=false;fail=2;assert(!AliasAreTexturesResident(4,names,output)&&output[0]==9&&output[3]==9);
 Reset();all=false;malformed=2;assert(!AliasAreTexturesResident(4,names,output)&&error==GL_INVALID_OPERATION&&output[0]==9);
 Reset();valid=false;assert(!AliasAreTexturesResident(4,names,output)&&error==GL_INVALID_VALUE&&output[0]==9);
 Reset();GLuint invalid[]={1,2,3,0};assert(!AliasAreTexturesResident(4,invalid,output)&&!queries&&error==GL_INVALID_VALUE);
 Reset();assert(AliasAreTexturesResident(0,nullptr,nullptr)&&!queries);
 assert(!AliasAreTexturesResident(-1,names,output)&&error==GL_INVALID_VALUE);
 Reset();assert(!AliasAreTexturesResident(4097,(GLuint*)1,(BYTE*)1)&&error==GL_OUT_OF_MEMORY&&!queries);
 Reset();assert(!AliasAreTexturesResident(2,(GLuint*)(~uintptr_t(0)-2),output)&&!queries&&error==GL_INVALID_VALUE);
 Reset();capacity=17;float priority[]={-.25f,.5f,1.5f,-0.0f};AliasPrioritizeTextures(4,names,priority);assert(calls==2&&packed.size()==32);
 for(unsigned i=0;i<4;++i){assert(!memcmp(packed.data()+i*8,names+i,4));assert(!memcmp(packed.data()+i*8+4,priority+i,4));}
 Reset();capacity=17;fail=1;AliasPrioritizeTextures(4,names,priority);assert(calls==1&&packed.empty());
 Reset();capacity=7;AliasPrioritizeTextures(4,names,priority);assert(error==GL_OUT_OF_MEMORY&&!calls);
 Reset();AliasCopyTexImage1D(GL_TEXTURE_1D,0,GL_RGBA,-7,-9,10,1);assert(calls==1&&fn==FEnum_glCopyTexImage1D&&scalars[3]==(ULONG)-7&&scalars[6]==1);
 AliasCopyTexSubImage1D(GL_TEXTURE_1D,0,-1,-4,-8,1);assert(calls==2&&fn==FEnum_glCopyTexSubImage1D&&scalars[2]==0xffffffff);
 Reset();AliasCopyTexImage1D(GL_TEXTURE_1D,0,GL_RGBA16,0,0,4,0);assert(calls==1&&scalars[2]==GL_RGBA16);
 AliasCopyTexImage1D(GL_TEXTURE_1D,0,GL_INTENSITY,0,0,4,0);assert(calls==2&&scalars[2]==GL_INTENSITY);
 Reset();AliasCopyTexImage1D(GL_TEXTURE_1D,0,4,0,0,4,0);assert(!calls&&error==GL_INVALID_VALUE);
 AliasCopyTexImage1D(GL_TEXTURE_1D,0,0x804e,0,0,4,0);assert(!calls&&error==GL_INVALID_VALUE);
 Reset();AliasCopyTexImage1D(GL_TEXTURE_1D,0,GL_RGBA,0,0,1,1);assert(!calls&&error==GL_INVALID_VALUE);
 AliasCopyTexImage1D(GL_TEXTURE_2D,0,GL_RGBA,0,0,1,0);assert(!calls&&error==GL_INVALID_ENUM);
 Reset();AliasCopyTexSubImage1D(GL_TEXTURE_1D,0,-2,0,0,1);assert(!calls&&error==GL_INVALID_VALUE);
 Reset();AliasCopyTexSubImage1D(GL_TEXTURE_1D,0,9,0,0,1);assert(!calls&&error==GL_INVALID_VALUE);
 Reset();AliasCopyTexSubImage1D(GL_TEXTURE_1D,0,(-2147483647-1),0,0,2147483647);assert(!calls&&error==GL_INVALID_VALUE);
 Reset();malformed=2;AliasCopyTexSubImage1D(GL_TEXTURE_1D,0,0,0,0,1);assert(!calls&&error==GL_INVALID_OPERATION);
 Reset();fail=1;AliasCopyTexSubImage1D(GL_TEXTURE_1D,0,0,0,0,1);assert(!calls&&queries==1);
 Reset();extent=border=0;AliasCopyTexSubImage1D(GL_TEXTURE_1D,0,0,0,0,0);assert(calls==1);
 Reset();ready=false;AliasPrioritizeTextures(4,(GLuint*)1,(float*)1);assert(!calls&&error==GL_INVALID_OPERATION);
 assert(!AliasAreTexturesResident(4,(GLuint*)1,(BYTE*)1)&&!queries);
 puts("PASS texture aliases: real residency replies, atomic output, priority typed chunks, 1D copy bounds/arguments");
}
'''
with tempfile.TemporaryDirectory(prefix='dreamgpu-icd-textures-') as temporary:
    p=Path(temporary);(p/'test.cpp').write_text(source)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++23','-O1','-Wall','-Wextra','-Werror','-fno-exceptions','-fno-rtti','-fsanitize=address,undefined','-I'+str(ROOT/'guest/opengl'),'-I'+str(ROOT/'guest/include'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
