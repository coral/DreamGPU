#!/usr/bin/env python3
"""Actual bounded failure reader: closes handles and never reuses stale bytes."""
from pathlib import Path
import subprocess
import tempfile
here = Path(__file__).resolve().parent
source = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stddef.h>
typedef int BOOL; typedef uint32_t DWORD; typedef intptr_t HANDLE;
#define TRUE 1
#define FALSE 0
#define INVALID_HANDLE_VALUE -1
#define GENERIC_READ 1
#define FILE_SHARE_READ 1
#define FILE_SHARE_WRITE 2
#define OPEN_EXISTING 3
static const char *contents; static BOOL available=TRUE,read_ok=TRUE;
static unsigned closes;
static BOOL Contains(const char *text,const char *word){return strstr(text,word)!=NULL;}
static HANDLE CreateFileA(const char *path,DWORD access,DWORD share,void *sa,DWORD mode,DWORD flags,void *template_file){
 assert(!strcmp(path,"C:\\UT99\\System\\OpenGLid.err")&&access==1&&share==3&&!sa&&mode==3&&!flags&&!template_file);
 return available?7:INVALID_HANDLE_VALUE;
}
static BOOL ReadFile(HANDLE file,void *out,DWORD capacity,DWORD *bytes,void *overlapped){
 assert(file==7&&capacity==16384&&!overlapped);
 if(!read_ok)return FALSE;
 *bytes=(DWORD)strlen(contents);if(*bytes>capacity)*bytes=capacity;memcpy(out,contents,*bytes);return TRUE;
}
static BOOL CloseHandle(HANDLE file){assert(file==7);closes++;return TRUE;}
static DWORD last_error;
static DWORD GetLastError(){return last_error;}
static void SetLastError(DWORD error){last_error=error;}
static BOOL FindClose(HANDLE){assert(0);return FALSE;}
#include "handles.hpp"
#include "present-status.h"
int main(void){
 contents="DG_PRESENT_FAILED error=1400\r\n";assert(GlidePresentFailed());assert(closes==1);
 contents="DG_WGL_BOUND\r\n";assert(!GlidePresentFailed());assert(closes==2);
 contents="DG_PRESENT_FAILED error=6";read_ok=FALSE;assert(!GlidePresentFailed()&&!GlidePresentError[0]&&closes==3);
 available=FALSE;assert(!GlidePresentFailed()&&!GlidePresentError[0]&&closes==3);
}
'''
with tempfile.TemporaryDirectory() as temporary:
    root = Path(temporary); (root/'test.cpp').write_text(source)
    subprocess.run(['c++','-std=c++23','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(here),str(root/'test.cpp'),'-o',str(root/'test')],check=True)
    subprocess.run([str(root/'test')],check=True)
print('PASS actual bounded Glide failure reader')
