/* SPDX-License-Identifier: GPL-2.0-or-later
 * Exercise the actual WNDOBJ binding/clip/lifetime code against a fake GDI
 * engine. Native guest probes additionally verify NT5 callback/lock behavior.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define _FRAMEBUF_PCH_
#define APIENTRY
#define DG_KERNEL_CALL
#define TRUE 1
#define FALSE 0
#define DC_TRIVIAL 0
#define DC_RECT 1
#define DC_COMPLEX 3
#define CD_ANY 0
#define CT_RECTANGLES 0
#define WOC_DELETE 1
#define WOC_RGN_CLIENT 2
#define WO_RGN_CLIENT 2
#define FL_ZERO_MEMORY 1
#define ALLOC_TAG 0
#define FILE_DEVICE_VIDEO 0x23
#define METHOD_BUFFERED 0
#define FILE_ANY_ACCESS 0
#define CTL_CODE(device, function, method, access) (((device) << 16) | ((function) << 2))
#define max(a, b) ((a) > (b) ? (a) : (b))
#define min(a, b) ((a) < (b) ? (a) : (b))
typedef uint32_t ULONG;
typedef int32_t LONG;
typedef uintptr_t ULONG_PTR;
typedef int BOOL;
typedef void VOID;
typedef void *PVOID;
typedef void *HSEMAPHORE;
typedef void *HWND;
typedef ULONG FLONG;
typedef struct {
    LONG left, top, right, bottom;
} RECTL;
typedef struct {
    void *dhpdev, *hsurf;
} SURFOBJ;
typedef struct {
    ULONG iDComplexity;
    RECTL rclBounds;
} CLIPOBJ;
typedef struct {
    CLIPOBJ coClient;
    PVOID pvConsumer;
    RECTL rclClient;
    SURFOBJ *psoOwner;
} WNDOBJ;
#include "dg-kernel.h"
#include "dg-window.h"
typedef struct {
    void *hSurfEng;
    ULONG ScreenWidth, ScreenHeight, ScreenDelta, BitsPerPixel;
    DG_KERNEL_INTERFACE Kernel;
    void *WindowLock, *Windows[DG_GL_MAX_DRAWABLES];
    ULONG NextBinding;
} PDEV, *PPDEV;

static WNDOBJ object;
static void (*callback)(WNDOBJ *, FLONG);
static RECTL regions[4];
static ULONG region_count, enum_count_override, lock, owner = 7, process = 1234, presents, deletes,
                                                      active, coherent;
static DG_KERNEL_PRESENT_REQUEST received;
static void *EngAllocMem(ULONG flags, ULONG size, ULONG tag) {
    (void)flags;
    (void)tag;
    return calloc(1, size);
}
static void EngFreeMem(void *memory) {
    free(memory);
}
static void EngAcquireSemaphore(void *semaphore) {
    (void)semaphore;
    assert(!lock);
    lock = 1;
}
static void EngReleaseSemaphore(void *semaphore) {
    (void)semaphore;
    assert(lock);
    lock = 0;
}
static WNDOBJ *EngCreateWnd(SURFOBJ *surface, HWND window, void (*changed)(WNDOBJ *, FLONG),
                            ULONG flags, int format) {
    // The system ICD uses pixel format1 (promoted from the old diagnostic path).
    assert(!lock && window && flags == WO_RGN_CLIENT && format == 1);
    memset(&object, 0, sizeof(object));
    object.psoOwner = surface;
    object.rclClient = (RECTL){10, 20, 110, 100};
    callback = changed;
    return &object;
}
static void EngDeleteWnd(WNDOBJ *wnd) {
    assert(wnd == &object && !wnd->pvConsumer);
    ++deletes;
}
static void WNDOBJ_vSetConsumer(WNDOBJ *wnd, void *consumer) {
    wnd->pvConsumer = consumer;
}
static ULONG WNDOBJ_cEnumStart(WNDOBJ *wnd, ULONG type, ULONG dir, ULONG limit) {
    (void)wnd;
    (void)type;
    (void)dir;
    assert(limit == DG_WINDOW_MAX_CLIPS);
    return enum_count_override ? enum_count_override : region_count;
}
static BOOL WNDOBJ_bEnum(WNDOBJ *wnd, ULONG bytes, ULONG *data) {
    (void)wnd;
    assert(bytes >= 4 + region_count * sizeof(RECTL));
    *data = region_count;
    memcpy(data + 1, regions, region_count * sizeof(RECTL));
    return FALSE;
}
static void CLIPOBJ_cEnumStart(CLIPOBJ *clip, BOOL all, ULONG type, ULONG dir, ULONG limit) {
    (void)clip;
    (void)all;
    (void)type;
    (void)dir;
    (void)limit;
}
static BOOL CLIPOBJ_bEnum(CLIPOBJ *clip, ULONG bytes, ULONG *data) {
    (void)clip;
    (void)bytes;
    *data = 0;
    return FALSE;
}
static ULONG Owner(void *context, ULONG client) {
    (void)context;
    return client == owner ? process : 0;
}
static ULONG Present(void *context, const DG_KERNEL_PRESENT_REQUEST *request) {
    (void)context;
    assert(request->Client == owner);
    received = *request;
    ++presents;
    return DG_ESCAPE_OK;
}
static ULONG Cohere(void *context, ULONG reason) {
    (void)context;
    assert(reason == DG_COHERE_SYNC);
    ++coherent;
    active = 0;
    return 1;
}
#include "../../../guest/nt/display/window.cpp"

int main(void) {
    PDEV dev = {};
    dev.hSurfEng = (void *)1;
    dev.ScreenWidth = 160;
    dev.ScreenHeight = 120;
    dev.ScreenDelta = 640;
    dev.BitsPerPixel = 32;
    dev.WindowLock = (void *)1;
    dev.Kernel.Owner = Owner;
    dev.Kernel.Present = Present;
    dev.Kernel.Cohere = Cohere;
    dev.Kernel.DesktopActive = &active;
    dev.Kernel.WindowCapabilities = DG_WINDOW_CAP_FRONT_ONLY;
    SURFOBJ surface = {&dev, (void *)1};
    DG_WINDOW_BIND bind = {42, DG_WINDOW_MAGIC, 1, 7, 3, 5, 0};
    DG_WINDOW_REPLY reply;
    DG_WINDOW_PRESENT present = {DG_WINDOW_MAGIC, 1, 0, 0};
    CLIPOBJ dc = {DC_RECT, {15, 25, 105, 90}};
    ULONG old;
    assert(sizeof(bind) == 28 && sizeof(reply) == 16 && sizeof(present) == 16);
    assert(DgBindWindow(&surface, sizeof(bind), &bind, sizeof(reply), &reply));
    assert(reply.Status == DG_ESCAPE_OK && reply.Binding &&
           reply.Capabilities == DG_WINDOW_CAP_FRONT_ONLY);
    present.Binding = reply.Binding;
    assert(!DrvDrawEscape(&surface, DG_DRAW_ESCAPE, NULL, NULL, sizeof(present), &present));
    /* Disjoint visible rectangles preserve an occluder and are further
     * intersected with the caller's GDI DC clip. */
    regions[0] = (RECTL){10, 20, 40, 100};
    regions[1] = (RECTL){60, 20, 110, 100};
    region_count = 2;
    callback(&object, WOC_RGN_CLIENT);
    assert(DrvDrawEscape(&surface, DG_DRAW_ESCAPE, &dc, NULL, sizeof(present), &present));
    assert(received.Count == 2 && received.WindowX == 10 && received.WindowY == 20);
    assert(received.Clips[0].Left == 15 && received.Clips[0].Right == 40 &&
           received.Clips[0].Top == 25);
    assert(received.Clips[1].Left == 60 && received.Clips[1].Right == 105 &&
           received.Clips[1].Bottom == 90);
    assert(received.Flags == 0);
    /* Optional front publication preserves the same kernel-owned clipping;
     * unsupported/unknown flags never reach the producer. */
    old = presents;
    present.Flags = DG_WINDOW_PRESENT_FRONT_ONLY << 1;
    assert(!DrvDrawEscape(&surface, DG_DRAW_ESCAPE, &dc, NULL, sizeof(present), &present));
    present.Flags = DG_WINDOW_PRESENT_FRONT_ONLY;
    dev.Kernel.WindowCapabilities = 0;
    assert(!DrvDrawEscape(&surface, DG_DRAW_ESCAPE, &dc, NULL, sizeof(present), &present));
    assert(presents == old);
    dev.Kernel.WindowCapabilities = DG_WINDOW_CAP_FRONT_ONLY;
    assert(DrvDrawEscape(&surface, DG_DRAW_ESCAPE, &dc, NULL, sizeof(present), &present));
    assert(received.Flags == DG_WINDOW_PRESENT_FRONT_ONLY && received.Count == 2);
    present.Flags = 0;
    assert(DrvDrawEscape(&surface, DG_DRAW_ESCAPE, &dc, NULL, sizeof(present), &present));
    assert(received.Flags == 0); /* front-only never sticks to a binding */
    /* Clip count overflow invalidates the entire snapshot instead of exposing
     * a partially enumerated window. */
    enum_count_override = (ULONG)-1;
    callback(&object, WOC_RGN_CLIENT);
    assert(!DrvDrawEscape(&surface, DG_DRAW_ESCAPE, NULL, NULL, sizeof(present), &present));
    enum_count_override = 0;
    region_count = 0;
    callback(&object, WOC_RGN_CLIENT);
    assert(DrvDrawEscape(&surface, DG_DRAW_ESCAPE, NULL, NULL, sizeof(present), &present));
    assert(received.Count == 0);
    old = present.Binding;
    bind.Drawable = 6;
    assert(DgBindWindow(&surface, sizeof(bind), &bind, sizeof(reply), &reply));
    assert(reply.Status == DG_ESCAPE_OK && reply.Binding != old);
    assert(!DrvDrawEscape(&surface, DG_DRAW_ESCAPE, NULL, NULL, sizeof(present), &present));
    present.Binding = reply.Binding;
    /* Last-context teardown retired client7 but leaves the actual HWND alive.
     * A fresh client from that same process may reuse it; old bindings expire. */
    owner = bind.Client = 8;
    assert(DgBindWindow(&surface, sizeof(bind), &bind, sizeof(reply), &reply));
    assert(reply.Status == DG_ESCAPE_OK && reply.Binding != present.Binding);
    assert(!DrvDrawEscape(&surface, DG_DRAW_ESCAPE, NULL, NULL, sizeof(present), &present));
    present.Binding = reply.Binding;
    assert(DrvDrawEscape(&surface, DG_DRAW_ESCAPE, NULL, NULL, sizeof(present), &present));
    /* Even with a valid new client and the old token retired, a different
     * process cannot take over the tracked HWND. */
    process = 2345;
    owner = bind.Client = 9;
    assert(DgBindWindow(&surface, sizeof(bind), &bind, sizeof(reply), &reply));
    assert(reply.Status == DG_ESCAPE_OWNER && reply.Capabilities == 0);
    process = 1234;
    owner = 8;
    active = 1;
    DrvSynchronizeSurface(&surface, NULL, 0);
    assert(coherent == 1 && !active);
    DrvSynchronizeSurface(&surface, NULL, 0);
    assert(coherent == 1);
    callback(&object, WOC_DELETE);
    assert(!DrvDrawEscape(&surface, DG_DRAW_ESCAPE, NULL, NULL, sizeof(present), &present));
    bind.Client = 8;
    assert(DgBindWindow(&surface, sizeof(bind), &bind, sizeof(reply), &reply));
    assert(reply.Status == DG_ESCAPE_OK);
    DgDeleteWindows(&dev);
    assert(deletes == 1 && !lock);
    puts("window binding: visible/DC clips, front capability/flags, bounded enumeration, rebind, "
         "ownership, teardown and coherence passed");
    return 0;
}
