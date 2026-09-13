/* SPDX-License-Identifier: GPL-2.0-or-later
 * Included in dibcall.c. CURSORSHAPE belongs to GDI for this call only.
 * Capture through bounded register payloads; neither VxD nor host retains it.
 */
/* The Win16 stack segment differs from the driver data segment. */
#ifndef DG9_CURSOR_LOCAL
#define DG9_CURSOR_LOCAL __far
#endif
#include "dg-cursor.h"
extern WORD DgNativeCursor(DWORD operation, DWORD a, DWORD b, DWORD c);
extern BOOL Dg9CursorPrepareNative16(void);
#ifndef DG9_CURSOR_OFFSET
#define DG9_CURSOR_OFFSET(pointer) ((DWORD)(pointer) & 0xffff)
#endif
static BOOL dg_cursor16_native, dg_cursor16_visible;
static short dg_cursor16_x, dg_cursor16_y;

static DWORD Dg9CursorPosition16(void) {
    return (WORD)dg_cursor16_x | ((DWORD)(WORD)dg_cursor16_y << 16);
}
static WORD Dg9CursorFallback16(void) {
    DgNativeCursor(DG9_CURSOR_ABORT, 0, 0, 0);
    if (dg_cursor16_native && !DgNativeCursor(DG9_CURSOR_RELEASE, 0, 0, 0))
        return DG9_CURSOR_FAILED;
    dg_cursor16_native = dg_cursor16_visible = FALSE;
    return DG9_CURSOR_FALLBACK;
}
WORD Dg9CursorSet16(CURSORSHAPE __far *shape) {
    BYTE header[12];
    const BYTE __far *bytes = (const BYTE __far *)shape;
    DG9_CURSOR_LAYOUT layout;
    DWORD offset, words[3], at, i, count;
    if (!shape) {
        if (!DgNativeCursor(DG9_CURSOR_AVAILABLE, 0, 0, 0))
            return Dg9CursorFallback16();
        if (!dg_cursor16_native && !Dg9CursorPrepareNative16())
            return DG9_CURSOR_FAILED;
        dg_cursor16_native = TRUE; /* A failed reply must retire possible native ownership. */
        if (!DgNativeCursor(DG9_CURSOR_MOVE, Dg9CursorPosition16(), DG_CURSOR_NATIVE_ENABLED, 0))
            return Dg9CursorFallback16();
        dg_cursor16_native = TRUE;
        dg_cursor16_visible = FALSE;
        return DG9_CURSOR_NATIVE;
    }
    offset = DG9_CURSOR_OFFSET(shape);
    if (offset > 65536UL - sizeof(header))
        return Dg9CursorFallback16();
    for (i = 0; i < sizeof(header); ++i)
        header[i] = bytes[i];
    if (!Dg9CursorLayout(header, wBpp, &layout) || layout.Bytes > 65536UL - offset - sizeof(header))
        return Dg9CursorFallback16();
    words[0] = words[1] = words[2] = 0;
    for (i = 0; i < sizeof(header); ++i)
        words[i / 4] |= (DWORD)header[i] << ((i % 4) * 8);
    if (!DgNativeCursor(DG9_CURSOR_BEGIN, words[0], words[1], words[2]))
        return Dg9CursorFallback16();
    bytes += sizeof(header);
    for (at = 0; at < layout.Bytes; at += count) {
        count = layout.Bytes - at;
        if (count > 12)
            count = 12;
        words[0] = words[1] = words[2] = 0;
        for (i = 0; i < count; ++i)
            words[i / 4] |= (DWORD)bytes[at + i] << ((i % 4) * 8);
        if (!DgNativeCursor(DG9_CURSOR_DATA, words[0], words[1], words[2]))
            return Dg9CursorFallback16();
    }
    if (!dg_cursor16_native && !Dg9CursorPrepareNative16()) {
        DgNativeCursor(DG9_CURSOR_ABORT, 0, 0, 0);
        return DG9_CURSOR_FAILED;
    }
    dg_cursor16_native = TRUE; /* Commit may succeed even when completion is uncertain. */
    if (!DgNativeCursor(DG9_CURSOR_COMMIT, Dg9CursorPosition16(), 0, 0))
        return Dg9CursorFallback16();
    dg_cursor16_native = dg_cursor16_visible = TRUE;
    return DG9_CURSOR_NATIVE;
}
WORD Dg9CursorMove16(int x, int y) {
    dg_cursor16_x = x;
    dg_cursor16_y = y;
    if (!dg_cursor16_native)
        return DG9_CURSOR_FALLBACK;
    if (!DgNativeCursor(DG9_CURSOR_MOVE, Dg9CursorPosition16(),
                        DG_CURSOR_NATIVE_ENABLED | (dg_cursor16_visible ? DG_CURSOR_VISIBLE : 0),
                        0))
        return DG9_CURSOR_FAILED;
    return DG9_CURSOR_NATIVE;
}
WORD Dg9CursorCheck16(void) {
    if (!dg_cursor16_native)
        return DG9_CURSOR_FALLBACK;
    return DgNativeCursor(DG9_CURSOR_QUERY, 0, 0, 0) ? DG9_CURSOR_NATIVE : DG9_CURSOR_FAILED;
}
/* Disable/mode transition calls this before handing the surface back to GDI. */
WORD Dg9CursorDisable16(void) {
    return Dg9CursorFallback16();
}
