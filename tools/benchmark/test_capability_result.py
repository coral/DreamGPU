#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise full bounded capability result reads, never diagnostic tail truncation."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent
CODE = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
using DWORD=uint32_t;using BOOL=int;using BYTE=uint8_t;using HANDLE=void *;
struct LARGE_INTEGER { int64_t QuadPart; };
#define GENERIC_READ 1
#define FILE_SHARE_READ 2
#define OPEN_EXISTING 3
#define FILE_ATTRIBUTE_NORMAL 4
#define OUTPUT_MAX 65536
#define INVALID_HANDLE_VALUE reinterpret_cast<void *>(intptr_t(-1))
static BYTE Output[OUTPUT_MAX];static DWORD OutputBytes;
static unsigned scenario,closed,size_calls,reads;
static HANDLE CreateFileA(const char *path,DWORD access,DWORD share,void *a,DWORD creation,DWORD flags,void *b) {
 assert(!strcmp(path,"fixed.json")&&access==GENERIC_READ&&share==FILE_SHARE_READ&&!a&&!b);
 assert(creation==OPEN_EXISTING&&flags==FILE_ATTRIBUTE_NORMAL);
 return scenario==1?INVALID_HANDLE_VALUE:reinterpret_cast<void *>(intptr_t(1));
}
static BOOL FileSize(HANDLE,LARGE_INTEGER *out) {
 ++size_calls;out->QuadPart=scenario==2?0:scenario==3?OUTPUT_MAX+1:scenario==8?OUTPUT_MAX:23;
 if(scenario==6&&size_calls==2)++out->QuadPart;
 return scenario!=7;
}
static BOOL ReadFile(HANDLE,void *bytes,DWORD size,DWORD *read,void *) {
 ++reads;assert(size<=sizeof(Output));memset(bytes,'x',size);*read=size-(scenario==5?1:0);return scenario!=4;
}
static void CloseHandle(HANDLE) { ++closed; }
#include "capability-result.h"
int main() {
 for(scenario=0;scenario<=8;++scenario) {
  closed=size_calls=reads=0;OutputBytes=97;
  const char *error=ReadCapabilityResult("fixed.json");
  assert(closed==(scenario==1?0:1));
  if(!scenario||scenario==8)assert(!error&&OutputBytes==(scenario==8?OUTPUT_MAX:23));
  else assert(error&&!OutputBytes);
  if(scenario==1)assert(!strcmp(error,"missing-capability-output")&&!reads);
  if(scenario==2||scenario==7)assert(!strcmp(error,"empty-capability-output")&&!reads);
  if(scenario==3)assert(!strcmp(error,"capability-output-too-large")&&!reads);
  if(scenario>=4&&scenario<=6)assert(!strcmp(error,"incomplete-capability-output")&&reads==1);
 }
}
'''
with tempfile.TemporaryDirectory(prefix='dreamgpu-capability-result-') as directory:
    work = Path(directory)
    (work / 'test.cpp').write_text(CODE)
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I', str(ROOT), str(work / 'test.cpp'),
                    '-o', str(work / 'test')], check=True)
    subprocess.run([str(work / 'test')], check=True)
print('PASS actual capability result reader: full bytes, missing/empty/oversize/short/failing/changing '
      'files, writer exclusion, handle cleanup and no partial success (ASan/UBSan)')
