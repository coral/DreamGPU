#!/usr/bin/env python3
"""Exercise the real input handoff's ownership, acknowledgment and cleanup gates."""
from pathlib import Path
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
SOURCE = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
typedef uint32_t DWORD,UINT; typedef uintptr_t DWORD_PTR,WPARAM;
typedef intptr_t LPARAM,LRESULT; typedef int BOOL;
typedef void *HWND,*HMODULE; typedef struct { int x,y; } POINT;
typedef struct { LRESULT (*lpfnWndProc)(HWND,UINT,WPARAM,LPARAM); HMODULE hInstance; const char *lpszClassName; } WNDCLASSA;
typedef struct {int dx,dy;DWORD dwFlags;DWORD_PTR dwExtraInfo;} MOUSEINPUT;
typedef struct {DWORD type;MOUSEINPUT mi;} INPUT;
typedef struct {UINT message;DWORD_PTR tag;} MSG;
#define CALLBACK
#define TRUE 1
#define FALSE 0
#define WM_LBUTTONDOWN 1
#define WM_LBUTTONUP 2
#define VK_LBUTTON 3
#define VK_RBUTTON 4
#define VK_MBUTTON 5
#define VK_SHIFT 6
#define VK_CONTROL 7
#define VK_MENU 8
#define SM_CXSCREEN 0
#define SM_CYSCREEN 1
#define WS_EX_TOPMOST 1
#define WS_EX_TOOLWINDOW 2
#define WS_POPUP 4
#define SW_SHOWNOACTIVATE 5
#define INPUT_MOUSE 0
#define MOUSEEVENTF_MOVE 1
#define MOUSEEVENTF_ABSOLUTE 2
#define MOUSEEVENTF_LEFTDOWN 4
#define MOUSEEVENTF_LEFTUP 8
#define PM_REMOVE 1
#define QS_ALLINPUT 255
#define WAIT_FAILED 0xffffffffU
static unsigned sends,destroys,unregisters,restores,registered,events,index_event;
static DWORD tick;static DWORD_PTR current_tag;
static BOOL held,foreign,wrong_foreground,drop_up;
static UINT inserted=3;
static LRESULT (*procedure)(HWND,UINT,WPARAM,LPARAM);
static HWND owned=(HWND)(uintptr_t)1;
static void Record(const char *s,DWORD value){(void)s;(void)value;}
static DWORD_PTR GetMessageExtraInfo(void){return current_tag;}
static LRESULT DefWindowProcA(HWND w,UINT m,WPARAM p,LPARAM l){(void)w;(void)m;(void)p;(void)l;return 0;}
static int GetAsyncKeyState(int key){(void)key;return held?0x8000:0;}
static BOOL GetCursorPos(POINT *p){p->x=100;p->y=200;return TRUE;}
static int GetSystemMetrics(int key){return key==SM_CXSCREEN?1024:768;}
static HMODULE GetModuleHandleA(const char *s){assert(!s);return (HMODULE)(uintptr_t)2;}
static BOOL RegisterClassA(WNDCLASSA *c){procedure=c->lpfnWndProc;registered++;return TRUE;}
static DWORD last_error;
static DWORD GetLastError(void){return last_error;}
static void SetLastError(DWORD value){last_error=value;}
static HWND CreateWindowExA(DWORD e,const char *c,const char *t,DWORD s,int x,int y,int w,int h,HWND p,void *m,HMODULE i,void *a){
 (void)c;(void)t;(void)i;assert(e==3&&s==4&&x==32&&y==32&&w==32&&h==32&&!p&&!m&&!a);return owned;}
