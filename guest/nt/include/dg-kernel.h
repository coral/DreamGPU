/* SPDX-License-Identifier: GPL-2.0-or-later
 * Private kernel interface. The device-control dispatch checks the actual
 * IRP RequestorMode before VideoPort may return any kernel addresses.
 */
#ifndef DG_KERNEL_H
#define DG_KERNEL_H
#include "dg-ioctl.h"
#include "cursor.h"
#include "dg-window.h"
#define IOCTL_VIDEO_DG_KERNEL CTL_CODE(FILE_DEVICE_VIDEO, 0x903, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define DG_KERNEL_VERSION 7
#ifndef DG_KERNEL_CALL
#define DG_KERNEL_CALL __stdcall
#endif
typedef ULONG(DG_KERNEL_CALL *DG_KERNEL_SUBMIT)(void *, const DG_COMMAND *, ULONG);
typedef struct {
    LONG Left, Top, Right, Bottom;
} DG_WINDOW_RECT;
#define DG_WINDOW_MAX_CLIPS 128
typedef struct {
    ULONG Client, Context, Drawable;
    ULONG Width, Height, Stride;
    LONG WindowX, WindowY;
    ULONG WindowWidth, WindowHeight, Count, Flags;
    DG_WINDOW_RECT Clips[DG_WINDOW_MAX_CLIPS];
    ULONG Bpp; /* Actual primary storage: 16 RGB565 or 32 XRGB8888. */
} DG_KERNEL_PRESENT_REQUEST;
/* Zero rejects the client; otherwise returns its current kernel process
 * identity. This value stays inside the matched miniport/display pair and is
 * never copied into a user reply. Version 5 returned only a boolean. */
typedef ULONG(DG_KERNEL_CALL *DG_KERNEL_OWNER)(void *, ULONG);
typedef ULONG(DG_KERNEL_CALL *DG_KERNEL_PRESENT)(void *, const DG_KERNEL_PRESENT_REQUEST *);
#define DG_COHERE_SYNC 0
#define DG_COHERE_DESTINATION 1
#define DG_COHERE_SOURCE 2
#define DG_COHERE_TEARDOWN 3
#define DG_COHERE_MODE 4
typedef ULONG(DG_KERNEL_CALL *DG_KERNEL_COHERE)(void *, ULONG);
typedef struct {
    ULONG Operation, Width, Height, HotX, HotY, Format;
    LONG X, Y;
    ULONG Flags;
    const ULONG *Pixels; /* kernel pointer, never exposed through user IOCTLs */
} DG_KERNEL_CURSOR_REQUEST;
typedef ULONG(DG_KERNEL_CALL *DG_KERNEL_CURSOR)(void *, const DG_KERNEL_CURSOR_REQUEST *);
typedef struct {
    ULONG Version, Size;
    DG_KERNEL_SUBMIT Submit;
    void *Context;
    DG_KERNEL_OWNER Owner;
    DG_KERNEL_PRESENT Present;
    DG_KERNEL_COHERE Cohere;
    const volatile ULONG *DesktopActive;
    DG_KERNEL_CURSOR Cursor;
    ULONG WindowCapabilities;
} DG_KERNEL_INTERFACE;
#endif
