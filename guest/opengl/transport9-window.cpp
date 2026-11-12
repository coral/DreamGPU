/* SPDX-License-Identifier: GPL-2.0-or-later
 * Win98 uses a private compatible bitmap to enter GDI's authoritative clipped
 * BitBlt DDI. Pixel data stays in the native GL drawable; the bitmap contains
 * only a binding tag. NT retains its WNDOBJ/DrawEscape path.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "dg-escape.h"
#include "dg-window.h"
#include "../win9x/dg-window9.h"
#include "transport.h"
#include "transport9-retry.h"

extern "C" {
extern BOOL JglTransportIs9x(void);
extern HANDLE JglTransportDevice(void);
static struct JGL9_BINDING {
    ULONG Token, Client, Width, Height, Depth;
    HWND Window;
    HDC Memory;
    HBITMAP Bitmap, Previous;
    char Property[48];
    /* Property's global atom, referenced by the binding; 0: use the name. */
    ATOM Atom;
} Bindings[DG9_WINDOW_SLOTS];
/* The binding's window property. A string name costs USER a global atom
 * lookup in every GetProp, and each present asks. The binding holds its own
 * reference to the atom of its unique name from bind to release, so the atom
 * names exactly that property for as long as it is used; SetProp/RemoveProp
 * by atom take and drop no reference. Without an atom the name works as
 * before (SetPropA by name fails the same way when the table is full). */
static LPCSTR BindingProperty(const struct JGL9_BINDING *binding) {
    return binding->Atom ? (LPCSTR)MAKEINTATOM(binding->Atom) : binding->Property;
}
/* The tag bitmap is at least 16 pixels wide, so its first scanline holds the
 * whole tag at 16 or 32 bpp and SetBitmapBits row padding cannot split it. */
