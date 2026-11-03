/* SPDX-License-Identifier: GPL-2.0-or-later
 * Actual Win16 capture + VxD conversion/transport, mocked only at call gate,
 * locked allocator, and synchronous cursor MMIO. No guest pointers in gate.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef uint32_t DWORD;
typedef int32_t LONG;
typedef uint16_t WORD;
typedef uint8_t BYTE;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define __far
#define PG_SYS 0
#define PAGE_ALLOC_MIN 0
#define PAGE_ALLOC_MAX 0
#define PAGECONTIG 1
#define PAGEFIXED 2
#define PAGEUSEALIGN 4
#define PAGEZEROINIT 8
#include "gpu.h"
#include "cursor.h"
typedef struct {
    DWORD Client_EDX, Client_EBX, Client_ECX, Client_ESI;
} REGS, *PCRS_32;
typedef struct {
    DWORD bpp;
} FBHDA_t;
static FBHDA_t Primary = {32};
FBHDA_t *hda = &Primary;
static DWORD Registers[2048], HostPixels[8192], Submits, Shapes, Moves, Prepares, Failure,
    Allocations;
static void *Allocate(DWORD pages, DWORD *physical, DWORD flags) {
    assert(pages == 8 && flags == 15);
    ++Allocations;
    *physical = 0x10000;
    return calloc(pages, 4096);
}
#define _PageAllocate(pages, a, b, c, d, e, physical, flags) Allocate(pages, physical, flags)
#define _PageFree(pointer, flags) free(pointer)
volatile DWORD *DgVxdRegisters(void) {
    return Registers;
}
static void Write(volatile DWORD *regs, DWORD offset, DWORD value);
#define DG9_CURSOR_READ(regs, offset) ((regs)[(offset) / 4])
#define DG9_CURSOR_WRITE(regs, offset, value) Write(regs, offset, value)
#include "../../../guest/win9x/dg-cursor-vxd.c"
static void Write(volatile DWORD *regs, DWORD offset, DWORD value) {
    regs[offset / 4] = value;
    if (offset != DG_CURSOR_REG_SUBMIT)
        return;
    ++Submits;
    if (Failure == 1) {
        regs[DG_CURSOR_REG_STATUS / 4] = DG_STATUS_BUSY;
        return;
    }
    regs[DG_CURSOR_REG_STATUS / 4] = DG_STATUS_DONE | (Failure == 2 ? DG_STATUS_ERROR : 0);
    regs[DG_CURSOR_REG_ERROR / 4] = Failure == 2 ? DG_CURSOR_ERROR_SHAPE : 0;
    regs[DG_CURSOR_REG_COMPLETED / 4] = regs[DG_CURSOR_REG_SEQUENCE / 4];
    if (Failure)
        return;
    if (value == DG_CURSOR_SHAPE) {
        ++Shapes;
        assert(regs[DG_CURSOR_REG_ADDR_HI / 4] == 0);
        assert(regs[DG_CURSOR_REG_ADDR_LO / 4] == 0x10000);
        memcpy(HostPixels, dg_cursor_pixels, regs[DG_CURSOR_REG_BYTES / 4]);
    } else {
        assert(value == DG_CURSOR_MOVE);
        ++Moves;
    }
}
WORD DgNativeCursor(DWORD operation, DWORD a, DWORD b, DWORD c) {
    REGS state = {operation << 16, a, b, c};
    return Dg9CursorGate(&state);
}
BOOL Dg9CursorPrepareNative16(void) {
    ++Prepares;
    return TRUE;
}
static WORD wBpp = 32;
typedef struct {
    short xHotSpot, yHotSpot, cx, cy, cbWidth;
    BYTE Planes, BitsPixel;
} CURSORSHAPE;
#define DG9_CURSOR_OFFSET(pointer) ((uintptr_t)(pointer) & 0xffff)
#include "../../../guest/win9x/dg-cursor16.c"
static unsigned char Shape[65536] __attribute__((aligned(65536)));
static void Mono(void) {
    CURSORSHAPE h = {0, 0, 4, 2, 4, 1, 1};
    memset(Shape, 0xa5, sizeof(Shape));
    memcpy(Shape, &h, 12);
    Shape[12] = 0x30;
    Shape[16] = 0xc0;
    Shape[20] = 0x50;
    Shape[24] = 0xa0;
}
int main(void) {
    DWORD i, submits, shapes, old[16];
    DG9_CURSOR_LAYOUT layout;
    BYTE malformed[12];
    assert(sizeof(CURSORSHAPE) == 12);
    Registers[DG_REG_CAPS / 4] = DG_CAP_CURSOR;
    Registers[DG_CURSOR_REG_VERSION / 4] = DG_CURSOR_ABI_VERSION;
    Registers[DG_REG_GENERATION / 4] = 1;
    Dg9CursorInitialize();
    assert(Allocations == 1 && !Dg9CursorTakeoverSafe());
    Mono();
    assert(Dg9CursorLayout(Shape, 32, &layout) && layout.Bytes == 16);
    assert(Dg9CursorMove16(12, 34) == DG9_CURSOR_FALLBACK);
    assert(Dg9CursorSet16((CURSORSHAPE *)Shape) == DG9_CURSOR_NATIVE);
    assert(Shapes == 1 && Prepares == 1 && Dg9CursorTakeoverSafe());
    assert(Registers[DG_CURSOR_REG_X / 4] == 12 && Registers[DG_CURSOR_REG_Y / 4] == 34);
    assert(HostPixels[0] == 0 && HostPixels[1] == 0);
    assert(HostPixels[2] == 0 && HostPixels[3] == 0xffffff);
    assert(HostPixels[4] == 0xffffff && HostPixels[5] == 0);
    assert(HostPixels[6] == 0xffffff && HostPixels[7] == 0xffffff);
    memcpy(old, HostPixels, sizeof(old));
    memset(Shape, 0, sizeof(Shape));
    assert(!memcmp(old, HostPixels, sizeof(old)));
    shapes = Shapes;
    for (i = 0; i < 1000; ++i)
        assert(Dg9CursorMove16(i % 300, -3) == DG9_CURSOR_NATIVE);
    assert(Shapes == shapes && Moves == 1000 && Allocations == 1 && Prepares == 1);
    assert((LONG)Registers[DG_CURSOR_REG_Y / 4] == -3);
    assert(Dg9CursorSet16(NULL) == DG9_CURSOR_NATIVE && Dg9CursorTakeoverSafe());
    assert(Registers[DG_CURSOR_REG_FLAGS / 4] == DG_CURSOR_NATIVE_ENABLED);
    assert(Dg9CursorMove16(4, 5) == DG9_CURSOR_NATIVE && Registers[DG_CURSOR_REG_FLAGS / 4] == 2);
    assert(Dg9CursorCheck16() == DG9_CURSOR_NATIVE);

    /* Masked color keeps BGR XOR bytes and ignores the non-alpha fourth byte. */
    {
        CURSORSHAPE h = {1, 0, 2, 1, 2, 1, 32};
        memcpy(Shape, &h, 12);
        Shape[12] = 0x80;
        Shape[13] = 0xa5;
        Shape[14] = 1;
        Shape[15] = 2;
        Shape[16] = 3;
        Shape[17] = 0xff;
        Shape[18] = 4;
        Shape[19] = 5;
        Shape[20] = 6;
        Shape[21] = 0x17;
        assert(Dg9CursorSet16((CURSORSHAPE *)Shape) == 1 && HostPixels[0] == 0xffffff &&
               HostPixels[1] == 0x030201);
        assert(HostPixels[2] == 0 && HostPixels[3] == 0x060504);
        /* On a 16-bpp desktop the same 32-bit XOR truncates to 565 first. */
        wBpp = Primary.bpp = 16;
        assert(Dg9CursorSet16((CURSORSHAPE *)Shape) == DG9_CURSOR_NATIVE &&
               Dg9CursorTakeoverSafe() && HostPixels[0] == 0xffffff && HostPixels[1] == 0);
        assert(HostPixels[2] == 0 && HostPixels[3] == 0x000400);
        wBpp = Primary.bpp = 32;
    }
    /* RGB565 color on a 16-bpp desktop stays native (so GL takeover remains
     * safe): XOR words expand by bit replication, as the desktop does. */
    {
        CURSORSHAPE h = {1, 0, 2, 1, 2, 1, 16};
        memcpy(Shape, &h, 12);
        Shape[12] = 0x80; /* pixel 0 keeps the desktop, pixel 1 replaces it */
        Shape[13] = 0xa5; /* AND row padding */
        Shape[14] = 0xff; /* pixel 0 XOR 0xffff: exact inversion */
        Shape[15] = 0xff;
        Shape[16] = 0x45; /* pixel 1: 0x8d45 = r 17, g 42, b 5 */
        Shape[17] = 0x8d;
        assert(Dg9CursorLayout(Shape, 32, &layout) && layout.XorStride == 4 && layout.Bytes == 6);
        assert(Dg9CursorLayout(Shape, 16, &layout) && layout.XorStride == 4 && layout.Bytes == 6);
        wBpp = Primary.bpp = 16;
        assert(Dg9CursorSet16((CURSORSHAPE *)Shape) == DG9_CURSOR_NATIVE &&
               Dg9CursorTakeoverSafe());
        assert(HostPixels[0] == 0xffffff && HostPixels[1] == 0xffffff);
        assert(HostPixels[2] == 0 && HostPixels[3] == 0x8caa29);
        /* Extremes replicate to full range. */
        Shape[16] = 0x1f;
        Shape[17] = 0x00;
        assert(Dg9CursorSet16((CURSORSHAPE *)Shape) == DG9_CURSOR_NATIVE && HostPixels[3] == 0x0000ff);
        Shape[16] = 0xe0;
        Shape[17] = 0x07;
        assert(Dg9CursorSet16((CURSORSHAPE *)Shape) == DG9_CURSOR_NATIVE && HostPixels[3] == 0x00ff00);
        Shape[16] = 0x00;
        Shape[17] = 0x08; /* lowest red bit */
        assert(Dg9CursorSet16((CURSORSHAPE *)Shape) == DG9_CURSOR_NATIVE && HostPixels[3] == 0x080000);
        /* Win98 USER passes a color cursor at its bitmap's depth, so a 565
         * cursor also reaches a 32-bpp desktop: it expands the same way. */
        wBpp = Primary.bpp = 32;
        assert(Dg9CursorSet16((CURSORSHAPE *)Shape) == DG9_CURSOR_NATIVE &&
               Dg9CursorTakeoverSafe() && HostPixels[3] == 0x080000);
        /* Commit converts for the desktop depth current at commit; a desktop
         * that is neither 16 nor 32 bpp cannot take a native color cursor. */
        {
            DWORD w0 = 0, w1 = 0, w2 = 0, k, words[2 * 2];
            DG9_CURSOR_LAYOUT staged;
            for (k = 0; k < 12; ++k)
                (k < 4 ? &w0 : k < 8 ? &w1 : &w2)[0] |= (DWORD)Shape[k] << ((k % 4) * 8);
            assert(DreamGpuCursorBegin(16, w0, w1, w2, &staged));
            assert(DreamGpuCursorData(Shape[12] | (DWORD)Shape[13] << 8 | (DWORD)Shape[14] << 16 |
                                          (DWORD)Shape[15] << 24,
                                      Shape[16] | (DWORD)Shape[17] << 8, 0));
            assert(!DreamGpuCursorCommit(8, 0, 0, words, 4));
            assert(!Dg9CursorLayout(Shape, 8, &layout));
        }
    }
    /* 32-bit XOR (Wine's D3D8/9 cursors: CreateBitmap(32, 32, 1, 32)) on a
     * 16-bpp desktop truncates to 565 first, then replicates like the
     * desktop: exactly the 565 framebuffer's pixels. 32-bpp is unchanged. */
    {
        CURSORSHAPE h = {1, 0, 2, 1, 2, 1, 32};
        memcpy(Shape, &h, 12);
        Shape[12] = 0x80;
        Shape[13] = 0;
        Shape[14] = 0xff; /* pixel 0 XOR white: inversion */
        Shape[15] = 0xff;
        Shape[16] = 0xff;
        Shape[17] = 0x80; /* alpha byte ignored */
        Shape[18] = 0x2f; /* pixel 1 BGR 2f ab 8f -> 565 0x8d45 -> 0x8caa29 */
        Shape[19] = 0xab;
        Shape[20] = 0x8f;
        Shape[21] = 0x00;
        wBpp = Primary.bpp = 16;
        assert(Dg9CursorSet16((CURSORSHAPE *)Shape) == DG9_CURSOR_NATIVE &&
               Dg9CursorTakeoverSafe());
        assert(HostPixels[0] == 0xffffff && HostPixels[1] == 0xffffff);
        assert(HostPixels[2] == 0 && HostPixels[3] == 0x8caa29);
        wBpp = Primary.bpp = 32;
        assert(Dg9CursorSet16((CURSORSHAPE *)Shape) == DG9_CURSOR_NATIVE &&
               HostPixels[3] == 0x8fab2f);
        /* 8-bit indices would need the system palette: GDI keeps them. */
        Shape[11] = 8;
        assert(Dg9CursorSet16((CURSORSHAPE *)Shape) == DG9_CURSOR_FALLBACK &&
               !Dg9CursorTakeoverSafe());
        assert(!Dg9CursorLayout(Shape, 32, &layout) && !Dg9CursorLayout(Shape, 16, &layout));
    }
    /* Win98 USER converts every color cursor that does not match the desktop
     * depth to packed 4-bit standard-palette indices, high nibble first, in
     * WORD-aligned rows (observed on Win98 SE). Index 11 is yellow and 14 cyan
     * in Windows order; 15 inverts. */
    {
        CURSORSHAPE h = {16, 1, 32, 2, 4, 1, 4};
        DWORD k;
        memcpy(Shape, &h, 12);
        memset(Shape + 12, 0, 8 + 32);
        Shape[12 + 4] = 0x00; /* row 1 AND: pixels 16-31 keep the desktop */
        Shape[12 + 6] = 0xff;
        Shape[12 + 7] = 0xff;
        for (k = 0; k < 8; ++k) {
            Shape[20 + k] = 0xbb;     /* row 0: 0-15 yellow */
            Shape[20 + 8 + k] = 0xee; /* row 0: 16-31 cyan */
            Shape[36 + k] = 0x9c;     /* row 1: red, blue alternating */
            Shape[36 + 8 + k] = 0xf0; /* row 1: invert, transparent */
        }
        assert(Dg9CursorLayout(Shape, 16, &layout) && layout.XorStride == 16 && layout.Bytes == 40);
        wBpp = Primary.bpp = 16;
        assert(Dg9CursorSet16((CURSORSHAPE *)Shape) == DG9_CURSOR_NATIVE &&
               Dg9CursorTakeoverSafe());
        assert(HostPixels[0] == 0 && HostPixels[1] == 0xffff00);
        assert(HostPixels[2 * 16] == 0 && HostPixels[2 * 16 + 1] == 0x00ffff);
        assert(HostPixels[2 * 32 + 1] == 0xff0000 && HostPixels[2 * 33 + 1] == 0x0000ff);
        assert(HostPixels[2 * 48] == 0xffffff && HostPixels[2 * 48 + 1] == 0xffffff);
        assert(HostPixels[2 * 49] == 0xffffff && HostPixels[2 * 49 + 1] == 0);
        /* Half-intensity entries quantize to 565 on a 16-bpp desktop. */
        Shape[20] = 0x87;
        assert(Dg9CursorSet16((CURSORSHAPE *)Shape) == DG9_CURSOR_NATIVE &&
               HostPixels[1] == 0x848284 && HostPixels[3] == 0xc6c3c6);
        wBpp = Primary.bpp = 32;
        assert(Dg9CursorSet16((CURSORSHAPE *)Shape) == DG9_CURSOR_NATIVE &&
               HostPixels[1] == 0x808080 && HostPixels[3] == 0xc0c0c0);
        /* Odd widths keep WORD-aligned 4-bit rows. */
        h.cx = 5;
        h.xHotSpot = 1;
        memcpy(Shape, &h, 12);
        assert(Dg9CursorLayout(Shape, 32, &layout) && layout.XorStride == 4);
    }
    Mono();
    memcpy(malformed, Shape, 12);
    malformed[4] = 0;
    assert(!Dg9CursorLayout(malformed, 32, &layout));
    memcpy(malformed, Shape, 12);
    malformed[8] = 0;
    assert(!Dg9CursorLayout(malformed, 32, &layout));
    memcpy(malformed, Shape, 12);
    malformed[10] = 2;
    assert(!Dg9CursorLayout(malformed, 32, &layout));
    memcpy(malformed, Shape, 12);
    malformed[0] = 4;
    assert(!Dg9CursorLayout(malformed, 32, &layout));
    assert(Dg9CursorSet16((CURSORSHAPE *)(Shape + 65530)) == DG9_CURSOR_FALLBACK);
    /* A partial or overfilled stream never submits or changes accepted state. */
    assert(DgNativeCursor(DG9_CURSOR_BEGIN, 0, 4 | (2UL << 16), 4 | (1UL << 16) | (1UL << 24)));
    submits = Submits;
    assert(!DgNativeCursor(DG9_CURSOR_COMMIT, 0, 0, 0) && Submits == submits);
    assert(DgNativeCursor(DG9_CURSOR_DATA, 0, 0, 0));
    assert(!DgNativeCursor(DG9_CURSOR_DATA, 0, 1, 0)); /* nonzero tail padding */
    assert(!DgNativeCursor(DG9_CURSOR_COMMIT, 0, 0, 0) && Submits == submits);
    assert(!DgNativeCursor(DG9_CURSOR_MOVE, 0, 4, 0) && Submits == submits);
    assert(Dg9CursorSet16((CURSORSHAPE *)Shape) == 1);
    ++Registers[DG_REG_GENERATION / 4];
    assert(!Dg9CursorTakeoverSafe() && Dg9CursorCheck16() == 2);
    assert(Dg9CursorSet16((CURSORSHAPE *)Shape) == 1 && Dg9CursorTakeoverSafe());
    Failure = 1;
    submits = Submits;
    assert(Dg9CursorSet16((CURSORSHAPE *)Shape) == DG9_CURSOR_FAILED);
    assert(Submits == submits + 1 && !Dg9CursorTakeoverSafe());
    assert(!DgNativeCursor(DG9_CURSOR_BEGIN, 0, 4 | (2UL << 16), 4 | (1UL << 16) | (1UL << 24)));
    /* Device reset precedes freeing possibly referenced DMA. */
    ++Registers[DG_REG_GENERATION / 4];
    Dg9CursorShutdown();
    assert(!dg_cursor_pixels && DreamGpuCursorUsed() == 0);
    /* Direct production C++ ABI checks: output capacity is transactional,
     * staging has one owner, and maximum padded rows stay inside both views. */
    memset(HostPixels, 0xa5, sizeof(HostPixels));
    assert(DreamGpuCursorBegin(32, 0, 4 | (2UL << 16), 4 | (1UL << 16) | (1UL << 24), &layout));
    assert(DreamGpuCursorData(0, 0, 0) && DreamGpuCursorData(0, 0, 0));
    memcpy(old, HostPixels, sizeof(old));
    assert(!DreamGpuCursorCommit(32, 0, 0, HostPixels, 15));
    assert(!memcmp(old, HostPixels, sizeof(old)));
    assert(!DreamGpuCursorCommit(32, 0, 0, HostPixels, 16)); /* consumed, never retry */
    assert(!DreamGpuCursorBegin(32, 0, 4 | (2UL << 16), 4 | (1UL << 16) | (1UL << 24), NULL));
    assert(!DreamGpuCursorData(0, 0, 0));
    assert(DreamGpuCursorBegin(32, 0, 64 | (64UL << 16), 256 | (1UL << 16) | (1UL << 24), &layout));
    assert(layout.Bytes == DG9_CURSOR_RAW_MAX);
    for (i = 0; i < layout.Bytes / 12; ++i)
        assert(DreamGpuCursorData(0, 0, 0));
    assert(DreamGpuCursorData(0, 0, 0) && DreamGpuCursorUsed() == layout.Bytes);
    assert(!DreamGpuCursorData(0, 0, 0)); /* no room for another fragment */
    assert(DreamGpuCursorCommit(32, 0, 0, HostPixels, 8192));
    for (i = 0; i < 8192; ++i)
        assert(HostPixels[i] == 0);
    assert(!DreamGpuCursorCommit(32, 0, 0, HostPixels, 8192));
    puts("PASS actual Win16/VxD cursor: exact mono/color masks, padded rows, immutable shape, 1000 "
         "position-only moves, hidden state, bounded streams, maximum padded rows, short output "
         "capacity, malformed inputs and uncertain completion");
    return 0;
}
