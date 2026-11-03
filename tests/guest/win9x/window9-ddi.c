/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The actual 16-bit BitBlt source classifier over a flat DIB-engine model.
 * The C++ GDI model calls DgTestBitBlt where GDI would enter the driver DDI. */
#include <stdint.h>
#define __far
#define TYPE_DIBENG 0x5250
#define VRAM 0x8000
#define BUSY 16
#define SELECTEDDIB 4
#define FIVE6FIVE 0x0040
typedef uint16_t WORD;
typedef uint32_t DWORD;
typedef struct {
    WORD deType, deFlags, deBitsSelector, deWidth, deHeight, deBitsPixel, dePlanes;
    DWORD deBitsOffset;
} DIBENGINE, *LPDIBENGINE;
typedef void *LPPDEVICE;
static DIBENGINE primary;
static LPDIBENGINE lpDriverPDevice = &primary;
static DWORD *dg_test_bits;
#define DG9_WINDOW_WORDS(src) ((void)(src), dg_test_bits)
DWORD dg_test_token, dg_test_source, dg_test_destination, dg_test_extent;
unsigned dg_test_calls;
WORD DgWindowBlt16(DWORD binding, DWORD source, DWORD destination, DWORD extent) {
    ++dg_test_calls;
    dg_test_token = binding;
    dg_test_source = source;
    dg_test_destination = destination;
    dg_test_extent = extent;
    return 1;
}
#include "dg-window16.c"
WORD DgTestBitBlt(void *bits, unsigned depth, unsigned width, unsigned height,
                  unsigned screen_depth, unsigned screen_flags, unsigned dx, unsigned dy,
                  unsigned sx, unsigned sy, unsigned w, unsigned h, DWORD rop) {
    DIBENGINE source = {TYPE_DIBENG, 0, 1, (WORD)width, (WORD)height, (WORD)depth, 1, 0};
    primary.deType = TYPE_DIBENG;
    primary.deBitsPixel = (WORD)screen_depth;
    primary.deFlags = (WORD)screen_flags;
    primary.dePlanes = 1;
    dg_test_bits = (DWORD *)bits;
    return DgWindowSource(&primary, &source, (WORD)dx, (WORD)dy, (WORD)sx, (WORD)sy, (WORD)w,
                          (WORD)h, rop);
}
