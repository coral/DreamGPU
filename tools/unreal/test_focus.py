#!/usr/bin/env python3
"""Execute the real owned activation contract with bounded Win32 API doubles."""
from pathlib import Path
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
source = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t DWORD;
typedef uintptr_t DWORD_PTR;
typedef uintptr_t HWND;
typedef int BOOL;
typedef void *HMODULE;
typedef BOOL (*FARPROC)(DWORD);
#define WINAPI
#define TRUE 1
#define FALSE 0
#define WM_NULL 0
#define SMTO_ABORTIFHUNG 2
#define SMTO_BLOCK 1
static DWORD target_pid=7,target_thread=8,current_thread=9,grant_pid;
static HWND foreground;
static unsigned requests,acknowledgements,grants;
static BOOL request_ok,ack_ok,grant_available,grant_ok,changed_owner;
static void Record(const char *stage,DWORD value) {(void)stage;(void)value;}
static DWORD GetWindowThreadProcessId(HWND window,DWORD *pid) {
    *pid=window==1?target_pid:0;return window==1?target_thread:0;
}
static DWORD GetCurrentThreadId(void) {return current_thread;}
static HWND GetForegroundWindow(void) {return foreground;}
static BOOL SetForegroundWindow(HWND window) {
    assert(window==1);requests++;return request_ok;
}
static uintptr_t SendMessageTimeoutA(HWND window,unsigned msg,uintptr_t wp,
        intptr_t lp,unsigned flags,unsigned timeout,DWORD_PTR *result) {
    assert(window==1&&msg==WM_NULL&&!wp&&!lp&&flags==3&&timeout==1000);
    acknowledgements++;*result=0;
    if(ack_ok&&request_ok)foreground=1;
    if(changed_owner)target_pid=17;
    return ack_ok;
}
static BOOL Grant(DWORD pid) {grants++;grant_pid=pid;return grant_ok;}
static HMODULE GetModuleHandleA(const char *name) {
    assert(!strcmp(name,"user32.dll"));return (HMODULE)1;
}
static BOOL (*GetProcAddress(HMODULE module,const char *name))(DWORD) {
    assert(module==(HMODULE)1&&!strcmp(name,"AllowSetForegroundWindow"));
    return grant_available?Grant:NULL;
}
'''
source += '#include "' + str(HERE / 'focus.h') + '"\n'
source += r'''
int main(void) {
    request_ok=ack_ok=TRUE;
    assert(ActivateOwnedViewport(1,7));assert(requests==1&&acknowledgements==1);
    /* Already-active target still acknowledges its queue, without another request. */
    assert(ActivateOwnedViewport(1,7));assert(requests==1&&acknowledgements==2);
    foreground=0;request_ok=FALSE;
    assert(!ActivateOwnedViewport(1,7));assert(requests==2&&acknowledgements==3);
    request_ok=TRUE;ack_ok=FALSE;
    assert(!ActivateOwnedViewport(1,7));assert(requests==3&&acknowledgements==4);
    ack_ok=TRUE;changed_owner=TRUE;
    assert(!ActivateOwnedViewport(1,7));assert(requests==4&&acknowledgements==5);
    /* No activation, queue attachment, or message can reach a foreign window. */
    assert(!ActivateOwnedViewport(1,7));assert(!ActivateOwnedViewport(2,7));
    assert(!ActivateOwnedViewport(0,7));assert(!ActivateOwnedViewport(1,0));
    target_pid=7;target_thread=current_thread;
    assert(!ActivateOwnedViewport(1,7));assert(requests==4&&acknowledgements==5);
    GrantOwnedForeground(42);assert(grants==0);
    grant_available=TRUE;GrantOwnedForeground(42);assert(grants==1&&grant_pid==42);
    grant_ok=TRUE;GrantOwnedForeground(99);assert(grants==2&&grant_pid==99);
    puts("PASS owned asynchronous activation, deadline, ownership, exact child grants");
}
'''
with tempfile.TemporaryDirectory(prefix='dreamgpu-focus-') as temp:
    path = Path(temp)
    (path / 'test.cpp').write_text(source)
    subprocess.run([shutil.which('clang++') or 'c++', '-std=c++23','-O1', '-g',
                    '-fsanitize=address,undefined', '-Wall', '-Wextra', '-Werror',
                    str(path / 'test.cpp'), '-o', str(path / 'test')], check=True)
    subprocess.run([str(path / 'test')], check=True)
