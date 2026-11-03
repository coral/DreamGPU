/* SPDX-License-Identifier: GPL-2.0-or-later
 * The source is a private compatible bitmap. Its small tag names an OS-owned
 * VxD binding; only GDI's current BitBlt call provides the destination clips.
 */
#include "dg-window9.h"
#ifndef DG9_WINDOW_WORDS
#define DG9_WINDOW_WORDS(src)                                                                      \
    ((DWORD __far *)(((DWORD)(src)->deBitsSelector << 16) | (src)->deBitsOffset))
#endif
extern WORD DgWindowBlt16(DWORD binding, DWORD source, DWORD destination, DWORD extent);
static WORD DgWindowSource(LPDIBENGINE dst, LPPDEVICE source, WORD dx, WORD dy, WORD sx, WORD sy,
                           WORD width, WORD height, DWORD rop) {
    LPDIBENGINE src = (LPDIBENGINE)source;
    DG9_BITMAP_TAG tag;
    DWORD __far *words;
    /* The first scanline of a 16-pixel bitmap holds the whole 16-byte tag at
     * either accepted depth, so the tag never straddles row padding. */
    if (dst != lpDriverPDevice || !source || rop != 0x00cc0020UL || src->deType != TYPE_DIBENG ||
        (src->deBitsPixel != 16 && src->deBitsPixel != 32) || src->dePlanes != 1 ||
        src->deFlags & VRAM || !src->deBitsSelector || src->deBitsOffset > 65520UL ||
        src->deWidth < 16 || !src->deHeight)
        return 0;
    words = DG9_WINDOW_WORDS(src);
    tag.Magic0 = words[0];
    tag.Magic1 = words[1];
    if (tag.Magic0 != DG9_BITMAP_MAGIC0 || tag.Magic1 != DG9_BITMAP_MAGIC1)
        return 0;
    tag.Binding = words[2];
    tag.Reserved = words[3];
    /* Never mixed depths; a 16-bpp primary must be the RGB565 layout. */
    if (!tag.Binding || tag.Reserved || dst->deBitsPixel != src->deBitsPixel ||
        (dst->deBitsPixel == 16 && !(dst->deFlags & FIVE6FIVE)) ||
        src->deFlags & (BUSY | SELECTEDDIB) || dst->deFlags & BUSY)
        return 2;
    return DgWindowBlt16(tag.Binding, sx | ((DWORD)sy << 16), dx | ((DWORD)dy << 16),
                         width | ((DWORD)height << 16))
               ? 1
               : 2;
}
