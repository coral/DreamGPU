/* SPDX-License-Identifier: GPL-2.0-or-later
 * Included after vxd_vbe.c. Source-built Win9x bring-up for the native 2D ABI.
 * One locked DMA command, a shared hardware IRQ, and a sleeping semaphore
 * wait. The one-shot watchdog bounds a broken/masked IRQ; no status poll loop.
 */
#include "vpicd.h"
#include "gl.h"
extern void Dg9GlSignal(void);
extern void Dg9GlShutdown(void);

#include "blt32.h"

static volatile DWORD *dg_registers;
static DG9_COMMAND *dg_command;
static DWORD dg_physical, dg_irq, dg_sequence;
static DWORD dg_submit_flags;
static volatile DWORD dg_semaphore, dg_pending;
static BOOL dg_failed;
static VPICD_IRQ_Descriptor dg_irq_descriptor;

static DWORD DgRead(DWORD offset) {
    volatile DWORD *address = dg_registers + offset / 4;
    DWORD value = 0;
    // clang-format off: Watcom assembly uses newline instruction boundaries.
    _asm {
        mov edx, [address]
        mov eax, dword ptr [edx]
        mov [value], eax
    }
    // clang-format on
    return value;
}

static void DgWrite(DWORD offset, DWORD value) {
    volatile DWORD *address = dg_registers + offset / 4;
    // clang-format off: Watcom assembly uses newline instruction boundaries.
    _asm {
        mov edx, [address]
        mov eax, [value]
        mov dword ptr [edx], eax
    }
    // clang-format on
}

static void DgSignal(void) {
    /* IRQ and watchdog may both fire, but exactly one adds a semaphore token.
     * All these objects and this code live in the locked VxD segments. */
    DWORD pending = 0;
    // clang-format off: Watcom assembly uses newline instruction boundaries.
    _asm {
        xor eax, eax
        xchg eax, [dg_pending]
        mov [pending], eax
    }
    // clang-format on
    if (pending) Signal_Semaphore(dg_semaphore);
}

static BOOL __stdcall DgInterrupt(void) {
    DWORD status;
    if (!dg_registers)
        return FALSE;
    status = DgRead(DG_REG_IRQ_STATUS) & (DG_IRQ_COMPLETION | DG_IRQ_GL_COMPLETION);
    if (!status)
        return FALSE;
    DgWrite(DG_REG_IRQ_STATUS, status);
    if (status & DG_IRQ_COMPLETION)
        DgSignal();
    if (status & DG_IRQ_GL_COMPLETION)
        Dg9GlSignal();
    // clang-format off: Watcom assembly uses newline instruction boundaries.
    _asm mov eax, [dg_irq]
        // clang-format on
        // clang-format off: Watcom assembly uses newline instruction boundaries.
    VxDCall(VPICD, Phys_EOI)
        // clang-format on
        return TRUE;
}

static void __declspec(naked) DgInterruptEntry(void) {
    // clang-format off: Watcom assembly uses newline instruction boundaries.
    _asm {
        pushad
        call DgInterrupt
        test eax, eax
        popad
        jz unhandled
        clc
        ret
    unhandled:
        stc
        ret
    }
    // clang-format on
}

static void __declspec(naked) DgTimeoutEntry(void) {
    // clang-format off: Watcom assembly uses newline instruction boundaries.
    _asm {
        pushad
        call DgSignal
        popad
        ret
    }
    // clang-format on
}

static DWORD DgVirtualize(VPICD_IRQ_Descriptor *descriptor) {
    DWORD handle = 0;
    // clang-format off: Watcom assembly uses newline instruction boundaries.
    _asm {
        push edi
        mov edi, [descriptor]
    }
    // clang-format on
    // clang-format off: Watcom assembly uses newline instruction boundaries.
    VxDCall(VPICD, Virtualize_IRQ)
        // clang-format on
        // clang-format off: Watcom assembly uses newline instruction boundaries.
    _asm {
        jc failed
        mov [handle], eax
    failed:
        pop edi
    }
    // clang-format on
    return handle;
}

