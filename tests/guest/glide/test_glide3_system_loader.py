#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual DGSYSG3 system-provider preflight (tools/glide/provider3.h): the
Glide 2 clean-launch policy plus preloaded or neighbouring glide3x.dll."""
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
#include <initializer_list>
using HMODULE=void*;using UINT=unsigned;using DWORD=unsigned;
constexpr unsigned MAX_PATH=260,INVALID_FILE_ATTRIBUTES=~0u;
constexpr unsigned ERROR_SUCCESS=0,ERROR_FILE_NOT_FOUND=2,ERROR_PATH_NOT_FOUND=3;
static const char *cwd="C:\\",*exe="C:\\DGSYSG3.EXE",*system_dir="C:\\WINDOWS\\SYSTEM";
static const char *glide3="C:\\WINDOWS\\SYSTEM\\glide3x.dll",*gl="C:\\WINDOWS\\SYSTEM\\dgpugl.dll";
static bool loaded_glide2,loaded_glide3,loaded_gl,local_glide2,local_glide3,local_gl,truncate;
static DWORD file_error=ERROR_FILE_NOT_FOUND,last_error=ERROR_FILE_NOT_FOUND;
static void Record(const char*){}
static unsigned Copy(const char*s,char*d,unsigned n){unsigned len=std::strlen(s);if(truncate||len>=n)return n;std::memcpy(d,s,len+1);return len;}
static UINT GetSystemDirectoryA(char*p,UINT n){return Copy(system_dir,p,n);}
static DWORD GetModuleFileNameA(HMODULE m,char*p,DWORD n){return Copy(!m?exe:m==(void*)3?glide3:gl,p,n);}
static DWORD GetCurrentDirectoryA(DWORD n,char*p){return Copy(cwd,p,n);}
static int lstrlenA(const char*s){return std::strlen(s);}
static char*lstrcpyA(char*d,const char*s){return std::strcpy(d,s);}
static int lstrcmpiA(const char*a,const char*b){return strcasecmp(a,b);}
static HMODULE GetModuleHandleA(const char*n){
 if(!std::strcmp(n,"glide2x.dll"))return loaded_glide2?(void*)2:nullptr;
 if(!std::strcmp(n,"glide3x.dll"))return loaded_glide3?(void*)3:nullptr;
 return loaded_gl?(void*)1:nullptr;
}
static DWORD GetFileAttributesA(const char*n){
 last_error=file_error;const char*base=std::strrchr(n,'\\');base=base?base+1:n;
 const bool present=!strcasecmp(base,"glide2x.dll")?local_glide2:!strcasecmp(base,"glide3x.dll")?local_glide3:
                    !strcasecmp(base,"dgpugl.dll")?local_gl:false;
 return present?0:INVALID_FILE_ATTRIBUTES;
}
static DWORD GetLastError(){return last_error;}
static void SetLastError(DWORD e){last_error=e;}
#include "provider3.h"
int main(){
 assert(CleanSystemGlide3Launch());
 cwd="C:\\WINDOWS\\DreamGPU\\tools\\glide";exe="C:\\WINDOWS\\DreamGPU\\tools\\glide\\DGSYSG3.EXE";
 assert(CleanSystemGlide3Launch());
 local_glide3=true;assert(!CleanSystemGlide3Launch());local_glide3=false;
 cwd="C:\\";exe="C:\\DGSYSG3.EXE";
 // Any preloaded provider, Glide 2 or 3, invalidates the normal-loader proof.
 for(bool *flag:{&loaded_glide2,&loaded_glide3,&loaded_gl,&local_glide2,&local_glide3,&local_gl}){
  *flag=true;assert(!CleanSystemGlide3Launch());*flag=false;
 }
 cwd="C:\\PRIVATE";assert(!CleanSystemGlide3Launch());cwd="C:\\";
 file_error=5;assert(!CleanSystemGlide3Launch());
 file_error=ERROR_PATH_NOT_FOUND;assert(CleanSystemGlide3Launch());file_error=ERROR_FILE_NOT_FOUND;
 assert(SystemModule((void*)3,"glide3x.dll"));assert(SystemModule((void*)1,"dgpugl.dll"));
 glide3="c:\\windows\\system\\GLIDE3X.DLL";assert(SystemModule((void*)3,"glide3x.dll"));
 glide3="C:\\GAMES\\Diablo II\\glide3x.dll";assert(!SystemModule((void*)3,"glide3x.dll"));
 assert(!SystemModule(nullptr,"glide3x.dll"));
 truncate=true;assert(!CleanSystemGlide3Launch());assert(!SystemModule((void*)3,"glide3x.dll"));
 std::puts("PASS actual system Glide 3 loader policy: clean launch, Glide 2/3 preloads, neighbouring glide3x.dll, dependency paths, bounded API failures");
}
'''
with tempfile.TemporaryDirectory(prefix='dg-glide3-loader-') as temporary:
    work = Path(temporary)
    (work / 'test.cpp').write_text(HARNESS)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++23', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I', str(ROOT / 'tools/glide'),
                    str(work / 'test.cpp'), '-o', str(work / 'test')], check=True)
    subprocess.run([str(work / 'test')], check=True)
