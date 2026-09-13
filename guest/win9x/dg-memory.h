/* SPDX-License-Identifier: GPL-2.0-or-later
 * Flat C ABI for checked Win32 private-buffer spans and page ownership.
 * VMM callbacks never unwind; caller supplies a stable service table/context.
 * LockPages returns a kernel alias and its mapped address, never user storage.
 * Calls finish synchronously; no pin or callback escapes the copy transaction.
 */
#ifndef DG9_MEMORY_H
#define DG9_MEMORY_H
#define DG9_MEMORY_MAX_BYTES (65536UL + 48UL)
#define DG9_MEMORY_MAX_PAGES 18
#define DG9_MEMORY_BASE 0x4a4d0000UL
#define DG9_MEMORY_MASK 0xffff0000UL
#define DG9_MEMORY_COPY (DG9_MEMORY_BASE | 1)
#ifndef DREAMGPU_CDECL
#if defined(__WATCOMC__)
#define DREAMGPU_CDECL __cdecl
#elif defined(__i386__)
#define DREAMGPU_CDECL __attribute__((cdecl))
#else
#define DREAMGPU_CDECL
#endif
#endif
typedef struct {
    DWORD First, Pages, Offset, Bytes;
} DG9_USER_SPAN;
typedef struct {
    DWORD Alias, Pages;
    void *Address;
} DG9_USER_LOCK;
typedef struct {
    DWORD(DREAMGPU_CDECL *CheckPages)(void *, DWORD, DWORD);
    DWORD(DREAMGPU_CDECL *LockPages)(void *, DWORD, DWORD, void **);
    int(DREAMGPU_CDECL *CopyPtes)(void *, DWORD, DWORD, DWORD *);
    void(DREAMGPU_CDECL *UnlockPages)(void *, DWORD, DWORD);
    void *(DREAMGPU_CDECL *Allocate)(void *, DWORD);
    void(DREAMGPU_CDECL *Free)(void *, void *);
} DG9_MEMORY_SERVICES;
#ifdef __cplusplus
extern "C" {
#endif
int DREAMGPU_CDECL Dg9UserSpan(DWORD, DWORD, DG9_USER_SPAN *);
int DREAMGPU_CDECL Dg9UserPtes(const DWORD *, DWORD, int);
int DREAMGPU_CDECL DreamGpuLockUser(const DG9_MEMORY_SERVICES *, void *, DWORD, DWORD, int,
                                    DG9_USER_LOCK *);
void DREAMGPU_CDECL DreamGpuUnlockUser(const DG9_MEMORY_SERVICES *, void *, DG9_USER_LOCK *);
DWORD DREAMGPU_CDECL DreamGpuCopyUser(const DG9_MEMORY_SERVICES *, void *, DWORD, DWORD, DWORD,
                                      DWORD);
#ifdef __cplusplus
}
#endif
#endif
