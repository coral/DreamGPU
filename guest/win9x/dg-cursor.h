/* SPDX-License-Identifier: GPL-2.0-or-later
 * Private register-only Win16 cursor call gate (service 0x4a02).
 * No far/flat caller pointer crosses into the VxD.
 */
#ifndef DG9_CURSOR_H
#define DG9_CURSOR_H
#include "cursor.h"
#define DG9_CURSOR_BEGIN 1
#define DG9_CURSOR_DATA 2
#define DG9_CURSOR_COMMIT 3
#define DG9_CURSOR_MOVE 4
#define DG9_CURSOR_RELEASE 5
#define DG9_CURSOR_ABORT 6
#define DG9_CURSOR_QUERY 7
#define DG9_CURSOR_AVAILABLE 8
#define DG9_CURSOR_NATIVE 1
#define DG9_CURSOR_FALLBACK 0
#define DG9_CURSOR_FAILED 2
#define DG9_CURSOR_RAW_MAX 32768UL
#define DG9_CURSOR_HEADER_BYTES 12

typedef struct {
    DWORD Width, Height, HotX, HotY, AndStride, XorStride, Bits, Bytes;
} DG9_CURSOR_LAYOUT;

#ifndef DG9_CURSOR_LOCAL
#define DG9_CURSOR_LOCAL
#endif
static DWORD Dg9CursorWord(const BYTE DG9_CURSOR_LOCAL *bytes) {
    return bytes[0] | ((DWORD)bytes[1] << 8);
}

/* CURSORSHAPE is the packed DDK12-byte header followed by AND then XOR.
 * cbWidth is the AND stride (also XOR for monochrome), never a guessed width.
 * Color XOR rows are csBitsPixel wide. Win98 USER passes a color cursor whose
 * bitmap matches the desktop depth at that depth and converts every other
 * color cursor (Wine's 32-bit D3D8/9 cursors on a 16-bpp desktop, 16-color
 * .cur resources) to packed 4-bit indices of the standard 16-color palette,
 * high nibble first, in WORD-aligned rows.
 */
static BOOL Dg9CursorLayout(const BYTE DG9_CURSOR_LOCAL *header, DWORD bpp,
                            DG9_CURSOR_LAYOUT DG9_CURSOR_LOCAL *layout) {
    DWORD w = Dg9CursorWord(header + 4), h = Dg9CursorWord(header + 6);
    DWORD hotx = Dg9CursorWord(header), hoty = Dg9CursorWord(header + 2);
    DWORD stride = Dg9CursorWord(header + 8), bits = header[11], xor_stride;
    if (!w || !h || w > DG_CURSOR_MAX_DIMENSION || h > DG_CURSOR_MAX_DIMENSION || hotx >= w ||
        hoty >= h || header[10] != 1 || stride < (w + 7) / 8 || stride > 256)
        return FALSE;
    /* XOR colors convert to the desktop format (see cursor32.cpp). 8-bit
     * indices would need the system palette, so they stay with GDI. */
    if ((bpp != 16 && bpp != 32) || (bits != 1 && bits != 4 && bits != 16 && bits != 32))
        return FALSE;
    xor_stride = bits == 1 ? stride : bits == 4 ? (w + 3) / 4 * 2 : w * (bits / 8);
    if ((stride + xor_stride) * h > DG9_CURSOR_RAW_MAX)
        return FALSE;
    layout->Width = w;
    layout->Height = h;
    layout->HotX = hotx;
    layout->HotY = hoty;
    layout->AndStride = stride;
    layout->XorStride = xor_stride;
    layout->Bits = bits;
    layout->Bytes = (stride + xor_stride) * h;
    return TRUE;
}
#endif
