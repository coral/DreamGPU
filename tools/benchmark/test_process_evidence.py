#!/usr/bin/env python3
"""Actual failure-only process helper with hostile/bounded Toolhelp oracle."""
from pathlib import Path
import subprocess,tempfile
HERE=Path(__file__).resolve().parent
SOURCE=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef uint32_t DWORD;typedef int HANDLE;typedef int BOOL;
#define INVALID_HANDLE_VALUE -1
#define TH32CS_SNAPMODULE 8
struct Module {DWORD dwSize;char szModule[256];};typedef struct Module MODULEENTRY32;
static unsigned mode,count,closed;static DWORD clock;
static DWORD GetTickCount(void){return clock;}
static DWORD GetLastError(void){return 5;}
static BOOL GetExitCodeProcess(HANDLE p,DWORD *value){assert(p==99);*value=259;return mode!=3;}
static HANDLE CreateToolhelp32Snapshot(DWORD flags,DWORD pid){assert(flags==8&&pid==42);return mode==1?-1:55;}
static BOOL Module32First(HANDLE s,MODULEENTRY32 *e){assert(s==55&&e->dwSize==sizeof(*e));strcpy(e->szModule,"HL.EXE");return 1;}
static BOOL Module32Next(HANDLE s,MODULEENTRY32 *e){
 assert(s==55);++count;
 if(mode==2){clock+=100;strcpy(e->szModule,"DGPUGL.DLL");return 1;}
 if(mode==4){memset(e->szModule,'X',sizeof(e->szModule));return 1;}
 strcpy(e->szModule,count==1?"private-unrelated.dll":count==2?"HW.DLL":"dgpugl.dll");return count<4;
}
static BOOL CloseHandle(HANDLE s){assert(s==55);++closed;return 1;}
#include "process-evidence.h"
int main(void){char text[4096];DWORD bytes;
 for(mode=0;mode<5;++mode){memset(text,0,sizeof(text));strcpy(text,"prior");bytes=5;count=closed=clock=0;
  ProcessEvidence(42,99,text,sizeof(text)-1,&bytes);text[bytes]=0;assert(!strncmp(text,"prior",5));
  assert(strstr(text,"OWNED_PROCESS exit="));assert(!strstr(text,"private-unrelated"));
  assert(closed==(mode!=1));
  if(mode==0)assert(strstr(text,"OWNED_MODULE hl.exe")&&strstr(text,"OWNED_MODULE hw.dll")&&strstr(text,"OWNED_MODULE dgpugl.dll"));
  if(mode==1)assert(strstr(text,"snapshot-error=0x00000005"));
  if(mode==2)assert(count==5&&clock==500&&strstr(text,"truncated"));
  if(mode==3)assert(strstr(text,"query-error:0x00000005"));
  if(mode==4)assert(count==128&&strstr(text,"truncated"));
 }
 mode=0;memset(text,'z',sizeof(text));bytes=3;ProcessEvidence(42,99,text,4,&bytes);assert(bytes==4&&text[4]=='z');
 puts("PASS actual process evidence: owned PID, fixed module allowlist, bounded time/count/capacity, close and failures");return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    p=Path(directory);(p/'tlhelp32.h').write_text('');(p/'test.c').write_text(SOURCE)
    subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-g','-I'+str(HERE),'-I'+str(p),str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
