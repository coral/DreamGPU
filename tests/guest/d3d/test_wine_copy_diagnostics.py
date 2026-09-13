#!/usr/bin/env python3
"""Compile actual bounded Wine counters against deterministic host mocks."""
import os, pathlib, subprocess, tempfile
HERE=pathlib.Path(__file__).resolve().parent
SOURCE=r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned long DWORD;typedef int BOOL;typedef int HRESULT;typedef void *HANDLE;
#define TRUE 1
#define FALSE 0
#define NULL_HANDLE ((void*)0)
#define SYNCHRONIZE 1
#define WAIT_OBJECT_0 0
#define GENERIC_WRITE 1
#define FILE_SHARE_READ 2
#define CREATE_ALWAYS 3
#define INVALID_HANDLE_VALUE ((void*)-1)
struct fmt {unsigned int id;};struct resource {unsigned int width,height,usage,pool;struct fmt *format;};
struct texture;struct wined3d_swapchain {struct texture *front_buffer;struct texture **back_buffers;};
struct texture {struct wined3d_swapchain *swapchain;};
struct wined3d_surface {struct resource resource;struct texture *container;unsigned int locations;};
struct wined3d_context {int x;};
static unsigned int start,end,closes,writes;static char output[24576];static size_t length;
static DWORD GetLastError(void){return 71;}static void SetLastError(DWORD e){assert(e==71);}
static DWORD GetTickCount(void){return 100;}
static HANDLE OpenEventA(DWORD a,BOOL b,const char *n){(void)a;(void)b;return strstr(n,"Measuring")?(HANDLE)1:(HANDLE)2;}
static DWORD WaitForSingleObject(HANDLE h,DWORD d){(void)d;return (h==(HANDLE)1?start:end)?0:258;}
static int CloseHandle(HANDLE h){assert(h);++closes;return 1;}
static HANDLE CreateFileA(const char *p,DWORD a,DWORD b,void*c,DWORD d,DWORD e,void*f){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;assert(!strcmp(p,"C:\\DGWCOPY.LOG"));length=0;return (HANDLE)3;}
static int WriteFile(HANDLE h,const void *p,DWORD n,DWORD *written,void *o){(void)o;assert(h==(HANDLE)3);assert(length+n<sizeof(output));memcpy(output+length,p,n);length+=n;output[length]=0;*written=n;++writes;return 1;}
#define wsprintfA(buffer,...) snprintf(buffer,512,__VA_ARGS__)
static HRESULT surface_load_location(struct wined3d_surface *s,struct wined3d_context *c,DWORD d){(void)s;(void)c;(void)d;return 17;}
#include "wine-copy-diagnostics.h"
int main(void){
 struct fmt fmt={3};struct texture tex={0};struct wined3d_surface s={{640,480,8,1,&fmt},&tex,4};unsigned int i;
 dg_copy_record(2,&s,1);assert(!dg_copy_used);dg_copy_present();assert(!dg_copy_state);
 start=1;dg_copy_present();assert(dg_copy_state==1&&dg_copy_presents==1);
 assert(dg_surface_load_location(8,222,&s,NULL,1)==17);
 assert(dg_copy_counts[0].key.source==8&&dg_copy_counts[0].key.root_line==222&&!dg_copy_depth);
 for(i=0;i<100;++i)dg_copy_record(2,&s,1);
 assert(dg_copy_counts[1].count==100);
 for(i=1;i<64;++i){s.resource.width=i;dg_copy_record(2,&s,1);}
 assert(dg_copy_used==64&&dg_copy_overflow==1&&!writes);
 end=1;dg_copy_present();assert(dg_copy_state==2&&strstr(output,"final=1")&&strstr(output,"overflow=1"));
 assert(closes==3&&length<24576);i=writes;dg_copy_present();assert(writes==i);
 puts("Wine copy counters: aggregation, source ownership, capacity, overflow, phase and final flush PASS");return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='dg-wine-counters-') as temporary:
 p=pathlib.Path(temporary);(p/'test.c').write_text(SOURCE)
 subprocess.run([os.environ.get('CC','cc'),'-std=c99','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-g','-I',str(HERE.parents[2] / "guest/d3d"),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
