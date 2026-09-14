/* SPDX-License-Identifier: GPL-2.0-or-later
 * Exercise the actual GDI hook against a checked command sink, without a VM.
 * clang++ -x c++ -std=c++23 -Wall -Wextra -Werror -Iguest/include
 *   -Iguest/nt/include tests/guest/nt/blit.c -o /tmp/dg-blit-test
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define _FRAMEBUF_PCH_
#define APIENTRY
#define DG_KERNEL_CALL
#define TRUE 1
#define FALSE 0
#define DC_TRIVIAL 0
#define DC_RECT 1
#define DC_COMPLEX 3
#define CD_RIGHTDOWN 1
#define CD_LEFTDOWN 2
#define CD_RIGHTUP 3
#define CD_LEFTUP 4
#define CT_RECTANGLES 0
#define XO_TRIVIAL 1
#define FILE_DEVICE_VIDEO 0x23
#define METHOD_BUFFERED 0
#define FILE_ANY_ACCESS 0
#define CTL_CODE(device, function, method, access) (((device) << 16) | ((function) << 2))
#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))
typedef uint32_t ULONG;
typedef int32_t LONG;
typedef int BOOL;
#include "dg-kernel.h"
typedef ULONG ROP4;
typedef struct {
    LONG left, top, right, bottom;
} RECTL;
typedef struct {
    LONG x, y;
} POINTL;
typedef struct {
    ULONG flXlate;
} XLATEOBJ;
typedef struct {
    ULONG iSolidColor;
} BRUSHOBJ;
typedef struct {
    void *dhpdev;
    void *hsurf;
} SURFOBJ;
typedef struct {
    ULONG iDComplexity;
    RECTL rclBounds;
} CLIPOBJ;
typedef struct {
    void *hDriver, *hSurfEng;
    ULONG NativeCaps, ScreenWidth, ScreenHeight, BitsPerPixel;
    LONG ScreenDelta;
    DG_KERNEL_INTERFACE Kernel;
} PDEV, *PPDEV;
#include "dg-ioctl.h"

static ULONG pixels[32 * 32], expected[32 * 32];
static RECTL rectangles[3];
static ULONG rectangle_count, enum_direction, expected_direction;
static ULONG submits, fallbacks;
static BOOL fail_submit;
static ULONG mixed, coheres;
static BOOL fail_cohere;
static ULONG Coherent(void *context, ULONG reason) {
    (void)context;
    (void)reason;
    ++coheres;
    if (fail_cohere)
        return FALSE;
    mixed = 0;
    return TRUE;
}

static ULONG DgKernelSubmit(void *context, const DG_COMMAND *commands, ULONG count) {
    ULONG n;
    (void)context;
    assert(count && count <= DG_MAX_COMMANDS);
    ++submits;
    if (fail_submit)
        return FALSE;
    for (n = 0; n < count; ++n) {
        const DG_COMMAND *c = &commands[n];
        LONG y;
        assert(c->Bpp == 4 && c->Reserved == 0);
        assert(c->DestinationStride == 128 && c->Height && c->Width);
        assert(c->Destination + (c->Height - 1) * 128 + c->Width * 4 <= sizeof(pixels));
        if (c->Opcode == DG_CMD_FILL) {
            for (y = 0; y < (LONG)c->Height; ++y) {
                ULONG x;
                for (x = 0; x < c->Width; ++x)
                    pixels[c->Destination / 4 + y * 32 + x] = c->Color;
            }
        } else {
            assert(c->Opcode == DG_CMD_COPY && c->SourceStride == 128);
            assert(c->Source + (c->Height - 1) * 128 + c->Width * 4 <= sizeof(pixels));
            if (c->Destination > c->Source)
                for (y = c->Height - 1; y >= 0; --y)
                    memmove(pixels + c->Destination / 4 + y * 32, pixels + c->Source / 4 + y * 32,
                            c->Width * 4);
            else
                for (y = 0; y < (LONG)c->Height; ++y)
                    memmove(pixels + c->Destination / 4 + y * 32, pixels + c->Source / 4 + y * 32,
                            c->Width * 4);
        }
    }
    return TRUE;
}

static void CLIPOBJ_cEnumStart(CLIPOBJ *clip, BOOL all, ULONG type, ULONG direction, ULONG limit) {
    (void)clip;
    (void)all;
    (void)type;
    (void)limit;
    assert(direction == expected_direction);
    enum_direction = direction;
}

static BOOL CLIPOBJ_bEnum(CLIPOBJ *clip, ULONG size, ULONG *data) {
    ULONG i;
    RECTL *output = (RECTL *)(data + 1);
    (void)clip;
    assert(size >= sizeof(ULONG) + rectangle_count * sizeof(RECTL));
    data[0] = rectangle_count;
    for (i = 0; i < rectangle_count; ++i)
        output[i] = rectangles[(enum_direction == CD_LEFTUP || enum_direction == CD_LEFTDOWN)
                                   ? rectangle_count - i - 1
                                   : i];
    return FALSE;
}

static BOOL EngBitBlt(SURFOBJ *dest, SURFOBJ *source, SURFOBJ *mask, CLIPOBJ *clip, XLATEOBJ *xlate,
                      RECTL *rect, POINTL *source_point, POINTL *mask_point, BRUSHOBJ *brush,
                      POINTL *brush_origin, ROP4 rop) {
    (void)dest;
    (void)source;
    (void)mask;
    (void)clip;
    (void)xlate;
    (void)rect;
    (void)source_point;
    (void)mask_point;
    (void)brush;
    (void)brush_origin;
    (void)rop;
    ++fallbacks;
    assert(!mixed); /* Never let the DIB engine read/write stale VRAM. */
    return TRUE;
}
static BOOL DgDesktopActive(PPDEV dev) {
    return dev->Kernel.DesktopActive && *dev->Kernel.DesktopActive;
}
static BOOL DgCoherent(PPDEV dev, ULONG reason) {
    return !DgDesktopActive(dev) || dev->Kernel.Cohere(dev->Kernel.Context, reason);
}

