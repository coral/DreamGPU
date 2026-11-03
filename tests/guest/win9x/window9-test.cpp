/* SPDX-License-Identifier: GPL-2.0-or-later */
/* The actual Win98 GL window frontend against a GDI model: compatible bitmaps
 * take the desktop depth, SetBitmapBits uses WORD-aligned DDB rows over
 * DWORD-aligned DIB-engine storage, and BitBlt enters the actual 16-bit
 * classifier (window9-ddi.c) with that storage. */
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include "windows.h"
#include "../../../guest/opengl/transport9-window.cpp"
extern "C" {
WORD DgTestBitBlt(void *bits, unsigned depth, unsigned width, unsigned height,
                  unsigned screen_depth, unsigned screen_flags, unsigned dx, unsigned dy,
                  unsigned sx, unsigned sy, unsigned w, unsigned h, DWORD rop);
extern DWORD dg_test_token, dg_test_source, dg_test_destination, dg_test_extent;
extern unsigned dg_test_calls;
}
constexpr unsigned kVram = 0x8000, kFive6Five = 0x0040;
enum { kStock = 1, kBitmap, kRegion };
struct MockWindow {
    RECT client;
    POINT screen;
    DWORD process;
    struct {
        char name[48];
        HANDLE value;
    } props[4];
};
struct MockObject {
    int kind, width, height, depth;
    DWORD stride;
    unsigned char *bits;
    HDC selected_in;
};
struct MockDc {
    HWND window;
    bool memory;
    HGDIOBJ selected;
};
static MockWindow app{{0, 0, 64, 48}, {100, 50}, 0x1234, {}};
static MockObject stock{kStock, 1, 1, 1, 2, nullptr, nullptr};
static MockObject *last_bitmap;
static int desktop_depth = 32;
static unsigned desktop_flags = kVram, live, set_bits, bitblts, fallbacks, nt_escapes;
static DWORD ioctl_log[8], next_token = 0x100;
static unsigned ioctl_count;
static bool is9x = true;

