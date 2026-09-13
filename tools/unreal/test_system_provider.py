#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual UT normal-loader admission and observed-module path policy."""
from pathlib import Path
import shutil
import subprocess
import tempfile
HERE = Path(__file__).resolve().parent
SOURCE = r'''
#include <cassert>
#include <cstring>
#include <string>
#include <map>
using BOOL=int; using UINT=unsigned; using DWORD=unsigned;
#define TRUE 1
#define FALSE 0
#define MAX_PATH 260
#define INVALID_FILE_ATTRIBUTES (~0u)
#define ERROR_FILE_NOT_FOUND 2
static unsigned error;
static std::string system_dir="C:\\WINNT\\system32";
static std::map<std::string,unsigned> files;
static BOOL Equal(const char*a,const char*b) {
 while(*a&&*b) {char x=*a++,y=*b++;if(x>='A'&&x<='Z')x+=32;if(y>='A'&&y<='Z')y+=32;if(x!=y)return FALSE;}return *a==*b;
}
static BOOL Join(char*out,const char*a,const char*b) {
 if(strlen(a)+strlen(b)+2>MAX_PATH)return FALSE;
 strcpy(out,a);strcat(out,"\\");strcat(out,b);return TRUE;
}
static UINT GetSystemDirectoryA(char*out,UINT n) {if(system_dir.size()>=n)return system_dir.size()+1;strcpy(out,system_dir.c_str());return system_dir.size();}
static DWORD GetFileAttributesA(const char*p) {auto i=files.find(p);if(i!=files.end()){error=i->second;return i->second?INVALID_FILE_ATTRIBUTES:0;}error=ERROR_FILE_NOT_FOUND;return INVALID_FILE_ATTRIBUTES;}
static unsigned GetLastError(){return error;}
#include "system-provider.h"
static UtSystemProviders valid(bool d3d) {
 UtSystemProviders s;assert(s.initialize());
 s.observe("DGPUGL.DLL","c:\\winnt\\SYSTEM32\\dgpugl.dll");
 if(d3d){s.observe("ddraw.dll","C:\\WINNT\\system32\\ddraw.dll");s.observe("wined3d.dll","C:\\WINNT\\system32\\wined3d.dll");}
 else s.observe("glide2x.dll","C:\\WINNT\\system32\\glide2x.dll");
 assert(s.complete(d3d));return s;
}
int main(){
 assert(UtCleanDirectory("C:\\UT99\\System"));
 for(const char *p:UtProviderNames){std::string f=std::string("C:\\UT99\\System\\")+p;files[f]=0;assert(!UtCleanDirectory("C:\\UT99\\System"));files[f]=5;assert(!UtCleanDirectory("C:\\UT99\\System"));files.clear();}
 for(bool d3d:{false,true}){
  auto s=valid(d3d);s.observe("engine.dll","C:\\UT99\\System\\Engine.dll");assert(s.complete(d3d));
  for(const char *name:UtProviderNames){s=valid(d3d);std::string p=std::string("C:\\UT99\\System\\")+name;s.observe(name,p.c_str());assert(!s.complete(d3d));}
  s=valid(d3d);s.observe("jrgopengl.dll","C:\\WINNT\\system32\\jrgopengl.dll");assert(!s.complete(d3d));
  s=valid(d3d);s.observe("dgddr.dll","C:\\WINNT\\system32\\dgddr.dll");assert(!s.complete(d3d));
  s=valid(d3d);s.observe("dgpugl.dll","C:\\WINNT\\system32\\dgpugl.dll");assert(!s.complete(d3d));
  s=valid(d3d);s.observe("opengl32.dll","C:\\WINNT\\system32-foreign\\opengl32.dll");assert(!s.complete(d3d));
  s=UtSystemProviders{};assert(s.initialize());assert(!s.complete(d3d));
 }
 system_dir="C:\\WINDOWS\\SYSTEM";auto s=UtSystemProviders{};assert(s.initialize());s.observe("glide2x.dll","C:\\WINDOWS\\SYSTEM\\glide2x.dll");s.observe("dgpugl.dll","C:\\WINDOWS\\SYSTEM\\dgpugl.dll");assert(s.complete(false));
 system_dir=std::string(260,'x');s=UtSystemProviders{};assert(!s.initialize());
}
'''
with tempfile.TemporaryDirectory(prefix='ut-system-provider-') as temporary:
    path=Path(temporary);source=path/'test.cpp';source.write_text(SOURCE)
    compiler=shutil.which('clang++') or 'c++'
    subprocess.run([compiler,'-std=c++23','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-g','-I',str(HERE),str(source),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True)
for source in ['setup.cpp','setup-d3d.cpp']:
    text=(HERE/source).read_text()
    assert 'Half-Life\\\\' not in text
assert 'UT99\\\\System\\\\D3DDrv.dll' in (HERE/'setup-d3d.cpp').read_text()
assert 'UTD3D\\\\D3DDrv.dll' not in (HERE/'setup-d3d.cpp').read_text()
print('PASS actual UT normal system provider admission and module identity')
