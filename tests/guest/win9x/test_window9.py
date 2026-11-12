#!/usr/bin/env python3
"""Actual Win98 GL window frontend with the actual 16-bit DDI classifier.

transport9-window.cpp runs against a GDI model whose compatible bitmaps take
the desktop depth; its tag is read back by dg-window16.c at 16 and 32 bpp."""
import os
from pathlib import Path
import subprocess
import tempfile
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
header = r'''
#ifndef TEST_WINDOWS_H
#define TEST_WINDOWS_H
#include <stdint.h>
#include <string.h>
typedef uint16_t WORD;
typedef uint32_t DWORD, ULONG;
typedef int32_t LONG;
typedef int BOOL;
typedef uintptr_t ULONG_PTR;
typedef void *HANDLE;
typedef struct MockDc *HDC;
typedef struct MockWindow *HWND;
typedef struct MockObject *HGDIOBJ, *HBITMAP, *HRGN;
typedef const char *LPCSTR;
typedef char *LPSTR;
typedef struct { LONG left, top, right, bottom; } RECT;
typedef struct { LONG x, y; } POINT;
#define TRUE 1
#define FALSE 0
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define HGDI_ERROR ((HGDIOBJ)(intptr_t)-1)
#define BITSPIXEL 12
#define MM_TEXT 1
#define ERROR 0
#define SRCCOPY 0x00CC0020UL
#define DCX_CACHE 0x2
#define DCX_CLIPCHILDREN 0x8
#define DCX_CLIPSIBLINGS 0x10
#define ZeroMemory(p, n) memset((p), 0, (n))
#ifdef __cplusplus
extern "C" {
#endif
int ExtEscape(HDC, int, int, LPCSTR, int, LPSTR);
int DrawEscape(HDC, int, int, LPCSTR);
BOOL DeviceIoControl(HANDLE, DWORD, void *, DWORD, void *, DWORD, DWORD *, void *);
HWND WindowFromDC(HDC);
DWORD GetWindowThreadProcessId(HWND, DWORD *);
DWORD GetCurrentProcessId(void);
BOOL GetClientRect(HWND, RECT *);
int GetDeviceCaps(HDC, int);
HDC CreateCompatibleDC(HDC);
HBITMAP CreateCompatibleBitmap(HDC, int, int);
HGDIOBJ SelectObject(HDC, HGDIOBJ);
LONG SetBitmapBits(HBITMAP, DWORD, const void *);
BOOL DeleteObject(HGDIOBJ);
BOOL DeleteDC(HDC);
BOOL SetPropA(HWND, LPCSTR, HANDLE);
HANDLE GetPropA(HWND, LPCSTR);
HANDLE RemovePropA(HWND, LPCSTR);
int wsprintfA(LPSTR, LPCSTR, ...);
HDC GetDCEx(HWND, HRGN, DWORD);
int ReleaseDC(HWND, HDC);
HRGN CreateRectRgn(int, int, int, int);
int GetClipRgn(HDC, HRGN);
int SelectClipRgn(HDC, HRGN);
int GetMapMode(HDC);
BOOL GetViewportOrgEx(HDC, POINT *);
BOOL GetWindowOrgEx(HDC, POINT *);
BOOL BitBlt(HDC, int, int, int, int, HDC, int, int, DWORD);
DWORD GetLastError(void);
void Sleep(DWORD);
DWORD GetTickCount(void);
#ifdef __cplusplus
}
#endif
#endif
'''
with tempfile.TemporaryDirectory(prefix="dreamgpu-window9-") as temporary:
    temp = Path(temporary)
    (temp / "windows.h").write_text(header)
    sanitize = ["-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g"]
    warnings = ["-Wall", "-Wextra", "-Werror"]
    for cc, cxx in (("clang", os.environ.get("CXX", "clang++")), ("gcc", "g++")):
        ddi, frontend, out = temp / (cc + "-ddi.o"), temp / (cc + "-frontend.o"), temp / cc
        subprocess.run([cc, "-std=c99", *warnings, *sanitize, "-I" + str(ROOT / "guest/win9x"),
                        "-c", str(HERE / "window9-ddi.c"), "-o", str(ddi)], check=True)
        subprocess.run([cxx, "-std=c++23", "-fno-exceptions", "-fno-rtti", *warnings, *sanitize,
                        "-I" + str(temp), "-I" + str(ROOT / "guest/opengl"),
                        "-I" + str(ROOT / "guest/nt/include"), "-c", str(HERE / "window9-test.cpp"),
                        "-o", str(frontend)], check=True)
        subprocess.run([cxx, *sanitize, str(frontend), str(ddi), "-o", str(out)], check=True)
        subprocess.run([str(out)], check=True)
