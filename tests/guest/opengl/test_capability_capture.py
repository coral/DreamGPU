#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run the real GL/WGL metadata capture against bounded/failing public API fakes."""
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
source = (ROOT / 'tools/opengl/capability.cpp').read_text()
core = source[source.index('static void ModulePath('):source.index('extern "C" void WINAPI WinMainCRTStartup')]
PREAMBLE = r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "capability-json.h"
using DWORD=uint32_t;using BYTE=uint8_t;using BOOL=int;
using HMODULE=void *;using HWND=void *;using HDC=void *;using HGLRC=void *;
using GLenum=unsigned;using GLint=int;using GLubyte=unsigned char;
#define WINAPI
#define APIENTRY
#define MAX_PATH 260
#define PFD_DRAW_TO_WINDOW 4
#define PFD_SUPPORT_OPENGL 32
#define PFD_DOUBLEBUFFER 1
#define PFD_TYPE_RGBA 0
struct PIXELFORMATDESCRIPTOR {
 uint16_t nSize,nVersion;uint32_t dwFlags;uint8_t iPixelType,cColorBits,cRedBits,cRedShift,
 cGreenBits,cGreenShift,cBlueBits,cBlueShift,cAlphaBits,cAlphaShift,cAccumBits,cAccumRedBits,
 cAccumGreenBits,cAccumBlueBits,cAccumAlphaBits,cDepthBits,cStencilBits,cAuxBuffers,iLayerType,
 bReserved;uint32_t dwLayerMask,dwVisibleMask,dwDamageMask;
};
static unsigned scenario,described,created,bound,unbound,destroyed,released;
static DWORD GetModuleFileNameA(HMODULE,char *out,DWORD size) {
 const char path[]="C:\\WINDOWS\\SYSTEM\\provider.dll";assert(size>sizeof(path));
 std::memcpy(out,path,sizeof(path));return sizeof(path)-1;
}
static HMODULE GetModuleHandleA(const char *name) {
 if((scenario==11&&!strcmp(name,"dgpugl.dll")) ||
    (scenario==12&&!strcmp(name,"dgpuicd.dll")))return nullptr;
 return reinterpret_cast<void *>(1);
}
static DWORD GetVersion() { return 0x80000a04; }
namespace system_loader { static bool module(HMODULE value,const char *name) {
 return value&&scenario!=6&&!(scenario==13&&!strcmp(name,"dgpugl.dll"));
} }
static HDC GetDC(HWND) { return scenario==9?nullptr:reinterpret_cast<void *>(2); }
static BOOL ReleaseDC(HWND,HDC) { ++released;return 1; }
static int ChoosePixelFormat(HDC,const PIXELFORMATDESCRIPTOR *) { return 1; }
static int DescribePixelFormat(HDC,int index,unsigned size,PIXELFORMATDESCRIPTOR *out) {
 if(!out) { assert(!size&&index==1);return scenario==7?257:2; }
 ++described;assert(size==sizeof(*out));
 out->nSize=sizeof(*out);out->nVersion=1;out->dwFlags=37;out->cColorBits=24;
 out->cDepthBits=24;out->cStencilBits=index==1?8:0;return 2;
}
static BOOL SetPixelFormat(HDC,int,const PIXELFORMATDESCRIPTOR *) { return 1; }
static HGLRC FakeCreate(HDC) { ++created;return reinterpret_cast<void *>(3); }
static BOOL FakeBind(HDC dc,HGLRC) { if(dc){++bound;return scenario!=5;}++unbound;return 1; }
static BOOL FakeDelete(HGLRC) { ++destroyed;return scenario!=10; }
static const GLubyte *FakeString(GLenum key) {
 static GLubyte huge[16385];
 if(scenario==1&&key==GL_EXTENSIONS){memset(huge,'x',sizeof(huge));return huge;}
 if(scenario==2&&key==GL_VENDOR)return nullptr;
 if(scenario==4&&key==GL_EXTENSIONS)return reinterpret_cast<const GLubyte *>("");
 return reinterpret_cast<const GLubyte *>(key==GL_VENDOR?"DreamGPU":"value");
}
static void FakeInteger(GLenum key,GLint *out) {out[0]=16;if(key==GL_MAX_VIEWPORT_DIMS)out[1]=32;}
static GLenum FakeError() {return scenario==3?0x500:GL_NO_ERROR;}
template<class T,class U> static T pointer(U value) {
 static_assert(sizeof(T)==sizeof(U));T result;std::memcpy(&result,&value,sizeof(result));return result;
}
template<class T> static T Entry(HMODULE,const char *name) {
 if(scenario==8)return nullptr;
 if(!strcmp(name,"wglCreateContext"))return pointer<T>(&FakeCreate);
 if(!strcmp(name,"wglMakeCurrent"))return pointer<T>(&FakeBind);
 if(!strcmp(name,"wglDeleteContext"))return pointer<T>(&FakeDelete);
 if(!strcmp(name,"glGetString"))return pointer<T>(&FakeString);
 if(!strcmp(name,"glGetIntegerv"))return pointer<T>(&FakeInteger);
 assert(!strcmp(name,"glGetError"));return pointer<T>(&FakeError);
}
'''
MAIN = r'''
static bool sink(void *context,const char *bytes,unsigned count) {
 static_cast<std::string *>(context)->append(bytes,count);return true;
}
int main(int argc,char **argv) {
 assert(argc==2);scenario=std::atoi(argv[1]);std::string output;CapabilityJson json(sink,&output);
 json.text("{\"schema\":1,\"kind\":\"dreamgpu.opengl.capabilities\","
           "\"evidence\":\"reported_capabilities_only\",\"execution_validation\":false");
 bool complete=Capture(reinterpret_cast<void *>(1),reinterpret_cast<void *>(2),json);
 json.text(",\"complete\":");json.text(complete?"true":"false");json.text("}");assert(json.flush());
 assert(complete==(scenario==0||scenario==4||scenario==11));
 assert(released==(scenario==8||scenario==9?0:1));
 assert(created==destroyed);assert(bound==created);
 assert(unbound==(created&&scenario!=5?1:0));
 fwrite(output.data(),1,output.size(),stdout);
}
'''
# Constants come from the pinned SDK, while platform calls alone are mocked.
text = (ROOT / 'vendor/reactos/sdk/include/GL/gl.h').read_text()
constants = ''
for name in sorted(set(re.findall(r'\bGL_[A-Z0-9_]+', core + PREAMBLE))):
    constants += re.search(r'^\s*#define\s+' + name + r'\s+[^\n]+', text, re.M).group(0) + '\n'
with tempfile.TemporaryDirectory(prefix='dreamgpu-gl-capability-') as directory:
    work = Path(directory)
    # GL constants must precede fake functions using them.
    (work / 'test.cpp').write_text(constants + PREAMBLE + core + MAIN)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I', str(ROOT / 'tools/d3d'),
                    str(work / 'test.cpp'), '-o', str(work / 'test')], check=True)
    for scenario in range(14):
        result = subprocess.run([str(work / 'test'), str(scenario)], check=True, capture_output=True)
        captured = json.loads(result.stdout)
        assert captured['complete'] == (scenario in (0, 4, 11))
        if scenario == 0:
            assert captured['pixel_format_count'] == 2 and captured['provider_identity_verified']
            assert len(captured['pixel_formats']) == 2
            assert len(captured['pixel_formats'][0]['descriptor_bytes']) == 40
            assert captured['vendor'] == 'DreamGPU' and captured['context_created']
            assert len(captured['limits']) == 13
            assert captured['limits'][1]['values'] == [16, 32]
        if scenario == 11:
            assert captured['frontend_module'] is None
            assert captured['icd_embeds_frontend'] and captured['provider_identity_verified']
        if scenario in (12, 13):
            assert not captured['provider_identity_verified']
        if scenario == 1:
            assert len(captured['extensions']) == 16384
            assert captured['string_queries'][3]['complete'] is False
        if scenario == 3:
            assert all(query['error'] == '0x00000500' for query in captured['string_queries'])
            assert all(limit['error'] == '0x00000500' for limit in captured['limits'])
        if scenario == 7:
            assert captured['pixel_format_count'] == 257 and len(captured['pixel_formats']) == 256
            assert not captured['pixel_formats_complete']
print('PASS actual GL/WGL capability capture: public queries, descriptors, context cleanup, '
      'query/provider failures and bounded-string/pixel-format truncation (ASan/UBSan)')
