/* SPDX-License-Identifier: GPL-2.0-or-later
 * Required Watcom boundary: VMM register services, CLI/flags, naked watchdog
 * and IRQ semaphore wakeup. Channel/window policy lives in channel32.cpp.
 */
#include "gpu.h"
#include "gl.h"
#include "channel32.h"
extern volatile DWORD *DgVxdRegisters(void);
extern BOOL Dg9CursorTakeoverSafe(void);
extern BOOL Dg9Primary(DWORD *, DWORD *, DWORD *, DWORD *);
extern void Dg9PrimaryGpuOwned(BOOL);
static DWORD dg_gl_sequence;
static volatile DWORD dg_gl_wait, dg_gl_pending;
/* One bounded boot event permits state-driven fixture logon with the normal
 * driver. No addresses or per-frame diagnostic I/O enter this path. */
static BOOL dg_boot_process_seen;
static void Dg9BootChar(char value) {
    // clang-format off: Watcom assembly uses newline instruction boundaries.
    _asm {
        mov al, [value]
        out 0e9h, al
    }
    // clang-format on
}
static void Dg9BootProcessExit(void) {
    static const char event[] = "DGBOOT process-exit\n";
    unsigned i;
    if (dg_boot_process_seen)
        return;
    dg_boot_process_seen = TRUE;
    for (i = 0; i < sizeof(event) - 1; ++i)
        Dg9BootChar(event[i]);
}
static BOOL Dg9GlCritical(void) {
    DWORD owner = 0, claims = 0, reentered = 0;
    BOOL boosted = FALSE;
    VMMCall(Get_VMM_Reenter_Count);
    // clang-format off: Watcom assembly uses newline instruction boundaries.
    _asm mov [reentered], ecx
                       // clang-format on
                       if (!reentered) boosted = Get_Crit_Section_Status(&owner, &claims);
#ifdef DG_IDENTITY_H
    if (boosted || claims || reentered) {
        DgIdentityText("DGCRITICAL");
        DgIdentityField("owner", owner);
        DgIdentityField("claims", claims);
        DgIdentityField("current", Get_Cur_VM_Handle());
        DgIdentityChar('\n');
        DgIdentityText("DGSCHEDULE");
        DgIdentityField("boost", boosted);
        DgIdentityField("reentered", reentered);
        DgIdentityChar('\n');
    }
#endif
    return claims != 0 || reentered != 0;
}

