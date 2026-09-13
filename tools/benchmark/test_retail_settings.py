#!/usr/bin/env python3
"""Actual native-renderer preflight: existing key, typed settings and readback."""
from pathlib import Path
import subprocess
import tempfile
HERE=Path(__file__).resolve().parent
SOURCE=r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef uint32_t DWORD;typedef unsigned char BYTE;typedef int HKEY;
#define HKEY_CURRENT_USER 1
#define KEY_QUERY_VALUE 1
#define KEY_SET_VALUE 2
#define ERROR_SUCCESS 0
#define REG_DWORD 4
#define REG_SZ 1
#define FALSE 0
#define INVALID_FILE_ATTRIBUTES 0xffffffffU
#define FILE_ATTRIBUTE_DIRECTORY 16
static unsigned mode,writes,closed,copied;static DWORD engine;static char driver[32];
static DWORD GetFileAttributesA(const char *path){assert(!strcmp(path,"C:\\SIERRA\\Half-Life\\gldrv"));return mode==8?INVALID_FILE_ATTRIBUTES:FILE_ATTRIBUTE_DIRECTORY;}
static int CopyFileA(const char *source,const char *destination,int fail){
 assert(!strcmp(source,"C:\\SIERRA\\Half-Life\\dgpugl.dll"));
 assert(!strcmp(destination,"C:\\SIERRA\\Half-Life\\gldrv\\dgpugl.dll")&&!fail);
 ++copied;return mode!=9;
}
static int RegOpenKeyExA(HKEY root,const char *path,DWORD unused,DWORD access,HKEY *key){
 assert(root==1&&!strcmp(path,"Software\\Valve\\Half-Life\\Settings")&&!unused&&access==3);
 if(mode==1)return 2;*key=2;return 0;
}
static int RegCloseKey(HKEY key){assert(key==2);++closed;return 0;}
static int RegQueryValueExA(HKEY key,const char *name,void *reserved,DWORD *type,BYTE *out,DWORD *bytes){
 assert(key==2&&!reserved);
 if(!strcmp(name,"EngineType")){
  assert(*bytes==4);*type=REG_DWORD;*bytes=4;memcpy(out,&engine,4);
  if(mode==2&&!writes)*type=REG_SZ;
  if(mode==3&&!writes)*bytes=3;
  if(mode==5&&writes){DWORD wrong=1;memcpy(out,&wrong,4);}
 }else{
  assert(!strcmp(name,"EngineGLDriver"));assert(*bytes>=strlen(driver)+1);
  *type=REG_SZ;*bytes=(DWORD)strlen(driver)+1;memcpy(out,driver,*bytes);
  if(mode==6)out[0]='x';
  if(mode==7)--*bytes;
 }
 return 0;
}
static int RegSetValueExA(HKEY key,const char *name,DWORD unused,DWORD type,const BYTE *data,DWORD bytes){
 assert(key==2&&!unused);if(mode==4)return 5;++writes;
 if(!strcmp(name,"EngineType")){assert(type==REG_DWORD&&bytes==4);memcpy(&engine,data,4);}
 else{assert(!strcmp(name,"EngineGLDriver")&&type==REG_SZ&&bytes==sizeof("dgpugl.dll"));memcpy(driver,data,bytes);}
 return 0;
}
#include "retail-settings.h"
int main(void){
 unsigned i;
 for(i=1;i<=3;++i){mode=0;writes=closed=0;engine=i;strcpy(driver,"old.dll");
  assert(!ConfigureRetailGl());assert(engine==2&&!strcmp(driver,"dgpugl.dll")&&writes==2&&closed==1);}
 for(mode=1;mode<=9;++mode){writes=closed=0;engine=1;strcpy(driver,"old.dll");assert(ConfigureRetailGl());
  assert(closed==(mode!=1));if(mode<=3||mode>=8)assert(!writes);}
 mode=0;writes=closed=0;engine=99;assert(ConfigureRetailGl());assert(!writes&&closed==1);
 puts("PASS actual retail native-GL selection: existing typed key, software/D3D migration, exact readback and fail-before-launch errors");return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    p=Path(directory);(p/'test.c').write_text(SOURCE)
    subprocess.run(['cc','-std=c11','-fsanitize=address,undefined','-g','-I'+str(HERE),str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
