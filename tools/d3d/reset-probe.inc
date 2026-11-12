/* SPDX-License-Identifier: GPL-2.0-or-later
 * IDirect3DDevice8/9::Reset while the device window has no client area, and
 * recovery when it returns (frontend minimized/hidden windows, Wine 0059).
 * probe.cpp runs it right after the reference triangle, before the other
 * stages create resources (a reset needs every default-pool resource
 * released), on its windowed 320x240 device. A windowed device installs no
 * focus hooks: the probe's own ShowWindow calls change nothing but the
 * window. A state reset destroys WineD3D's GL contexts and creates the
 * primary one again on the device window's DC. Warcraft III resets once it
 * is active again; dgpugl.dll created no context on a window without a
 * client area (its fullscreen window still minimized), so that reset failed
 * after destroying every context and the next one dereferenced a NULL
 * context (wined3d_device_reset -> delete_opengl_contexts). Here:
 * - a reset with the window visible renders the reference frame exactly;
 * - a reset with the window hidden, then shown without activation, renders
 *   it exactly;
 * - two resets with the window minimized (zero client area) succeed, with a
 *   scene and Present in between, as a game's loop retries;
 * - the restored (and so active again) window renders the reference frame
 *   exactly without another reset (the context made while minimized keeps a
 *   placeholder until then); and a final reset renders it again.
 * On failure the window is shown again and the remaining stages are skipped.
 * The back buffer surface is released for each reset (an outstanding
 * reference blocks it) and left acquired, with probe.cpp's state set again,
 * for the stages that follow.
 *
 * Fullscreen() (Wine 0060) runs last, after probe.cpp has released its
 * windowed device, on a fullscreen 640x480 device and window of its own. Splinter Cell (D3D8)
 * creates its window at 640x480, renders fullscreen at 1024x768 and sizes the
 * window back from WM_SIZE, nested in WineD3D's own SetWindowPos; DreamGPU
 * presents into the window's client area, so the game showed in a corner of
 * the mode. After a focus change WineD3D left the window it had minimized
 * iconic on activation, and the game never reset. Here the window is created
 * at 320x240 and its procedure shrinks it back to 320x240 on every other
 * WM_SIZE, synchronously, as the game does:
 * - after CreateDevice, the application's own SetWindowPos and MoveWindow,
 *   and a Reset, the window covers the mode (0,0, 640x480 client area);
 * - focus loss (the WM_ACTIVATEAPP Windows sends, sent here directly so that
 *   foreground rules do not matter) minimizes the window and loses the
 *   device; activation restores it, covering the mode, the device needs a
 *   reset, and Reset succeeds and renders;
 * - the clamped shrink requests never nest (no WM_SIZE loop).
 * With an X8R8G8B8 mode the frames are compared exactly (probe.cpp's
 * reference), with R5G6B5 only rendered and presented.
 */