extern void Dg9ChannelInitializeCallbacks(void);
extern void Dg9CursorInitialize(void);
extern void Dg9CursorShutdown(void);
static void DgVxdInitialize(PCIAddress *address, volatile DWORD *registers) {
    DWORD irq = PCI_ConfigRead8(address, 0x3c);
    if (dg_registers || irq == 0 || irq >= 16)
        return;
    /* VMM ignores PhysAddrPTR and PAGECONTIG unless PAGEUSEALIGN is set.
     * The original DDK PageAllocate contract also requires PAGEFIXED here. */
    dg_command =
        (DG9_COMMAND *)_PageAllocate(1, PG_SYS, 0, 0, PAGE_ALLOC_MIN, PAGE_ALLOC_MAX, &dg_physical,
                                     PAGECONTIG | PAGEFIXED | PAGEUSEALIGN | PAGEZEROINIT);
    if (!dg_command)
        return;
    if (!dg_physical || (dg_physical & 4095)) {
        _PageFree(dg_command, 0);
        dg_command = NULL;
        return;
    }
    memset(&dg_irq_descriptor, 0, sizeof(dg_irq_descriptor));
    dg_irq_descriptor.IRQ_Number = irq;
    dg_irq_descriptor.Options = VPICD_OPT_CAN_SHARE;
    dg_irq_descriptor.Hw_Int_Proc = (DWORD)DgInterruptEntry;
    dg_irq_descriptor.IRET_Time_Out = 500;
    dg_irq = DgVirtualize(&dg_irq_descriptor);
    if (!dg_irq) {
        _PageFree(dg_command, 0);
        dg_command = NULL;
        return;
    }
    Dg9ChannelInitializeCallbacks();
    dg_registers = registers;
    dg_submit_flags = DG_SUBMIT_START;
    if (DgRead(DG_REG_CAPS) & DG_CAP_INLINE_NO_IRQ)
        dg_submit_flags |= DG_SUBMIT_INLINE_NO_IRQ;
    PCI_ConfigWrite16(address, 4, PCI_ConfigRead16(address, 4) | 6);
    DgWrite(DG_REG_RESET, 1);
    Dg9CursorInitialize();
    DgWrite(DG_REG_IRQ_STATUS, DG_IRQ_COMPLETION);
    DgWrite(DG_REG_IRQ_ENABLE, DG_IRQ_COMPLETION | DG_IRQ_GL_COMPLETION);
    dbg_printf("DG DMA: linear=%lX physical=%lX irq=%ld submit=%ld\n", dg_command, dg_physical, irq,
               dg_submit_flags);
    // clang-format off: Watcom assembly uses newline instruction boundaries.
    _asm mov eax, [dg_irq]
        // clang-format on
        // clang-format off: Watcom assembly uses newline instruction boundaries.
    VxDCall(VPICD, Physically_Unmask)
    // clang-format on
}

void DgVxdShutdown(void) {
    if (!dg_registers)
        return;
    DgWrite(DG_REG_IRQ_ENABLE, 0);
    DgWrite(DG_REG_RESET, 1);
    Dg9GlShutdown();
    Dg9CursorShutdown();
    // clang-format off: Watcom assembly uses newline instruction boundaries.
    _asm mov eax, [dg_irq]
               // clang-format on
               // clang-format off: Watcom assembly uses newline instruction boundaries.
    VxDCall(VPICD, Force_Default_Behavior)
               // clang-format on
               dg_registers = NULL;
    dg_irq = 0;
    if (dg_command)
        _PageFree(dg_command, 0);
    dg_command = NULL;
}