static_assert(sizeof(DG9_BITMAP_TAG) <= 16 * 2, "tag fits one 16-bpp scanline");
static BOOL WindowRequest(DG9_WINDOW_REQUEST *request, DG9_WINDOW_REPLY *reply) {
    DWORD returned = 0;
    HANDLE device = JglTransportDevice();
    if (device == INVALID_HANDLE_VALUE)
        return FALSE;
    request->Version = DG9_WINDOW_VERSION;
    ZeroMemory(reply, sizeof(*reply));
    /* Only a DG9_ERROR_BUSY refusal is resent, bounded: the VxD refused it
     * before any pin, mutex, owner or window state, so the operation did not
     * execute (dg-refusal9.h) and wrote no reply. */
    return Jgl9DeviceControl(device, DG9_WINDOW_IOCTL, request, sizeof(*request), reply,
                             sizeof(*reply), &returned) &&
           returned == sizeof(*reply) && reply->Version == DG9_WINDOW_VERSION &&
           reply->Status == DG_ESCAPE_OK;
}
int JglTransportBind(HDC dc, const DG_WINDOW_BIND *request, DG_WINDOW_REPLY *reply) {
    DG9_WINDOW_REQUEST control;
    DG9_WINDOW_REPLY result;
    DG9_BITMAP_TAG tag;
    struct JGL9_BINDING *binding;
    RECT rect;
    HWND window;
    DWORD process;
    unsigned i;
    int depth;
    if (!JglTransportIs9x())
        return ExtEscape(dc, WNDOBJ_SETUP, sizeof(*request), (LPCSTR)request, sizeof(*reply),
                         (LPSTR)reply);
    window = WindowFromDC(dc);
    if (!window || (ULONG)(ULONG_PTR)window != request->Window ||
        !GetWindowThreadProcessId(window, &process) || process != GetCurrentProcessId() ||
        !GetClientRect(window, &rect) || rect.right <= 0 || rect.bottom <= 0)
        return 0;
    /* The compatible tag bitmap takes this depth: XRGB8888 or RGB565 only. */
    depth = GetDeviceCaps(dc, BITSPIXEL);
    if (depth != 16 && depth != 32)
        return 0;
    for (i = 0; i < DG9_WINDOW_SLOTS && Bindings[i].Token; ++i) {
    }
    if (i == DG9_WINDOW_SLOTS)
        return 0;
    binding = &Bindings[i];
    ZeroMemory(binding, sizeof(*binding));
    binding->Memory = CreateCompatibleDC(dc);
    binding->Bitmap = CreateCompatibleBitmap(dc, (rect.right > 16 ? rect.right : 16), rect.bottom);
    if (!binding->Memory || !binding->Bitmap)
        goto failed;
    binding->Previous = (HBITMAP)SelectObject(binding->Memory, binding->Bitmap);
    if (!binding->Previous || binding->Previous == (HBITMAP)HGDI_ERROR)
        goto failed;
    ZeroMemory(&control, sizeof(control));
    control.Operation = DG9_WINDOW_BIND;
    control.Client = request->Client;
    control.Context = request->Context;
    control.Drawable = request->Drawable;
    control.Width = rect.right;
    control.Height = rect.bottom;
    if (!WindowRequest(&control, &result) || !result.Binding)
        goto failed;
    binding->Token = result.Binding;
    binding->Client = request->Client;
    binding->Window = window;
    binding->Width = rect.right;
    binding->Height = rect.bottom;
    binding->Depth = (ULONG)depth;
    wsprintfA(binding->Property, "DreamGPU.%08lx.%08lx", GetCurrentProcessId(), binding->Token);
    binding->Atom = GlobalAddAtomA(binding->Property);
    tag.Magic0 = DG9_BITMAP_MAGIC0;
    tag.Magic1 = DG9_BITMAP_MAGIC1;
    tag.Binding = binding->Token;
    tag.Reserved = 0;
    if (SetBitmapBits(binding->Bitmap, sizeof(tag), &tag) != sizeof(tag) ||
        !SetPropA(window, BindingProperty(binding), (HANDLE)(ULONG_PTR)binding->Token))
        goto failed;
    reply->Version = DG_WINDOW_VERSION;
    reply->Status = DG_ESCAPE_OK;
    reply->Binding = binding->Token;
    reply->Capabilities = DG_WINDOW_CAP_FRONT_ONLY;
    return 1;
failed:
    if (binding->Token) {
        ULONG token = binding->Token;
        JglTransportUnbind(token);
    } else {
        if (binding->Memory && binding->Previous && binding->Previous != (HBITMAP)HGDI_ERROR)
            SelectObject(binding->Memory, binding->Previous);
        if (binding->Bitmap)
            DeleteObject(binding->Bitmap);
        if (binding->Memory)
            DeleteDC(binding->Memory);
        ZeroMemory(binding, sizeof(*binding));
    }
    return 0;
}
void JglTransportUnbind(ULONG token) {
    DG9_WINDOW_REQUEST request;
    DG9_WINDOW_REPLY reply;
    unsigned i;
    if (!JglTransportIs9x())
        return;
    for (i = 0; i < DG9_WINDOW_SLOTS; ++i) {
        struct JGL9_BINDING *binding = &Bindings[i];
        if (binding->Token != token || !token)
            continue;
        ZeroMemory(&request, sizeof(request));
        request.Operation = DG9_WINDOW_RELEASE;
        request.Client = binding->Client;
        request.Binding = token;
        WindowRequest(&request, &reply); /* private client close is final rundown */
        if (binding->Window &&
            GetPropA(binding->Window, BindingProperty(binding)) == (HANDLE)(ULONG_PTR)token)
            RemovePropA(binding->Window, BindingProperty(binding));
        if (binding->Atom)
            GlobalDeleteAtom(binding->Atom);
        if (binding->Previous && binding->Previous != (HBITMAP)HGDI_ERROR)
            SelectObject(binding->Memory, binding->Previous);
        DeleteObject(binding->Bitmap);
        DeleteDC(binding->Memory);
        ZeroMemory(binding, sizeof(*binding));
        return;
    }
}
int JglTransportPresent(HDC dc, const DG_WINDOW_PRESENT *present) {
    unsigned i;
    struct JGL9_BINDING *binding = NULL;
    DG9_WINDOW_REQUEST request;
    DG9_WINDOW_REPLY reply;
    HDC fresh;
    HRGN clip;
    POINT viewport, origin;
    RECT rect;
    BOOL drawn, finished;
    int has_clip;
    if (!JglTransportIs9x())
        return DrawEscape(dc, DG_DRAW_ESCAPE, sizeof(*present), (LPCSTR)present);
    if (present->Magic != DG_WINDOW_MAGIC || present->Version != DG_WINDOW_VERSION ||
        present->Flags & ~DG_WINDOW_PRESENT_FLAGS_MASK)
        return 0;
    for (i = 0; i < DG9_WINDOW_SLOTS; ++i)
        if (Bindings[i].Token && Bindings[i].Token == present->Binding) {
            binding = &Bindings[i];
            break;
        }
    if (!binding || WindowFromDC(dc) != binding->Window ||
        GetPropA(binding->Window, BindingProperty(binding)) != (HANDLE)(ULONG_PTR)binding->Token ||
        !GetClientRect(binding->Window, &rect) || rect.right != (LONG)binding->Width ||
        rect.bottom != (LONG)binding->Height || GetMapMode(dc) != MM_TEXT ||
        !GetViewportOrgEx(dc, &viewport) || !GetWindowOrgEx(dc, &origin) || viewport.x ||
        viewport.y || origin.x || origin.y)
        return 0;
    /* A depth change retires the tag bitmap's format. Nothing has been
     * submitted yet, so the frontend may bind a fresh compatible bitmap once. */
    if (GetDeviceCaps(dc, BITSPIXEL) != (int)binding->Depth)
        return DG_WINDOW_PRESENT_REBIND;
    /* Hold a fresh private DC through the whole present. Its GDI object cannot
     * be recycled by caller ReleaseDC/HWND reuse during the native wait. */
    fresh = GetDCEx(binding->Window, NULL, DCX_CACHE | DCX_CLIPCHILDREN | DCX_CLIPSIBLINGS);
    if (!fresh)
        return 0;
    clip = CreateRectRgn(0, 0, 0, 0);
    if (!clip) {
        ReleaseDC(binding->Window, fresh);
        return 0;
    }
    has_clip = GetClipRgn(dc, clip);
    if (has_clip < 0 || (has_clip && SelectClipRgn(fresh, clip) == ERROR)) {
        DeleteObject(clip);
        ReleaseDC(binding->Window, fresh);
        return 0;
    }
    DeleteObject(clip);
    ZeroMemory(&request, sizeof(request));
    request.Operation = DG9_WINDOW_PREPARE;
    request.Client = binding->Client;
    request.Binding = binding->Token;
    request.Flags = present->Flags ? DG9_WINDOW_FRONT_ONLY : 0;
    if (!WindowRequest(&request, &reply)) {
        ReleaseDC(binding->Window, fresh);
        return 0;
    }
    drawn = BitBlt(fresh, 0, 0, binding->Width, binding->Height, binding->Memory, 0, 0, SRCCOPY);
    request.Operation = DG9_WINDOW_FINISH;
    request.Flags = 0;
    finished = WindowRequest(&request, &reply);
    ReleaseDC(binding->Window, fresh);
    return drawn && finished ? 1 : 0;
}

} /* extern C */
