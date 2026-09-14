/* SPDX-License-Identifier: GPL-2.0-or-later
 * Context-free fixed-size Win32 control. All caller buffers stay pinned while
 * the thread sleeps on the dedicated display IRQ; no GL owner or lock exists.
 * Included after dg-memory-vxd.c and dg-gl-vxd.c in the locked VxD segment.
 */
#define DG_TIMING_U32 DWORD
#include "display-timing.h"
#undef DG_TIMING_U32
static volatile DWORD dg_timing_wait, dg_timing_pending;
static DWORD dg_timing_sequence;

void Dg9TimingSignal(void) {
    DWORD pending = 0;
    // clang-format off: Watcom instruction boundaries.
    _asm {
        xor eax, eax
        xchg eax, [dg_timing_pending]
        mov [pending], eax
    }
    // clang-format on
    if (pending && dg_timing_wait) Signal_Semaphore(dg_timing_wait);
}
static void __declspec(naked) Dg9TimingTimeout(void) {
    // clang-format off: Watcom instruction boundaries.
    _asm {
        pushad
        call Dg9TimingSignal
        popad
        ret
    }
    // clang-format on
}
static BOOL Dg9TimingSupported(void) {
    return DgVxdRegisters() && (Dg9GlRead(NULL, DG_REG_CAPS) & DG_CAP_DISPLAY_TIMING) &&
           Dg9GlRead(NULL, DG_TIMING_REG_VERSION) == DG_TIMING_VERSION;
}
BOOL Dg9TimingSetRate(DWORD rate) {
    if (!rate || rate == 0xffffffffUL)
        rate = DG_TIMING_DEFAULT_HZ;
    if (!DgTimingRateValid(rate))
        return FALSE;
    if (!Dg9TimingSupported())
        return rate == DG_TIMING_DEFAULT_HZ;
    Dg9GlWrite(DG_TIMING_REG_RATE, rate);
    return Dg9GlRead(NULL, DG_TIMING_REG_RATE) == rate;
}
void Dg9TimingShutdown(void) {
    if (Dg9TimingSupported())
        Dg9GlWrite(DG_TIMING_REG_COMMAND, DG_TIMING_CANCEL);
    Dg9TimingSignal();
}
static BOOL Dg9TimingControl(struct DIOCParams *params, DWORD *result) {
    DG9_USER_LOCK input = {0}, output = {0}, returned = {0};
    DG_TIMING_REQUEST request;
    DG_TIMING_REPLY reply = {0};
    DWORD timer = 0, serial, phase, count, i, flags;
    if (params->dwIoControlCode != DG_TIMING_ESCAPE)
        return FALSE;
    *result = 87;
    if (params->lpOverlapped || params->cbInBuffer != sizeof(request) ||
        params->cbOutBuffer != sizeof(reply) || Dg9GlCritical())
        return TRUE;
    if (!DreamGpuLockUser(&dg_memory_services, NULL, params->lpInBuffer, sizeof(request), 0,
                          &input) ||
        !DreamGpuLockUser(&dg_memory_services, NULL, params->lpOutBuffer, sizeof(reply), 1,
                          &output) ||
        !DreamGpuLockUser(&dg_memory_services, NULL, params->lpcbBytesReturned, 4, 1, &returned))
        goto cleanup;
    memcpy(&request, input.Address, sizeof(request));
    if (request.Version != DG_TIMING_VERSION || request.Operation > DG_TIMING_WAIT_END ||
        request.Reserved0 || request.Reserved1)
        goto cleanup;
    reply.Version = DG_TIMING_VERSION;
    reply.Status = DG_TIMING_REPLY_UNSUPPORTED;
    if (!Dg9TimingSupported())
        goto publish;
    /* One waiter owns the sequence. Refuse a concurrent request explicitly;
     * never wait holding the Win16/global critical section or any GL mutex. */
    flags = Dg9GlDisableInterrupts();
    if (dg_timing_wait) {
        Dg9GlRestoreInterrupts(flags);
        *result = 170;
        goto cleanup;
    }
    dg_timing_wait = 1; /* Reserved before any yielding VMM allocation. */
    Dg9GlRestoreInterrupts(flags);
    dg_timing_wait = Create_Semaphore(0);
    if (!dg_timing_wait) {
        *result = 8;
        goto cleanup;
    }
    reply.Status = DG_TIMING_REPLY_OK;
    if (request.Operation) {
        if (!++dg_timing_sequence)
            ++dg_timing_sequence;
        Dg9GlWrite(DG_REG_IRQ_STATUS, DG_IRQ_DISPLAY_TIMING);
        Dg9GlWrite(DG_TIMING_REG_SEQUENCE, dg_timing_sequence);
        dg_timing_pending = 1;
        timer = Set_Async_Time_Out(250, 0, Dg9TimingTimeout);
        if (!timer) {
            reply.Status = DG_TIMING_REPLY_TIMEOUT;
        } else {
            Dg9GlWrite(DG_TIMING_REG_COMMAND, request.Operation);
            Wait_Semaphore(dg_timing_wait, 0);
            if (Dg9GlRead(NULL, DG_TIMING_REG_COMPLETED) != dg_timing_sequence ||
                Dg9GlRead(NULL, DG_TIMING_REG_STATUS) != DG_TIMING_DONE)
                reply.Status = Dg9GlRead(NULL, DG_TIMING_REG_STATUS) == DG_TIMING_CANCELLED
                                   ? DG_TIMING_REPLY_CANCELLED
                                   : DG_TIMING_REPLY_TIMEOUT;
        }
        if (reply.Status != DG_TIMING_REPLY_OK)
            Dg9GlWrite(DG_TIMING_REG_COMMAND, DG_TIMING_CANCEL);
    }
    for (i = 0; i < 3; ++i) {
        serial = Dg9GlRead(NULL, DG_TIMING_REG_SNAPSHOT);
        reply.ScanLine = Dg9GlRead(NULL, DG_TIMING_REG_SCANLINE);
        reply.Height = Dg9GlRead(NULL, DG_TIMING_REG_HEIGHT);
        phase = Dg9GlRead(NULL, DG_TIMING_REG_PHASE);
        reply.RateHz = phase >> 16;
        reply.InVBlank = phase & 1;
        reply.UntilBeginNs = Dg9GlRead(NULL, DG_TIMING_REG_BEGIN_NS);
        reply.UntilEndNs = Dg9GlRead(NULL, DG_TIMING_REG_END_NS);
        if (serial == Dg9GlRead(NULL, DG_TIMING_REG_SERIAL))
            break;
    }
    if (i == 3)
        reply.Status = DG_TIMING_REPLY_CANCELLED;
    if (timer)
        Cancel_Time_Out(timer);
    flags = Dg9GlDisableInterrupts();
    dg_timing_pending = 0;
    Destroy_Semaphore(dg_timing_wait);
    dg_timing_wait = 0;
    Dg9GlRestoreInterrupts(flags);
publish:
    count = sizeof(reply);
    memcpy(output.Address, &reply, sizeof(reply));
    memcpy(returned.Address, &count, sizeof(count));
    *result = 0;
cleanup:
    DreamGpuUnlockUser(&dg_memory_services, NULL, &returned);
    DreamGpuUnlockUser(&dg_memory_services, NULL, &output);
    DreamGpuUnlockUser(&dg_memory_services, NULL, &input);
    return TRUE;
}