extern "C" {
BOOL JglTransportIs9x(void) {
    return is9x;
}
HANDLE JglTransportDevice(void) {
    return (HANDLE)(uintptr_t)0x77;
}
int ExtEscape(HDC, int escape, int, LPCSTR, int, LPSTR) {
    assert(!is9x && escape == WNDOBJ_SETUP);
    ++nt_escapes;
    return 1;
}
int DrawEscape(HDC, int escape, int, LPCSTR) {
    assert(!is9x && escape == (int)DG_DRAW_ESCAPE);
    ++nt_escapes;
    return 1;
}
BOOL DeviceIoControl(HANDLE device, DWORD code, void *input, DWORD in_bytes, void *output,
                     DWORD out_bytes, DWORD *returned, void *overlapped) {
    DG9_WINDOW_REQUEST request;
    assert(device == JglTransportDevice() && code == DG9_WINDOW_IOCTL && !overlapped);
    assert(in_bytes == sizeof(request) && out_bytes == sizeof(DG9_WINDOW_REPLY));
    std::memcpy(&request, input, sizeof(request));
    assert(request.Version == DG9_WINDOW_VERSION && request.Client == 7);
    DG9_WINDOW_REPLY reply{DG9_WINDOW_VERSION, DG_ESCAPE_OK, request.Binding, 0};
    if (request.Operation == DG9_WINDOW_BIND) {
        assert(request.Context == 8 && request.Drawable == 9 && !request.Binding &&
               request.Width == (DWORD)app.client.right &&
               request.Height == (DWORD)app.client.bottom && !request.Flags);
        reply.Binding = ++next_token;
        reply.Capabilities = DG9_WINDOW_FRONT_ONLY;
    } else
        assert(request.Binding && !request.Context && !request.Width);
    assert(ioctl_count < 8);
    ioctl_log[ioctl_count++] = request.Operation;
    std::memcpy(output, &reply, sizeof(reply));
    *returned = sizeof(reply);
    return TRUE;
}
HWND WindowFromDC(HDC dc) {
    return dc->window;
}
DWORD GetWindowThreadProcessId(HWND window, DWORD *process) {
    *process = window->process;
    return 1;
}
DWORD GetCurrentProcessId(void) {
    return 0x1234;
}
BOOL GetClientRect(HWND window, RECT *rect) {
    *rect = window->client;
    return TRUE;
}
int GetDeviceCaps(HDC dc, int index) {
    assert(dc && index == BITSPIXEL);
    return desktop_depth;
}
HDC CreateCompatibleDC(HDC) {
    ++live;
    return new MockDc{nullptr, true, &stock};
}
HBITMAP CreateCompatibleBitmap(HDC dc, int width, int height) {
    assert(dc && !dc->memory && width > 0 && height > 0);
    auto *bitmap = new MockObject{
        kBitmap, width,  height, desktop_depth, (DWORD)((width * desktop_depth + 31) / 32 * 4),
        nullptr, nullptr};
    bitmap->bits = static_cast<unsigned char *>(std::malloc(bitmap->stride * height));
    std::memset(bitmap->bits, 0xcd, bitmap->stride * height);
    ++live;
    return last_bitmap = bitmap;
}
HGDIOBJ SelectObject(HDC dc, HGDIOBJ object) {
    HGDIOBJ previous = dc->selected;
    assert(dc->memory && object && object->kind != kRegion);
    if (previous->kind == kBitmap)
        previous->selected_in = nullptr;
    if (object->kind == kBitmap)
        object->selected_in = dc;
    dc->selected = object;
    return previous;
}
/* DDB bits use WORD-aligned rows; the DIB engine stores DWORD-aligned rows. */
LONG SetBitmapBits(HBITMAP bitmap, DWORD bytes, const void *data) {
    const DWORD row = (DWORD)(bitmap->width * bitmap->depth + 15) / 16 * 2;
    const auto *input = static_cast<const unsigned char *>(data);
    DWORD i = 0;
    ++set_bits;
    for (; i < bytes && i / row < (DWORD)bitmap->height; ++i)
        bitmap->bits[i / row * bitmap->stride + i % row] = input[i];
    return (LONG)i;
}
BOOL DeleteObject(HGDIOBJ object) {
    assert(object && object->kind != kStock && !object->selected_in);
    std::free(object->bits);
    delete object;
    --live;
    return TRUE;
}
BOOL DeleteDC(HDC dc) {
    assert(dc->memory && dc->selected == &stock);
    delete dc;
    --live;
    return TRUE;
}
BOOL SetPropA(HWND window, LPCSTR name, HANDLE value) {
    for (auto &prop : window->props)
        if (!prop.name[0]) {
            std::snprintf(prop.name, sizeof(prop.name), "%s", name);
            prop.value = value;
            return TRUE;
        }
    return FALSE;
}
HANDLE GetPropA(HWND window, LPCSTR name) {
    for (auto &prop : window->props)
        if (prop.name[0] && !std::strcmp(prop.name, name))
            return prop.value;
    return nullptr;
}
HANDLE RemovePropA(HWND window, LPCSTR name) {
    for (auto &prop : window->props)
        if (prop.name[0] && !std::strcmp(prop.name, name)) {
            HANDLE value = prop.value;
            std::memset(&prop, 0, sizeof(prop));
            return value;
        }
    return nullptr;
}
/* Win32 longs are 32-bit: %08lx consumes a DWORD. */
int wsprintfA(LPSTR output, LPCSTR format, ...) {
    va_list args;
    char *out = output;
    va_start(args, format);
    for (const char *f = format; *f;)
        if (!std::strncmp(f, "%08lx", 5)) {
            out += std::sprintf(out, "%08x", va_arg(args, unsigned));
            f += 5;
        } else {
            assert(*f != '%');
            *out++ = *f++;
        }
    *out = 0;
    va_end(args);
    return (int)(out - output);
}
HDC GetDCEx(HWND window, HRGN clip, DWORD flags) {
    assert(window == &app && !clip && flags == (DCX_CACHE | DCX_CLIPCHILDREN | DCX_CLIPSIBLINGS));
    ++live;
    return new MockDc{window, false, nullptr};
}
int ReleaseDC(HWND window, HDC dc) {
    assert(dc->window == window && !dc->memory);
    delete dc;
    --live;
    return 1;
}
HRGN CreateRectRgn(int, int, int, int) {
    ++live;
    return new MockObject{kRegion, 0, 0, 0, 0, nullptr, nullptr};
}
int GetClipRgn(HDC, HRGN) {
    return 0;
}
int SelectClipRgn(HDC, HRGN) {
    std::abort();
}
int GetMapMode(HDC) {
    return MM_TEXT;
}
BOOL GetViewportOrgEx(HDC, POINT *point) {
    *point = {0, 0};
    return TRUE;
}
BOOL GetWindowOrgEx(HDC, POINT *point) {
    *point = {0, 0};
    return TRUE;
}
/* GDI hands the driver screen coordinates and the selected source bitmap. */
BOOL BitBlt(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy, DWORD rop) {
    assert(dst && !dst->memory && src && src->memory && src->selected->kind == kBitmap);
    MockObject *bitmap = src->selected;
    ++bitblts;
    const WORD result = DgTestBitBlt(bitmap->bits, bitmap->depth, bitmap->width, bitmap->height,
                                     desktop_depth, desktop_flags, x + dst->window->screen.x,
                                     y + dst->window->screen.y, sx, sy, w, h, rop);
    if (!result)
        ++fallbacks; /* DIB engine would copy the bitmap's own pixels. */
    return result != 2;
}
} /* extern C */