static DWORD Dg9GlDisableInterrupts(void) {
    DWORD flags = 0;
    // clang-format off: Watcom assembly uses newline instruction boundaries.
    _asm {
        pushfd
        pop eax
        mov [flags], eax
        cli
    }
    // clang-format on
    return flags;
}
static void Dg9GlRestoreInterrupts(DWORD flags) {
    // clang-format off: Watcom assembly uses newline instruction boundaries.
    _asm {
        mov eax, [flags]
        push eax
        popfd
    }
    // clang-format on
}
static ULONG Dg9GlRead(void *ignored, ULONG reg) {
    (void)ignored;
    return DgVxdRegisters()[reg / 4];
}
static void Dg9GlWrite(ULONG reg, ULONG value) {
    DgVxdRegisters()[reg / 4] = value;
}
void Dg9GlSignal(void) {
    DWORD pending = 0;
    // clang-format off: Watcom assembly uses newline instruction boundaries.
    _asm {
        xor eax, eax
        xchg eax, [dg_gl_pending]
        mov [pending], eax
    }
    // clang-format on
    if (pending && dg_gl_wait) Signal_Semaphore(dg_gl_wait);
}
static void __declspec(naked) Dg9GlTimeout(void) {
    // clang-format off: Watcom assembly uses newline instruction boundaries.
    _asm {
        pushad
        call Dg9GlSignal
        popad
        ret
    }
    // clang-format on
}
static ULONG __cdecl Dg9Submit(ULONG physical, ULONG bytes, DG_ESCAPE_REPLY *reply) {
    ULONG sequence, status, timer;
    if ((Dg9GlRead(NULL, DG_GL_REG_STATUS) & DG_STATUS_BUSY))
        return DG_ESCAPE_STOPPED;
    dg_gl_wait = Create_Semaphore(0);
    if (!dg_gl_wait)
        return DG_ESCAPE_RESOURCES;
    sequence = ++dg_gl_sequence;
    if (!sequence)
        sequence = ++dg_gl_sequence;
    Dg9GlWrite(DG_GL_REG_ADDR_LO, physical);
    Dg9GlWrite(DG_GL_REG_ADDR_HI, 0);
    Dg9GlWrite(DG_GL_REG_BYTES, bytes);
    Dg9GlWrite(DG_GL_REG_SEQUENCE, sequence);
    Dg9GlWrite(DG_GL_REG_GENERATION, reply->Generation);
    Dg9GlWrite(DG_REG_IRQ_STATUS, DG_IRQ_GL_COMPLETION);
    dg_gl_pending = 1;
    timer = Set_Async_Time_Out(2000, 0, Dg9GlTimeout);
    if (!timer) {
        dg_gl_pending = 0;
        Destroy_Semaphore(dg_gl_wait);
        dg_gl_wait = 0;
        return DG_ESCAPE_RESOURCES;
    }
    Dg9GlWrite(DG_GL_REG_SUBMIT, 1);
    status = Dg9GlRead(NULL, DG_GL_REG_STATUS);
    if ((status & DG_STATUS_BUSY) || Dg9GlRead(NULL, DG_GL_REG_COMPLETED) != sequence)
        Wait_Semaphore(dg_gl_wait, 0);
    Cancel_Time_Out(timer);
    dg_gl_pending = 0;
    Destroy_Semaphore(dg_gl_wait);
    dg_gl_wait = 0;
    status = Dg9GlRead(NULL, DG_GL_REG_STATUS);
    if ((status & DG_STATUS_BUSY) || Dg9GlRead(NULL, DG_GL_REG_COMPLETED) != sequence) {
        /* A failed GL waiter must not reset another client's 2D/3D state. */
        return DG_ESCAPE_TIMEOUT;
    }
    reply->CompletedSequence = sequence;
    reply->DeviceError = status & DG_STATUS_ERROR ? Dg9GlRead(NULL, DG_GL_REG_ERROR) : 0;
    return status & DG_STATUS_ERROR ? DG_ESCAPE_HOST : DG_ESCAPE_OK;
}

