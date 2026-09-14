/* SPDX-License-Identifier: GPL-2.0-or-later
 * NT5 window presentation. WNDOBJ_SETUP runs under GDI's window lock; actual
 * drawing uses DrawEscape, since DrvEscape must not draw on the device.
 * No caller-supplied screen rectangle or kernel pointer crosses this ABI.
 */
#ifndef DG_WINDOW_H
#define DG_WINDOW_H
#define DG_WINDOW_MAGIC 0x4a524757UL
#define DG_WINDOW_VERSION 1
#define DG_DRAW_ESCAPE 0x4a524702UL
/* DrawEscape outcomes known to precede every kernel presentation. Rebinding
 * may be attempted once; an incomplete clip snapshot waits for GDI's callback.
 * Neither outcome means a GL command or a buffer exchange should be replayed. */
#define DG_WINDOW_PRESENT_REBIND 2
#define DG_WINDOW_PRESENT_NOT_READY 3
/* Optional response capability. Legacy replies leave this word zero. */
#define DG_WINDOW_CAP_FRONT_ONLY 1
/* Publish the current front buffer without exchanging front/back. This is
 * accepted only when the binding reply advertises CAP_FRONT_ONLY. */
#define DG_WINDOW_PRESENT_FRONT_ONLY 1
#define DG_WINDOW_PRESENT_FLAGS_MASK DG_WINDOW_PRESENT_FRONT_ONLY
#ifndef WNDOBJ_SETUP
#define WNDOBJ_SETUP 4354
#endif
typedef struct {
    unsigned int Window; /* HWND first, as required by WNDOBJ_SETUP. */
    unsigned int Magic, Version, Client, Context, Drawable, Reserved;
} DG_WINDOW_BIND;
typedef struct {
    unsigned int Version, Status, Binding, Capabilities;
} DG_WINDOW_REPLY;
typedef struct {
    unsigned int Magic, Version, Binding, Flags; /* Zero exchanges front/back. */
} DG_WINDOW_PRESENT;
#endif
