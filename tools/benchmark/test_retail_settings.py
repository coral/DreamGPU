#!/usr/bin/env python3
"""Actual system OpenGL renderer preflight: existing key, typed settings and readback."""
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
static unsigned mode,writes,closed;static DWORD engine;static char driver[32];
/* Deliberately no file-copy/file-loading APIs: the production selector may
 * change renderer preferences only, never install a private minidriver. */
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
  if(mode==10&&!writes)return 2;
  if(mode==5&&writes){DWORD wrong=1;memcpy(out,&wrong,4);}
 }else{
  assert(!strcmp(name,"EngineGLDriver"));assert(*bytes>=strlen(driver)+1);
  *type=REG_SZ;*bytes=(DWORD)strlen(driver)+1;memcpy(out,driver,*bytes);
  if(mode==6)out[0]='x';
  if(mode==7)--*bytes;
  if(mode==8)*type=REG_DWORD;
  if(mode==9)++*bytes;
 }
 return 0;
}
static int RegSetValueExA(HKEY key,const char *name,DWORD unused,DWORD type,const BYTE *data,DWORD bytes){
 assert(key==2&&!unused);if(mode==4)return 5;++writes;
 if(!strcmp(name,"EngineType")){assert(type==REG_DWORD&&bytes==4);memcpy(&engine,data,4);}
 else{assert(!strcmp(name,"EngineGLDriver")&&type==REG_SZ&&bytes==sizeof("default"));memcpy(driver,data,bytes);}
 return 0;
}
#include "retail-settings.h"
int main(void){
 unsigned i;
 const char *prior[]={"old.dll","dgpugl.dll","jrgopengl.dll","default",""};
 for(unsigned d=0;d<sizeof(prior)/sizeof(prior[0]);++d)
 for(i=1;i<=3;++i){mode=0;writes=closed=0;engine=i;strcpy(driver,prior[d]);
  assert(!ConfigureRetailGl());assert(engine==2&&!strcmp(driver,"default")&&writes==2&&closed==1);}
 for(mode=1;mode<=10;++mode){writes=closed=0;engine=1;strcpy(driver,"old.dll");assert(ConfigureRetailGl());
  assert(closed==(mode!=1));if(mode<=3||mode==10)assert(!writes);}
 mode=0;writes=closed=0;engine=99;assert(ConfigureRetailGl());assert(!writes&&closed==1);
 puts("PASS actual retail system-GL selection: existing typed key, software/D3D migration, exact readback and fail-before-launch errors");return 0;
}
'''
with tempfile.TemporaryDirectory() as directory:
    p=Path(directory);(p/'test.c').write_text(SOURCE)
    subprocess.run(['cc','-std=c11','-fsanitize=address,undefined','-g','-I'+str(HERE),str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
# Both maintained launch paths must use the same normal loader route.
root=HERE.parents[1]
import importlib.util
import sys
sys.path.insert(0,str(root/'scripts/benchmarks'))
spec=importlib.util.spec_from_file_location('halflife',root/'scripts/benchmarks/halflife.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
for mode in ('timedemo','playdemo'):
    command=module.launch_command('dgperf',mode)
    assert '-gl ' in command and '-gldrv' not in command and 'dgpugl.dll' not in command
    assert command.endswith('+'+mode+' dgperf')
runner=(HERE/'runner.cpp').read_text()
assert '-gldrv' not in runner
print('PASS runner/controller normal OpenGL launch commands')
