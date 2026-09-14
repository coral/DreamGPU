/* SPDX-License-Identifier: GPL-2.0-or-later
 * Native 2D operations on the CPU-authoritative primary. Other GDI operations
 * use the existing engine bitmap directly. Every submitted batch completes
 * before returning, so the DIB engine never sees an outstanding host writer.
 */
extern "C" {
#include "framebuf.h"
#include "dg-ioctl.h"
#include "dg-kernel.h"

static BOOL WorthNativeBlt(PPDEV dev, RECTL *whole, CLIPOBJ *clip) {
    RECTL rect = *whole;
    if (clip && clip->iDComplexity != DC_TRIVIAL) {
        rect.left = max(rect.left, clip->rclBounds.left);
        rect.top = max(rect.top, clip->rclBounds.top);
        rect.right = min(rect.right, clip->rclBounds.right);
        rect.bottom = min(rect.bottom, clip->rclBounds.bottom);
    }
    rect.left = max(rect.left, 0);
    rect.top = max(rect.top, 0);
    rect.right = min(rect.right, (LONG)dev->ScreenWidth);
    rect.bottom = min(rect.bottom, (LONG)dev->ScreenHeight);
    if (rect.left >= rect.right || rect.top >= rect.bottom)
        return FALSE;
    /* Measured NT5/TCG repaint traces were dominated by 512-byte rectangles:
     * a kernel submission costs more than writing these few pixels. Keep tiny
     * CPU-authoritative blits in the DIB engine. Bound by the clipped rectangle
     * so a mostly obscured large window does not force a tiny device request. */
    return (ULONG)(rect.right - rect.left) * (ULONG)(rect.bottom - rect.top) >
           512 / (dev->BitsPerPixel / 8);
}

static BOOL Submit(PPDEV dev, DG_BATCH *batch) {
    if (!batch->Count)
        return TRUE;
    if (!dev->Kernel.Submit ||
        !dev->Kernel.Submit(dev->Kernel.Context, batch->Commands, batch->Count))
        return FALSE;
    batch->Count = 0;
    return TRUE;
}

static BOOL Append(PPDEV dev, DG_BATCH *batch, ULONG opcode, RECTL *whole, RECTL *clip,
                   POINTL *source, ULONG color) {
    RECTL rect;
    DG_COMMAND *command;
    LONG sx, sy;
    rect.left = max(max(whole->left, clip->left), 0);
    rect.top = max(max(whole->top, clip->top), 0);
    rect.right = min(min(whole->right, clip->right), (LONG)dev->ScreenWidth);
    rect.bottom = min(min(whole->bottom, clip->bottom), (LONG)dev->ScreenHeight);
    if (rect.left >= rect.right || rect.top >= rect.bottom)
        return TRUE;
    command = &batch->Commands[batch->Count];
    memset(command, 0, sizeof(*command));
    command->Opcode = opcode;
    command->Bpp = dev->BitsPerPixel / 8;
    command->Destination = rect.top * dev->ScreenDelta + rect.left * command->Bpp;
    command->DestinationStride = dev->ScreenDelta;
    command->Width = rect.right - rect.left;
    command->Height = rect.bottom - rect.top;
    if (opcode == DG_CMD_COPY) {
        sx = source->x + rect.left - whole->left;
        sy = source->y + rect.top - whole->top;
        command->Source = sy * dev->ScreenDelta + sx * command->Bpp;
        command->SourceStride = dev->ScreenDelta;
    } else {
        command->Color = color;
    }
    ++batch->Count;
    return batch->Count < DG_MAX_COMMANDS || Submit(dev, batch);
}

static BOOL NativeBlt(PPDEV dev, ULONG opcode, RECTL *rect, POINTL *source, CLIPOBJ *clip,
                      ULONG color) {
    DG_BATCH batch;
    ULONG direction = CD_RIGHTDOWN;
    BOOL more;
    struct {
        ULONG count;
        RECTL rectangles[32];
    } enumeration;
    ULONG i;
    batch.Count = 0;
    if (!clip || clip->iDComplexity == DC_TRIVIAL)
        return Append(dev, &batch, opcode, rect, rect, source, color) && Submit(dev, &batch);
    if (clip->iDComplexity == DC_RECT)
        return Append(dev, &batch, opcode, rect, &clip->rclBounds, source, color) &&
               Submit(dev, &batch);
    /* Order clipped copies as well as pixels inside each copy. Otherwise an
     * earlier clipped rectangle could overwrite a later rectangle's source. */
    if (opcode == DG_CMD_COPY) {
        if (rect->top > source->y)
            direction = rect->left > source->x ? CD_LEFTUP : CD_RIGHTUP;
        else
            direction = rect->left > source->x ? CD_LEFTDOWN : CD_RIGHTDOWN;
    }
    CLIPOBJ_cEnumStart(clip, FALSE, CT_RECTANGLES, direction, 0);
    do {
        more = CLIPOBJ_bEnum(clip, sizeof(enumeration), (ULONG *)&enumeration);
        for (i = 0; i < enumeration.count; ++i)
            if (!Append(dev, &batch, opcode, rect, &enumeration.rectangles[i], source, color))
                return FALSE;
    } while (more);
    return Submit(dev, &batch);
}

BOOL APIENTRY DrvBitBlt(SURFOBJ *dest, SURFOBJ *source, SURFOBJ *mask, CLIPOBJ *clip,
                        XLATEOBJ *xlate, RECTL *rect, POINTL *source_point, POINTL *mask_point,
                        BRUSHOBJ *brush, POINTL *brush_origin, ROP4 rop) {
    PPDEV dev = (PPDEV)dest->dhpdev;
    ULONG opcode = 0, color = 0;
    if (dev && dev->BitsPerPixel == 32 && dev->NativeCaps && dest->hsurf == dev->hSurfEng &&
        dev->ScreenDelta > 0 && !mask && rect->left < rect->right && rect->top < rect->bottom) {
        if (rop == 0x0000 || rop == 0xffff ||
            (rop == 0xf0f0 && brush && brush->iSolidColor != 0xffffffff)) {
            opcode = DG_CMD_FILL;
            color = rop == 0 ? 0 : rop == 0xffff ? 0xffffffff : brush->iSolidColor;
        } else if (rop == 0xcccc && source && source_point && source->hsurf == dest->hsurf &&
                   (!xlate || (xlate->flXlate & XO_TRIVIAL)) && source_point->x >= 0 &&
                   source_point->y >= 0 &&
                   source_point->x <= (LONG)dev->ScreenWidth - (rect->right - rect->left) &&
                   source_point->y <= (LONG)dev->ScreenHeight - (rect->bottom - rect->top)) {
            opcode = DG_CMD_COPY;
        }
        if (opcode && (DgDesktopActive(dev) || WorthNativeBlt(dev, rect, clip)))
            return NativeBlt(dev, opcode, rect, source_point, clip, color);
    }
    /* RGB565 uses the DIB engine after returning any native desktop to CPU
     * ownership; native GDI color/ROP shortcuts currently target 32-bit pixels.
     * Covers both CPU writes to the primary and CPU reads from it (for
     * example BitBlt into a memory DC). EngBitBlt can bypass the sync hook
     * when called by this driver, so synchronize explicitly before fallback. */
    if (dev && dest->hsurf == dev->hSurfEng && !DgCoherent(dev, DG_COHERE_DESTINATION))
        return FALSE;
    if (source && source->dhpdev) {
        PPDEV source_dev = (PPDEV)source->dhpdev;
        if (source->hsurf == source_dev->hSurfEng && !DgCoherent(source_dev, DG_COHERE_SOURCE))
            return FALSE;
    }
    return EngBitBlt(dest, source, mask, clip, xlate, rect, source_point, mask_point, brush,
                     brush_origin, rop);
}

BOOL APIENTRY DrvCopyBits(SURFOBJ *dest, SURFOBJ *source, CLIPOBJ *clip, XLATEOBJ *xlate,
                          RECTL *rect, POINTL *point) {
    return DrvBitBlt(dest, source, NULL, clip, xlate, rect, point, NULL, NULL, NULL, 0xcccc);
}

} /* extern C */
