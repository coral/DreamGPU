#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute prepared Wine fullscreen window activation and size keeping (0060).

Splinter Cell (D3D8, fullscreen 1024x768, window created at 640x480) sizes its
window back to 640x480 from WM_SIZE, nested in WineD3D's own SetWindowPos.
Native fullscreen flips cover the screen anyway; DreamGPU presents into the
window's client area, so the game showed in a corner of the mode. After a
focus change WineD3D left the window it had minimized iconic on activation and
the game never reset. The prepared window functions (setup, restore, focus,
the subclass procedure, the device message hook, activation, present and the
window part of wined3d_device_reset) run here against a model of user32 under
ASan/UBSan. As on Windows, a sent message reaches WineD3D's WH_CALLWNDPROC
hook (USE_HOOKS, as shipped) with a copy whose edits are discarded (Windows
2000 ignored the first 0060's hook-time clamp), then the window procedure
installed last, which forwards through CallWindowProcA/W; SetWindowLongPtrA
on a Unicode window would make it ANSI. Mutations of the patch must fail the
test.
"""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('patches', ROOT / 'tests/guest/support/patches.py')
patches = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patches)
MANIFEST = ROOT / 'support/guest/d3d/patches/base/manifest.json'

PREAMBLE = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef int BOOL, HRESULT, LONG;
typedef unsigned int UINT, DWORD;
typedef unsigned char BYTE;
typedef long LRESULT, LPARAM, LONG_PTR;
typedef unsigned long WPARAM;
typedef struct fake_window *HWND;
typedef LRESULT (*WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef struct { LONG left, top, right, bottom; } RECT;
typedef struct { int unused; } RGNDATA;
typedef struct { HWND hwnd, hwndInsertAfter; int x, y, cx, cy; UINT flags; } WINDOWPOS;
typedef struct { DWORD State, Protect; } MEMORY_BASIC_INFORMATION;
#define TRUE 1
#define FALSE 0
#define CDECL
#define CALLBACK
#define GWLP_WNDPROC (-4)
#define WM_NCDESTROY 0x0082
#define USE_HOOKS
#define TRACE(...) ((void)0)
#define WARN(...) ((void)0)
#define ERR(...) ((void)0)
#define FIXME(...) ((void)0)
#define FAILED(x) ((HRESULT)(x) < 0)
#define WINED3D_OK 0
#define E_FAIL ((HRESULT)0x80004005)
#define WINED3DERR_INVALIDCALL ((HRESULT)0x8876086c)
#define WINED3D_SCANLINE_ORDERING_UNKNOWN 0
#define WINED3D_RESTORE_MODE_ON_ACTIVATE 0x10
#define WINED3D_FOCUS_MESSAGES 0x20
#define WINED3D_HANDLE_RESTORE 0x40
#define WINED3DCREATE_NOWINDOWCHANGES 0x800
#define MEM_COMMIT 0x1000
#define PAGE_NOACCESS 1
/* Windows' values. */
#define SWP_NOSIZE 0x0001
#define SWP_NOMOVE 0x0002
#define SWP_NOZORDER 0x0004
#define SWP_NOACTIVATE 0x0010
#define SWP_FRAMECHANGED 0x0020
#define SWP_SHOWWINDOW 0x0040
#define SWP_HIDEWINDOW 0x0080
#define SWP_NOSENDCHANGING 0x0400
#define SWP_NOCLIENTSIZE 0x0800
#define SWP_NOCLIENTMOVE 0x1000
#define SWP_STATECHANGED 0x8000
#define HWND_TOPMOST ((HWND)-1)
#define SW_HIDE 0
#define SW_MINIMIZE 6
#define SW_SHOWNOACTIVATE 4
#define SW_RESTORE 9
#define SIZE_RESTORED 0
#define SIZE_MINIMIZED 1
#define WM_DESTROY 0x0002
#define WM_SIZE 0x0005
#define WM_ACTIVATEAPP 0x001c
#define WM_WINDOWPOSCHANGING 0x0046
#define WM_WINDOWPOSCHANGED 0x0047
#define WM_DISPLAYCHANGE 0x007e
#define WM_SYSCOMMAND 0x0112
#define SC_RESTORE 0xf120
#define GWL_STYLE (-16)
#define GWL_EXSTYLE (-20)
#define WS_POPUP 0x80000000
#define WS_CHILD 0x40000000
#define WS_VISIBLE 0x10000000
#define WS_CAPTION 0x00c00000
#define WS_SYSMENU 0x00080000
#define WS_THICKFRAME 0x00040000
#define WS_EX_TOPMOST 0x00000008
#define WS_EX_WINDOWEDGE 0x00000100
#define WS_EX_CLIENTEDGE 0x00000200

struct wined3d { UINT flags; };
struct wined3d_adapter { UINT ordinal; };
struct wined3d_display_mode { UINT width, height, refresh_rate; int format_id, scanline_ordering; };
struct wined3d_swapchain_desc {
    UINT backbuffer_width, backbuffer_height; int backbuffer_format; UINT refresh_rate;
    HWND device_window; BOOL windowed;
};
struct wined3d_device_parent;
struct wined3d_device_parent_ops {
    void (*mode_changed)(struct wined3d_device_parent *);
    void (*activate)(struct wined3d_device_parent *, BOOL);
};
struct wined3d_device_parent { const struct wined3d_device_parent_ops *ops; int state; };
struct wined3d_swapchain;
struct wined3d_device {
    struct wined3d_device_parent *device_parent; struct wined3d *wined3d; struct wined3d_adapter *adapter;
    LONG style, exStyle; BYTE filter_messages : 1;
    struct { HWND focus_window; DWORD flags; } create_parms;
    HWND focus_window;
    HWND kept_window; UINT kept_width, kept_height; UINT kept_clamps; BOOL kept_inactive;
    struct wined3d_swapchain **swapchains; UINT swapchain_count; void *cs;
};
struct wined3d_swapchain {
    struct wined3d_device *device; void **back_buffers; struct wined3d_swapchain_desc desc;
    struct wined3d_display_mode original_mode, d3d_mode; BOOL reapply_mode; HWND device_window;
};

/* user32 model: one top-level window. Sent messages reach WineD3D's
 * WH_CALLWNDPROC hook (device_process_message, registered windows only) and
 * then the application's procedure; DefWindowProc turns a changed client
 * size into WM_SIZE, as Windows does (SWP_NOCLIENTSIZE otherwise). */
struct fake_window {
    LONG x, y, cx, cy, nx, ny, ncx, ncy, style, exstyle; BOOL iconic, visible, alive, unicode, created_unicode;
    WNDPROC proc;
};
static struct fake_window win;
static HWND const hwnd = &win;
static struct wined3d_device *registered;
static WNDPROC app;
static unsigned depth, deepest, sizes, last_size_cx, last_size_cy, shows, last_show, mode_sets, fail_modes;
static unsigned displaychanges, shrinks, activations, deactivations, wrong_variant, hook_edits;
static struct wined3d_display_mode last_mode;
static BOOL mode_is_desktop;

LRESULT device_process_message(struct wined3d_device *device, HWND window, BOOL unicode,
        UINT message, WPARAM wparam, LPARAM lparam, WNDPROC proc);
void wined3d_swapchain_activate(struct wined3d_swapchain *swapchain, BOOL activate);
/* The hook gets a copy of a WINDOWPOS; what it writes there is lost. */
static LRESULT send(UINT message, WPARAM wparam, LPARAM lparam) {
    assert(win.alive);
    if (registered) {
        WINDOWPOS copy;
        LPARAM hooked = lparam;
        if (message == WM_WINDOWPOSCHANGING || message == WM_WINDOWPOSCHANGED) {
            copy = *(const WINDOWPOS *)lparam; hooked = (LPARAM)&copy;
        }
        device_process_message(registered, hwnd, FALSE, message, wparam, hooked, app);
        if (hooked != lparam && memcmp(&copy, (const void *)lparam, sizeof(copy))) ++hook_edits;
    }
    return win.proc(hwnd, message, wparam, lparam);
}
/* Window procedure chain. */
static BOOL IsWindowUnicode(HWND w) { assert(w == hwnd && win.alive); return win.unicode; }
static LONG_PTR GetWindowLongPtrA(HWND w, int index) { assert(w == hwnd && index == GWLP_WNDPROC); return (LONG_PTR)win.proc; }
static LONG_PTR GetWindowLongPtrW(HWND w, int index) { return GetWindowLongPtrA(w, index); }
static LONG_PTR set_proc(HWND w, int index, LONG_PTR value, BOOL unicode) {
    LONG_PTR old = (LONG_PTR)win.proc;
    assert(w == hwnd && index == GWLP_WNDPROC && win.alive && value);
    if (unicode != win.created_unicode) ++wrong_variant;
    win.unicode = unicode; /* SetWindowLongPtrA makes a Unicode window ANSI. */
    win.proc = (WNDPROC)value;
    return old;
}
static LONG_PTR SetWindowLongPtrA(HWND w, int index, LONG_PTR value) { return set_proc(w, index, value, FALSE); }
static LONG_PTR SetWindowLongPtrW(HWND w, int index, LONG_PTR value) { return set_proc(w, index, value, TRUE); }
static LRESULT CallWindowProcA(WNDPROC p, HWND w, UINT m, WPARAM wp, LPARAM lp) {
    if (win.created_unicode) ++wrong_variant;
    return p(w, m, wp, lp);
}
static LRESULT CallWindowProcW(WNDPROC p, HWND w, UINT m, WPARAM wp, LPARAM lp) {
    if (!win.created_unicode) ++wrong_variant;
    return p(w, m, wp, lp);
}
static BOOL DestroyWindow(HWND w) {
    assert(w == hwnd && win.alive);
    send(WM_DESTROY, 0, 0); send(WM_NCDESTROY, 0, 0);
    win.alive = FALSE;
    return TRUE;
}
static LONG InterlockedCompareExchange(LONG *p, LONG v, LONG c) { LONG old = *p; if (old == c) *p = v; return old; }
static LONG InterlockedExchange(LONG *p, LONG v) { LONG old = *p; *p = v; return old; }
static void Sleep(DWORD ms) { (void)ms; assert(!"kept-window lock held"); }
static BOOL SetWindowPos(HWND w, HWND after, int x, int y, int cx, int cy, UINT flags) {
    WINDOWPOS pos = {w, after, x, y, cx, cy, flags}, changed;
    LONG nx, ny, ncx, ncy;
    assert(w == hwnd);
    if (++depth > deepest) deepest = depth;
    assert(depth < 64); /* An unbounded SetWindowPos/WM_SIZE loop. */
    if (!(flags & SWP_NOSENDCHANGING)) send(WM_WINDOWPOSCHANGING, 0, (LPARAM)&pos);
    nx = pos.flags & SWP_NOMOVE ? win.x : pos.x; ny = pos.flags & SWP_NOMOVE ? win.y : pos.y;
    ncx = pos.flags & SWP_NOSIZE ? win.cx : pos.cx; ncy = pos.flags & SWP_NOSIZE ? win.cy : pos.cy;
    changed = pos; changed.x = nx; changed.y = ny; changed.cx = ncx; changed.cy = ncy;
    if (ncx == win.cx && ncy == win.cy) changed.flags |= SWP_NOCLIENTSIZE;
    if (nx == win.x && ny == win.y) changed.flags |= SWP_NOCLIENTMOVE;
    win.x = nx; win.y = ny; win.cx = ncx; win.cy = ncy;
    if (!win.iconic) { win.nx = nx; win.ny = ny; win.ncx = ncx; win.ncy = ncy; }
    if (pos.flags & SWP_SHOWWINDOW) win.visible = TRUE;
    if (pos.flags & SWP_HIDEWINDOW) win.visible = FALSE;
    send(WM_WINDOWPOSCHANGED, 0, (LPARAM)&changed);
    --depth;
    return TRUE;
}
static BOOL MoveWindow(HWND w, int x, int y, int cx, int cy, BOOL repaint) {
    (void)repaint; return SetWindowPos(w, NULL, x, y, cx, cy, SWP_NOZORDER | SWP_NOACTIVATE);
}
/* Minimizing marks the window iconic before positioning it, restoring clears
 * it first; both are state changes. */
static BOOL ShowWindow(HWND w, int command) {
    BOOL was_visible = win.visible;
    assert(w == hwnd); ++shows; last_show = command;
    if (command == SW_MINIMIZE && !win.iconic) {
        win.iconic = TRUE;
        SetWindowPos(w, NULL, -32000, -32000, 160, 24, SWP_STATECHANGED | SWP_NOZORDER | SWP_SHOWWINDOW);
    } else if ((command == SW_RESTORE || command == SW_SHOWNOACTIVATE) && win.iconic) {
        win.iconic = FALSE;
        SetWindowPos(w, NULL, win.nx, win.ny, win.ncx, win.ncy,
                SWP_STATECHANGED | SWP_NOZORDER | SWP_SHOWWINDOW | (command == SW_SHOWNOACTIVATE ? SWP_NOACTIVATE : 0));
    } else if (command == SW_HIDE) {
        win.visible = FALSE;
    }
    return was_visible;
}
static LRESULT DefWindowProcA(HWND w, UINT message, WPARAM wparam, LPARAM lparam) {
    const WINDOWPOS *pos = (const WINDOWPOS *)lparam;
    (void)wparam;
    if (message == WM_WINDOWPOSCHANGED && !(pos->flags & SWP_NOCLIENTSIZE))
        send(WM_SIZE, win.iconic ? SIZE_MINIMIZED : SIZE_RESTORED, (LPARAM)((pos->cy << 16) | pos->cx));
    if (message == WM_SYSCOMMAND && wparam == SC_RESTORE) ShowWindow(w, SW_RESTORE);
    return 0;
}
static LRESULT DefWindowProcW(HWND w, UINT message, WPARAM wparam, LPARAM lparam) {
    return DefWindowProcA(w, message, wparam, lparam);
}
static BOOL IsIconic(HWND w) { assert(w == hwnd); return win.iconic; }
static BOOL IsWindowVisible(HWND w) { assert(w == hwnd); return win.visible; }
static BOOL GetWindowRect(HWND w, RECT *r) {
    assert(w == hwnd); r->left = win.x; r->top = win.y; r->right = win.x + win.cx; r->bottom = win.y + win.cy; return TRUE;
}
static LONG GetWindowLongA(HWND w, int index) {
    assert(w == hwnd); return index == GWL_STYLE ? win.style | (win.visible ? (LONG)WS_VISIBLE : 0) : win.exstyle;
}
static LONG SetWindowLongA(HWND w, int index, LONG value) {
    LONG old = GetWindowLongA(w, index);
    if (index == GWL_STYLE) win.style = value & ~(LONG)WS_VISIBLE; else win.exstyle = value;
    return old;
}
static unsigned VirtualQuery(void *p, MEMORY_BASIC_INFORMATION *info, unsigned size) {
    (void)size; assert(p); info->State = MEM_COMMIT; info->Protect = 0; return 1;
}
static BOOL wined3d_register_window(HWND w, struct wined3d_device *d) { assert(w == hwnd); registered = d; return TRUE; }
static void wined3d_unregister_window(HWND w) { assert(w == hwnd); registered = NULL; }
static void *InterlockedExchangePointer(void **p, void *v) { void *old = *p; *p = v; return old; }
static void *InterlockedCompareExchangePointer(void **p, void *v, void *c) { void *old = *p; if (old == c) *p = v; return old; }
/* Mode changes reach the window as WM_DISPLAYCHANGE. */
static HRESULT wined3d_set_adapter_display_mode(struct wined3d *w, UINT ordinal, const struct wined3d_display_mode *mode) {
    (void)w; assert(!ordinal);
    if (fail_modes) { --fail_modes; return WINED3DERR_INVALIDCALL; }
    ++mode_sets; mode_is_desktop = !mode; if (mode) last_mode = *mode;
    send(WM_DISPLAYCHANGE, 32, 0);
    return WINED3D_OK;
}
static unsigned presents;
static void wined3d_cs_emit_present(void *cs, struct wined3d_swapchain *s, const RECT *a, const RECT *b, HWND w,
        const RGNDATA *r, DWORD f) {
    (void)cs; (void)s; (void)a; (void)b; (void)w; (void)r; (void)f; ++presents;
}
/* D3D8/9's device state, as their device_parent_activate. */
enum { STATE_OK, STATE_LOST, STATE_NOT_RESET };
static void parent_mode_changed(struct wined3d_device_parent *p) { (void)p; }
static void parent_activate(struct wined3d_device_parent *p, BOOL activate) {
    if (!activate && p->state == STATE_OK) p->state = STATE_LOST;
    else if (activate && p->state == STATE_LOST) p->state = STATE_NOT_RESET;
}
static const struct wined3d_device_parent_ops parent_ops = {parent_mode_changed, parent_activate};
'''

WRAPPER = r'''
/* The window part of wined3d_device_reset(), as prepared, with
 * wined3d_swapchain_resize_buffers() reduced to the new size. */
static HRESULT reset_window(struct wined3d_device *device, const struct wined3d_swapchain_desc *swapchain_desc,
        const struct wined3d_display_mode *mode)
{
    struct wined3d_swapchain *swapchain = device->swapchains[0];
    struct wined3d_display_mode m;
    BOOL DisplayModeChanged = swapchain->reapply_mode;
    HRESULT hr = WINED3D_OK;
@BLOCK@
    swapchain->desc.backbuffer_width = swapchain_desc->backbuffer_width;
    swapchain->desc.backbuffer_height = swapchain_desc->backbuffer_height;
    return hr;
}
'''

TEST = r'''
/* Applications. The game answers every other WM_SIZE at once with its
 * creation size, nested in the SetWindowPos that sent it. */
static LONG small_cx = 640, small_cy = 480;
static BOOL windowed_on_displaychange;
static LRESULT game(HWND w, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_SIZE && wparam != SIZE_MINIMIZED
            && ((lparam & 0xffff) != small_cx || ((lparam >> 16) & 0xffff) != small_cy)) {
        ++shrinks; SetWindowPos(w, NULL, 0, 0, small_cx, small_cy, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (message == WM_DISPLAYCHANGE) {
        ++displaychanges;
        if (windowed_on_displaychange) SetWindowPos(w, NULL, 64, 64, 640, 480, SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (message == WM_ACTIVATEAPP) { if (wparam) ++activations; else ++deactivations; }
    return DefWindowProcA(w, message, wparam, lparam);
}
/* Answers every WM_WINDOWPOSCHANGED, even an unchanged one, with a request
 * (then, switched off, behaves as the game). */
static BOOL stubborn_on;
static LRESULT stubborn(HWND w, UINT message, WPARAM wparam, LPARAM lparam) {
    const WINDOWPOS *pos = (const WINDOWPOS *)lparam;
    if (!stubborn_on) return game(w, message, wparam, lparam);
    if (message == WM_WINDOWPOSCHANGED && !win.iconic && (pos->cx != 640 || pos->cy != 480)) {
        ++shrinks; SetWindowPos(w, NULL, 0, 0, 640, 480, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    return DefWindowProcA(w, message, wparam, lparam);
}
/* Enforces its size in its own WM_WINDOWPOSCHANGING handler. */
static LRESULT editor(HWND w, UINT message, WPARAM wparam, LPARAM lparam) {
    WINDOWPOS *pos = (WINDOWPOS *)lparam;
    if (message == WM_WINDOWPOSCHANGING && !(pos->flags & (SWP_NOSIZE | SWP_STATECHANGED)) && !win.iconic) {
        ++shrinks; pos->cx = 640; pos->cy = 480;
    }
    return DefWindowProcA(w, message, wparam, lparam);
}
/* Another module subclassing the window after wined3d. */
static WNDPROC other_prev;
static unsigned other_calls;
static LRESULT other(HWND w, UINT message, WPARAM wparam, LPARAM lparam) {
    ++other_calls;
    return win.created_unicode ? CallWindowProcW(other_prev, w, message, wparam, lparam)
            : CallWindowProcA(other_prev, w, message, wparam, lparam);
}

static struct wined3d api;
static struct wined3d_adapter adapter;
static struct wined3d_device_parent parent;
static struct wined3d_device device;
static struct wined3d_swapchain chain, *chains[1] = {&chain};
static void *buffers[1];

/* A new window with the application's procedure; the previous one (same
 * handle, as Windows may reuse it) is destroyed first. */
static BOOL window_unicode;
static void create_window(LONG x, LONG y, LONG cx, LONG cy) {
    if (win.alive) DestroyWindow(hwnd);
    assert(!registered);
    memset(&win, 0, sizeof(win));
    win.x = win.nx = x; win.y = win.ny = y; win.cx = win.ncx = cx; win.cy = win.ncy = cy;
    win.style = WS_CAPTION | WS_SYSMENU; win.visible = TRUE; win.alive = TRUE;
    win.unicode = win.created_unicode = window_unicode; win.proc = app;
}
static int kept_entries(void) {
    int i, n = 0;
    for (i = 0; i < DEVICE_KEPT_WINDOWS_MAX; ++i) n += device_kept_windows[i].window != NULL;
    return n;
}
/* d3d8/d3d9 CreateDevice for a fullscreen device: focus window, fullscreen
 * window setup, then the swapchain. */
static void create_device(UINT wined3d_flags, DWORD create_flags, UINT w, UINT h, BOOL windowed) {
    memset(&device, 0, sizeof(device)); memset(&chain, 0, sizeof(chain)); memset(&parent, 0, sizeof(parent));
    api.flags = wined3d_flags; parent.ops = &parent_ops;
    device.device_parent = &parent; device.wined3d = &api; device.adapter = &adapter;
    device.create_parms.focus_window = hwnd; device.create_parms.flags = create_flags;
    device.swapchains = chains; device.swapchain_count = 1;
    chain.device = &device; chain.back_buffers = buffers; chain.device_window = hwnd;
    chain.desc.device_window = hwnd; chain.desc.windowed = windowed;
    chain.desc.backbuffer_width = w; chain.desc.backbuffer_height = h;
    chain.original_mode.width = 1280; chain.original_mode.height = 1024;
    shrinks = sizes = deepest = 0;
    if (!windowed) {
        assert(!wined3d_device_acquire_focus_window(&device, hwnd));
        wined3d_device_setup_fullscreen_window(&device, hwnd, w, h);
        chain.d3d_mode.width = w; chain.d3d_mode.height = h;
    }
}
static int covers(LONG w, LONG h) { return !win.iconic && !win.x && !win.y && win.cx == w && win.cy == h; }
static void activate(BOOL active) { send(WM_ACTIVATEAPP, active, 0); }
static void present(void) { assert(!wined3d_swapchain_present(&chain, NULL, NULL, NULL, NULL, 0)); }
static struct wined3d_swapchain_desc desc(UINT w, UINT h, BOOL windowed) {
    struct wined3d_swapchain_desc d = {w, h, 0, 0, hwnd, windowed}; return d;
}

int main(void) {
    const UINT d3d8 = WINED3D_HANDLE_RESTORE, ddraw = WINED3D_RESTORE_MODE_ON_ACTIVATE | WINED3D_FOCUS_MESSAGES;
    struct wined3d_swapchain_desc d;
    unsigned before;

    /* (b) CreateDevice: wined3d's own SetWindowPos (filter_messages set) and
     * the game's nested shrink; the clamped request changes nothing, so no
     * second WM_SIZE and no nesting. */
    app = game; create_window(100, 100, 640, 480);
    create_device(d3d8, 0, 1024, 768, FALSE);
    assert(covers(1024, 768) && win.style == (LONG)(WS_POPUP | WS_SYSMENU) && shrinks == 1 && deepest == 2);
    assert(device.kept_window == hwnd && device.kept_clamps == 1 && win.proc != game && kept_entries() == 1);
    /* Application requests while active: shrink, move away, both. */
    before = shrinks;
    SetWindowPos(hwnd, NULL, 0, 0, 640, 480, SWP_NOMOVE | SWP_NOZORDER);
    assert(covers(1024, 768));
    MoveWindow(hwnd, 100, 100, 1024, 768, TRUE); assert(covers(1024, 768));
    MoveWindow(hwnd, 50, 50, 320, 240, TRUE); assert(covers(1024, 768) && shrinks == before);
    /* Larger requests that still cover the mode, and pure moves, pass (the
     * game accepts this size). */
    small_cx = 1032; small_cy = 776;
    MoveWindow(hwnd, -4, -4, 1032, 776, TRUE); assert(win.x == -4 && win.cx == 1032);
    SetWindowPos(hwnd, NULL, 8, 8, 0, 0, SWP_NOSIZE | SWP_NOZORDER); assert(win.x == 8 && win.cx == 1032);
    small_cx = 640; small_cy = 480;
    MoveWindow(hwnd, 0, 0, 1024, 768, TRUE); assert(covers(1024, 768));

    /* (b) Same-mode Reset (setup again) with the nested shrink. */
    present(); assert(!device.kept_clamps);
    d = desc(1024, 768, FALSE); assert(!reset_window(&device, &d, NULL));
    assert(covers(1024, 768) && deepest <= 2);

    /* (a) Focus loss: desktop mode, minimized, lost; the application may
     * resize from WM_DISPLAYCHANGE then and while inactive. */
    windowed_on_displaychange = TRUE; mode_sets = 0;
    activate(FALSE);
    assert(win.iconic && last_show == SW_MINIMIZE && mode_is_desktop && chain.reapply_mode && parent.state == STATE_LOST);
    assert(win.nx == 64 && win.ncx == 640 && device.kept_inactive);
    windowed_on_displaychange = FALSE;
    /* Activation: restored without activation, covering the mode, the game's
     * shrink (from the restore's WM_SIZE) clamped; the device needs a reset. */
    before = shows; deepest = 0;
    activate(TRUE);
    assert(!win.iconic && last_show == SW_SHOWNOACTIVATE && shows == before + 1);
    assert(covers(1024, 768) && parent.state == STATE_NOT_RESET && deepest <= 2 && !device.kept_inactive);
    /* Activation of a window that is not iconic does not show it again. */
    before = shows; activate(TRUE); assert(shows == before && covers(1024, 768));
    /* The game's own minimize while active passes (a state change). */
    ShowWindow(hwnd, SW_MINIMIZE); assert(win.iconic && win.x == -32000 && win.cx == 160);
    activate(TRUE); assert(covers(1024, 768));

    /* Reset with reapply_mode after focus returns: the mode again, window kept. */
    d = desc(1024, 768, FALSE); mode_sets = 0;
    assert(!reset_window(&device, &d, NULL) && mode_sets == 1 && last_mode.width == 1024 && covers(1024, 768));
    chain.reapply_mode = FALSE;

    /* (b) Reset to a smaller and a larger fullscreen mode: the window takes
     * the new size from the mode change on; the game's shrinks are clamped
     * to the new size. */
    d = desc(800, 600, FALSE); assert(!reset_window(&device, &d, NULL));
    assert(covers(800, 600) && device.kept_width == 800 && chain.desc.backbuffer_width == 800);
    d = desc(1280, 1024, FALSE); assert(!reset_window(&device, &d, NULL));
    assert(covers(1280, 1024) && device.kept_width == 1280);
    /* A failed mode change keeps the old size kept. */
    fail_modes = 1; d = desc(640, 480, FALSE);
    assert(reset_window(&device, &d, NULL) == WINED3DERR_INVALIDCALL && device.kept_width == 1280 && device.kept_height == 1024);
    SetWindowPos(hwnd, NULL, 0, 0, 640, 480, SWP_NOMOVE | SWP_NOZORDER); assert(covers(1280, 1024));

    /* (b) Reset to windowed: free from the mode change on (the game resizes
     * from WM_DISPLAYCHANGE), styles restored, focus released. */
    windowed_on_displaychange = TRUE;
    d = desc(640, 480, TRUE); assert(!reset_window(&device, &d, NULL));
    windowed_on_displaychange = FALSE;
    assert(chain.desc.windowed && !device.kept_window && !registered && win.x == 64 && win.cx == 640);
    assert(win.proc == game && !kept_entries());
    /* The game's own windowed size, unclamped. */
    SetWindowPos(hwnd, NULL, 10, 10, 320, 240, SWP_NOZORDER); assert(win.x == 10 && win.cx == 640);
    activate(FALSE); assert(!win.iconic && win.cx == 640);
    /* And back to fullscreen: kept again by the setup. */
    d = desc(1024, 768, FALSE); assert(!reset_window(&device, &d, NULL));
    assert(!chain.desc.windowed && registered == &device && device.kept_window == hwnd && covers(1024, 768));
    /* Releasing the focus window (device destruction) ends keeping. */
    wined3d_device_release_focus_window(&device);
    assert(!device.kept_window && win.proc == game && !kept_entries());

    /* An application that answers every WM_WINDOWPOSCHANGED: bounded
     * clamps, then its size; the next present clamps again. */
    app = stubborn; stubborn_on = TRUE; create_window(0, 0, 640, 480);
    create_device(d3d8, 0, 1024, 768, FALSE);
    assert(win.cx == 640 && device.kept_clamps == DEVICE_KEPT_CLAMPS_MAX && deepest <= DEVICE_KEPT_CLAMPS_MAX + 2);
    assert(shrinks == DEVICE_KEPT_CLAMPS_MAX + 1);
    /* The next present gives a new budget. */
    stubborn_on = FALSE; present();
    MoveWindow(hwnd, 0, 0, 1024, 768, TRUE); assert(covers(1024, 768) && device.kept_clamps == 1);
    /* Destroying a kept window ends keeping (WM_NCDESTROY). */
    DestroyWindow(hwnd); assert(!device.kept_window && !kept_entries());

    /* A procedure that edits WM_WINDOWPOSCHANGING itself: wined3d clamps
     * again after it returns. */
    app = editor; create_window(0, 0, 640, 480);
    create_device(d3d8, 0, 1024, 768, FALSE);
    assert(covers(1024, 768) && shrinks >= 1);
    SetWindowPos(hwnd, NULL, 0, 0, 800, 600, SWP_NOZORDER); assert(covers(1024, 768));

    /* A Unicode window is subclassed and called with the W functions and
     * stays Unicode; the original procedure returns when keeping stops. */
    app = game; window_unicode = TRUE; create_window(100, 100, 640, 480);
    create_device(d3d8, 0, 1024, 768, FALSE);
    assert(covers(1024, 768) && win.unicode && win.proc != game);
    SetWindowPos(hwnd, NULL, 0, 0, 640, 480, SWP_NOMOVE | SWP_NOZORDER); assert(covers(1024, 768));
    wined3d_device_release_focus_window(&device);
    assert(win.unicode && win.proc == game && !kept_entries());
    window_unicode = FALSE;

    /* Subclassed after wined3d: keeping stops without unlinking (the later
     * procedure keeps receiving messages, wined3d's passes them through),
     * starts again on the same entry, and the window's destruction ends it. */
    app = game; create_window(100, 100, 640, 480);
    create_device(d3d8, 0, 1024, 768, FALSE);
    other_prev = (WNDPROC)SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)other);
    SetWindowPos(hwnd, NULL, 0, 0, 640, 480, SWP_NOMOVE | SWP_NOZORDER); assert(covers(1024, 768) && other_calls);
    wined3d_device_restore_fullscreen_window(&device, hwnd);
    assert(!device.kept_window && win.proc == other && kept_entries() == 1);
    before = other_calls;
    SetWindowPos(hwnd, NULL, 0, 0, 640, 480, SWP_NOMOVE | SWP_NOZORDER);
    assert(win.cx == 640 && other_calls > before);
    wined3d_device_setup_fullscreen_window(&device, hwnd, 1024, 768);
    assert(covers(1024, 768) && device.kept_window == hwnd && kept_entries() == 1 && win.proc == other);
    wined3d_device_restore_fullscreen_window(&device, hwnd);
    DestroyWindow(hwnd); assert(!kept_entries());

    /* NOWINDOWCHANGES: nothing kept, an iconic window stays iconic. */
    app = game; create_window(100, 100, 640, 480);
    create_device(d3d8, WINED3DCREATE_NOWINDOWCHANGES, 1024, 768, FALSE);
    assert(!device.kept_window && win.cx == 640);
    ShowWindow(hwnd, SW_MINIMIZE); before = shows; activate(TRUE);
    assert(win.iconic && shows == before);

    /* DirectDraw (no WINED3D_HANDLE_RESTORE): unchanged. */
    create_window(100, 100, 640, 480);
    create_device(ddraw, 0, 1024, 768, FALSE);
    assert(!device.kept_window && win.cx == 640);
    ShowWindow(hwnd, SW_MINIMIZE); before = shows; activate(TRUE);
    assert(win.iconic && shows == before);

    /* Windows ignores WINDOWPOS edits made in the hook; wined3d makes none
     * there. Every procedure call used the window's character set. */
    assert(!hook_edits && !wrong_variant);

    puts("PASS prepared fullscreen window: D3D8/9 activation restores the minimized window; creation, application "
         "requests, same/new-mode and windowed resets keep the mode covered without WM_SIZE loops; focus loss, "
         "minimize, bounded clamps, NOWINDOWCHANGES and DirectDraw unchanged; subclass clamp before and after the "
         "application's procedure, Unicode windows, later subclassing (pass-through) and destruction");
    return 0;
}
'''

CLAMP = '    if (message == WM_WINDOWPOSCHANGING)\n        device_clamp_fullscreen_window(device, window, (WINDOWPOS *)lparam);\n'
PROC_CLAMP = ('        if (message == WM_WINDOWPOSCHANGING && entry->device)\n'
              '            device_clamp_fullscreen_window(entry->device, window, (WINDOWPOS *)lparam);\n')
FILTER = '    if (device->filter_messages && message != WM_DISPLAYCHANGE)\n'
MUTATIONS = {
    'no-activation-restore': [(
        'if ((device->wined3d->flags & WINED3D_HANDLE_RESTORE) && IsIconic(swapchain->device_window))\n'
        '                ShowWindow(swapchain->device_window, SW_SHOWNOACTIVATE);\n', '')],
    'activation-restore-without-gate': [(
        'if ((device->wined3d->flags & WINED3D_HANDLE_RESTORE) && IsIconic(swapchain->device_window))',
        'if (IsIconic(swapchain->device_window))')],
    'activation-restore-activates': [(
        'ShowWindow(swapchain->device_window, SW_SHOWNOACTIVATE);', 'ShowWindow(swapchain->device_window, SW_RESTORE);')],
    'no-clamp': [(PROC_CLAMP, '')],
    # The first 0060: Windows 2000 ignored the hook's edit.
    'hook-clamp-only': [(PROC_CLAMP, ''), (FILTER, CLAMP + FILTER)],
    'no-clamp-after-application': [(
        '    if (message == WM_WINDOWPOSCHANGING)\n        device_kept_window_message(window, message, lparam, &unicode);\n', '')],
    'ansi-subclass': [('        entry->unicode = IsWindowUnicode(window);\n', '        entry->unicode = FALSE;\n')],
    'unlink-later-subclass': [('        if (proc == (LONG_PTR)device_kept_window_proc)\n', '        if (TRUE)\n')],
    'never-unsubclass': [(
        '            if (entry->unicode)\n                SetWindowLongPtrW(window, GWLP_WNDPROC, (LONG_PTR)entry->proc);\n'
        '            else\n                SetWindowLongPtrA(window, GWLP_WNDPROC, (LONG_PTR)entry->proc);\n', '')],
    'no-destroy-cleanup': [(
        '            if (entry->device && entry->device->kept_window == window)\n                entry->device->kept_window = NULL;\n'
        '            memset(entry, 0, sizeof(*entry));\n', '')],
    'clamp-while-inactive': [('|| device->kept_inactive\n', '\n')],
    'clamp-state-changes': [('(pos->flags & (SWP_NOSIZE | SWP_STATECHANGED)) || IsIconic(window)', '(pos->flags & SWP_NOSIZE)')],
    'clamp-pure-moves': [('(pos->flags & (SWP_NOSIZE | SWP_STATECHANGED))', '(pos->flags & SWP_STATECHANGED)')],
    'unbounded-clamps': [('#define DEVICE_KEPT_CLAMPS_MAX 8', '#define DEVICE_KEPT_CLAMPS_MAX 100000')],
    'no-present-budget': [('    swapchain->device->kept_clamps = 0;\n', '')],
    'reset-keeps-old-size': [(
        '        else if (!swapchain->desc.windowed)\n            device_keep_fullscreen_window(device, swapchain->device_window,\n'
        '                    swapchain_desc->backbuffer_width, swapchain_desc->backbuffer_height);\n', '')],
    'windowed-reset-kept-until-restore': [(
        '        if (swapchain_desc->windowed)\n            device_unkeep_fullscreen_window(device);\n        else if', '        if')],
    'failed-reset-forgets-size': [(
        '            if (kept_window)\n                device_keep_fullscreen_window(device, kept_window, kept_width, kept_height);\n'
        '            else\n                device_unkeep_fullscreen_window(device);\n',
        '(void)kept_window;(void)kept_width;(void)kept_height;\n')],
    'kept-for-ddraw': [('    if (!(device->wined3d->flags & WINED3D_HANDLE_RESTORE)\n            || (device', '    if ((device')],
    'kept-with-nowindowchanges': [(
        '\n            || (device->create_parms.flags & WINED3DCREATE_NOWINDOWCHANGES))\n        return;\n\n    if (device->kept_window',
        ')\n        return;\n\n    if (device->kept_window')],
}


def function(source, prefix):
    start = source.index(prefix)
    return source[start:source.index('\n}', start) + 2]


def between(source, first, last):
    start = source.index(first)
    return source[start:source.index(last, start)]


def build(work, name, text, expect_pass):
    path = work / (name + '.c')
    path.write_text(text)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    '-Wno-unused-parameter', '-Wno-unused-variable', '-Wno-unused-but-set-variable',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-g',
                    str(path), '-o', str(work / name)], check=True)
    result = subprocess.run([str(work / name)], capture_output=not expect_pass)
    if expect_pass:
        result.check_returncode()
    else:
        assert result.returncode != 0, (name, result.stdout)


with tempfile.TemporaryDirectory(prefix='dreamgpu-fullscreen-window-') as directory:
    work = Path(directory)
    for name in json.loads(MANIFEST.read_text())['files']:
        destination = work / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'vendor/wine9x' / name, destination)
    patches.apply(work, MANIFEST)
    device = (work / 'wined3d/device.c').read_text()
    swapchain = (work / 'wined3d/swapchain.c').read_text()
    private = (work / 'wined3d/wined3d_private.h').read_text()

    # The device keeps the window state; the context-loss bit layout (0059) is unchanged.
    assert '    HWND kept_window;\n    UINT kept_width, kept_height;\n    UINT kept_clamps;\n    BOOL kept_inactive;\n' in private
    assert '    BYTE contexts_lost : 1;\n    BYTE padding : 1;\n' in private
    # Ordering in the prepared wined3d_device_reset: swapchain->desc changes
    # only after the window is set up, so the kept size changes before the
    # mode change (and is restored if it fails).
    reset = function(device, 'HRESULT CDECL wined3d_device_reset(')
    keep = reset.index('device_keep_fullscreen_window(device, swapchain->device_window,')
    assert reset.index('device_unkeep_fullscreen_window(device);') < keep < reset.index(
        'if (FAILED(hr = wined3d_set_adapter_display_mode(device->wined3d, device->adapter->ordinal, &m)))')
    assert keep < reset.index('MoveWindow(swapchain->device_window') < reset.index(
        'swapchain->desc.windowed = swapchain_desc->windowed;') < reset.index('wined3d_swapchain_resize_buffers(')
    # Only the subclass procedure clamps (Windows ignores hook-time edits),
    # before and after the next procedure.
    assert 'device_clamp_fullscreen_window(' not in function(device, 'LRESULT device_process_message(')
    proc = function(device, 'static LRESULT CALLBACK device_kept_window_proc(')
    assert proc.index('device_kept_window_message(') < proc.index('CallWindowProcW(') < proc.rindex('device_kept_window_message(')

    def program(device, swapchain):
        code = [function(device, 'static LONG fullscreen_style('), function(device, 'static LONG fullscreen_exstyle('),
                between(device, '#ifndef SWP_STATECHANGED', 'void CDECL wined3d_device_setup_fullscreen_window(')]
        for prefix in ('void CDECL wined3d_device_setup_fullscreen_window(',
                       'void CDECL wined3d_device_restore_fullscreen_window(',
                       'HRESULT CDECL wined3d_device_acquire_focus_window(',
                       'void CDECL wined3d_device_release_focus_window(', 'LRESULT device_process_message('):
            code.append(function(device, prefix))
        for prefix in ('void wined3d_swapchain_activate(', 'HRESULT CDECL wined3d_swapchain_present('):
            code.append(function(swapchain, prefix))
        block = between(function(device, 'HRESULT CDECL wined3d_device_reset('),
                        '    if (mode)\n    {\n        DisplayModeChanged = TRUE;',
                        '    if (FAILED(hr = wined3d_swapchain_resize_buffers(')
        code.append(WRAPPER.replace('@BLOCK@', block))
        return PREAMBLE + '\n'.join(code) + TEST

    build(work, 'fixed', program(device, swapchain), True)
    for name, replacements in MUTATIONS.items():
        mutated_device, mutated_swapchain = device, swapchain
        for old, new in replacements:
            assert (mutated_device + mutated_swapchain).count(old) == 1, name
            mutated_device, mutated_swapchain = mutated_device.replace(old, new), mutated_swapchain.replace(old, new)
        build(work, name, program(mutated_device, mutated_swapchain), False)
    print(f'PASS all {len(MUTATIONS)} mutations of the patch are rejected')
