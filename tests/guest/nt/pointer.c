/* SPDX-License-Identifier: GPL-2.0-or-later
 * Actual NT cursor DDI conversion with fake GDI/kernel entry points. Verify
 * lossless AND/XOR, orientation, alpha and that normal movement never asks
 * GDI to read or modify the desktop. Real guest tests cover DDI scheduling. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define _FRAMEBUF_PCH_
#define APIENTRY
#define DG_KERNEL_CALL
#define TRUE 1
#define FALSE 0
#define ALLOC_TAG 0
#define FILE_DEVICE_VIDEO 0x23
#define METHOD_BUFFERED 0
#define FILE_ANY_ACCESS 0
#define CTL_CODE(d, f, m, a) (((d) << 16) | ((f) << 2))
#define BMF_1BPP 1
#define BMF_4BPP 2
#define BMF_8BPP 3
#define BMF_16BPP 4
#define BMF_24BPP 5
#define BMF_32BPP 6
#define SPS_CHANGE 1
#define SPS_ANIMATESTART 2
#define SPS_ANIMATEUPDATE 4
#define SPS_ALPHA 0x10
#define SPS_ERROR 0
#define SPS_DECLINE 1
#define SPS_ACCEPT_NOEXCLUDE 2
#define SPS_ACCEPT_EXCLUDE 3
#define XO_TRIVIAL 1
typedef uint32_t ULONG;
typedef int32_t LONG;
typedef uintptr_t ULONG_PTR;
typedef int BOOL;
typedef void VOID;
typedef uint8_t BYTE;
typedef ULONG FLONG;
typedef struct {
    LONG cx, cy;
} SIZEL;
typedef struct {
    LONG left, top, right, bottom;
} RECTL;
typedef struct {
    void *dhpdev, *pvBits, *pvScan0;
    ULONG cjBits, iBitmapFormat;
    LONG lDelta;
    SIZEL sizlBitmap;
} SURFOBJ;
typedef struct {
    ULONG flXlate;
} XLATEOBJ;
typedef struct {
    BYTE peRed, peGreen, peBlue, peFlags;
} PALETTEENTRY;
#include "dg-kernel.h"
typedef struct {
    DG_KERNEL_INTERFACE Kernel;
    BOOL PointerNative, PointerHasShape, PointerSoftware;
    ULONG *PointerPixels;
    ULONG BitsPerPixel, RedMask, GreenMask, BlueMask;
    PALETTEENTRY *PaletteEntries;
} PDEV, *PPDEV;

static ULONG coherent, software_shapes, software_moves, native_shapes, native_moves;
static DG_KERNEL_CURSOR_REQUEST last;
static ULONG received[DG_CURSOR_MAX_PIXELS * 2];
static void *EngAllocMem(ULONG flags, ULONG bytes, ULONG tag) {
    (void)flags;
    (void)tag;
    return malloc(bytes);
}
static void EngFreeMem(void *memory) {
    free(memory);
}
static BOOL DgCoherent(PPDEV pdev, ULONG reason) {
    (void)pdev;
    assert(reason == DG_COHERE_DESTINATION);
    ++coherent;
    return TRUE;
}
static void EngMovePointer(SURFOBJ *s, LONG x, LONG y, RECTL *r) {
    (void)s;
    (void)x;
    (void)y;
    (void)r;
    ++software_moves;
}
static ULONG EngSetPointerShape(SURFOBJ *s, SURFOBJ *m, SURFOBJ *c, XLATEOBJ *xl, LONG hx, LONG hy,
                                LONG x, LONG y, RECTL *r, FLONG flags) {
    (void)s;
    (void)m;
    (void)c;
    (void)xl;
    (void)hx;
    (void)hy;
    (void)x;
    (void)y;
    (void)r;
    (void)flags;
    ++software_shapes;
    return SPS_ACCEPT_EXCLUDE;
}
static ULONG XLATEOBJ_iXlate(XLATEOBJ *xlate, ULONG value) {
    (void)xlate;
    return value == 3 ? 0x0000a0b0 : 0x00ff0011;
}
static ULONG Cursor(void *context, const DG_KERNEL_CURSOR_REQUEST *request) {
    (void)context;
    last = *request;
    if (request->Operation == DG_CURSOR_SHAPE) {
        ++native_shapes;
        memcpy(received, request->Pixels, request->Width * request->Height * 8);
    } else
        ++native_moves;
    return TRUE;
}
#include "../display/pointer.c"

int main(void) {
    PDEV pdev = {0};
    SURFOBJ primary = {0}, mask = {0}, color = {0};
    BYTE mono[8] = {0x30, 0, 0, 0, 0x50, 0, 0, 0};
    ULONG alpha[4] = {0x00000000, 0x80402010, 0xffffffff, 0xff0080ff};
    XLATEOBJ xlate = {0};
    ULONG before;
    RECTL bounds = {1, 2, 3, 4};
    pdev.Kernel.Cursor = Cursor;
    pdev.BitsPerPixel = 32;
    pdev.RedMask = 0xff0000;
    pdev.GreenMask = 0xff00;
    pdev.BlueMask = 0xff;
    primary.dhpdev = &pdev;
    mask.pvBits = mask.pvScan0 = mono;
    mask.cjBits = sizeof(mono);
    mask.iBitmapFormat = BMF_1BPP;
    mask.lDelta = 4;
    mask.sizlBitmap = (SIZEL){4, 2};
    assert(DrvSetPointerShape(&primary, &mask, NULL, NULL, 1, 0, 40, 50, &bounds, SPS_CHANGE) ==
           SPS_ACCEPT_NOEXCLUDE);
    assert(native_shapes == 1 && !coherent && !software_shapes && !software_moves);
    assert(last.Format == DG_CURSOR_AND_XOR && last.X == 40 && last.Y == 50 && last.HotX == 1);
    assert(!bounds.left && !bounds.top && !bounds.right && !bounds.bottom);
    /* black, white, transparent, invert; none may be reduced to alpha. */
    assert(received[0] == 0 && received[1] == 0);
    assert(received[2] == 0 && received[3] == 0xffffff);
    assert(received[4] == 0xffffff && received[5] == 0);
    assert(received[6] == 0xffffff && received[7] == 0xffffff);
    for (before = 0; before < 1000; ++before)
        DrvMovePointer(&primary, before, 24, NULL);
    assert(native_moves == 1000 && native_shapes == 1 && !coherent && !software_moves);
    DrvMovePointer(&primary, -1, -1, NULL);
    assert(last.Flags == DG_CURSOR_NATIVE_ENABLED);
    DrvMovePointer(&primary, 10, 15, NULL);
    assert(last.Flags == (DG_CURSOR_NATIVE_ENABLED | DG_CURSOR_VISIBLE));

    /* Negative DIB strides preserve top-left logical scanline order. */
    mask.pvScan0 = mono + 4;
    mask.lDelta = -4;
    assert(DrvSetPointerShape(&primary, &mask, NULL, NULL, 0, 0, 0, 0, NULL, 0) ==
           SPS_ACCEPT_NOEXCLUDE);
    assert(received[2] == 0xffffff && received[3] == 0);
    assert(received[4] == 0 && received[5] == 0xffffff);
    mask.pvScan0 = mono;
    mask.lDelta = 4;

    color.pvBits = color.pvScan0 = alpha;
    color.cjBits = sizeof(alpha);
    color.lDelta = 16;
    color.iBitmapFormat = BMF_32BPP;
    color.sizlBitmap = (SIZEL){4, 1};
    assert(DrvSetPointerShape(&primary, NULL, &color, NULL, 0, 0, 12, 14, NULL, SPS_ALPHA) ==
           SPS_ACCEPT_NOEXCLUDE);
    assert(last.Format == DG_CURSOR_ARGB_PREMULTIPLIED);
    assert(received[2] == 0x80402010 && received[3] == 0 && !coherent);

    /* Indexed color is translated to the primary's RGB format. */
    {
        BYTE indices[4] = {3, 4, 3, 4};
        color.pvBits = color.pvScan0 = indices;
        color.cjBits = 4;
        color.lDelta = 4;
        color.iBitmapFormat = BMF_8BPP;
        assert(DrvSetPointerShape(&primary, &mask, &color, &xlate, 0, 0, 4, 5, NULL, 0) ==
               SPS_ACCEPT_NOEXCLUDE);
        assert(received[1] == 0xa0b0 && received[3] == 0xff0011);
    }
    before = native_shapes;
    mask.cjBits = 4; /* malformed second mask scanline must not be read */
    assert(DrvSetPointerShape(&primary, &mask, NULL, NULL, 0, 0, 1, 2, NULL, 0) ==
           SPS_ACCEPT_EXCLUDE);
    assert(native_shapes == before && coherent == 1 && software_shapes == 1 && !last.Flags);
    DrvMovePointer(&primary, 3, 4, NULL);
    assert(coherent == 2 && software_moves == 1);
    mask.cjBits = sizeof(mono);
    assert(DrvSetPointerShape(&primary, &mask, NULL, NULL, 0, 0, 1, 2, NULL, 0) ==
           SPS_ACCEPT_NOEXCLUDE);
    assert(coherent == 3 && software_moves == 2); /* remove old software pointer once */
    assert(DrvSetPointerShape(&primary, NULL, NULL, NULL, 0, 0, 1, 2, NULL, 0) ==
           SPS_ACCEPT_NOEXCLUDE);
    DrvMovePointer(&primary, 5, 6, NULL);
    assert(last.Flags == DG_CURSOR_NATIVE_ENABLED && !pdev.PointerHasShape);
    /* RGB expansion is not a valid implementation of packed16/palette XOR. */
    color.pvBits = color.pvScan0 = alpha;
    color.cjBits = sizeof(alpha);
    color.lDelta = 16;
    color.iBitmapFormat = BMF_32BPP;
    pdev.BitsPerPixel = 16;
    before = native_shapes;
    assert(DrvSetPointerShape(&primary, &mask, &color, &xlate, 0, 0, 0, 0, NULL, 0) ==
           SPS_ACCEPT_EXCLUDE);
    assert(native_shapes == before);
    assert(DrvSetPointerShape(&primary, &mask, NULL, NULL, 0, 0, 0, 0, NULL, 0) ==
           SPS_ACCEPT_NOEXCLUDE);
    pdev.BitsPerPixel = 32;
    before = native_shapes;
    alpha[0] = 0x01020000; /* invalid non-premultiplied color */
    assert(DrvSetPointerShape(&primary, NULL, &color, NULL, 0, 0, 0, 0, NULL, SPS_ALPHA) ==
           SPS_ACCEPT_EXCLUDE);
    assert(native_shapes == before);
    assert(DrvSetPointerShape(&primary, &mask, NULL, NULL, 0, 0, 0, 0, NULL, 0) ==
           SPS_ACCEPT_NOEXCLUDE);
    DgDisablePointer(&pdev);
    assert(!last.Flags && !pdev.PointerPixels && !pdev.PointerNative);
    puts("NT native cursor: exact masks/alpha/color/negative stride, bounded fallback and no GDI "
         "work on movement passed");
    return 0;
}
