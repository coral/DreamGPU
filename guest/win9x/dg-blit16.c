/* SPDX-License-Identifier: GPL-2.0-or-later
 * Included in dibcall.c instead of the unused upstream BitBltDevProc hook.
 * GDI supplies already-clipped rectangles here. Only primary-screen copies
 * and BLACKNESS/WHITENESS fills have unambiguous pixel semantics at this ABI.
 */
#include "dg-window16.c"
extern WORD DgNativeBlt(DWORD operation, DWORD source, DWORD destination, DWORD extent);

BOOL WINAPI __loadds BitBlt(LPDIBENGINE dst, WORD dx, WORD dy, LPPDEVICE src, WORD sx, WORD sy,
                            WORD width, WORD height, DWORD rop, LPBRUSH brush, LPDRAWMODE draw) {
    WORD operation = 0, result, left, top, right, bottom;
    result = DgWindowSource(dst, src, dx, dy, sx, sy, width, height, rop);
    if (result)
        return result == 1;
    if (dst == lpDriverPDevice && (dst->deFlags & VRAM) && !(dst->deFlags & BUSY) &&
        (dst->deBitsPixel == 8 || dst->deBitsPixel == 16 || dst->deBitsPixel == 32) && width &&
        height && (DWORD)dx + width <= dst->deWidth && (DWORD)dy + height <= dst->deHeight) {
        if (rop == 0x00000042UL)
            operation = 1; /* BLACKNESS */
        else if (rop == 0x00ff0062UL)
            operation = 2; /* WHITENESS */
        else if (rop == 0x00cc0020UL && (LPDIBENGINE)src == dst &&
                 (DWORD)sx + width <= dst->deWidth && (DWORD)sy + height <= dst->deHeight)
            operation = 3; /* SRCCOPY */
    }
    if (operation) {
        left = dx;
        top = dy;
        right = dx + width;
        bottom = dy + height;
        if (operation == 3) {
            if (sx < left)
                left = sx;
            if (sy < top)
                top = sy;
            if (sx + width > right)
                right = sx + width;
            if (sy + height > bottom)
                bottom = sy + height;
        }
        /* The software pointer must be excluded from both the source and
         * destination before the device touches VRAM. Completion precedes
         * EndAccess, so restoring the pointer cannot race the blit. */
        DIB_BeginAccess((LPPDEVICE)dst, left, top, right, bottom, CURSOREXCLUDE);
        result = DgNativeBlt(operation, sx | ((DWORD)sy << 16), dx | ((DWORD)dy << 16),
                             width | ((DWORD)height << 16));
        DIB_EndAccess((LPPDEVICE)dst, CURSOREXCLUDE);
        if (result == 1)
            return TRUE;
        /* Once a command has been submitted it may have partially changed an
         * overlapping source. Replaying it in software would corrupt pixels. */
        if (result == 2)
            return FALSE;
    }
    return DIB_BitBlt(dst, dx, dy, src, sx, sy, width, height, rop, brush, draw);
}