static volatile DWORD *__cdecl Dg9ApiRegisters(void) {
    return DgVxdRegisters();
}
static DWORD __cdecl Dg9ApiRead(DWORD reg) {
    return Dg9GlRead(NULL, reg);
}
static void __cdecl Dg9ApiWrite(DWORD reg, DWORD value) {
    Dg9GlWrite(reg, value);
}
static int __cdecl Dg9ApiCritical(void) {
    return Dg9GlCritical();
}
static DWORD __cdecl Dg9ApiDisable(void) {
    return Dg9GlDisableInterrupts();
}
static void __cdecl Dg9ApiRestore(DWORD flags) {
    Dg9GlRestoreInterrupts(flags);
}
static DWORD __cdecl Dg9ApiContext(void) {
    return (DWORD)_GetCurrentContext();
}
static DWORD __cdecl Dg9ApiSemaphore(DWORD count) {
    return Create_Semaphore(count);
}
static void __cdecl Dg9ApiDestroy(DWORD semaphore) {
    Destroy_Semaphore(semaphore);
}
static void __cdecl Dg9ApiWait(DWORD semaphore) {
    Wait_Semaphore(semaphore, 0);
}
static void __cdecl Dg9ApiSignalSemaphore(DWORD semaphore) {
    Signal_Semaphore(semaphore);
}
static void *__cdecl Dg9ApiAllocate(DWORD pages, DWORD *physical) {
    return (void *)_PageAllocate(pages, PG_SYS, 0, 0, PAGE_ALLOC_MIN, PAGE_ALLOC_MAX, physical,
                                 PAGECONTIG | PAGEFIXED | PAGEUSEALIGN | PAGEZEROINIT);
}
static void __cdecl Dg9ApiFree(void *address) {
    _PageFree(address, 0);
}
static int __cdecl Dg9ApiPending(void) {
    return dg_gl_wait != 0;
}
static void __cdecl Dg9ApiSignal(void) {
    Dg9GlSignal();
}
static int __cdecl Dg9ApiCursor(void) {
    return Dg9CursorTakeoverSafe();
}
static int __cdecl Dg9ApiPrimary(DWORD *w, DWORD *h, DWORD *s, DWORD *o) {
    return Dg9Primary(w, h, s, o);
}
static void __cdecl Dg9ApiPrimaryOwned(int owned) {
    Dg9PrimaryGpuOwned(owned);
}
static void __cdecl Dg9ApiFatal(DWORD status) {
    Dg9GlWrite(DG_GL_REG_FAULT_STOP, status == DG_ESCAPE_TIMEOUT ? DG_GL_FAULT_DRIVER_TIMEOUT
                                                                 : DG_GL_FAULT_DRIVER_INTERNAL);
    /* A void DIB access cannot return into stale primary memory. */
    for (;;) {
        _asm { cli }
        _asm {
            hlt
        }
    }
}
#ifdef DG_IDENTITY_H
static void __cdecl Dg9ApiTraceText(const char *s) {
    DgIdentityText(s);
}
static void __cdecl Dg9ApiTraceField(const char *s, DWORD value) {
    DgIdentityField(s, value);
}
static void __cdecl Dg9ApiTraceChar(char c) {
    DgIdentityChar(c);
}
#endif
static const DG9_CHANNEL_SERVICES dg_channel_services = {
    Dg9ApiRegisters,       Dg9ApiRead,       Dg9ApiWrite,     Dg9ApiCritical,     Dg9ApiDisable,
    Dg9ApiRestore,         Dg9ApiContext,    Dg9ApiSemaphore, Dg9ApiDestroy,      Dg9ApiWait,
    Dg9ApiSignalSemaphore, Dg9ApiAllocate,   Dg9ApiFree,      Dg9Submit,          Dg9ApiPending,
    Dg9ApiSignal,          Dg9ApiCursor,     Dg9ApiPrimary,   Dg9ApiPrimaryOwned, Dg9ApiFatal,
#ifdef DG_IDENTITY_H
    Dg9ApiTraceText,       Dg9ApiTraceField, Dg9ApiTraceChar,
#else
    NULL,NULL,NULL,
#endif
    &dg_memory_services};
void Dg9ChannelInitializeCallbacks(void) {
    DreamGpuChannelBind(&dg_channel_services);
}
static void Dg9ChannelParams(struct DIOCParams *source, DG9_CHANNEL_CONTROL *target) {
    target->dwIoControlCode = source->dwIoControlCode;
    target->lpOverlapped = source->lpOverlapped;
    target->cbInBuffer = source->cbInBuffer;
    target->cbOutBuffer = source->cbOutBuffer;
    target->lpInBuffer = source->lpInBuffer;
    target->lpOutBuffer = source->lpOutBuffer;
    target->lpcbBytesReturned = source->lpcbBytesReturned;
    target->tagProcess = source->tagProcess;
    target->hDevice = source->hDevice;
    target->CloseHandle = source->dwIoControlCode == DIOC_CLOSEHANDLE;
}
static BOOL Dg9GlControl(struct DIOCParams *params, DWORD *result) {
    DG9_CHANNEL_CONTROL input;
    if (!DgVxdRegisters())
        return FALSE;
    Dg9ChannelParams(params, &input);
    return DreamGpuGlControl(&input, result);
}
void Dg9GlShutdown(void) {
    DreamGpuGlShutdown();
}
void Dg9PrimaryAccess(void) {
    DreamGpuPrimaryAccess();
}
void Dg9PrimaryFault(void) {
    DreamGpuPrimaryFault();
}