namespace reset_probe {

static void Pump(void) {
    MSG message;
    for (UINT count = 0; count < 64 && PeekMessageA(&message, NULL, 0, 0, PM_REMOVE); ++count) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
}

/* probe.cpp's fixed-function state; a reset restores every default. */
static BOOL Setup(Device *device) {
    if (!HR(DEVICE(SetRenderState, device, D3DRS_LIGHTING, FALSE),
            "FAIL reset: disable lighting") ||
        !HR(DEVICE(SetRenderState, device, D3DRS_ZENABLE, FALSE), "FAIL reset: disable depth") ||
        !HR(DEVICE(SetRenderState, device, D3DRS_CULLMODE, D3DCULL_NONE),
            "FAIL reset: disable culling") ||
        !HR(DEVICE(SetRenderState, device, D3DRS_DITHERENABLE, FALSE),
            "FAIL reset: disable dither") ||
        !HR(DEVICE(SetTextureStageState, device, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1),
            "FAIL reset: diffuse operation") ||
        !HR(DEVICE(SetTextureStageState, device, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE),
            "FAIL reset: diffuse argument"))
        return FALSE;
#if DG_D3D_VERSION == 8
    return HR(DEVICE(SetVertexShader, device, D3DFVF_XYZRHW | D3DFVF_DIFFUSE),
              "FAIL reset: fixed vertex format");
#else
    return HR(DEVICE(SetFVF, device, D3DFVF_XYZRHW | D3DFVF_DIFFUSE),
              "FAIL reset: fixed vertex format");
#endif
}

static BOOL Reset(Device *device, Surface **surface, const D3DPRESENT_PARAMETERS *parameters,
                  const char *stage) {
    D3DPRESENT_PARAMETERS copy = *parameters;
    if (*surface) {
        SURFACE(Release, *surface);
        *surface = NULL;
    }
    if (!HR(DEVICE(Reset, device, &copy), stage))
        return FALSE;
    return HR(DEVICE(TestCooperativeLevel, device), "FAIL reset: cooperative level after reset") &&
           Setup(device);
}

/* A scene without readback: the minimized window has nothing to read. */
static BOOL Scene(Device *device, const char *stage) {
    struct Vertex {
        float x, y, z, rhw;
        DWORD color;
    } vertices[3] = {{80, 60, .5f, 1, 0xffff0000},
                     {240, 60, .5f, 1, 0xffff0000},
                     {160, 180, .5f, 1, 0xffff0000}};
    if (!HR(DEVICE(Clear, device, 0, NULL, D3DCLEAR_TARGET, 0xff102030, 1, 0), stage) ||
        !HR(DEVICE(BeginScene, device), stage) ||
        !HR(DEVICE(DrawPrimitiveUP, device, D3DPT_TRIANGLELIST, 1, vertices, sizeof(vertices[0])),
            stage) ||
        !HR(DEVICE(EndScene, device), stage))
        return FALSE;
    return TRUE;
}

/* probe.cpp's reference frame, read back from the real back buffer. */
static BOOL Frame(Device *device, Surface **surface, const char *stage) {
    D3DLOCKED_RECT locked;
    BOOL exact;
    Log(stage);
    if (!Scene(device, "FAIL reset: reference scene"))
        return FALSE;
    if (!*surface) {
#if DG_D3D_VERSION == 8
        if (!HR(DEVICE(GetBackBuffer, device, 0, D3DBACKBUFFER_TYPE_MONO, surface),
                "FAIL reset: acquire backbuffer"))
            return FALSE;
#else
        if (!HR(DEVICE(GetBackBuffer, device, 0, 0, D3DBACKBUFFER_TYPE_MONO, surface),
                "FAIL reset: acquire backbuffer"))
            return FALSE;
#endif
    }
    if (!HR(SURFACE(LockRect, *surface, &locked, NULL, D3DLOCK_READONLY),
            "FAIL reset: lock backbuffer"))
        return FALSE;
    exact = Pixels(&locked);
    return HR(SURFACE(UnlockRect, *surface), "FAIL reset: unlock backbuffer") && exact &&
           HR(DEVICE(Present, device, NULL, NULL, NULL, NULL), "FAIL reset: present");
}

static BOOL ClientArea(HWND window, LONG width, LONG height, const char *stage) {
    RECT client = {};
    if (!GetClientRect(window, &client))
        return Check(FALSE, stage, GetLastError());
    if (client.right - client.left == width && client.bottom - client.top == height)
        return TRUE;
    Number("client width", (DWORD)(client.right - client.left));
    Number("client height", (DWORD)(client.bottom - client.top));
    return Check(FALSE, stage, 0);
}

static BOOL Stages(Device *device, Surface **surface, HWND window,
                   const D3DPRESENT_PARAMETERS *parameters) {
    if (!Reset(device, surface, parameters, "FAIL reset: visible window") ||
        !Frame(device, surface, "STAGE reset: visible window, exact reference frame"))
        return FALSE;

    Log("STAGE reset: hidden window");
    ShowWindow(window, SW_HIDE);
    Pump();
    if (!Check(!IsWindowVisible(window), "FAIL reset: window not hidden", 0) ||
        !Reset(device, surface, parameters, "FAIL reset: hidden window"))
        return FALSE;
    ShowWindow(window, SW_SHOWNA);
    Pump();
    if (!ClientArea(window, 320, 240, "FAIL reset: shown client area") ||
        !Frame(device, surface, "STAGE reset: shown window, exact reference frame"))
        return FALSE;

    Log("STAGE reset: minimized window (zero client area), reset, scene, present, reset");
    ShowWindow(window, SW_MINIMIZE);
    Pump();
    if (!Check(IsIconic(window), "FAIL reset: window not minimized", 0) ||
        !ClientArea(window, 0, 0, "FAIL reset: minimized client area") ||
        !Reset(device, surface, parameters, "FAIL reset: minimized window") ||
        !Scene(device, "FAIL reset: minimized scene") ||
        !HR(DEVICE(Present, device, NULL, NULL, NULL, NULL), "FAIL reset: minimized present") ||
        !Reset(device, surface, parameters, "FAIL reset: minimized window again") ||
        !Check(IsIconic(window), "FAIL reset: reset restored the window", 0))
        return FALSE;

    ShowWindow(window, SW_RESTORE);
    Pump();
    if (!ClientArea(window, 320, 240, "FAIL reset: restored client area") ||
        !Frame(device, surface,
               "STAGE reset: restored window without reset, exact reference frame"))
        return FALSE;

    return Reset(device, surface, parameters, "FAIL reset: visible window again") &&
           Frame(device, surface, "STAGE reset: visible window again, exact reference frame");
}

static void Run(Device *device, Surface **surface, HWND window,
                const D3DPRESENT_PARAMETERS *parameters) {
    if (Stages(device, surface, window, parameters)) {
        Log("PASS reset: visible, hidden and shown, minimized (twice) and restored windows; "
            "4 exact reference frames");
        return;
    }
    failed = TRUE;
    /* Leave a normal window for probe.cpp's present and release. */
    if (IsIconic(window))
        ShowWindow(window, SW_RESTORE);
    else if (!IsWindowVisible(window))
        ShowWindow(window, SW_SHOWNA);
    Pump();
}

enum : LONG { kModeWidth = 640, kModeHeight = 480, kSmallWidth = 320, kSmallHeight = 240 };
static BOOL shrinking;
static UINT shrink_requests, shrink_depth, shrink_deepest;

/* The game's window procedure: any other size is answered at once, nested in
 * the SetWindowPos that sent WM_SIZE. The depth limit keeps a clamp loop from
 * overflowing the stack; it is reported as a failure. */
static LRESULT CALLBACK ShrinkingWindowProc(HWND window, UINT message, WPARAM wparam,
                                            LPARAM lparam) {
    if (message == WM_SIZE && shrinking && wparam != SIZE_MINIMIZED &&
        (LOWORD(lparam) != kSmallWidth || HIWORD(lparam) != kSmallHeight)) {
        ++shrink_requests;
        if (shrink_depth < 32) {
            if (++shrink_depth > shrink_deepest)
                shrink_deepest = shrink_depth;
            SetWindowPos(window, NULL, 0, 0, kSmallWidth, kSmallHeight,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            --shrink_depth;
        }
    }
    return DefWindowProcA(window, message, wparam, lparam);
}

/* A 640x480 HAL mode, X8R8G8B8 preferred. */
static BOOL FullscreenFormat(D3D *d3d, D3DFORMAT *format) {
    static const D3DFORMAT formats[] = {D3DFMT_X8R8G8B8, D3DFMT_R5G6B5};
    for (UINT f = 0; f < sizeof(formats) / sizeof(formats[0]); ++f) {
#if DG_D3D_VERSION == 8
        const UINT count = D3D_CALL(GetAdapterModeCount, d3d, D3DADAPTER_DEFAULT);
#else
        const UINT count = D3D_CALL(GetAdapterModeCount, d3d, D3DADAPTER_DEFAULT, formats[f]);
#endif
        for (UINT i = 0; i < count && i < 4096; ++i) {
            D3DDISPLAYMODE mode = {};
#if DG_D3D_VERSION == 8
            if (FAILED(D3D_CALL(EnumAdapterModes, d3d, D3DADAPTER_DEFAULT, i, &mode)))
                continue;
#else
            if (FAILED(D3D_CALL(EnumAdapterModes, d3d, D3DADAPTER_DEFAULT, formats[f], i, &mode)))
                continue;
#endif
            if (mode.Width == (UINT)kModeWidth && mode.Height == (UINT)kModeHeight &&
                mode.Format == formats[f] &&
                SUCCEEDED(D3D_CALL(CheckDeviceType, d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
                                   formats[f], formats[f], FALSE))) {
                *format = formats[f];
                return TRUE;
            }
        }
    }
    return FALSE;
}

/* At 0,0, not iconic, with a client area of the mode's size. */
static BOOL CoversMode(HWND window, const char *stage) {
    RECT bounds = {}, client = {};
    const BOOL iconic = IsIconic(window);
    if (!GetWindowRect(window, &bounds) || !GetClientRect(window, &client))
        return Check(FALSE, stage, GetLastError());
    if (!iconic && bounds.left == 0 && bounds.top == 0 && client.right == kModeWidth &&
        client.bottom == kModeHeight)
        return TRUE;
    Number("iconic", (DWORD)iconic);
    Number("window left", (DWORD)bounds.left);
    Number("window top", (DWORD)bounds.top);
    Number("client width", (DWORD)client.right);
    Number("client height", (DWORD)client.bottom);
    Number("shrink requests", shrink_requests);
    return Check(FALSE, stage, 0);
}

static BOOL FullscreenFrame(Device *device, Surface **surface, BOOL exact, const char *stage) {
    if (exact)
        return Frame(device, surface, stage);
    Log(stage);
    return Scene(device, "FAIL fullscreen reset: scene") &&
           HR(DEVICE(Present, device, NULL, NULL, NULL, NULL), "FAIL fullscreen reset: present");
}

static BOOL CooperativeLevel(Device *device, HRESULT expected, const char *stage) {
    const HRESULT result = DEVICE(TestCooperativeLevel, device);
    return Check(result == expected, stage, (DWORD)result);
}

static BOOL FullscreenStages(D3D *d3d, D3DFORMAT format, HWND window, Device **device,
                             Surface **surface) {
    D3DPRESENT_PARAMETERS parameters = {};
    const BOOL exact = format == D3DFMT_X8R8G8B8;
    parameters.BackBufferWidth = kModeWidth;
    parameters.BackBufferHeight = kModeHeight;
    parameters.BackBufferFormat = format;
    parameters.BackBufferCount = 1;
    parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    parameters.hDeviceWindow = window;
    parameters.Windowed = FALSE;
    parameters.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
#if DG_D3D_VERSION == 8
    parameters.FullScreen_PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
#else
    parameters.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
#endif
    if (!exact)
        Log("STAGE fullscreen reset: R5G6B5 mode, frames rendered without exact comparison");
    {
        D3DPRESENT_PARAMETERS copy = parameters;
        if (!HR(D3D_CALL(CreateDevice, d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
                         D3DCREATE_SOFTWARE_VERTEXPROCESSING, &copy, device),
                "FAIL fullscreen reset: fullscreen device creation"))
            return FALSE;
    }
    Pump();
    if (!CoversMode(window, "FAIL fullscreen reset: window after CreateDevice") ||
        !Check(shrink_requests != 0, "FAIL fullscreen reset: the window never asked to shrink",
               0) ||
        !Setup(*device) ||
        !FullscreenFrame(*device, surface, exact,
                         "STAGE fullscreen reset: created device covers the mode"))
        return FALSE;

    Log("STAGE fullscreen reset: application SetWindowPos and MoveWindow while active");
    SetWindowPos(window, NULL, 0, 0, kSmallWidth, kSmallHeight,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    MoveWindow(window, 32, 32, kSmallWidth, kSmallHeight, TRUE);
    Pump();
    if (!CoversMode(window, "FAIL fullscreen reset: application resize while active") ||
        !Reset(*device, surface, &parameters, "FAIL fullscreen reset: Reset") ||
        !CoversMode(window, "FAIL fullscreen reset: window after Reset") ||
        !FullscreenFrame(*device, surface, exact, "STAGE fullscreen reset: frame after Reset"))
        return FALSE;

    Log("STAGE fullscreen reset: focus loss minimizes, activation restores");
    if (*surface) {
        SURFACE(Release, *surface);
        *surface = NULL;
    }
    SendMessageA(window, WM_ACTIVATEAPP, FALSE, 0);
    Pump();
    if (!Check(IsIconic(window), "FAIL fullscreen reset: focus loss did not minimize", 0) ||
        !CooperativeLevel(*device, D3DERR_DEVICELOST,
                          "FAIL fullscreen reset: device not lost after focus loss"))
        return FALSE;
    SendMessageA(window, WM_ACTIVATEAPP, TRUE, 0);
    Pump();
    if (!Check(!IsIconic(window), "FAIL fullscreen reset: window still minimized after activation",
               0) ||
        !CoversMode(window, "FAIL fullscreen reset: window after activation") ||
        !CooperativeLevel(*device, D3DERR_DEVICENOTRESET,
                          "FAIL fullscreen reset: device not ready for reset after activation") ||
        !Reset(*device, surface, &parameters, "FAIL fullscreen reset: Reset after activation") ||
        !CoversMode(window, "FAIL fullscreen reset: window after Reset after activation") ||
        !FullscreenFrame(*device, surface, exact,
                         "STAGE fullscreen reset: frame after activation and Reset"))
        return FALSE;

    Number("shrink requests", shrink_requests);
    Number("deepest nested shrink", shrink_deepest);
    return Check(shrink_deepest == 1, "FAIL fullscreen reset: nested shrink requests",
                 shrink_deepest);
}

static void Fullscreen(D3D *d3d) {
    WNDCLASSA klass = {};
    D3DFORMAT format = D3DFMT_UNKNOWN;
    Device *device = NULL;
    Surface *surface = NULL;
    HWND window = NULL;
    BOOL passed = FALSE;

    Log("STAGE fullscreen reset: 640x480 device on a 320x240 window that shrinks itself from "
        "WM_SIZE");
    if (!Check(FullscreenFormat(d3d, &format), "FAIL fullscreen reset: no 640x480 HAL mode", 0))
        return;
    klass.lpfnWndProc = ShrinkingWindowProc;
    klass.hInstance = GetModuleHandleA(NULL);
    klass.lpszClassName = "DreamGPUFullscreenResetProbe";
    if (!Check(RegisterClassA(&klass) != 0, "FAIL fullscreen reset: window registration",
               GetLastError()))
        return;
    window = CreateWindowA(klass.lpszClassName, "DreamGPU fullscreen reset probe",
                           WS_POPUP | WS_VISIBLE, 32, 32, kSmallWidth, kSmallHeight, NULL, NULL,
                           klass.hInstance, NULL);
    if (Check(window != NULL, "FAIL fullscreen reset: window creation", GetLastError())) {
        shrinking = TRUE;
        shrink_requests = shrink_depth = shrink_deepest = 0;
        passed = FullscreenStages(d3d, format, window, &device, &surface);
        shrinking = FALSE;
    }
    if (surface)
        SURFACE(Release, surface);
    if (device)
        DEVICE(Release, device);
    if (window)
        DestroyWindow(window);
    UnregisterClassA(klass.lpszClassName, klass.hInstance);
    Pump();
    if (passed)
        Log("PASS fullscreen reset: covers the 640x480 mode after creation, application resizes "
            "and resets; focus loss minimizes, activation restores, Reset renders; no nested "
            "shrink");
}

} // namespace reset_probe
