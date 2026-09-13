#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Test actual fingerprint admission; optional retained retail binary gives positive oracle."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
SOURCE = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
#include <fstream>
#include <iterator>
using DWORD=uint32_t;using BYTE=uint8_t;using BOOL=int;using HANDLE=uintptr_t;using HWND=uintptr_t;
constexpr DWORD GENERIC_READ=1,FILE_SHARE_READ=1,OPEN_EXISTING=3,FILE_ATTRIBUTE_NORMAL=128;
constexpr DWORD INVALID_FILE_SIZE=0xffffffff;constexpr BOOL FALSE=0,TRUE=1;
static std::vector<BYTE> data;
static size_t pos;
static bool missing,short_read,read_fail,large;
static unsigned closed;
static HANDLE CreateFileA(const char *p,DWORD access,DWORD share,void*,DWORD open,DWORD,void*){
 assert(!strcmp(p,"C:\\UT99\\System\\Window.dll")&&access==GENERIC_READ&&share==FILE_SHARE_READ&&open==OPEN_EXISTING);
 pos=0;return missing?0:1;
}
static BOOL CloseHandle(HANDLE h){assert(h==1);++closed;return TRUE;}
constexpr HANDLE INVALID_HANDLE_VALUE=~HANDLE{};
static DWORD GetLastError(){return 0;}
static void SetLastError(DWORD){}
BOOL FindClose(HANDLE){return TRUE;}
#include "handles.hpp"
static DWORD GetFileSize(HANDLE,DWORD *high){*high=0;return large?0x300000:static_cast<DWORD>(data.size());}
static BOOL ReadFile(HANDLE,BYTE *out,DWORD count,DWORD *read,void*){
 assert(count<=4096&&pos+count<=data.size());if(read_fail)return FALSE;
 *read=short_read?count-1:count;memcpy(out,data.data()+pos,*read);pos+=*read;return TRUE;
}
static BOOL Equal(const char*a,const char*b){return !strcmp(a,b);}
static DWORD Length(const char*s){return static_cast<DWORD>(strlen(s));}
static void Text(const char*){}
static const char *class_name="RetailWLog";
static int GetClassNameA(HWND,char *out,int cap){assert(cap==128);strcpy(out,class_name);return static_cast<int>(strlen(out));}
#include "engine-exit.h"
int main(int argc,char **argv){
 data={1,2,3};assert(!LockRetailExitModule());assert(closed==1);
 missing=true;assert(!LockRetailExitModule());assert(closed==1);missing=false;
 large=true;assert(!LockRetailExitModule());large=false;
 read_fail=true;assert(!LockRetailExitModule());read_fail=false;
 short_read=true;assert(!LockRetailExitModule());short_read=false;
 assert(RetailLogClass(2));class_name="RetailWLogFake";assert(!RetailLogClass(2));
 if(argc>1){
  std::ifstream file(argv[1],std::ios::binary);assert(file);
  data.assign(std::istreambuf_iterator<char>(file),{});
  auto lock=LockRetailExitModule();assert(lock);lock.reset();
  data[0]^=1;assert(!LockRetailExitModule());
 }
}
'''
with tempfile.TemporaryDirectory(prefix='dreamgpu-retail-exit-') as d:
    p=Path(d);(p/'test.cpp').write_text(SOURCE)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++23','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined','-I',str(HERE),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test'),*sys.argv[1:]],check=True)
print('PASS actual retail exit fingerprint, tampering/read failures, close ownership, log class')
