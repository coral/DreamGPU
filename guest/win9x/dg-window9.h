/* SPDX-License-Identifier: GPL-2.0-or-later
 * Win98 owned compatible-bitmap bridge. User packets carry no desktop clip
 * rectangles or addresses. GDI supplies every actual destination rectangle.
 */
#ifndef DG_WINDOW9_H
#define DG_WINDOW9_H
#define DG9_WINDOW_IOCTL 0x4a524709UL
#define DG9_WINDOW_VERSION 1
#define DG9_WINDOW_BIND 1
#define DG9_WINDOW_PREPARE 2
#define DG9_WINDOW_FINISH 3
#define DG9_WINDOW_RELEASE 4
#define DG9_WINDOW_FRONT_ONLY 1
#define DG9_BITMAP_MAGIC0 0x4a524739UL
#define DG9_BITMAP_MAGIC1 0x31444242UL
#define DG9_WINDOW_SLOTS 64
/* Bind uses Client, Context, Drawable, Width, Height; Binding/Flags zero.
 * Other operations use Client/Binding; PREPARE alone accepts FRONT_ONLY.
 */
typedef struct {
    DWORD Version, Operation, Client, Binding, Context, Drawable, Width, Height, Flags;
} DG9_WINDOW_REQUEST;
typedef struct {
    DWORD Version, Status, Binding, Capabilities;
} DG9_WINDOW_REPLY;
typedef struct {
    DWORD Magic0, Magic1, Binding, Reserved;
} DG9_BITMAP_TAG;
#endif
