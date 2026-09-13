#!/usr/bin/env python3
"""Actual D3D system-loader policy: public loading, module ownership and failures."""
# SPDX-License-Identifier: GPL-2.0-or-later
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
HARNESS = r'''
#include <cassert>
#include <cstring>
#include <strings.h>
#include <cstdio>
using DWORD=unsigned; using HMODULE=void*;
constexpr unsigned MAX_PATH=260,INVALID_FILE_ATTRIBUTES=~0u;
constexpr unsigned ERROR_SUCCESS=0,ERROR_FILE_NOT_FOUND=2,ERROR_PATH_NOT_FOUND=3;
struct MEMORY_BASIC_INFORMATION {void* AllocationBase;};
static const char *cwd="C:\\",*exe="C:\\PROBE.EXE",*system_dir="C:\\WINNT\\SYSTEM32";
static const char *api="C:\\WINNT\\SYSTEM32\\ddraw.dll";
static const char *provider="C:\\WINNT\\SYSTEM32\\winedd.dll";
static const char *renderer="C:\\WINNT\\SYSTEM32\\wined3d.dll";
static const char *gl="C:\\WINNT\\SYSTEM32\\dgpugl.dll";
static const char *neighbor=nullptr;
static bool truncate,query_fail,load_fail;
static unsigned file_error=2,last_error=2,freed,loads;
static void* mapped=(void*)2;
static unsigned copy(const char*s,char*d,unsigned n){unsigned len=std::strlen(s);if(truncate||len>=n)return n;std::memcpy(d,s,len+1);return len;}
static DWORD GetSystemDirectoryA(char*p,DWORD n){return copy(system_dir,p,n);}
static DWORD GetModuleFileNameA(HMODULE m,char*p,DWORD n){return copy(!m?exe:m==(void*)1?api:m==(void*)2?provider:m==(void*)3?renderer:gl,p,n);}
static DWORD GetCurrentDirectoryA(DWORD n,char*p){return copy(cwd,p,n);}
static int lstrlenA(const char*s){return std::strlen(s);}
static int lstrcmpiA(const char*a,const char*b){return strcasecmp(a,b);}
static HMODULE GetModuleHandleA(const char*n){return !std::strcmp(n,"wined3d.dll")?(void*)3:(void*)4;}
static DWORD GetFileAttributesA(const char*n){assert(!std::strstr(n,"\\\\"));last_error=file_error;return neighbor&&std::strstr(n,neighbor)?0:INVALID_FILE_ATTRIBUTES;}
static void SetLastError(DWORD n){last_error=n;}
static DWORD GetLastError(){return last_error;}
static HMODULE LoadLibraryA(const char*n){assert(!std::strcmp(n,"ddraw.dll"));++loads;return load_fail?nullptr:(void*)1;}
static bool FreeLibrary(HMODULE){++freed;return true;}
static unsigned VirtualQuery(const void*,MEMORY_BASIC_INFORMATION*m,unsigned n){if(query_fail)return 0;m->AllocationBase=mapped;return n;}
#include "system-loader.h"
int main(){
 using namespace system_loader;
 assert(load("ddraw.dll")== (void*)1 && loads==1);
 assert(object((void*)42,"winedd.dll"));
 cwd="C:\\WINNT\\DreamGPU\\tools\\d3d";exe="C:\\WINNT\\DreamGPU\\tools\\d3d\\DGSYS7.EXE";assert(clean());
 cwd="c:\\winnt\\dreamgpu\\tools\\d3d\\";assert(clean());
 neighbor="glide2x.dll";assert(!clean());neighbor=nullptr;
 cwd="C:\\OTHER";assert(!clean());
 cwd="C:\\";exe="C:\\PROBE.EXE";

 mapped=(void*)1;assert(!object((void*)42,"winedd.dll"));mapped=(void*)2;
 query_fail=true;assert(!object((void*)42,"winedd.dll"));query_fail=false;
 neighbor="ddraw.dll";assert(!load("ddraw.dll")&&loads==1);neighbor=nullptr;
 cwd="C:\\PRIVATE";neighbor="PRIVATE\\winedd.dll";assert(!clean());neighbor=nullptr;
 exe="C:\\OTHER\\PROBE.EXE";neighbor="OTHER\\dgpugl.dll";assert(!clean());neighbor=nullptr;
 exe="relative.exe";assert(!clean());exe="C:\\PROBE.EXE";cwd="C:\\";
 file_error=5;assert(!clean());file_error=ERROR_PATH_NOT_FOUND;assert(clean());file_error=2;
 api="C:\\PRIVATE\\ddraw.dll";assert(!load("ddraw.dll")&&freed==1);api="C:\\WINNT\\SYSTEM32\\ddraw.dll";
 provider="C:\\WINNT\\SYSTEM32-other\\winedd.dll";assert(!object((void*)42,"winedd.dll"));provider="c:\\winnt\\system32\\WINEDD.DLL";
 assert(object((void*)42,"winedd.dll"));
 gl="C:\\PRIVATE\\dgpugl.dll";assert(!object((void*)42,"winedd.dll"));gl="C:\\WINNT\\SYSTEM32\\dgpugl.dll";
 renderer="C:\\PRIVATE\\wined3d.dll";assert(!object((void*)42,"winedd.dll"));renderer="C:\\WINNT\\SYSTEM32\\wined3d.dll";
 truncate=true;assert(!clean());assert(!module((void*)1,"ddraw.dll"));truncate=false;
 load_fail=true;assert(!load("ddraw.dll"));assert(!module(nullptr,"ddraw.dll"));
 std::puts("PASS actual D3D system loader: clean directories, public factory provider mapping, dependency paths and API failures");
}
'''
with tempfile.TemporaryDirectory(prefix='dg-d3d-loader-') as directory:
    work = Path(directory)
    (work / 'test.cpp').write_text(HARNESS)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++23', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I', str(ROOT / 'tools/d3d'),
                    str(work / 'test.cpp'), '-o', str(work / 'test')], check=True)
    subprocess.run([str(work / 'test')], check=True)
