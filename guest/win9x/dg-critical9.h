/* SPDX-License-Identifier: GPL-2.0-or-later
 * May the calling thread sleep in this VxD entry? Shared by dg-gl-vxd.c and
 * dg-vxd.c (separate Watcom translation units); the policy is plain C so the
 * host tests run it unchanged.
 *
 * The Win95+ VMM critical section is owned by a thread. Every Win32 thread
 * runs in the System VM, so the owner VM from Get_Crit_Section_Status cannot
 * tell the calling thread from another thread of the same game, and its claim
 * count is nonzero whenever ANY thread holds the section. A claim held by a
 * preempted disk/music thread looked exactly like our own, and the GL request
 * was refused. Sleeping is illegal only when this thread owns the critical
 * section (it would sleep holding it) or the VMM is reentered (hardware
 * interrupt, page fault or exception: asynchronous services only). Another
 * thread's claim is ordinary contention and a native driver simply blocks.
 *
 * VMM service semantics relied on (Windows 95 DDK vmm.doc, 1995-06):
 * - Get_VMM_Reenter_Count: ECX = reentry count, zero when not reentered.
 * - Get_Crit_Status_Thread (0x0145, VMM 4.0+): ECX = claim count, EDI =
 *   thread owning the critical section; with zero claims EDI is the current
 *   thread. CF set while the thread is in a time-critical operation such as
 *   hardware interrupt simulation. Uses flags; callable at interrupt time, so
 *   unlike Get_Crit_Section_Status it never blocks to finish delayed releases.
 * - Get_Cur_Thread_Handle (0x0108, VMM 4.0+): EDI = current thread. Uses EDI
 *   and flags; callable at interrupt time.
 * A delayed release may still report this thread as owner. That refusal is
 * transient and returns DG9_ERROR_BUSY (dg-refusal9.h), which callers retry.
 */
#ifndef DG_CRITICAL9_H
#define DG_CRITICAL9_H
typedef struct {
    DWORD Reentered, Claims, Owner, Current, Boost;
} DG9_CRITICAL_STATE;
/* Nonzero when the calling thread must not sleep. Boost (CF) alone is a
 * scheduling priority, not a held critical section; callers that also need
 * to avoid interrupt simulation test it themselves. */
static int Dg9CriticalCannotBlock(const DG9_CRITICAL_STATE *state) {
    return state->Reentered != 0 || (state->Claims != 0 && state->Owner == state->Current);
}
#if defined(__WATCOMC__)
// clang-format off: Watcom assembly uses newline instruction boundaries.
/* Both services return in EDI, which the C code may hold live: save it
 * explicitly, and touch no memory while it is pushed in case Watcom ever
 * addresses these locals relative to ESP (it uses an EBP frame here today).
 * EAX, ECX and EDX are scratch in this ABI. */
static DWORD Dg9CriticalCurrentThread(void) {
    DWORD thread = 0;
    _asm push edi
    VMMCall(Get_Cur_Thread_Handle)
    _asm {
        mov eax, edi
        pop edi
        mov [thread], eax
    }
    return thread;
}
static DWORD Dg9CriticalOwnerThread(DWORD *claims, DWORD *boost) {
    DWORD owner = 0, count = 0, time_critical = 0;
    _asm push edi
    VMMCall(Get_Crit_Status_Thread)
    _asm {
        sbb edx, edx
        mov eax, edi
        pop edi
        mov [owner], eax
        mov [count], ecx
        mov [time_critical], edx
    }
    *claims = count;
    *boost = time_critical != 0;
    return owner;
}
static void Dg9CriticalRead(DG9_CRITICAL_STATE *state) {
    DWORD reentered = 0;
    VMMCall(Get_VMM_Reenter_Count);
    _asm mov [reentered], ecx
    state->Reentered = reentered;
    state->Claims = state->Owner = state->Current = state->Boost = 0;
    if (reentered)
        return; /* already cannot block; keep the original reentry fast path */
    state->Owner = Dg9CriticalOwnerThread(&state->Claims, &state->Boost);
    state->Current = Dg9CriticalCurrentThread();
}
// clang-format on
#endif
#endif
