/* SPDX-License-Identifier: GPL-2.0-or-later
 * Flat scalar ABI for the single serialized Win9x cursor stream. The C++
 * core owns its bounded staging storage; no Win16 pointer crosses this ABI.
 * Layout is copied only after an accepted header. Commit validates the
 * output capacity before changing any DMA bytes.
 */
#ifndef DREAMGPU_CURSOR32_H
#define DREAMGPU_CURSOR32_H
#include "dg-cursor.h"
#ifndef DREAMGPU_CDECL
#if defined(__WATCOMC__)
#define DREAMGPU_CDECL __cdecl
#elif defined(__i386__)
#define DREAMGPU_CDECL __attribute__((cdecl))
#else
#define DREAMGPU_CDECL
#endif
#endif
#ifdef __cplusplus
extern "C" {
#endif
int DREAMGPU_CDECL DreamGpuCursorBegin(DWORD, DWORD, DWORD, DWORD, DG9_CURSOR_LAYOUT *);
int DREAMGPU_CDECL DreamGpuCursorData(DWORD, DWORD, DWORD);
int DREAMGPU_CDECL DreamGpuCursorCommit(DWORD, DWORD, DWORD, DWORD *, DWORD);
void DREAMGPU_CDECL DreamGpuCursorAbort(void);
void DREAMGPU_CDECL DreamGpuCursorReset(void);
DWORD DREAMGPU_CDECL DreamGpuCursorUsed(void);
#ifdef __cplusplus
}
#endif
#endif
