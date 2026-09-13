#!/usr/bin/env python3
"""Actual system-provider policy with neighboring-DLL and path-failure cases."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
HARNESS = r'''
#include <cassert>
#include <cstring>
#include <strings.h>
#include <initializer_list>
#include <cstdio>
using HMODULE=void*;using UINT=unsigned;using DWORD=unsigned;
constexpr unsigned MAX_PATH=260,INVALID_FILE_ATTRIBUTES=~0u;
constexpr unsigned ERROR_SUCCESS=0,ERROR_FILE_NOT_FOUND=2,ERROR_PATH_NOT_FOUND=3;
static const char *cwd="C:\\",*exe="C:\\DGSYSGR.EXE",*system_dir="C:\\WINNT\\SYSTEM32";
static const char *glide="C:\\WINNT\\SYSTEM32\\glide2x.dll",*gl="C:\\WINNT\\SYSTEM32\\dgpugl.dll";
static bool loaded_glide,loaded_gl,local_glide,local_gl,truncate;
static DWORD file_error=ERROR_FILE_NOT_FOUND,last_error=ERROR_FILE_NOT_FOUND;
static void Record(const char*){}
static unsigned Copy(const char*s,char*d,unsigned n){unsigned len=std::strlen(s);if(truncate||len>=n)return n;std::memcpy(d,s,len+1);return len;}
static UINT GetSystemDirectoryA(char*p,UINT n){return Copy(system_dir,p,n);}
static DWORD GetModuleFileNameA(HMODULE m,char*p,DWORD n){return Copy(!m?exe:m==(void*)1?glide:gl,p,n);}
static DWORD GetCurrentDirectoryA(DWORD n,char*p){return Copy(cwd,p,n);}
static int lstrlenA(const char*s){return std::strlen(s);}
static char*lstrcpyA(char*d,const char*s){return std::strcpy(d,s);}
static int lstrcmpiA(const char*a,const char*b){return strcasecmp(a,b);}
static HMODULE GetModuleHandleA(const char*n){return (std::strcmp(n,"glide2x.dll")==0?loaded_glide:loaded_gl)?(void*)1:nullptr;}
static DWORD GetFileAttributesA(const char*n){last_error=file_error;const char*base=std::strrchr(n,'\\');base=base?base+1:n;return (!std::strcmp(base,"glide2x.dll")?local_glide:!std::strcmp(base,"dgpugl.dll")?local_gl:false)?0:INVALID_FILE_ATTRIBUTES;}
static DWORD GetLastError(){return last_error;}
static void SetLastError(DWORD e){last_error=e;}
#include "provider.h"
int main(){
 assert(CleanSystemLaunch());
 cwd="C:\\WINNT\\DreamGPU\\tools\\glide";exe="C:\\WINNT\\DreamGPU\\tools\\glide\\DGSYSGR.EXE";assert(CleanSystemLaunch());
 local_gl=true;assert(!CleanSystemLaunch());local_gl=false;
 cwd="c:\\winnt\\dreamgpu\\tools\\glide\\";assert(CleanSystemLaunch());
 cwd="C:\\CLEAN";assert(!CleanSystemLaunch());
 cwd="C:\\";exe="C:\\DGSYSGR.EXE";

 cwd="C:\\PRIVATE";assert(!CleanSystemLaunch());cwd="C:\\";
 exe="C:\\PRIVATE\\DGSYSGR.EXE";assert(!CleanSystemLaunch());exe="C:\\DGSYSGR.EXE";
 loaded_glide=true;assert(!CleanSystemLaunch());loaded_glide=false;
 loaded_gl=true;assert(!CleanSystemLaunch());loaded_gl=false;
 local_glide=true;assert(!CleanSystemLaunch());local_glide=false;
 local_gl=true;assert(!CleanSystemLaunch());local_gl=false;
 file_error=5;assert(!CleanSystemLaunch());file_error=ERROR_PATH_NOT_FOUND;assert(CleanSystemLaunch());
 assert(SystemModule((void*)1,"glide2x.dll"));assert(SystemModule((void*)2,"dgpugl.dll"));
 assert(!SystemModule(nullptr,"dgpugl.dll"));
 glide="c:\\winnt\\system32\\GLIDE2X.DLL";assert(SystemModule((void*)1,"glide2x.dll"));
 glide="C:\\glide2x.dll";assert(!SystemModule((void*)1,"glide2x.dll"));
 glide="C:\\WINNT\\SYSTEM32-other\\glide2x.dll";assert(!SystemModule((void*)1,"glide2x.dll"));
 gl="C:\\SIERRA\\Half-Life\\dgpugl.dll";assert(!SystemModule((void*)2,"dgpugl.dll"));
 truncate=true;assert(!CleanSystemLaunch());assert(!SystemModule((void*)1,"glide2x.dll"));
 std::puts("PASS actual system Glide loader policy: clean launch, preloads, neighbors, dependency paths, bounded API failures");
}
'''
with tempfile.TemporaryDirectory(prefix='dg-glide-loader-') as temporary:
    work = Path(temporary)
    (work / 'test.cpp').write_text(HARNESS)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++23', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I', str(ROOT / 'tools/glide'),
                    str(work / 'test.cpp'), '-o', str(work / 'test')], check=True)
    subprocess.run([str(work / 'test')], check=True)
