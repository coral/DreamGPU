/* SPDX-License-Identifier: GPL-2.0-or-later
 * Minimal Win32 declarations for actual-source lifecycle tests. The separate
 * MinGW build checks the production platform headers and calling convention. */
#ifndef JGL_TEST_WINDOWS_H
#define JGL_TEST_WINDOWS_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef uint32_t ULONG, DWORD, UINT;
typedef int32_t LONG;
typedef uintptr_t ULONG_PTR;
typedef int BOOL;
typedef unsigned char BYTE;
typedef void *HANDLE, *HDC, *HWND, *HGLRC, *HINSTANCE, *LPVOID;
typedef const char *LPCSTR;
typedef char *LPSTR;
typedef struct {
    int Locked;
} CRITICAL_SECTION;
typedef struct {
    LONG left, top, right, bottom;
} RECT;
typedef struct {
    uint16_t nSize, nVersion;
    DWORD dwFlags;
    BYTE iPixelType, cColorBits, cRedBits, cRedShift, cGreenBits, cGreenShift;
    BYTE cBlueBits, cBlueShift, cAlphaBits, cAlphaShift, cAccumBits;
    BYTE cAccumRedBits, cAccumGreenBits, cAccumBlueBits, cAccumAlphaBits;
    BYTE cDepthBits, cStencilBits, cAuxBuffers, iLayerType, bReserved;
    DWORD dwLayerMask, dwVisibleMask, dwDamageMask;
} PIXELFORMATDESCRIPTOR;
#define WINAPI
#define APIENTRY
#define TRUE 1
#define FALSE 0
#define TLS_OUT_OF_INDEXES UINT32_MAX
#define HEAP_ZERO_MEMORY 8
#define DLL_PROCESS_ATTACH 1
#define DLL_PROCESS_DETACH 0
#define DLL_THREAD_DETACH 3
#define ERROR_INVALID_WINDOW_HANDLE 1400
#define ERROR_NOT_ENOUGH_MEMORY 8
#define ERROR_INVALID_HANDLE 6
#define ERROR_BUSY 170
#define ERROR_BAD_LENGTH 24
#define ERROR_WRITE_FAULT 29
#define ERROR_GEN_FAILURE 31
#define PFD_DRAW_TO_WINDOW 4
#define PFD_DRAW_TO_BITMAP 8
#define PFD_SUPPORT_OPENGL 32
#define PFD_DOUBLEBUFFER 1
#define PFD_STEREO 2
#define PFD_TYPE_RGBA 0
#define PFD_MAIN_PLANE 0
#define ZeroMemory(p, n) memset((p), 0, (n))
#define CopyMemory(d, s, n) memcpy((d), (s), (n))
#define min(a, b) ((a) < (b) ? (a) : (b))
void *TlsGetValue(DWORD);
BOOL TlsSetValue(DWORD, void *);
DWORD TlsAlloc(void);
BOOL TlsFree(DWORD);
void InitializeCriticalSection(CRITICAL_SECTION *);
void DeleteCriticalSection(CRITICAL_SECTION *);
void EnterCriticalSection(CRITICAL_SECTION *);
void LeaveCriticalSection(CRITICAL_SECTION *);
LONG InterlockedExchange(volatile LONG *, LONG);
HANDLE GetProcessHeap(void);
void *HeapAlloc(HANDLE, DWORD, size_t);
BOOL HeapFree(HANDLE, DWORD, void *);
DWORD GetCurrentThreadId(void);
DWORD GetCurrentProcessId(void);
void SetLastError(DWORD);
HWND WindowFromDC(HDC);
DWORD GetWindowThreadProcessId(HWND, DWORD *);
BOOL GetClientRect(HWND, RECT *);
HDC GetDC(HWND);
int ReleaseDC(HWND, HDC);
BOOL IsWindow(HWND);
int ExtEscape(HDC, int, int, LPCSTR, int, LPSTR);
int DrawEscape(HDC, int, int, LPCSTR);
HANDLE GetPropA(HWND, LPCSTR);
BOOL SetPropA(HWND, LPCSTR, HANDLE);
#ifdef __cplusplus
}
#endif
#endif
