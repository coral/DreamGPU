/* SPDX-License-Identifier: GPL-2.0-or-later
 * The Direct3D cursor (IDirect3DDevice8/9::SetCursorProperties,
 * SetCursorPosition, ShowCursor) on a fullscreen 640x480 device of its own,
 * run by probe.cpp after reset_probe::Fullscreen. WineD3D has two paths:
 * - a 32x32 image becomes a Windows cursor (CreateIconIndirect, SetCursor);
 *   SetCursorPosition moves it with SetCursorPos. On DreamGPU the display
 *   driver shows it as the native cursor plane, outside the presented frame.
 * - any other size, or a 32x32 one after SetCursorPos could not reach the
 *   position (GetCursorPos differs; WineD3D then destroys the Windows
 *   cursor), is drawn by WineD3D into the back buffer at Present with an
 *   alpha-tested blit (swapchain.c, WINEDDBLT_ALPHATEST).
 * Every frame clears the back buffer, presents, and reads the presented
 * image back with GetFrontBuffer (D3D8) / GetFrontBufferData (D3D9). Cursor
 * images: the left half opaque magenta, the right half green with alpha 0,
 * which the alpha test must drop. Hotspot 0,0 unless noted.
 * - baseline: no cursor shown, the frame is the clear colour (validates the
 *   readback itself);
 * - software 16x16: shown, positioned and moved; the left half is in the
 *   frame at each position, the right half is not, the old position is
 *   clear; hidden again with ShowCursor(FALSE) it is gone;
 * - edges (Wine 0061): the 16x16 cursor partly beyond the bottom-right
 *   corner, and with hotspot 8,8 partly beyond the top-left one, must show
 *   exactly its part inside the frame, as native Direct3D clips it. Before
 *   0061 wined3d_surface_blt rejected the whole rectangle
 *   (WINEDDERR_INVALIDRECT) and nothing was drawn;
 * - hardware 32x32: shown at two positions; GetCursorInfo shows a cursor at
 *   each, and the frame has no cursor pixels (if instead WineD3D drew it, the
 *   pixels are required and the path is logged);
 * - hardware 32x32 where it cannot follow: ClipCursor confines the Windows
 *   cursor to 0,0-64,64 and SetCursorPosition asks for 400,300, then 100,380:
 *   WineD3D must switch to its own drawing, the cursor is in the frame at
 *   each position and the old Windows cursor is no longer current.
 * Wherever the cursor is checked, every other pixel of the frame must be the
 * clear colour. With an X8R8G8B8 mode pixels are compared exactly, with
 * R5G6B5 within 8 per channel. A failure logs the stage, the pixel and the cursor state.
 */