static void reset() {
    ioctl_count = set_bits = bitblts = fallbacks = dg_test_calls = 0;
    dg_test_token = dg_test_source = dg_test_destination = dg_test_extent = 0;
}
static ULONG bind(HDC dc) {
    DG_WINDOW_BIND request{
        (unsigned)(uintptr_t)&app, DG_WINDOW_MAGIC, DG_WINDOW_VERSION, 7, 8, 9, 0};
    DG_WINDOW_REPLY reply{};
    if (!JglTransportBind(dc, &request, &reply)) {
        assert(!reply.Binding);
        return 0;
    }
    assert(reply.Version == DG_WINDOW_VERSION && reply.Status == DG_ESCAPE_OK &&
           reply.Binding == next_token && reply.Capabilities == DG_WINDOW_CAP_FRONT_ONLY);
    return reply.Binding;
}
static int present(HDC dc, ULONG binding) {
    DG_WINDOW_PRESENT present{DG_WINDOW_MAGIC, DG_WINDOW_VERSION, binding, 0};
    return JglTransportPresent(dc, &present);
}
/* The 16-byte tag occupies the first bytes of scanline zero; nothing else of
 * the bitmap is written. */
static void expect_tag(const MockObject *bitmap, ULONG binding) {
    const DWORD words[4] = {DG9_BITMAP_MAGIC0, DG9_BITMAP_MAGIC1, binding, 0};
    unsigned char bytes[16];
    for (unsigned i = 0; i < 16; ++i)
        bytes[i] = (unsigned char)(words[i / 4] >> (i % 4 * 8));
    assert(!std::memcmp(bitmap->bits, bytes, sizeof(bytes)));
    for (DWORD i = sizeof(bytes); i < bitmap->stride * bitmap->height; ++i)
        assert(bitmap->bits[i] == 0xcd);
}
static void set_desktop(int depth, unsigned flags) {
    desktop_depth = depth;
    desktop_flags = flags;
}
int main() {
    MockDc window_dc{&app, false, nullptr};
    HDC dc = &window_dc;
    /* Bind, tag round trip through GDI's DDI and release at both depths. */
    for (int depth : {32, 16})
        for (LONG width : {1, 15, 16, 17, 640}) {
            set_desktop(depth, depth == 16 ? kVram | kFive6Five : kVram);
            app.client = {0, 0, width, 48};
            reset();
            const ULONG binding = bind(dc);
            assert(binding && ioctl_count == 1 && ioctl_log[0] == DG9_WINDOW_BIND);
            assert(set_bits == 1 && live == 2 && last_bitmap->depth == depth &&
                   last_bitmap->width == (width > 16 ? width : 16) && last_bitmap->height == 48);
            expect_tag(last_bitmap, binding);
            assert(GetPropA(&app, Bindings[0].Property) == (HANDLE)(uintptr_t)binding);
            assert(present(dc, binding) == 1 && bitblts == 1 && !fallbacks);
            assert(ioctl_count == 3 && ioctl_log[1] == DG9_WINDOW_PREPARE &&
                   ioctl_log[2] == DG9_WINDOW_FINISH && live == 2);
            assert(dg_test_calls == 1 && dg_test_token == binding && dg_test_source == 0 &&
                   dg_test_destination == (100u | (50u << 16)) &&
                   dg_test_extent == ((DWORD)width | (48u << 16)));
            JglTransportUnbind(binding);
            assert(ioctl_count == 4 && ioctl_log[3] == DG9_WINDOW_RELEASE && !live);
            assert(!Bindings[0].Token && !app.props[0].name[0]);
        }
    /* Palettized and other depths stay unsupported before any allocation. */
    for (int depth : {1, 4, 8, 15, 24, 64}) {
        set_desktop(depth, kVram);
        reset();
        assert(!bind(dc) && !ioctl_count && !set_bits && !live && !Bindings[0].Token);
    }
    /* A 16-bpp primary that is not RGB565 fails closed in the DDI. */
    set_desktop(16, kVram);
    app.client = {0, 0, 64, 48};
    reset();
    ULONG binding = bind(dc);
    assert(binding && present(dc, binding) == 0 && bitblts == 1 && !fallbacks && !dg_test_calls);
    assert(ioctl_count == 3 && ioctl_log[2] == DG9_WINDOW_FINISH);
    JglTransportUnbind(binding);
    /* A depth change after bind asks for one rebind before any submission. */
    for (int from : {32, 16}) {
        const int to = from == 32 ? 16 : 32;
        set_desktop(from, from == 16 ? kVram | kFive6Five : kVram);
        reset();
        const ULONG stale = bind(dc);
        MockObject *old = last_bitmap;
        set_desktop(to, to == 16 ? kVram | kFive6Five : kVram);
        assert(present(dc, stale) == DG_WINDOW_PRESENT_REBIND && ioctl_count == 1 && !bitblts);
        /* Were GDI to blit the stale tag anyway, mixed depths never reach the VxD. */
        assert(DgTestBitBlt(old->bits, from, old->width, old->height, to, desktop_flags, 0, 0, 0, 0,
                            64, 48, SRCCOPY) == 2 &&
               !dg_test_calls);
        const ULONG fresh = bind(dc);
        assert(fresh && fresh != stale && last_bitmap->depth == to);
        expect_tag(last_bitmap, fresh);
        JglTransportUnbind(stale);
        assert(present(dc, fresh) == 1 && dg_test_calls == 1 && dg_test_token == fresh);
        JglTransportUnbind(fresh);
        assert(!live && !Bindings[0].Token && !Bindings[1].Token);
    }
    /* Unknown bindings are refused; NT keeps its WNDOBJ/DrawEscape path. */
    set_desktop(32, kVram);
    reset();
    assert(!present(dc, 0x9999) && !ioctl_count);
    is9x = false;
    DG_WINDOW_BIND request{
        (unsigned)(uintptr_t)&app, DG_WINDOW_MAGIC, DG_WINDOW_VERSION, 7, 8, 9, 0};
    DG_WINDOW_REPLY reply{};
    assert(JglTransportBind(dc, &request, &reply) == 1 && present(dc, 1) == 1 && nt_escapes == 2);
    assert(!ioctl_count && !live);
    std::puts("PASS actual Win98 GL window frontend: 16/32-bpp bind, tag round trip through the "
              "actual 16-bit classifier at widths 1..640, RGB565/non-565/mixed depths, "
              "1/4/8/15/24-bpp refusal and one rebind after a depth change");
}
