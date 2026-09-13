#!/usr/bin/env python3
"""Compile actual startup helper with deterministic Win32 ownership/time shims."""
from pathlib import Path
import subprocess
import tempfile
here=Path(__file__).resolve().parent
source=r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
typedef uint32_t DWORD; typedef intptr_t HANDLE;
#define INVALID_HANDLE_VALUE ((HANDLE)-1)
#define GENERIC_READ 1
#define GENERIC_WRITE 2
#define OPEN_EXISTING 3
#define ERROR_ACCESS_DENIED 5
#define ERROR_SHARING_VIOLATION 32
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_PATH_NOT_FOUND 3
static DWORD now,err,attempts,sleeps,failures;
static DWORD GetTickCount(void){return now;}
static DWORD GetLastError(void){return err;}
static void SetLastError(DWORD value){err=value;}
static void Sleep(DWORD ms){assert(ms==250);now+=ms;sleeps++;}
static HANDLE CreateFileA(const char*p,DWORD access,DWORD share,void*sa,DWORD mode,DWORD flags,void*t)
{assert(p[0]=='C'&&access==3&&!share&&!sa&&mode==3&&!flags&&!t);attempts++;return attempts>failures?7:INVALID_HANDLE_VALUE;}
#include "serial-startup.h"
static void reset(DWORD error,DWORD fail,DWORD time){err=error;failures=fail;attempts=sleeps=0;now=time;}
int main(void){
 reset(0,0,0);assert(OpenStartupSerial()==7&&attempts==1&&!sleeps);
 reset(5,4,0);assert(OpenStartupSerial()==7&&attempts==5&&sleeps==4);
 reset(32,2,0xfffffff0u);assert(OpenStartupSerial()==7&&sleeps==2);
 reset(2,3,0);assert(OpenStartupSerial()==7&&attempts==4&&sleeps==3);
 reset(3,2,0xfffffff0u);assert(OpenStartupSerial()==7&&attempts==3&&sleeps==2);
 reset(2,1000,0);assert(OpenStartupSerial()==-1&&attempts==81&&sleeps==80&&err==2);
 reset(87,1000,0);assert(OpenStartupSerial()==-1&&attempts==1&&!sleeps&&err==87);
 reset(5,1000,0xfffffff0u);assert(OpenStartupSerial()==-1&&attempts==81&&sleeps==80&&err==5);
}
'''
with tempfile.TemporaryDirectory()as d:
 p=Path(d);(p/'test.c').write_text(source)
 subprocess.run(['cc','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(here),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS actual startup retry: immediate success/failure, transient ownership, wraparound, fixed deadline')
