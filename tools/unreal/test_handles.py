#!/usr/bin/env python3
"""Run the actual freestanding owner through failure, move and reset paths."""
from pathlib import Path
import shutil, subprocess, tempfile
HERE = Path(__file__).resolve().parent
SOURCE = r'''
#include <cassert>
#include <cstdint>
#include <type_traits>
using HANDLE=std::uintptr_t; using DWORD=std::uint32_t;
#define INVALID_HANDLE_VALUE (~HANDLE{})
static DWORD error;
static unsigned closes[32],finds[32];
static DWORD GetLastError() {return error;}
static void SetLastError(DWORD value) {error=value;}
static int CloseHandle(HANDLE value) {assert(value<32);closes[value]++;error=999;return 1;}
static int FindClose(HANDLE value) {assert(value<32);finds[value]++;error=888;return 1;}
#include "handles.hpp"
static_assert(!std::is_copy_constructible_v<OwnedHandle>);
static_assert(!std::is_copy_assignable_v<OwnedHandle>);
static_assert(std::is_nothrow_move_constructible_v<OwnedHandle>);
static_assert(std::is_nothrow_destructible_v<OwnedHandle>);
static bool failure() {OwnedHandle file{3};SetLastError(123);return false;}
int main() {
 {OwnedHandle empty;OwnedHandle invalid{INVALID_HANDLE_VALUE};assert(!empty&&!invalid);}
 assert(!failure()&&closes[3]==1&&GetLastError()==123);
 {OwnedHandle file{4};OwnedHandle moved{static_cast<OwnedHandle&&>(file)};
  assert(!file&&moved.get()==4);moved.reset(4);assert(!closes[4]);
  OwnedHandle other{5};other=static_cast<OwnedHandle&&>(moved);
  assert(!moved&&closes[5]==1&&GetLastError()==123);
  other.reset(6);assert(closes[4]==1&&GetLastError()==123);
  HANDLE released=other.release();assert(released==6&&!other);CloseHandle(released);}
 assert(closes[4]==1&&closes[5]==1&&closes[6]==1);
 {FindHandle enumeration{7};SetLastError(259);enumeration.reset();assert(GetLastError()==259);}
 assert(finds[7]==1&&!closes[7]);
}
'''
with tempfile.TemporaryDirectory(prefix='dreamgpu-unreal-owner-') as temporary:
    root=Path(temporary);source=root/'test.cpp';source.write_text(SOURCE)
    compiler=shutil.which('clang++') or 'c++'
    flags=['-std=c++23','-Wall','-Wextra','-Werror','-I',str(HERE)]
    subprocess.run([compiler,*flags,'-fsanitize=address,undefined','-g',str(source),'-o',str(root/'test')],check=True)
    subprocess.run([str(root/'test')],check=True)
    if 'clang' in compiler:
        subprocess.run([compiler,*flags,'--analyze','-Xanalyzer','-analyzer-output=text',str(source)],check=True,cwd=root)
print('PASS actual scoped handles: failure, invalid sentinel, exact close, move, replacement, release and Win32 error preservation')
