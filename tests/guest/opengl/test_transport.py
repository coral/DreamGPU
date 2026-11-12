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
DWORD GetLastError(void);
void Sleep(DWORD);
DWORD GetTickCount(void);
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
/* Win9x VxD refusals: the next `refusals` calls fail with `refusal_error`
 * before touching output or count, exactly as the VxD refuses. */
static DWORD refusals, refusal_error, last_error, refused_calls, yields, sleeps, tick, tick_step=1;
DWORD GetLastError(void) { return last_error; }
void Sleep(DWORD ms) { assert(ms<=1); if(ms){++sleeps;tick+=tick_step;}else ++yields; }
DWORD GetTickCount(void) { return tick; }
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
    if (refusals) { --refusals; ++refused_calls; last_error=refusal_error; return FALSE; }
    ++io_calls; memcpy(output,&fixture,sizeof(fixture)); *returned=returned_fixture;
    if (!io_ok) last_error=31; /* ERROR_GEN_FAILURE */
    return io_ok;
}
int ExtEscape(HDC display,int escape,int in_bytes,LPCSTR input,int out_bytes,LPSTR output)
{ (void)display;(void)in_bytes;(void)input;(void)out_bytes;(void)output; assert(escape==DG_ESCAPE);++nt_calls;return 1; }
#include "transport.cpp"
static DG_ESCAPE_REQUEST request;
static struct { DG_ESCAPE_REPLY reply; unsigned char data[512]; } output;
static int call(DWORD operation)
{ request.Operation=operation; return JglTransportRequest(NULL,sizeof(request),(LPCSTR)&request,sizeof(output),(LPSTR)&output); }
static void refuse(DWORD count, DWORD error, DWORD start=1000, DWORD step=1)
{ refusals=count; refusal_error=error; refused_calls=yields=sleeps=0; tick=start; tick_step=step; io_calls=0; }
/* Only DG9_ERROR_BUSY is resent (dg-refusal9.h): a few yields, then 1 ms
 * sleeps, capped near 1.5 s; the request reaches the VxD once it is accepted. */
static void refusal(void)
{
    refuse(3,DG9_ERROR_BUSY);assert(call(DG_ESCAPE_SUBMIT)&&refused_calls==3&&io_calls==1&&yields==3&&!sleeps);
    /* The diagnostic run's worst observed burst: 51 refusals. */
    refuse(51,DG9_ERROR_BUSY);assert(call(DG_ESCAPE_SUBMIT)&&refused_calls==51&&io_calls==1);
    assert(yields==JGL9_REFUSAL_YIELDS&&sleeps==51-JGL9_REFUSAL_YIELDS);
    /* Persistent refusal fails as before, once the time cap passes. */
    refuse(~0u,DG9_ERROR_BUSY);assert(!call(DG_ESCAPE_SUBMIT)&&!io_calls&&GetLastError()==DG9_ERROR_BUSY);
    assert(tick-1000>=JGL9_REFUSAL_LIMIT_MS&&tick-1000<JGL9_REFUSAL_LIMIT_MS+2&&yields==JGL9_REFUSAL_YIELDS);
    /* Across GetTickCount wraparound, and with a coarse 55 ms Sleep(1). */
    refuse(~0u,DG9_ERROR_BUSY,0xffffff00u);assert(!call(DG_ESCAPE_SUBMIT)&&!io_calls);
    assert(tick-0xffffff00u>=JGL9_REFUSAL_LIMIT_MS&&tick-0xffffff00u<JGL9_REFUSAL_LIMIT_MS+2);
    refuse(~0u,DG9_ERROR_BUSY,1000,55);assert(!call(DG_ESCAPE_SUBMIT)&&refused_calls<JGL9_REFUSAL_YIELDS+30);
    assert(tick-1000>=JGL9_REFUSAL_LIMIT_MS&&tick-1000<JGL9_REFUSAL_LIMIT_MS+55);
    /* A stalled tick count still ends at the retry backstop. */
    refuse(~0u,DG9_ERROR_BUSY,1000,0);assert(!call(DG_ESCAPE_SUBMIT)&&refused_calls==JGL9_REFUSAL_RETRIES+1);
    /* Validation and every other error: no resend, no sleep. */
    refuse(1,DG9_ERROR_INVALID);assert(!call(DG_ESCAPE_SUBMIT)&&refused_calls==1&&!io_calls&&!yields&&!sleeps);
    assert(GetLastError()==DG9_ERROR_INVALID);
    refuse(1,5);assert(!call(DG_ESCAPE_SUBMIT)&&refused_calls==1&&!yields&&!sleeps);
    refusals=0;
}
int main(void)
{
    fixture.Version=DG_ESCAPE_VERSION;fixture.Status=DG_ESCAPE_OK;returned_fixture=sizeof(fixture);
    version=0;JglTransportInitialize();assert(call(DG_ESCAPE_OPEN)==1);assert(nt_calls==1&&!opens&&!io_calls);
    version=0x80000004;JglTransportInitialize();assert(version_calls==2);
    assert(!call(DG_ESCAPE_SUBMIT)&&!opens&&!io_calls);
    create_ok=FALSE;assert(!call(DG_ESCAPE_OPEN)&&opens==1&&!io_calls);create_ok=TRUE;
    assert(call(DG_ESCAPE_OPEN)&&opens==2&&io_calls==1);
    assert(call(DG_ESCAPE_SUBMIT)&&opens==2&&io_calls==2);
    refusal();
    io_calls=2;
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
    /* A refused OPEN keeps its one handle and is resent on it, not reopened;
     * a refused CLOSE keeps the handle until the VxD accepts the close. */
    refuse(2,DG9_ERROR_BUSY);assert(call(DG_ESCAPE_OPEN)&&opens==4&&io_calls==1&&refused_calls==2);
    refuse(2,DG9_ERROR_BUSY);assert(call(DG_ESCAPE_CLOSE)&&closes==3&&io_calls==1);
    refuse(~0u,DG9_ERROR_BUSY);assert(!call(DG_ESCAPE_OPEN)&&opens==5&&closes==3);
    refusals=0;assert(call(DG_ESCAPE_OPEN)&&opens==5);assert(call(DG_ESCAPE_CLOSE)&&closes==4);
    assert(version_calls==2);puts("actual transport adapter lifecycle/reply bounds and bounded ERROR_BUSY-only resend pass");return 0;
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
