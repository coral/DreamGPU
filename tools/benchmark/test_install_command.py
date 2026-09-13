#!/usr/bin/env python3
"""Run the actual suspended fixed-installer command under ASan/UBSan."""
from pathlib import Path
import shutil, subprocess, tempfile
HERE=Path(__file__).resolve().parent
code=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int BOOL; typedef uint32_t DWORD; typedef unsigned UINT;
typedef unsigned char BYTE; typedef intptr_t HANDLE;
typedef struct { DWORD cb; } STARTUPINFOA;
typedef struct { HANDLE hProcess,hThread; } PROCESS_INFORMATION;
#define TRUE 1
#define FALSE 0
#define MAX_PATH 260
#define INVALID_HANDLE_VALUE -1
#define GENERIC_READ 1
#define FILE_SHARE_READ 1
#define OPEN_EXISTING 1
#define FILE_ATTRIBUTE_NORMAL 1
#define DRIVE_CDROM 5
#define CREATE_SUSPENDED 4
#define SEM_FAILCRITICALERRORS 1
#define SEM_NOOPENFILEERRORBOX 2
static unsigned media,corrupt,stage,created,terminated,closed;
static BOOL ack=TRUE,launch=TRUE,resume=TRUE;
static char chosen;
static char *Append(char *out,const char *text){while(*text)*out++=*text++;*out=0;return out;}
static UINT SetErrorMode(UINT mode){(void)mode;return 0;}
static DWORD GetDriveTypeA(const char *root){return media&(1u<<(root[0]-'D'))?DRIVE_CDROM:3;}
static HANDLE CreateFileA(const char *path,DWORD a,DWORD b,void*c,DWORD d,DWORD e,void*f){
 (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;
 assert(path[1]==':'&&path[2]=='\\'); return path[0];
}
static BOOL ReadFile(HANDLE file,void *out,DWORD bytes,DWORD *read,void *over){
 (void)file;(void)over;assert(bytes==2);memcpy(out,corrupt?"XX":"MZ",2);*read=2;return TRUE;
}
static BOOL CloseHandle(HANDLE file){(void)file;closed++;return TRUE;}
static BOOL CreateProcessA(char *exe,char *command,void*a,void*b,BOOL inherit,DWORD flags,void*env,const char*cwd,STARTUPINFOA*s,PROCESS_INFORMATION*p){
 (void)a;(void)b;(void)env;assert(!inherit&&flags==CREATE_SUSPENDED&&!strcmp(cwd,"C:\\"));
 assert(exe==command&&!strcmp(exe+1,":\\DGSETUP.EXE")&&s->cb==sizeof(*s));
 assert(stage==0);stage=1;created++;chosen=exe[0];p->hProcess=100;p->hThread=101;return launch;
}
static BOOL Reply(const char *kind,const char *id,const char*error,BOOL payload){
 assert(stage==1&&!strcmp(kind,"INSTALLING")&&!strcmp(id,"request")&&!error&&!payload);stage=2;return ack;
}
static DWORD ResumeThread(HANDLE thread){assert(stage==2&&thread==101);stage=3;return resume?1:(DWORD)-1;}
static BOOL TerminateProcess(HANDLE process,DWORD code){assert(process==100&&code==1);terminated++;return TRUE;}
static DWORD WaitForSingleObject(HANDLE process,DWORD timeout){assert(process==100&&timeout==3000);return 0;}
'''
code+=(HERE/'install-command.h').read_text()
code+=r'''
int main(void){
 media=0;assert(!strcmp(InstallPackage("request"),"install-media-unavailable")&&!created);
 media=3;assert(!strcmp(InstallPackage("request"),"install-ambiguous-media")&&!created);
 media=1;corrupt=1;assert(!strcmp(InstallPackage("request"),"install-media-unavailable")&&!created);
 corrupt=0;assert(!InstallPackage("request")&&stage==3&&chosen=='D'&&!terminated);
 stage=0;media=2;ack=FALSE;assert(!strcmp(InstallPackage("request"),"install-ack-failed")&&stage==2&&terminated==1);
 stage=0;ack=TRUE;resume=FALSE;assert(!strcmp(InstallPackage("request"),"install-resume-failed")&&terminated==2);
 stage=0;launch=FALSE;assert(!strcmp(InstallPackage("request"),"install-launch-failed")&&stage==1);
 puts("PASS fixed installer media, suspended launch/ack order and failure cleanup");
}
'''
with tempfile.TemporaryDirectory(prefix='dg-install-') as directory:
 source=Path(directory)/'test.c';binary=Path(directory)/'test';source.write_text(code)
 subprocess.run([shutil.which('clang') or 'cc','-O1','-g','-fsanitize=address,undefined','-Wall','-Wextra','-Werror',str(source),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
