/*
 * Source: vendor/reactos/win32ss/drivers/displays/framebuf/pointer.c
 *         @ 22fb3bb2c1d8196cf501edbb49b3814739f7b016
 * Upstream: https://github.com/reactos/reactos
 * Copied with modifications; translated from C to C++23.
 */

/*
 * ReactOS Generic Framebuffer display driver
 *
 * Copyright (C) 2004 Filip Navara
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

extern "C" {
#include "framebuf.h"

/* GDI software save-under forces a complete mixed desktop readback for each
 * overlapping present. Native pointers are a separate display plane, including
 * exact monochrome/color AND-XOR pixels that alpha cursors cannot represent. */
static BOOL PointerRows(SURFOBJ *surface, ULONG width, ULONG height, ULONG bits) {
    ULONG bytes, stride;
    ULONG_PTR base, first, last;
    if (!surface || !surface->pvBits || !surface->pvScan0 ||
        surface->sizlBitmap.cx != (LONG)width || surface->sizlBitmap.cy != (LONG)height ||
        !height || surface->lDelta == (-2147483647L - 1))
        return FALSE;
    bytes = (width * bits + 7) / 8;
    stride = surface->lDelta < 0 ? -surface->lDelta : surface->lDelta;
    if (stride < bytes || stride > 1024 * 1024)
        return FALSE;
    base = (ULONG_PTR)surface->pvBits;
    first = (ULONG_PTR)surface->pvScan0;
    last = surface->lDelta < 0 ? first - (height - 1) * stride : first + (height - 1) * stride;
    if (first < base || last < base || surface->cjBits < bytes)
        return FALSE;
    return first - base <= surface->cjBits - bytes && last - base <= surface->cjBits - bytes;
}

static ULONG PointerBit(SURFOBJ *surface, ULONG x, ULONG y) {
    const BYTE *row = (const BYTE *)surface->pvScan0 + (LONG)y * surface->lDelta;
    return (row[x / 8] >> (7 - (x & 7))) & 1;
}

static ULONG PointerPixel(SURFOBJ *surface, ULONG x, ULONG y) {
    const BYTE *row = (const BYTE *)surface->pvScan0 + (LONG)y * surface->lDelta;
    switch (surface->iBitmapFormat) {
        case BMF_1BPP:
            return PointerBit(surface, x, y);
        case BMF_4BPP:
            return (row[x / 2] >> ((x & 1) ? 0 : 4)) & 15;
        case BMF_8BPP:
            return row[x];
        case BMF_16BPP:
            row += x * 2;
            return row[0] | ((ULONG)row[1] << 8);
        case BMF_24BPP:
            row += x * 3;
            return row[0] | ((ULONG)row[1] << 8) | ((ULONG)row[2] << 16);
        default:
            row += x * 4;
            return row[0] | ((ULONG)row[1] << 8) | ((ULONG)row[2] << 16) | ((ULONG)row[3] << 24);
    }
}

static BOOL PointerShape(PPDEV pdev, SURFOBJ *mask, SURFOBJ *color, XLATEOBJ *xlate, LONG hot_x,
                         LONG hot_y, FLONG flags, DG_KERNEL_CURSOR_REQUEST *request) {
    ULONG width, height, bits = 0, row, col, pixel, *out;
    if (flags & ~(SPS_CHANGE | SPS_ANIMATESTART | SPS_ANIMATEUPDATE | SPS_ALPHA))
        return FALSE;
    if (flags & SPS_ALPHA) {
        if (mask || !color || color->iBitmapFormat != BMF_32BPP)
            return FALSE;
        width = color->sizlBitmap.cx;
        height = color->sizlBitmap.cy;
        request->Format = DG_CURSOR_ARGB_PREMULTIPLIED;
    } else {
        if (!mask || mask->iBitmapFormat != BMF_1BPP || (mask->sizlBitmap.cy & 1))
            return FALSE;
        width = mask->sizlBitmap.cx;
        height = mask->sizlBitmap.cy / 2;
        request->Format = DG_CURSOR_AND_XOR;
    }
    if (!width || !height || width > DG_CURSOR_MAX_DIMENSION || height > DG_CURSOR_MAX_DIMENSION ||
        hot_x < 0 || hot_y < 0 || (ULONG)hot_x >= width || (ULONG)hot_y >= height)
        return FALSE;
    if (mask && !PointerRows(mask, width, height * 2, 1))
        return FALSE;
    if (color) {
        switch (color->iBitmapFormat) {
            case BMF_1BPP:
                bits = 1;
                break;
            case BMF_4BPP:
                bits = 4;
                break;
            case BMF_8BPP:
                bits = 8;
                break;
            case BMF_16BPP:
                bits = 16;
                break;
            case BMF_24BPP:
                bits = 24;
                break;
            case BMF_32BPP:
                bits = 32;
                break;
            default:
                return FALSE;
        }
        if (!PointerRows(color, width, height, bits))
            return FALSE;
        if (!(flags & SPS_ALPHA)) {
            /* XOR must happen in the primary's pixel space. Palette lookup
             * and 16-bit component expansion do not commute with XOR, so
             * those masked-color shapes keep GDI's exact fallback. Ordinary
             * monochrome and premultiplied alpha are independent planes. */
            if (pdev->BitsPerPixel != 32 || pdev->RedMask != 0xff0000 ||
                pdev->GreenMask != 0xff00 || pdev->BlueMask != 0xff)
                return FALSE;
            if (!xlate && bits != 32)
                return FALSE;
        }
    }
    if (!pdev->PointerPixels) {
        pdev->PointerPixels = (ULONG *)EngAllocMem(0, DG_CURSOR_MAX_BYTES, ALLOC_TAG);
        if (!pdev->PointerPixels)
            return FALSE;
    }
    out = pdev->PointerPixels;
    for (row = 0; row < height; ++row)
        for (col = 0; col < width; ++col) {
            if (flags & SPS_ALPHA) {
                ULONG alpha;
                pixel = PointerPixel(color, col, row);
                alpha = pixel >> 24;
                if ((pixel & 255) > alpha || ((pixel >> 8) & 255) > alpha ||
                    ((pixel >> 16) & 255) > alpha)
                    return FALSE;
                *out++ = pixel;
                *out++ = 0;
            } else {
                *out++ = PointerBit(mask, col, row) ? 0x00ffffff : 0;
                if (color) {
                    pixel = PointerPixel(color, col, row);
                    if (xlate && !(xlate->flXlate & XO_TRIVIAL))
                        pixel = XLATEOBJ_iXlate(xlate, pixel);
                    *out++ = pixel & 0x00ffffff;
                } else
                    *out++ = PointerBit(mask, col, row + height) ? 0x00ffffff : 0;
            }
        }
    request->Operation = DG_CURSOR_SHAPE;
    request->Width = width;
    request->Height = height;
    request->HotX = hot_x;
    request->HotY = hot_y;
    request->Pixels = pdev->PointerPixels;
    return TRUE;
}