#include "../../../guest/nt/display/blit.cpp"

int main(void) {
    ULONG i;
    LONG x, y;
    PDEV dev = {NULL,
                (void *)1,
                DG_CAP_FILL | DG_CAP_COPY,
                32,
                32,
                32,
                128,
                {.Version = DG_KERNEL_VERSION,
                 .Size = sizeof(DG_KERNEL_INTERFACE),
                 .Submit = DgKernelSubmit,
                 .Cohere = Coherent,
                 .DesktopActive = &mixed}};
    SURFOBJ surface = {&dev, (void *)1};
    RECTL full = {2, 2, 18, 18};
    POINTL source = {0, 0};
    BRUSHOBJ brush = {0xabcdef12};
    CLIPOBJ clip = {DC_COMPLEX, {2, 2, 18, 18}};
    rectangles[0] = (RECTL){2, 2, 8, 18};
    rectangles[1] = (RECTL){12, 2, 18, 18};
    rectangle_count = 2;
    for (i = 0; i < 32 * 32; ++i)
        pixels[i] = expected[i] = i;
    for (y = 2; y < 18; ++y)
        for (x = 2; x < 18; ++x)
            if (x < 8 || x >= 12)
                expected[y * 32 + x] = (y - 2) * 32 + x - 2;
    expected_direction = CD_LEFTUP;
    assert(DrvCopyBits(&surface, &surface, &clip, NULL, &full, &source));
    assert(submits == 1 && fallbacks == 0);
    assert(!memcmp(pixels, expected, sizeof(pixels)));

    expected_direction = CD_RIGHTDOWN;
    for (y = 2; y < 18; ++y)
        for (x = 2; x < 18; ++x)
            if (x < 8 || x >= 12)
                expected[y * 32 + x] = brush.iSolidColor;
    assert(DrvBitBlt(&surface, NULL, NULL, &clip, NULL, &full, NULL, NULL, &brush, NULL, 0xf0f0));
    assert(!memcmp(pixels, expected, sizeof(pixels)));

    source.x = -1;
    assert(DrvCopyBits(&surface, &surface, NULL, NULL, &full, &source));
    assert(fallbacks == 1 && submits == 2);
    fail_submit = TRUE;
    assert(!DrvBitBlt(&surface, NULL, NULL, NULL, NULL, &full, NULL, NULL, &brush, NULL, 0xf0f0));
    assert(fallbacks == 1); /* Never replay a possibly partially executed copy. */
    fail_submit = FALSE;
    {
        ULONG before = submits;
        RECTL tiny = {0, 0, 8, 16};
        CLIPOBJ tiny_clip = {DC_RECT, {2, 2, 10, 18}};
        assert(
            DrvBitBlt(&surface, NULL, NULL, NULL, NULL, &tiny, NULL, NULL, &brush, NULL, 0xf0f0));
        assert(submits == before && fallbacks == 2);
        /* Select using the visible region, not an obscured window's bounds. */
        assert(DrvBitBlt(&surface, NULL, NULL, &tiny_clip, NULL, &full, NULL, NULL, &brush, NULL,
                         0xf0f0));
        assert(submits == before && fallbacks == 3);
        tiny.right = 9;
        assert(
            DrvBitBlt(&surface, NULL, NULL, NULL, NULL, &tiny, NULL, NULL, &brush, NULL, 0xf0f0));
        assert(submits == before + 1 && fallbacks == 3);
    }
    {
        ULONG before = submits, cpu_before = fallbacks;
        RECTL tiny = {0, 0, 1, 1};
        SURFOBJ memory = {NULL, (void *)2};
        POINTL zero = {0, 0};
        assert(coheres == 0); /* Pure 2D never enters the GL channel. */
        mixed = 1;
        assert(
            DrvBitBlt(&surface, NULL, NULL, NULL, NULL, &tiny, NULL, NULL, &brush, NULL, 0xf0f0));
        assert(submits == before + 1 && fallbacks == cpu_before && mixed && !coheres);
        /* Unsupported CPU write requires whole-primary coherence. */
        assert(
            DrvBitBlt(&surface, &memory, NULL, NULL, NULL, &tiny, &zero, NULL, NULL, NULL, 0xcccc));
        assert(coheres == 1 && !mixed && fallbacks == cpu_before + 1);
        mixed = 1;
        /* Primary source into a memory bitmap is a CPU read too. */
        assert(DrvCopyBits(&memory, &surface, NULL, NULL, &tiny, &zero));
        assert(coheres == 2 && !mixed && fallbacks == cpu_before + 2);
        mixed = 1;
        fail_cohere = TRUE;
        assert(!DrvCopyBits(&memory, &surface, NULL, NULL, &tiny, &zero));
        assert(mixed && fallbacks == cpu_before + 2);
    }
    {
        ULONG before = submits, cpu_before = fallbacks, cohere_before = coheres;
        dev.BitsPerPixel = 16;
        dev.ScreenDelta = 64;
        fail_cohere = FALSE;
        mixed = 1;
        // Even a solid primary fill must first return native-owned RGB565
        // pixels and use DIB semantics, never the RGBA-only GDI shortcut.
        assert(
            DrvBitBlt(&surface, NULL, NULL, NULL, NULL, &full, NULL, NULL, &brush, NULL, 0xf0f0));
        assert(submits == before && fallbacks == cpu_before + 1 && coheres == cohere_before + 1 &&
               !mixed);
        mixed = 1;
        fail_cohere = TRUE;
        assert(
            !DrvBitBlt(&surface, NULL, NULL, NULL, NULL, &full, NULL, NULL, &brush, NULL, 0xf0f0));
        assert(submits == before && fallbacks == cpu_before + 1 && mixed);
    }
    puts("native GDI blit: clipped overlap, fill, unsupported source and failure propagation "
         "passed");
    return 0;
}
