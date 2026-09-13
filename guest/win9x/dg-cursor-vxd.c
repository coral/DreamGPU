/* SPDX-License-Identifier: GPL-2.0-or-later
 * Included in the locked VxD after common service declarations. The cursor
 * ABI completes synchronously: shape changes have one DMA snapshot, moves
 * only touch position registers, and neither operation waits or reads VRAM.
 */
#include "gpu.h"
#include "cursor32.h"
extern volatile DWORD *DgVxdRegisters(void);
extern FBHDA_t *hda;
static DWORD *dg_cursor_pixels;
static DWORD dg_cursor_physical, dg_cursor_sequence, dg_cursor_generation;
static DG9_CURSOR_LAYOUT dg_cursor_layout;
static BOOL dg_cursor_native, dg_cursor_visible, dg_cursor_faulted;

#ifndef DG9_CURSOR_READ
#define DG9_CURSOR_READ(regs, offset) ((regs)[(offset) / 4])
#define DG9_CURSOR_WRITE(regs, offset, value) ((regs)[(offset) / 4] = (value))
#endif

void Dg9CursorInitialize(void) {
    volatile DWORD *regs = DgVxdRegisters();
#ifdef DG_IDENTITY_H
    DgIdentityText("DGCURSORINIT");
    DgIdentityField("registers", regs != NULL);
    if (regs) {
        DgIdentityField("caps", DG9_CURSOR_READ(regs, DG_REG_CAPS));
        DgIdentityField("version", DG9_CURSOR_READ(regs, DG_CURSOR_REG_VERSION));
    }
    DgIdentityChar('\n');
#endif
    if (dg_cursor_pixels || !regs || !(DG9_CURSOR_READ(regs, DG_REG_CAPS) & DG_CAP_CURSOR) ||
        DG9_CURSOR_READ(regs, DG_CURSOR_REG_VERSION) != DG_CURSOR_ABI_VERSION)
        return;
    dg_cursor_pixels = (DWORD *)_PageAllocate(DG_CURSOR_MAX_BYTES / 4096, PG_SYS, 0, 0,
                                              PAGE_ALLOC_MIN, PAGE_ALLOC_MAX, &dg_cursor_physical,
                                              PAGECONTIG | PAGEFIXED | PAGEUSEALIGN | PAGEZEROINIT);
    if (!dg_cursor_pixels)
        return;
    if (!dg_cursor_physical || (dg_cursor_physical & 4095)) {
        _PageFree(dg_cursor_pixels, 0);
        dg_cursor_pixels = NULL;
        return;
    }
    dg_cursor_generation = DG9_CURSOR_READ(regs, DG_REG_GENERATION);
}

BOOL Dg9CursorTakeoverSafe(void) {
    volatile DWORD *regs = DgVxdRegisters();
    return regs && dg_cursor_pixels && !dg_cursor_faulted && dg_cursor_native &&
           dg_cursor_generation == DG9_CURSOR_READ(regs, DG_REG_GENERATION);
}

static WORD Dg9CursorSubmit(DWORD operation, DWORD position, DWORD flags) {
    volatile DWORD *regs = DgVxdRegisters();
    DWORD seq, status;
    if (!regs || !dg_cursor_pixels || dg_cursor_faulted || (flags & ~DG_CURSOR_FLAGS_MASK))
        return 0;
    if (++dg_cursor_sequence == 0)
        ++dg_cursor_sequence;
    seq = dg_cursor_sequence;
    if (operation == DG_CURSOR_SHAPE) {
        DG9_CURSOR_WRITE(regs, DG_CURSOR_REG_ADDR_LO, dg_cursor_physical);
        DG9_CURSOR_WRITE(regs, DG_CURSOR_REG_ADDR_HI, 0);
        DG9_CURSOR_WRITE(regs, DG_CURSOR_REG_BYTES,
                         dg_cursor_layout.Width * dg_cursor_layout.Height * 8);
        DG9_CURSOR_WRITE(regs, DG_CURSOR_REG_WIDTH, dg_cursor_layout.Width);
        DG9_CURSOR_WRITE(regs, DG_CURSOR_REG_HEIGHT, dg_cursor_layout.Height);
        DG9_CURSOR_WRITE(regs, DG_CURSOR_REG_HOT_X, dg_cursor_layout.HotX);
        DG9_CURSOR_WRITE(regs, DG_CURSOR_REG_HOT_Y, dg_cursor_layout.HotY);
        DG9_CURSOR_WRITE(regs, DG_CURSOR_REG_FORMAT, DG_CURSOR_AND_XOR);
    }
    DG9_CURSOR_WRITE(regs, DG_CURSOR_REG_X, (DWORD)(LONG)(short)(position & 0xffff));
    DG9_CURSOR_WRITE(regs, DG_CURSOR_REG_Y, (DWORD)(LONG)(short)(position >> 16));
    DG9_CURSOR_WRITE(regs, DG_CURSOR_REG_FLAGS, flags);
    DG9_CURSOR_WRITE(regs, DG_CURSOR_REG_SEQUENCE, seq);
    DG9_CURSOR_WRITE(regs, DG_CURSOR_REG_SUBMIT, operation);
    status = DG9_CURSOR_READ(regs, DG_CURSOR_REG_STATUS);
    if ((status & DG_STATUS_BUSY) || !(status & DG_STATUS_DONE) ||
        DG9_CURSOR_READ(regs, DG_CURSOR_REG_COMPLETED) != seq) {
        dg_cursor_faulted = TRUE;
        return 0; /* Ownership uncertain: do not reuse DMA. */
    }
    if ((status & DG_STATUS_ERROR) || DG9_CURSOR_READ(regs, DG_CURSOR_REG_ERROR))
        return 0;
    dg_cursor_generation = DG9_CURSOR_READ(regs, DG_REG_GENERATION);
    dg_cursor_native = (flags & DG_CURSOR_NATIVE_ENABLED) != 0;
    dg_cursor_visible = (flags & DG_CURSOR_VISIBLE) != 0;
    return 1;
}