extern void Dg9PrimaryAccess(void);
extern void Dg9PrimaryFault(void);
WORD DgVxdBlt(PCRS_32 state) {
    DWORD op = state->Client_EDX >> 16, timer, sequence, status;
    DG9_SURFACE surface;
    BOOL success;
    if (op == 0x201) {
        Dg9PrimaryFault();
        return 0;
    }
    if (op == 0x200) {
        Dg9PrimaryAccess();
        return 1;
    }
    if (op >= 1 && op <= 3)
        Dg9PrimaryAccess();
    /* No command has been submitted on any decline path. A critical VM must
     * stay on the DIB engine instead of spinning or blocking the scheduler. */
    if (!dg_registers || dg_failed)
        return 0;
    if (dg_semaphore || (DgRead(DG_REG_STATUS) & DG_STATUS_BUSY))
        return 2;
    if (Get_Crit_Section_Status(NULL, NULL))
        return 0;
    if (!hda)
        return 0;
    surface.Width = hda->width;
    surface.Height = hda->height;
    surface.Bpp = hda->bpp;
    surface.Pitch = hda->pitch;
    surface.Offset = hda->surface;
    if (!DreamGpuPrepareBlt(&surface, op, state->Client_EBX, state->Client_ECX, state->Client_ESI,
                            dg_command))
        return 0;
    dg_semaphore = Create_Semaphore(0);
    if (!dg_semaphore)
        return 0;
    sequence = ++dg_sequence;
    DgWrite(DG_REG_BATCH_ADDR_LO, dg_physical);
    DgWrite(DG_REG_BATCH_ADDR_HI, 0);
    DgWrite(DG_REG_BATCH_COUNT, 1);
    DgWrite(DG_REG_SUBMIT_SEQUENCE, sequence);
    DgWrite(DG_REG_IRQ_STATUS, DG_IRQ_COMPLETION);
    dg_pending = 1;
    timer = Set_Async_Time_Out(2000, 0, DgTimeoutEntry);
    if (!timer) {
        dg_pending = 0;
        Destroy_Semaphore(dg_semaphore);
        dg_semaphore = 0;
        return 0;
    }
    DgWrite(DG_REG_SUBMIT, dg_submit_flags);
    /* Inline completion is visible before the doorbell returns. Read once;
     * only an unfinished batch needs to sleep. Async completion retains its
     * IRQ even if it wins the race with this read, so no wakeup is lost. */
    status = DgRead(DG_REG_STATUS);
    if ((status & DG_STATUS_BUSY) || DgRead(DG_REG_COMPLETED_SEQUENCE) != sequence)
        Wait_Semaphore(dg_semaphore, 0);
    Cancel_Time_Out(timer);
    dg_pending = 0;
    status = DgRead(DG_REG_STATUS);
    success = !(status & (DG_STATUS_BUSY | DG_STATUS_ERROR)) &&
              DgRead(DG_REG_COMPLETED_SEQUENCE) == sequence;
    if (!success) {
        dbg_printf(
            "DG submit failed: sequence=%ld completed=%ld status=%lX error=%ld physical=%lX\n",
            sequence, DgRead(DG_REG_COMPLETED_SEQUENCE), status, DgRead(DG_REG_ERROR), dg_physical);
        DgWrite(DG_REG_RESET, 1);
        /* Stop issuing commands after an IRQ/device fault; do not add a
         * two-second delay to every subsequent desktop paint. */
        dg_failed = TRUE;
    }
    Destroy_Semaphore(dg_semaphore);
    dg_semaphore = 0;
    return success ? 1 : 2;
}

volatile DWORD *DgVxdRegisters(void) {
    return dg_registers;
}

BOOL Dg9Primary(DWORD *width, DWORD *height, DWORD *stride, DWORD *offset) {
    DG9_SURFACE surface;
    if (!hda)
        return FALSE;
    surface.Width = hda->width;
    surface.Height = hda->height;
    surface.Bpp = hda->bpp;
    surface.Pitch = hda->pitch;
    surface.Offset = hda->surface;
    if (!DreamGpuPrimaryMetadata(&surface))
        return FALSE;
    *width = hda->width;
    *height = hda->height;
    *stride = hda->pitch;
    *offset = hda->surface;
    return TRUE;
}

/* FBHDA is the driver's existing locked shared 16/32-bit state. Reserved res0
 * publishes only whether a DIB access needs the VxD coherence gate; it carries
 * no pointer, user authorization or native command. Ordinary CPU desktop
 * drawing therefore keeps its original path without a VxD transition. */
void Dg9PrimaryGpuOwned(BOOL owned) {
    if (hda)
        *(volatile DWORD *)&hda->res0 = owned ? 1 : 0;
}
