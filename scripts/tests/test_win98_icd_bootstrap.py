#!/usr/bin/env python3
"""Actual diagnostic registry transaction through the existing Win32 syscall seam."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class Bootstrap(unittest.TestCase):
    def test_registry_ownership_and_restore(self):
        code = (ROOT / 'tools/opengl/win98-icd-bootstrap.cpp').read_text()
        # Compile the production file/registry functions; the fixture uses
        # integer handles, so only the handle null spelling is adapted.
        first = code[code.index('namespace {'):code.index('bool win98()')]
        second = code[code.index('bool restore('):code.index('const char *arguments()')]
        source = first + second + '}\n'
        source = source.replace('HKEY value = nullptr', 'HKEY value = 0').replace('key.value = nullptr', 'key.value = 0')
        prefix = r'''
#include "test-win32.h"
#include "sha256.h"
#include <cassert>
inline int lstrcmpiA(const char*a,const char*b){return fake_win32::canon(a).compare(fake_win32::canon(b));}
static unsigned delete_calls;
inline LONG CountedDelete(HKEY key,const char*path){++delete_calls;return RegDeleteKeyA(key,path);}
#define RegDeleteKeyA CountedDelete

'''
        main = r'''
int main(){ (void)Inspect;
 using namespace fake_win32;
 for(unsigned foreign=0;foreign<3;++foreign){
  reset();delete_calls=0;Original prior;
  assert(backup(prior)&&!prior.key&&!prior.exists);
  assert(files[canon(BackupPath)].flushed);
  assert(apply(prior));
  if(foreign==1)keys[canon(KeyPath)]["other"]={REG_SZ,{'u',0}};
  if(foreign==2)keys[canon(KeyPath)+"\\foreign"]["value"]={REG_SZ,{'u',0}};
  assert(restore(prior)==(foreign==0));
  assert(delete_calls==(foreign==0?1u:0u));
  if(foreign==1)assert(keys[canon(KeyPath)]["other"].bytes==std::vector<BYTE>({'u',0}));
  if(foreign==2)assert(keys[canon(KeyPath)+"\\foreign"]["value"].bytes==std::vector<BYTE>({'u',0}));
  assert(files[canon(DisabledPath)].flushed);
  assert(handles.empty()&&key_handles.empty());
 }
 reset();Original prior;
 keys[canon(KeyPath)]["dgpuicd"]={REG_SZ,{'D','G','P','U','I','C','D','.','D','L','L',0}};
 auto exact=keys[canon(KeyPath)]["dgpuicd"].bytes;
 assert(backup(prior)&&prior.key&&prior.exists);
 assert(apply(prior));assert(restore(prior));
 assert(keys[canon(KeyPath)]["dgpuicd"].bytes==exact);
 reset();keys[canon(KeyPath)]["dgpuicd"]={REG_SZ,{'f','o','r','e','i','g','n',0}};
 assert(!backup(prior));assert(!files.count(canon(BackupPath)));assert(mutation==0);
 reset();assert(backup(prior));assert(apply(prior));
 keys[canon(KeyPath)]["dgpuicd"]={REG_SZ,{'f','o','r','e','i','g','n',0}};
 assert(!restore(prior));assert(!files.count(canon(DisabledPath)));
 assert(keys[canon(KeyPath)]["dgpuicd"].bytes==std::vector<BYTE>({'f','o','r','e','i','g','n',0}));
 reset();assert(backup(prior));files[canon(BackupPath)].bytes[0]^=1;
 assert(!read_backup(prior));assert(keys.empty());
 puts("PASS actual bootstrap registry preservation, foreign values/subkeys, conflicts, checksummed originals");
}
'''
        with tempfile.TemporaryDirectory(prefix='dreamgpu-win98-icd-bootstrap-') as temp:
            directory = Path(temp)
            src = directory / 'test.cpp'
            src.write_text(prefix + source + main)
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++23', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                            '-fno-exceptions', '-fno-rtti', '-fsanitize=address,undefined',
                            '-I' + str(ROOT / 'tools/setup'), str(src), '-o', str(directory / 'test')], check=True)
            subprocess.run([str(directory / 'test')], check=True)


if __name__ == '__main__':
    unittest.main()