static BOOL PointerMove(PPDEV pdev, LONG x, LONG y, ULONG flags) {
    DG_KERNEL_CURSOR_REQUEST request;
    memset(&request, 0, sizeof(request));
    request.Operation = DG_CURSOR_MOVE;
    request.X = x;
    request.Y = y;
    request.Flags = flags;
    return pdev->Kernel.Cursor && pdev->Kernel.Cursor(pdev->Kernel.Context, &request);
}

VOID DgDisablePointer(PPDEV pdev) {
    if (pdev->PointerNative)
        PointerMove(pdev, 0, 0, 0);
    pdev->PointerNative = pdev->PointerHasShape = pdev->PointerSoftware = FALSE;
    if (pdev->PointerPixels)
        EngFreeMem(pdev->PointerPixels);
    pdev->PointerPixels = NULL;
}

ULONG APIENTRY DrvSetPointerShape(SURFOBJ *surface, SURFOBJ *mask, SURFOBJ *color, XLATEOBJ *xlate,
                                  LONG hot_x, LONG hot_y, LONG x, LONG y, RECTL *bounds,
                                  FLONG flags) {
    PPDEV pdev = surface ? (PPDEV)surface->dhpdev : NULL;
    DG_KERNEL_CURSOR_REQUEST request;
    BOOL native = FALSE;
    ULONG result;
    if (!pdev)
        return SPS_ERROR;
    memset(&request, 0, sizeof(request));
    request.X = x;
    request.Y = y;
    request.Flags = DG_CURSOR_NATIVE_ENABLED | (x >= 0 ? DG_CURSOR_VISIBLE : 0);
    if (pdev->PointerSoftware) {
        if (!DgCoherent(pdev, DG_COHERE_DESTINATION))
            return SPS_ERROR;
        EngMovePointer(surface, -1, -1, NULL);
        pdev->PointerSoftware = FALSE;
    }
    if (!mask && !color &&
        !(flags & ~(SPS_CHANGE | SPS_ANIMATESTART | SPS_ANIMATEUPDATE | SPS_ALPHA))) {
        native = PointerMove(pdev, x, y, DG_CURSOR_NATIVE_ENABLED);
        pdev->PointerHasShape = FALSE;
    } else if (pdev->Kernel.Cursor &&
               PointerShape(pdev, mask, color, xlate, hot_x, hot_y, flags, &request)) {
        native = pdev->Kernel.Cursor(pdev->Kernel.Context, &request);
        if (native)
            pdev->PointerHasShape = TRUE;
    }
    if (native) {
        pdev->PointerNative = TRUE;
        if (bounds)
            memset(bounds, 0, sizeof(*bounds)); /* no exclusion region */
        return SPS_ACCEPT_NOEXCLUDE;
    }
    /* GDI remains the exact fallback for oversized shapes or unsupported
     * flags/formats. It must see coherent pixels before its software save. */
    if (pdev->PointerNative)
        PointerMove(pdev, 0, 0, 0);
    pdev->PointerNative = pdev->PointerHasShape = FALSE;
    if (!DgCoherent(pdev, DG_COHERE_DESTINATION))
        return SPS_ERROR;
    result = EngSetPointerShape(surface, mask, color, xlate, hot_x, hot_y, x, y, bounds, flags);
    pdev->PointerSoftware = result == SPS_ACCEPT_EXCLUDE || result == SPS_ACCEPT_NOEXCLUDE;
    return result;
}

VOID APIENTRY DrvMovePointer(SURFOBJ *surface, LONG x, LONG y, RECTL *bounds) {
    PPDEV pdev = surface ? (PPDEV)surface->dhpdev : NULL;
    if (!pdev)
        return;
    if (pdev->PointerNative) {
        PointerMove(pdev, x, y,
                    DG_CURSOR_NATIVE_ENABLED |
                        ((x >= 0 && pdev->PointerHasShape) ? DG_CURSOR_VISIBLE : 0));
        if (bounds)
            memset(bounds, 0, sizeof(*bounds));
    } else if (DgCoherent(pdev, DG_COHERE_DESTINATION))
        EngMovePointer(surface, x, y, bounds);
}

} /* extern C */
