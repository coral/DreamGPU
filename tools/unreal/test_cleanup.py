#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute the actual cleanup gate with Win32 failure and timeout seams."""
from pathlib import Path
import os
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
SOURCE = r'''
#include <cassert>
#include <cstdint>
using DWORD=uint32_t; using BOOL=int; using HANDLE=uintptr_t; using HWND=uintptr_t;
constexpr DWORD WAIT_OBJECT_0=0, WAIT_TIMEOUT=258, WAIT_FAILED=0xffffffff;
constexpr BOOL TRUE=1,FALSE=0; constexpr unsigned WM_COMMAND=0x111;
static DWORD initial,after,owner=7,exitcode;
static BOOL posted=TRUE,exitok=TRUE,killed=TRUE;
static unsigned waits,posts,kills,graceful,evidence;
static DWORD WaitForSingleObject(HANDLE h,DWORD ms) {
 assert(h==1);++waits;
 if(!ms)return initial;
 if(ms==15000)return after;
 assert(ms==3000);return WAIT_OBJECT_0;
}
static DWORD GetWindowThreadProcessId(HWND h,DWORD *p) {assert(h==2);*p=owner;return 9;}
static BOOL PostMessageA(HWND h,unsigned msg,uintptr_t wp,intptr_t lp) {
 assert(h==2&&msg==WM_COMMAND&&wp==40004&&!lp);++posts;return posted;
}
static BOOL GetExitCodeProcess(HANDLE h,DWORD *p) {assert(h==1);*p=exitcode;return exitok;}
static BOOL TerminateProcess(HANDLE h,unsigned code) {assert(h==1&&code==125);assert(evidence==1);++kills;return killed;}
static DWORD GetLastError(){return 5;}
static void Record(const char *s,DWORD) {if(s[0]=='C'&&s[8]=='G')++graceful;}
static BOOL RetailLogClass(HWND h){return h==2;}
static void CleanupFailureEvidence(HANDLE h,DWORD pid){assert(h==1&&pid==7&&!kills);++evidence;}
#include "cleanup.h"
static void reset(){initial=WAIT_TIMEOUT;after=WAIT_OBJECT_0;owner=7;exitcode=0;
 posted=exitok=killed=TRUE;waits=posts=kills=graceful=evidence=0;}
int main(){
 reset();assert(CleanupOwnedGame(1,7,2));assert(posts==1&&!kills&&waits==2);
 reset();initial=WAIT_OBJECT_0;assert(CleanupOwnedGame(1,7,0));assert(!posts&&!kills);
 reset();after=WAIT_TIMEOUT;assert(!CleanupOwnedGame(1,7,2));assert(kills==1);
 reset();owner=8;assert(!CleanupOwnedGame(1,7,2));assert(!posts&&kills==1);
 reset();assert(!CleanupOwnedGame(1,7,0));assert(!posts&&kills==1);
 reset();posted=FALSE;assert(!CleanupOwnedGame(1,7,2));assert(kills==1);
 reset();exitcode=9;assert(!CleanupOwnedGame(1,7,2));assert(!kills);
 reset();exitok=FALSE;assert(!CleanupOwnedGame(1,7,2));assert(!kills);
 reset();initial=WAIT_FAILED;assert(!CleanupOwnedGame(1,7,2));assert(!posts&&!kills);
 reset();after=WAIT_FAILED;assert(!CleanupOwnedGame(1,7,2));assert(!kills);
 reset();after=WAIT_TIMEOUT;killed=FALSE;assert(!CleanupOwnedGame(1,7,2));assert(kills==1);
}
'''
with tempfile.TemporaryDirectory(prefix="dreamgpu-ut-cleanup-") as d:
    root=Path(d)
    (root/'test.cpp').write_text(SOURCE)
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++23','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined','-I',str(HERE),str(root/'test.cpp'),
                    '-o',str(root/'test')],check=True)
    subprocess.run([str(root/'test')],check=True)
print('PASS actual owned engine-quit cleanup; forced/nonzero/unknown exits fail')