static BOOL ShowWindow(HWND w,int how){assert(w==owned&&how==SW_SHOWNOACTIVATE);return TRUE;}
static DWORD GetWindowThreadProcessId(HWND w,DWORD *p){assert(w==owned);*p=7;return 8;}
static DWORD GetCurrentProcessId(void){return 7;}
static HWND WindowFromPoint(POINT p){assert(p.x==48&&p.y==48);return foreign?NULL:owned;}
static UINT SendInput(UINT count,INPUT *input,int size){
 sends++;assert(size==sizeof(INPUT));
 if(count==1){assert(input[0].mi.dwFlags==MOUSEEVENTF_LEFTUP);return 1;}
 assert(count==3&&input[0].mi.dwFlags==3&&input[1].mi.dwFlags==4&&input[2].mi.dwFlags==8);
 assert(input[0].mi.dx>0&&input[0].mi.dx<65535&&input[0].mi.dy>0&&input[0].mi.dy<65535);
 for(unsigned i=0;i<count;i++)assert(input[i].mi.dwExtraInfo==0x44474649UL);
 events=drop_up?1:2;return inserted;}
static DWORD GetTickCount(void){return tick;}
static BOOL PeekMessageA(MSG *m,HWND w,UINT a,UINT b,UINT flags){
 assert(!w&&!a&&!b&&flags==PM_REMOVE);if(index_event>=events)return FALSE;
 m->message=index_event++==0?WM_LBUTTONDOWN:WM_LBUTTONUP;m->tag=0x44474649UL;return TRUE;}
static BOOL TranslateMessage(MSG *m){(void)m;return TRUE;}
static LRESULT DispatchMessageA(MSG *m){current_tag=m->tag;return procedure(owned,m->message,0,0);}
static DWORD MsgWaitForMultipleObjects(DWORD n,void *h,BOOL all,DWORD timeout,DWORD mask){
 assert(!n&&!h&&!all&&timeout<=1000&&mask==QS_ALLINPUT);tick+=timeout;return 258;}
static HWND GetForegroundWindow(void){return wrong_foreground?NULL:owned;}
static BOOL DestroyWindow(HWND w){assert(w==owned);destroys++;return TRUE;}
static BOOL UnregisterClassA(const char *c,HMODULE i){(void)c;(void)i;unregisters++;return TRUE;}
static BOOL SetCursorPos(int x,int y){assert(x==100&&y==200);restores++;return TRUE;}
'''
SOURCE += '#include "' + str(HERE / 'focus-input.h') + '"\n'
SOURCE += r'''
static void reset(void){sends=destroys=unregisters=restores=registered=events=index_event=0;
 tick=0;held=foreign=wrong_foreground=drop_up=FALSE;inserted=3;}
int main(void){POINT saved;HWND w;
 reset();w=AcquireOwnedInput(&saved);assert(w==owned&&sends==1&&destroys==0);
 ReleaseOwnedInput(w,&saved);assert(destroys==1&&unregisters==1&&restores==1);
 reset();held=TRUE;assert(!AcquireOwnedInput(&saved)&&!sends&&!registered);
 reset();foreign=TRUE;assert(!AcquireOwnedInput(&saved)&&!sends&&destroys==1&&unregisters==1);
 reset();inserted=2;assert(!AcquireOwnedInput(&saved)&&sends==2&&destroys==1&&restores==1);
 reset();inserted=0;assert(!AcquireOwnedInput(&saved)&&sends==1&&destroys==1);
 reset();drop_up=TRUE;assert(!AcquireOwnedInput(&saved)&&tick==1000&&sends==1&&destroys==1);
 reset();wrong_foreground=TRUE;assert(!AcquireOwnedInput(&saved)&&sends==1&&destroys==1);
 reset();{OwnedInputWindow input;assert(input.acquire());assert(!input.acquire());SetLastError(77);}
 assert(destroys==1&&unregisters==1&&restores==1&&GetLastError()==77);
 reset();{OwnedInputWindow input;held=TRUE;assert(!input.acquire());}
 assert(!destroys&&!registered);
 puts("PASS owned real-input transaction, foreign/held rejection, partial release, deadline, cleanup");
}
'''
with tempfile.TemporaryDirectory(prefix='dreamgpu-focus-input-') as temp:
    root = Path(temp)
    (root / 'test.cpp').write_text(SOURCE)
    subprocess.run([shutil.which('clang++') or 'c++', '-std=c++23','-O1', '-g', '-fsanitize=address,undefined',
                    '-Wall', '-Wextra', '-Werror', str(root / 'test.cpp'), '-o', str(root / 'test')], check=True)
    subprocess.run([str(root / 'test')], check=True)
