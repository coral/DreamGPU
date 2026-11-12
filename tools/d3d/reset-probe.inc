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

} // namespace reset_probe
