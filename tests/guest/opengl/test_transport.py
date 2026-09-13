#!/usr/bin/env python3
"""Compile the real platform adapter against deterministic OS boundary mocks."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3] / "guest/opengl"
header = r'''
#ifndef TEST_WINDOWS_H
#define TEST_WINDOWS_H
#include <stdint.h>
#include <string.h>
typedef uint32_t DWORD, ULONG;
typedef int BOOL;
typedef void *HANDLE, *HDC;
typedef const char *LPCSTR;
typedef char *LPSTR;
#define TRUE 1
#define FALSE 0
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define OPEN_EXISTING 3
#define CopyMemory memcpy
DWORD GetVersion(void);
HANDLE CreateFileA(const char *, DWORD, DWORD, void *, DWORD, DWORD, HANDLE);
BOOL CloseHandle(HANDLE);
BOOL DeviceIoControl(HANDLE, DWORD, void *, DWORD, void *, DWORD, DWORD *, void *);
int ExtEscape(HDC,int,int,LPCSTR,int,LPSTR);
#endif
'''
source = r'''
#include <assert.h>
#include <stdio.h>
#include "windows.h"
#include "dg-escape.h"
static DWORD version, version_calls, opens, closes, io_calls, nt_calls;
static BOOL io_ok=TRUE, create_ok=TRUE;
static DG_ESCAPE_REPLY fixture;
static DWORD returned_fixture;
DWORD GetVersion(void) { ++version_calls; return version; }
HANDLE CreateFileA(const char *name,DWORD access,DWORD share,void *security,DWORD disposition,DWORD flags,HANDLE template_handle)
{
    assert(!strcmp(name,"\\\\.\\DREAMGPU"));
    assert(!access&&!share&&!security&&disposition==OPEN_EXISTING&&!flags&&!template_handle);
    ++opens; return create_ok ? (HANDLE)(uintptr_t)(100+opens) : INVALID_HANDLE_VALUE;
}
BOOL CloseHandle(HANDLE handle) { assert(handle!=INVALID_HANDLE_VALUE); ++closes; return TRUE; }
BOOL DeviceIoControl(HANDLE handle,DWORD code,void *input,DWORD in_bytes,void *output,DWORD out_bytes,DWORD *returned,void *overlapped)
{
    assert(handle!=INVALID_HANDLE_VALUE&&code==DG_ESCAPE&&!overlapped);
    assert(input&&in_bytes>=sizeof(DG_ESCAPE_REQUEST)&&out_bytes>=sizeof(fixture));
    ++io_calls; memcpy(output,&fixture,sizeof(fixture)); *returned=returned_fixture; return io_ok;
}
int ExtEscape(HDC display,int escape,int in_bytes,LPCSTR input,int out_bytes,LPSTR output)
{ (void)display;(void)in_bytes;(void)input;(void)out_bytes;(void)output; assert(escape==DG_ESCAPE);++nt_calls;return 1; }
#include "transport.cpp"
static DG_ESCAPE_REQUEST request;
static struct { DG_ESCAPE_REPLY reply; unsigned char data[512]; } output;
static int call(DWORD operation)
{ request.Operation=operation; return JglTransportRequest(NULL,sizeof(request),(LPCSTR)&request,sizeof(output),(LPSTR)&output); }
int main(void)
{
    fixture.Version=DG_ESCAPE_VERSION;fixture.Status=DG_ESCAPE_OK;returned_fixture=sizeof(fixture);
    version=0;JglTransportInitialize();assert(call(DG_ESCAPE_OPEN)==1);assert(nt_calls==1&&!opens&&!io_calls);
    version=0x80000004;JglTransportInitialize();assert(version_calls==2);
    assert(!call(DG_ESCAPE_SUBMIT)&&!opens&&!io_calls);
    create_ok=FALSE;assert(!call(DG_ESCAPE_OPEN)&&opens==1&&!io_calls);create_ok=TRUE;
    assert(call(DG_ESCAPE_OPEN)&&opens==2&&io_calls==1);
    assert(call(DG_ESCAPE_SUBMIT)&&opens==2&&io_calls==2);
    io_ok=FALSE;assert(!call(DG_ESCAPE_CLOSE)&&!closes);io_ok=TRUE;
    assert(call(DG_ESCAPE_SUBMIT));
    returned_fixture=sizeof(fixture)-1;assert(!call(DG_ESCAPE_SUBMIT));
    returned_fixture=sizeof(output)+1;assert(!call(DG_ESCAPE_SUBMIT));
    returned_fixture=sizeof(fixture);fixture.ResultBytes=4;assert(!call(DG_ESCAPE_SUBMIT));
    returned_fixture=sizeof(fixture)+4;assert(call(DG_ESCAPE_SUBMIT));
    fixture.ResultBytes=513;assert(!call(DG_ESCAPE_SUBMIT));fixture.ResultBytes=0;returned_fixture=sizeof(fixture);
    fixture.Version=0;assert(call(DG_ESCAPE_CLOSE)&&!closes);fixture.Version=DG_ESCAPE_VERSION;
    fixture.Status=DG_ESCAPE_TIMEOUT;assert(call(DG_ESCAPE_CLOSE)&&!closes);fixture.Status=DG_ESCAPE_OK;
    fixture.Client=123;assert(call(DG_ESCAPE_CLOSE)&&!closes);fixture.Client=0;
    fixture.ResultType=1;assert(call(DG_ESCAPE_CLOSE)&&!closes);fixture.ResultType=0;
    assert(call(DG_ESCAPE_CLOSE)&&closes==1);assert(!call(DG_ESCAPE_SUBMIT));
    assert(call(DG_ESCAPE_OPEN)&&opens==3);assert(call(DG_ESCAPE_CLOSE)&&closes==2);
    assert(version_calls==2);puts("actual transport adapter lifecycle/reply bounds pass");return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="dg-transport-test-") as directory:
    temp = Path(directory)
    (temp / "windows.h").write_text(header)
    (temp / "test.cpp").write_text(source)
    for compiler in ("clang++", "g++"):
        executable = temp / compiler
        subprocess.run([compiler, "-std=c++23", "-fno-exceptions", "-fno-rtti", "-Wall", "-Wextra", "-Werror",
                        "-fsanitize=address,undefined", "-I"+str(temp), "-I"+str(root),
                        "-I"+str(root.parent / "nt/include"), str(temp / "test.cpp"), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