namespace cursor_probe {

using reset_probe::FullscreenFormat;
using reset_probe::Pump;
using reset_probe::kModeHeight;
using reset_probe::kModeWidth;

static constexpr DWORD Background = 0x00102030, Opaque = 0x00ff00ff, Transparent = 0x0000ff00;

struct CursorInfo {
    DWORD size, flags;
    HCURSOR cursor;
    POINT position;
};
typedef BOOL(WINAPI *GetCursorInfoFunction)(CursorInfo *);
static GetCursorInfoFunction get_cursor_info;

static BOOL Near(DWORD actual, DWORD expected, DWORD tolerance) {
    for (UINT shift = 0; shift < 24; shift += 8) {
        const int a = (int)((actual >> shift) & 255), e = (int)((expected >> shift) & 255);
        if (a - e > (int)tolerance || e - a > (int)tolerance)
            return FALSE;
    }
    return TRUE;
}

/* A8R8G8B8 image: left half opaque, right half transparent. */
static BOOL Image(Device *device, UINT size, Surface **image) {
    D3DLOCKED_RECT locked;
#if DG_D3D_VERSION == 8
    if (!HR(DEVICE(CreateImageSurface, device, size, size, D3DFMT_A8R8G8B8, image),
            "FAIL cursor: image surface creation"))
        return FALSE;
#else
    if (!HR(DEVICE(CreateOffscreenPlainSurface, device, size, size, D3DFMT_A8R8G8B8,
                   D3DPOOL_SYSTEMMEM, image, NULL),
            "FAIL cursor: image surface creation"))
        return FALSE;
#endif
    if (!HR(SURFACE(LockRect, *image, &locked, NULL, 0), "FAIL cursor: lock image"))
        return FALSE;
    for (UINT y = 0; y < size; ++y)
        for (UINT x = 0; x < size; ++x)
            *(DWORD *)((BYTE *)locked.pBits + y * locked.Pitch + x * 4) =
                x < size / 2 ? 0xff000000 | Opaque : Transparent;
    return HR(SURFACE(UnlockRect, *image), "FAIL cursor: unlock image");
}

struct Context {
    Device *device;
    Surface *readback;
    DWORD tolerance;
    D3DLOCKED_RECT locked;
};

/* Clear, present, read the presented image back and keep it locked. */
static BOOL Frame(Context *context, const char *stage) {
    Log(stage);
    Pump();
    if (!HR(DEVICE(Clear, context->device, 0, NULL, D3DCLEAR_TARGET, 0xff000000 | Background, 1, 0),
            "FAIL cursor: clear") ||
        !HR(DEVICE(Present, context->device, NULL, NULL, NULL, NULL), "FAIL cursor: present"))
        return FALSE;
#if DG_D3D_VERSION == 8
    if (!HR(DEVICE(GetFrontBuffer, context->device, context->readback),
            "FAIL cursor: front buffer readback"))
        return FALSE;
#else
    if (!HR(DEVICE(GetFrontBufferData, context->device, 0, context->readback),
            "FAIL cursor: front buffer readback"))
        return FALSE;
#endif
    return HR(SURFACE(LockRect, context->readback, &context->locked, NULL, D3DLOCK_READONLY),
              "FAIL cursor: lock front buffer copy");
}

static BOOL Unlock(Context *context) {
    return HR(SURFACE(UnlockRect, context->readback), "FAIL cursor: unlock front buffer copy");
}

/* Every on-screen pixel of [left,right) x [top,bottom) is expected; the first
 * mismatch is logged when quiet is FALSE. */
static BOOL Area(Context *context, LONG left, LONG top, LONG right, LONG bottom, DWORD expected,
                 BOOL quiet, const char *stage) {
    left = left < 0 ? 0 : left;
    top = top < 0 ? 0 : top;
    right = right > kModeWidth ? kModeWidth : right;
    bottom = bottom > kModeHeight ? kModeHeight : bottom;
    for (LONG y = top; y < bottom; ++y)
        for (LONG x = left; x < right; ++x) {
            const DWORD actual =
                *(DWORD *)((BYTE *)context->locked.pBits + y * context->locked.Pitch + x * 4) &
                0x00ffffff;
            if (Near(actual, expected, context->tolerance))
                continue;
            if (!quiet) {
                Check(FALSE, stage, actual);
                Number("pixel x", (DWORD)x);
                Number("pixel y", (DWORD)y);
                Number("pixel expected", expected);
            }
            return FALSE;
        }
    return TRUE;
}

/* The image's two halves, as far as they are on screen, at the cursor whose
 * hotspot is at x,y, and nothing anywhere else in the frame. */
static BOOL Drawn(Context *context, LONG x, LONG y, LONG size, LONG hotspot, BOOL quiet,
                  const char *stage) {
    const LONG left = x - hotspot, top = y - hotspot, right = left + size, bottom = top + size;
    return Area(context, left, top, left + size / 2, bottom, Opaque, quiet, stage) &&
           Area(context, left + size / 2, top, right, bottom, Background, quiet, stage) &&
           Area(context, 0, 0, kModeWidth, top, Background, quiet, stage) &&
           Area(context, 0, bottom, kModeWidth, kModeHeight, Background, quiet, stage) &&
           Area(context, 0, top, left, bottom, Background, quiet, stage) &&
           Area(context, right, top, kModeWidth, bottom, Background, quiet, stage);
}

static BOOL Clear(Context *context, LONG x, LONG y, LONG size, BOOL quiet, const char *stage) {
    return Area(context, x, y, x + size, y + size, Background, quiet, stage);
}

static void State(const char *stage) {
    POINT position = {};
    Log(stage);
    if (GetCursorPos(&position)) {
        Number("GetCursorPos x", (DWORD)position.x);
        Number("GetCursorPos y", (DWORD)position.y);
    }
    if (get_cursor_info) {
        CursorInfo info = {};
        info.size = sizeof(info);
        if (get_cursor_info(&info)) {
            Number("GetCursorInfo flags", info.flags);
            Number("GetCursorInfo cursor", (DWORD)(ULONG_PTR)info.cursor);
            Number("GetCursorInfo x", (DWORD)info.position.x);
            Number("GetCursorInfo y", (DWORD)info.position.y);
        }
    }
}

static void Move(Context *context, LONG x, LONG y) {
    DEVICE(SetCursorPosition, context->device, x, y, 0);
}

static BOOL Software(Context *context, Surface *image) {
    if (!HR(DEVICE(SetCursorProperties, context->device, 0, 0, image),
            "FAIL cursor: 16x16 SetCursorProperties"))
        return FALSE;
    DEVICE(ShowCursor, context->device, TRUE);
    Move(context, 120, 90);
    if (!Frame(context, "STAGE cursor: software 16x16 at 120,90"))
        return FALSE;
    BOOL ok = Drawn(context, 120, 90, 16, 0, FALSE, "FAIL cursor: software 16x16 at 120,90");
    if (!Unlock(context) || !ok)
        return FALSE;
    Move(context, 500, 400);
    if (!Frame(context, "STAGE cursor: software 16x16 moved to 500,400"))
        return FALSE;
    ok = Drawn(context, 500, 400, 16, 0, FALSE, "FAIL cursor: software 16x16 at 500,400") &&
         Clear(context, 120, 90, 16, FALSE, "FAIL cursor: software 16x16 left at 120,90");
    if (!Unlock(context) || !ok)
        return FALSE;

    /* The parts inside the frame of a cursor crossing an edge (Wine 0061). */
    Move(context, kModeWidth - 4, kModeHeight - 4);
    if (!Frame(context, "STAGE cursor: software 16x16 partly beyond the bottom-right corner"))
        return FALSE;
    ok = Drawn(context, kModeWidth - 4, kModeHeight - 4, 16, 0, FALSE,
               "FAIL cursor: software 16x16 across the bottom-right corner");
    if (!Unlock(context) || !ok)
        return FALSE;
    if (!HR(DEVICE(SetCursorProperties, context->device, 8, 8, image),
            "FAIL cursor: 16x16 SetCursorProperties with hotspot 8,8"))
        return FALSE;
    Move(context, 4, 4);
    if (!Frame(context, "STAGE cursor: software 16x16, hotspot 8,8, partly beyond the top-left "
                        "corner"))
        return FALSE;
    ok = Drawn(context, 4, 4, 16, 8, FALSE,
               "FAIL cursor: software 16x16 (hotspot 8,8) across the top-left corner");
    if (!Unlock(context) || !ok)
        return FALSE;

    if (!HR(DEVICE(SetCursorProperties, context->device, 0, 0, image),
            "FAIL cursor: 16x16 SetCursorProperties again"))
        return FALSE;
    Move(context, 300, 200);
    if (!Frame(context, "STAGE cursor: software 16x16 at 300,200 before hiding"))
        return FALSE;
    ok = Drawn(context, 300, 200, 16, 0, FALSE, "FAIL cursor: software 16x16 at 300,200");
    if (!Unlock(context) || !ok)
        return FALSE;
    DEVICE(ShowCursor, context->device, FALSE);
    if (!Frame(context, "STAGE cursor: software 16x16 hidden"))
        return FALSE;
    ok = Clear(context, 300, 200, 16, FALSE, "FAIL cursor: hidden software cursor drawn");
    return Unlock(context) && ok;
}

/* A shown Windows cursor at x,y, or else WineD3D's drawing in the frame. */
static BOOL Hardware(Context *context, LONG x, LONG y, HCURSOR *cursor, const char *stage,
                     const char *failure) {
    CursorInfo info = {};
    BOOL windows = FALSE;
    POINT position = {};
    BOOL info_valid;
    info.size = sizeof(info);
    info_valid = get_cursor_info && get_cursor_info(&info);
    if (info_valid)
        windows = (info.flags & 1 /* CURSOR_SHOWING */) && info.cursor &&
                  info.position.x == x && info.position.y == y;
    else
        windows = GetCursorPos(&position) && position.x == x && position.y == y;
    if (!Frame(context, stage))
        return FALSE;
    const BOOL drawn = Drawn(context, x, y, 32, 0, TRUE, NULL);
    const BOOL clear = Clear(context, x, y, 32, TRUE, NULL);
    if (!Unlock(context))
        return FALSE;
    if (windows && clear) {
        Log(info_valid ? "STAGE cursor: Windows cursor shown at the position, frame clear"
                       : "STAGE cursor: no GetCursorInfo; Windows cursor at the position, "
                         "frame clear");
        if (cursor && info_valid)
            *cursor = info.cursor;
        return TRUE;
    }
    if (!windows && drawn) {
        Log("STAGE cursor: WineD3D drew the 32x32 cursor (software path)");
        return TRUE;
    }
    State("cursor state");
    Number("frame has the cursor", (DWORD)drawn);
    Number("frame clear at the cursor", (DWORD)clear);
    return Check(FALSE, failure, (DWORD)windows);
}

static BOOL Diverged(Context *context, LONG x, LONG y, HCURSOR old, const char *stage) {
    Move(context, x, y);
    if (!Frame(context, stage))
        return FALSE;
    BOOL ok = Drawn(context, x, y, 32, 0, FALSE,
                    "FAIL cursor: 32x32 not drawn where the Windows cursor cannot follow");
    if (!Unlock(context) || !ok)
        return FALSE;
    if (get_cursor_info && old) {
        CursorInfo info = {};
        info.size = sizeof(info);
        if (get_cursor_info(&info) && info.cursor == old) {
            State("cursor state");
            return Check(FALSE, "FAIL cursor: WineD3D's Windows cursor still current", 0);
        }
    }
    return TRUE;
}

static BOOL Stages(D3D *d3d, D3DFORMAT format, HWND window, Context *context, Surface **small,
                   Surface **large) {
    D3DPRESENT_PARAMETERS parameters = {};
    HCURSOR windows_cursor = NULL;
    parameters.BackBufferWidth = kModeWidth;
    parameters.BackBufferHeight = kModeHeight;
    parameters.BackBufferFormat = format;
    parameters.BackBufferCount = 1;
    parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    parameters.hDeviceWindow = window;
    parameters.Windowed = FALSE;
#if DG_D3D_VERSION == 8
    parameters.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
#else
    parameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
#endif
    if (format != D3DFMT_X8R8G8B8)
        Log("STAGE cursor: R5G6B5 mode, pixels compared within 8 per channel");
    if (!HR(D3D_CALL(CreateDevice, d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
                     D3DCREATE_SOFTWARE_VERTEXPROCESSING, &parameters, &context->device),
            "FAIL cursor: fullscreen device creation"))
        return FALSE;
#if DG_D3D_VERSION == 8
    if (!HR(DEVICE(CreateImageSurface, context->device, kModeWidth, kModeHeight, D3DFMT_A8R8G8B8,
                   &context->readback),
            "FAIL cursor: front buffer copy creation"))
        return FALSE;
#else
    if (!HR(DEVICE(CreateOffscreenPlainSurface, context->device, kModeWidth, kModeHeight,
                   D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &context->readback, NULL),
            "FAIL cursor: front buffer copy creation"))
        return FALSE;
#endif
    if (!Image(context->device, 16, small) || !Image(context->device, 32, large))
        return FALSE;

    if (!Frame(context, "STAGE cursor: baseline without a cursor"))
        return FALSE;
    BOOL ok = Area(context, 0, 0, kModeWidth, kModeHeight, Background, FALSE,
                   "FAIL cursor: baseline frame (front buffer readback)");
    if (!Unlock(context) || !ok)
        return FALSE;

    if (!Software(context, *small))
        return FALSE;

    Log("STAGE cursor: hardware 32x32 shown and moved");
    if (!HR(DEVICE(SetCursorProperties, context->device, 0, 0, *large),
            "FAIL cursor: 32x32 SetCursorProperties"))
        return FALSE;
    DEVICE(ShowCursor, context->device, TRUE);
    Move(context, 200, 150);
    if (!Hardware(context, 200, 150, &windows_cursor, "STAGE cursor: 32x32 at 200,150",
                  "FAIL cursor: 32x32 at 200,150 neither a shown Windows cursor nor drawn"))
        return FALSE;
    Move(context, 330, 240);
    if (!Hardware(context, 330, 240, NULL, "STAGE cursor: 32x32 moved to 330,240",
                  "FAIL cursor: 32x32 at 330,240 neither a shown Windows cursor nor drawn"))
        return FALSE;

    Log("STAGE cursor: Windows cursor confined to 0,0-64,64; 32x32 asked at 400,300 and 100,380");
    {
        const RECT confine = {0, 0, 64, 64};
        ClipCursor(&confine);
        ok = Diverged(context, 400, 300, windows_cursor, "STAGE cursor: 32x32 at 400,300") &&
             Diverged(context, 100, 380, windows_cursor, "STAGE cursor: 32x32 moved to 100,380");
        ClipCursor(NULL);
    }
    if (!ok)
        return FALSE;
    if (!Frame(context, "STAGE cursor: 32x32 at 100,380, old position clear"))
        return FALSE;
    ok = Clear(context, 400, 300, 32, FALSE, "FAIL cursor: 32x32 left at 400,300");
    return Unlock(context) && ok;
}

static void Run(D3D *d3d) {
    WNDCLASSA klass = {};
    D3DFORMAT format = D3DFMT_UNKNOWN;
    Context context = {};
    Surface *small = NULL, *large = NULL;
    HWND window = NULL;
    BOOL passed = FALSE;

    Log("STAGE cursor: SetCursorProperties/SetCursorPosition/ShowCursor on a fullscreen 640x480 "
        "device");
    if (!Check(FullscreenFormat(d3d, &format), "FAIL cursor: no 640x480 HAL mode", 0))
        return;
    get_cursor_info =
        Entry<GetCursorInfoFunction>(GetModuleHandleA("user32.dll"), "GetCursorInfo");
    if (!get_cursor_info)
        Log("STAGE cursor: GetCursorInfo unavailable, GetCursorPos used");
    context.tolerance = format == D3DFMT_X8R8G8B8 ? 0 : 8;
    klass.lpfnWndProc = WindowProc;
    klass.hInstance = GetModuleHandleA(NULL);
    klass.lpszClassName = "DreamGPUCursorProbe"; /* no class cursor */
    if (!Check(RegisterClassA(&klass) != 0, "FAIL cursor: window registration", GetLastError()))
        return;
    window = CreateWindowA(klass.lpszClassName, "DreamGPU cursor probe", WS_POPUP | WS_VISIBLE, 0,
                           0, kModeWidth, kModeHeight, NULL, NULL, klass.hInstance, NULL);
    if (Check(window != NULL, "FAIL cursor: window creation", GetLastError()))
        passed = Stages(d3d, format, window, &context, &small, &large);
    if (!passed)
        failed = TRUE;
    ClipCursor(NULL);
    if (small)
        SURFACE(Release, small);
    if (large)
        SURFACE(Release, large);
    if (context.readback)
        SURFACE(Release, context.readback);
    if (context.device)
        DEVICE(Release, context.device);
    if (window)
        DestroyWindow(window);
    UnregisterClassA(klass.lpszClassName, klass.hInstance);
    Pump();
    if (passed)
        Log("PASS cursor: software 16x16 shown, moved, clipped at both corners and hidden; "
            "32x32 shown and moved; 32x32 drawn by WineD3D where the Windows cursor cannot "
            "follow; alpha-tested halves exact, nothing else drawn");
}

} // namespace cursor_probe
