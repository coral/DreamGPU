#!/usr/bin/env python3
"""Actual fixed retail-media gate rejects missing/wrong media without a launch."""
from pathlib import Path
import subprocess
import tempfile
here=Path(__file__).resolve().parent
source=r'''
#include <cassert>
#include <cstring>
#include <cstdint>
using DWORD=uint32_t;using BOOL=int;
#define TRUE 1
#define FALSE 0
#define DRIVE_CDROM 5
#define INVALID_FILE_ATTRIBUTES 0xffffffffU
#define FILE_ATTRIBUTE_DIRECTORY 16
static int mode;
static unsigned GetDriveTypeA(const char *p){return mode!=1&&(p[0]=='D'||p[0]=='E')?5:3;}
static int GetVolumeInformationA(const char *p,char *out,unsigned size,void*,void*,void*,void*,unsigned){
 assert(size>=10);strcpy(out,p[0]=='E'&&mode!=2?"HALF_LIFE":"WIN98SE");return mode!=3;
}
static int lstrcmpiA(const char *a,const char *b){return strcmp(a,b);}
static DWORD GetFileAttributesA(const char *p){
 assert(p[0]=='E');
 if(strstr(p,"SIERRA.INF"))return mode==4?INVALID_FILE_ATTRIBUTES:mode==5?FILE_ATTRIBUTE_DIRECTORY:0;
 assert(strstr(p,"DATA1.CAB"));return mode==6?INVALID_FILE_ATTRIBUTES:mode==7?FILE_ATTRIBUTE_DIRECTORY:0;
}
#include "retail-media.h"
int main(){assert(DgRetailMediaAvailable());for(mode=1;mode<=7;mode++)assert(!DgRetailMediaAvailable());}
'''
with tempfile.TemporaryDirectory(prefix='dreamgpu-retail-media-') as temp:
    root=Path(temp);(root/'test.cpp').write_text(source)
    subprocess.run(['c++','-std=c++23','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                    '-I'+str(here),str(root/'test.cpp'),'-o',str(root/'test')],check=True)
    subprocess.run([str(root/'test')],check=True)
print('PASS original retail CD, absent/wrong/unreadable volume and missing/nonfile setup markers')
