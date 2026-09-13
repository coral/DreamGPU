#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute the real post-summary evidence helper against bounded Win32 seams."""
from pathlib import Path
import subprocess
import tempfile
HERE = Path(__file__).resolve().parent
SOURCE = r'''
#include <cstdint>
#include <cstring>
#include <cassert>
#include <cstdio>
using DWORD=uint32_t;using BOOL=int;using HANDLE=int;
#define TRUE 1
#define FALSE 0
#define MAX_PATH 260
#define INVALID_HANDLE_VALUE -1
#define TH32CS_SNAPMODULE 8
#define STILL_ACTIVE 259
#define ERROR_NO_MORE_FILES 18
#define GENERIC_READ 1
#define FILE_SHARE_READ 2
#define FILE_SHARE_WRITE 4
#define OPEN_EXISTING 3
#define FILE_ATTRIBUTE_NORMAL 0
struct MODULEENTRY32 {DWORD dwSize;char szModule[256];char szExePath[260];};
static unsigned mode,index_,closed,queries;static DWORD clock_,last_error;
static const char *names[]={"hl.exe","hw.dll","opengl32.dll","dgpuicd.dll","dgpugl.dll"};
static const char *gl="GL_VENDOR: DreamGPU\r\nGL_RENDERER: DreamGPU (native host OpenGL)\nGL_VERSION: 1.1 DreamGPU\n";
static DWORD GetSystemDirectoryA(char *s,DWORD n){assert(n==260);strcpy(s,"C:\\WINNT\\system32");return mode==9?260:18;}
static BOOL GetExitCodeProcess(HANDLE h,DWORD *v){assert(h==99);++queries;*v=mode==1||(mode==14&&queries==2)?0:259;return 1;}
static DWORD GetTickCount(){return clock_;}
static DWORD GetLastError(){return last_error;}
static HANDLE CreateToolhelp32Snapshot(DWORD f,DWORD p){assert(f==8&&p==42);return mode==2?-1:55;}
static BOOL fill(MODULEENTRY32 *e){
 if(index_==5&&mode!=7){last_error=mode==8?5:18;return 0;}
 unsigned i=index_%5;strcpy(e->szModule,names[i]);
 snprintf(e->szExePath,sizeof(e->szExePath),"%s\\%s",i<2?"C:\\SIERRA\\Half-Life":"C:\\WINNT\\system32",names[i]);
 if(mode==3&&i==3)strcpy(e->szExePath,"C:\\SIERRA\\Half-Life\\dgpuicd.dll");
 if(mode==4&&i==4)strcpy(e->szModule,"unrelated.dll");
 if(mode==5&&i==3)memset(e->szExePath,'x',sizeof(e->szExePath));
 if(mode==6&&i==4)strcpy(e->szModule,"sw.dll");
 if(mode==7)strcpy(e->szModule,"unrelated.dll");
 if(mode==10)clock_+=125;
 if(mode==13&&i==4){strcpy(e->szModule,"dgpuicd.dll");strcpy(e->szExePath,"C:\\WINNT\\system32\\dgpuicd.dll");}
 return 1;
}
static BOOL Module32First(HANDLE h,MODULEENTRY32 *e){assert(h==55&&e->dwSize==sizeof(*e));index_=0;return fill(e);}
static BOOL Module32Next(HANDLE h,MODULEENTRY32 *e){assert(h==55);++index_;return fill(e);}
static BOOL CloseHandle(HANDLE h){assert(h==55||h==66);++closed;return mode!=11;}
static HANDLE CreateFileA(const char *,DWORD access,DWORD share,void *,DWORD create,DWORD flags,void *){
 assert(access==GENERIC_READ&&share==6&&create==3&&flags==0);return mode==12?-1:66;
}
static BOOL ReadFile(HANDLE h,void *p,DWORD n,DWORD *read,void *){assert(h==66);*read=(DWORD)strlen(gl);assert(n>=*read);memcpy(p,gl,*read);return 1;}
#include "renderer-evidence.h"
int main(){
 char out[4096],scratch[32768];DWORD used;
 for(mode=0;mode<=14;++mode){used=0;closed=queries=clock_=last_error=0;
  BOOL ok=DgRendererModules(42,99,"C:\\SIERRA\\Half-Life",out,sizeof(out),&used);
  assert(ok==(mode==0||mode==12));
  assert(closed==((mode==1||mode==2||mode==9)?0:1));
  if(mode==7)assert(index_==128);
 }
 mode=0;used=0;assert(DgRendererLog("fixed",scratch,sizeof(scratch),out,sizeof(out),&used));
 out[used]=0;assert(strstr(out,"ENGINE_GL_VENDOR: DreamGPU\r\n"));
 const char *bad[]={"GL_VENDOR: Microsoft Corporation\nGL_RENDERER: GDI Generic\nGL_VERSION: 1.1\n",
 "GL_VENDOR: DreamGPU\n", "GL_VENDOR: DreamGPU\nGL_VENDOR: DreamGPU\n",
 "GL_VENDOR: DreamGPU\nGL_RENDERER: DreamGPU (native host OpenGL)\nGL_VERSION: 1.1",
 "GL_VENDOR: DreamGPU\nGL_RENDERER: DreamGPU (native host OpenGL)\nGL_VERSION: bad\001data\n"};
 for(auto text:bad){used=0;assert(!DgRendererConsole(text,(DWORD)strlen(text),out,sizeof(out),&used));}
 used=0;memset(out,'z',sizeof(out));assert(!DgRendererConsole(gl,(DWORD)strlen(gl),out,3,&used));assert(used==3&&out[3]=='z');
 BOOL observed=TRUE;used=0;
 assert(!DgRendererConsole("379 frames 5 seconds 70 fps\n",27,out,sizeof(out),&used,&observed)&&!observed);
 observed=FALSE;used=0;assert(!DgRendererConsole(bad[0],(DWORD)strlen(bad[0]),out,sizeof(out),&used,&observed)&&observed);
 mode=12;used=0;assert(!DgRendererLog("fixed",scratch,sizeof(scratch),out,sizeof(out),&used));
 puts("PASS renderer proof: exact system/game paths, missing/foreign/duplicate/unterminated modules, exit/enumeration/deadline/capacity failures, real GL strings, read-only log");
}
'''
with tempfile.TemporaryDirectory() as directory:
    p = Path(directory)
    (p / 'tlhelp32.h').write_text('')
    (p / 'test.cpp').write_text(SOURCE)
    subprocess.run(['c++', '-std=c++23', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-g', '-I'+str(HERE), '-I'+str(p),
                    str(p / 'test.cpp'), '-o', str(p / 'test')], check=True)
    subprocess.run([str(p / 'test')], check=True)