WORD Dg9CursorGate(PCRS_32 state) {
    DWORD op = state->Client_EDX >> 16, a = state->Client_EBX, b = state->Client_ECX,
          c = state->Client_ESI;
    volatile DWORD *regs = DgVxdRegisters();
#ifdef DG_IDENTITY_H
    if (op != DG9_CURSOR_DATA && op != DG9_CURSOR_MOVE && op != DG9_CURSOR_QUERY) {
        DgIdentityText("DGCURSOR");
        DgIdentityField("op", op);
        DgIdentityField("pixels", dg_cursor_pixels != NULL);
        DgIdentityField("native", dg_cursor_native);
        DgIdentityField("faulted", dg_cursor_faulted);
        DgIdentityField("used", DreamGpuCursorUsed());
        DgIdentityChar('\n');
    }
#endif
    if (op == DG9_CURSOR_QUERY)
        return Dg9CursorTakeoverSafe() ? 1 : 0;
    if (!regs || !dg_cursor_pixels || dg_cursor_faulted)
        return 0;
    if (dg_cursor_generation != DG9_CURSOR_READ(regs, DG_REG_GENERATION)) {
        dg_cursor_native = dg_cursor_visible = FALSE;
        DreamGpuCursorAbort();
        dg_cursor_generation = DG9_CURSOR_READ(regs, DG_REG_GENERATION);
    }
    if (op == DG9_CURSOR_AVAILABLE)
        return 1;
    if (op == DG9_CURSOR_BEGIN) {
        if (!hda) {
            DreamGpuCursorReset();
            return 0;
        }
        return DreamGpuCursorBegin(hda->bpp, a, b, c, &dg_cursor_layout);
    }
    if (op == DG9_CURSOR_DATA)
        return DreamGpuCursorData(a, b, c);
    if (op == DG9_CURSOR_ABORT) {
        DreamGpuCursorAbort();
        return 1;
    }
    if (op == DG9_CURSOR_RELEASE) {
        DreamGpuCursorAbort();
        return Dg9CursorSubmit(DG_CURSOR_MOVE, a, 0);
    }
    if (op == DG9_CURSOR_MOVE) {
        if (c || (b & ~DG_CURSOR_FLAGS_MASK))
            return 0;
        return Dg9CursorSubmit(DG_CURSOR_MOVE, a, b);
    }
    if (op == DG9_CURSOR_COMMIT) {
        if (!hda) {
            DreamGpuCursorAbort();
            return 0;
        }
        if (!DreamGpuCursorCommit(hda->bpp, b, c, dg_cursor_pixels,
                                  DG_CURSOR_MAX_BYTES / sizeof(DWORD)))
            return 0;
        return Dg9CursorSubmit(DG_CURSOR_SHAPE, a, DG_CURSOR_NATIVE_ENABLED | DG_CURSOR_VISIBLE);
    }
    return 0;
}

/* Caller resets the device first, so no old cursor DMA can outlive the pages. */
void Dg9CursorShutdown(void) {
    if (dg_cursor_pixels)
        _PageFree(dg_cursor_pixels, 0);
    dg_cursor_pixels = NULL;
    dg_cursor_physical = 0;
    DreamGpuCursorReset();
    dg_cursor_native = dg_cursor_visible = dg_cursor_faulted = FALSE;
    dg_cursor_sequence = 0;
}
