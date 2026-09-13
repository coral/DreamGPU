#!/usr/bin/env python3
"""Compile the exact runner modal collector against a bounded Win32 oracle."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

HERE=Path(__file__).resolve().parent

class OwnedModalTests(unittest.TestCase):
    def test_owned_static_only_timeout_and_log_preservation(self):
        source=(HERE/'runner.cpp').read_text()
        last_field=source.index('} MODAL_SCAN;')
        first=source.rindex('typedef struct {',0,last_field)
        last=source.index('static BOOL FixedAcknowledged(',first)
        implementation=source[first:last]
        shim=r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#define CALLBACK
#define TRUE 1
#define FALSE 0
#define WM_GETTEXT 13
#define SMTO_ABORTIFHUNG 2
#define SMTO_BLOCK 1
#define OUTPUT_MAX 65536
#define Equal(a,b) (!strcmp(a,b))
typedef uint32_t DWORD;typedef uintptr_t DWORD_PTR;typedef intptr_t LPARAM;
typedef int BOOL;typedef int HWND;typedef unsigned char BYTE;
static BYTE Output[OUTPUT_MAX];static DWORD OutputBytes,Clock,Calls,Foreign,EditReads;
static int Mode;
static const char Hex[]="0123456789abcdef";
static DWORD GetLastError(void){return 193;}
static char *Append(char *p,const char *text){while(*text)*p++=*text++;*p=0;return p;}
static DWORD GetTickCount(void){return Clock;}
static void GetWindowThreadProcessId(HWND w,DWORD *pid){*pid=w==1?99:42;}
static BOOL IsWindowVisible(HWND w){(void)w;return TRUE;}
static int GetClassNameA(HWND w,char *out,int size){const char *name=w<=2?"#32770":w==4?"Edit":"Static";assert((int)strlen(name)<size);strcpy(out,name);return strlen(name);}
static BOOL SendMessageTimeoutA(HWND w,int message,int capacity,LPARAM destination,int flags,int timeout,DWORD_PTR *result)
{
 char *out=(char *)destination;const char *text=w==2?"Loader error":"Missing engine.dll";
 (void)message;(void)flags;assert(capacity==512 && timeout==50);++Calls;
 if(w==1)++Foreign;if(w==4)++EditReads;
 if(Mode==2){Clock+=50;return FALSE;}
 strcpy(out,text);*result=strlen(out);return TRUE;
}
static BOOL EnumChildWindows(HWND w,BOOL (*callback)(HWND,LPARAM),LPARAM p)
{int i;(void)w;for(i=3;i<(Mode==2?100:6);i++)if(!callback(i,p))break;return TRUE;}
static BOOL EnumWindows(BOOL (*callback)(HWND,LPARAM),LPARAM p)
{callback(1,p);if(Mode!=3)callback(2,p);return TRUE;}
'''
        checks=r'''
int main(void)
{
 strcpy((char *)Output,"existing log");OutputBytes=strlen((char *)Output);
 CaptureOwnedModal(42);Output[OutputBytes]=0;
 assert(strstr((char *)Output,"OWNED_WINDOW title: Loader error"));
 assert(strstr((char *)Output,"OWNED_MODAL static: Missing engine.dll"));
 assert(!Foreign && !EditReads && Calls==3);
 Mode=2;Clock=Calls=0;OutputBytes=0;CaptureOwnedModal(42);
 assert(Calls==10 && Clock==500 && OutputBytes>0);
 Mode=3;Clock=Calls=0;memset(Output,'z',sizeof(Output));OutputBytes=OUTPUT_MAX;
 CaptureOwnedModal(42);assert(OutputBytes==OUTPUT_MAX && Output[OUTPUT_MAX-1]=='z');
 assert(!strcmp(ProcessLaunchFailure("launch-failed"),"launch-failed"));
 assert(OutputBytes==33 && !memcmp(Output,"CREATE_PROCESS_ERROR 0x000000c1\r\n",33));
 return 0;
}
'''
        with tempfile.TemporaryDirectory(prefix='dg-modal-') as directory:
            path=Path(directory);(path/'test.c').write_text(shim+implementation+checks)
            subprocess.run([os.environ.get('CC','cc'),'-std=c99','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',str(path/'test.c'),'-o',str(path/'test')],check=True)
            subprocess.run([str(path/'test')],check=True)

if __name__=='__main__':unittest.main()
