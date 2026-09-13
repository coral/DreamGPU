/* SPDX-License-Identifier: GPL-2.0-or-later
 * Flat cdecl-only VMM boundary. No register-convention functions, by-value
 * aggregates, Windows headers or pageable user pointers enter the C++ core.
 * Bind once before device IRQs are enabled; the table outlives the device.
 */
#ifndef DREAMGPU_CHANNEL32_H
#define DREAMGPU_CHANNEL32_H
#include "dg-memory.h"
#include "dg-escape.h"
typedef struct {
    DWORD dwIoControlCode, lpOverlapped, cbInBuffer, cbOutBuffer;
    DWORD lpInBuffer, lpOutBuffer, lpcbBytesReturned, tagProcess, hDevice;
    int CloseHandle;
} DG9_CHANNEL_CONTROL;
typedef struct {
    DWORD Client_EDI, Client_EBX, Client_ECX, Client_ESI;
} DG9_CHANNEL_BLT;
typedef struct {
    volatile DWORD *(DREAMGPU_CDECL *Registers)(void);
    DWORD(DREAMGPU_CDECL *Read)(DWORD);
    void(DREAMGPU_CDECL *Write)(DWORD, DWORD);
    int(DREAMGPU_CDECL *Critical)(void);
    DWORD(DREAMGPU_CDECL *DisableInterrupts)(void);
    void(DREAMGPU_CDECL *RestoreInterrupts)(DWORD);
    DWORD(DREAMGPU_CDECL *CurrentContext)(void);
    DWORD(DREAMGPU_CDECL *CreateSemaphore)(DWORD);
    void(DREAMGPU_CDECL *DestroySemaphore)(DWORD);
    void(DREAMGPU_CDECL *WaitSemaphore)(DWORD);
    void(DREAMGPU_CDECL *SignalSemaphore)(DWORD);
    void *(DREAMGPU_CDECL *Allocate)(DWORD, DWORD *);
    void(DREAMGPU_CDECL *Free)(void *);
    DWORD(DREAMGPU_CDECL *Submit)(DWORD, DWORD, DG_ESCAPE_REPLY *);
    int(DREAMGPU_CDECL *WaitPending)(void);
    void(DREAMGPU_CDECL *Signal)(void);
    int(DREAMGPU_CDECL *CursorSafe)(void);
    int(DREAMGPU_CDECL *Primary)(DWORD *, DWORD *, DWORD *, DWORD *);
    void(DREAMGPU_CDECL *PrimaryOwned)(int);
    void(DREAMGPU_CDECL *Fatal)(DWORD);
    void(DREAMGPU_CDECL *TraceText)(const char *);
    void(DREAMGPU_CDECL *TraceField)(const char *, DWORD);
    void(DREAMGPU_CDECL *TraceChar)(char);
    const DG9_MEMORY_SERVICES *Memory;
} DG9_CHANNEL_SERVICES;
#ifdef __cplusplus
extern "C" {
#endif
void DREAMGPU_CDECL DreamGpuChannelBind(const DG9_CHANNEL_SERVICES *);
int DREAMGPU_CDECL DreamGpuGlControl(const DG9_CHANNEL_CONTROL *, DWORD *);
int DREAMGPU_CDECL DreamGpuWindowControl(const DG9_CHANNEL_CONTROL *, DWORD *);
unsigned short DREAMGPU_CDECL DreamGpuWindowBlt(const DG9_CHANNEL_BLT *);
void DREAMGPU_CDECL DreamGpuPrimaryAccess(void);
void DREAMGPU_CDECL DreamGpuPrimaryFault(void);
void DREAMGPU_CDECL DreamGpuGlShutdown(void);
#ifdef __cplusplus
}
#endif
#endif
