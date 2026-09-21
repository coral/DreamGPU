/* SPDX-License-Identifier: GPL-2.0-or-later
 * Installed fullscreen primary/GDI coherence, independent of installer smoke.
 */
static WORD FullscreenExpected(UINT x, UINT y, UINT phase) {
    if (phase == 1 && x >= 8 && x < 16 && y >= 8 && y < 16)
        return 0xffff; // Partial ReleaseDC write.
    if (phase == 2 && x < 16 && y < 16)
        return 0xffff; // Keyed source: all other texels are transparent zero.
    if (phase == 3 && x >= 12 && x < 20 && y >= 12 && y < 20)
        return 0xffff; // Eight-by-eight partial source blit.
    static const WORD colors[4][3] = {
        {0xf800, 0x07e0, 0x001f}, {0x001f, 0xf800, 0x07e0},
        {0x07e0, 0x001f, 0xf800}, {0xf800, 0x001f, 0x07e0}};
    return colors[phase][x < 8 ? 0 : x >= 24 ? 2 : 1];
}
static COLORREF FullscreenColor(WORD pixel) {
    return RGB(((pixel >> 11) & 31) * 255 / 31, ((pixel >> 5) & 63) * 255 / 63,
               (pixel & 31) * 255 / 31);
}
static BOOL FullscreenPaint(HDC dc, const RECT &rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    if (!Check(brush != NULL, "FAIL fullscreen GDI brush", GetLastError()))
        return FALSE;
    BOOL painted = FillRect(dc, &rect, brush);
    BOOL deleted = DeleteObject(brush);
    return Check(painted && deleted, "FAIL fullscreen GDI fill", GetLastError());
}
static BOOL FullscreenSeed(UINT phase) {
    HDC dc = GetDC(NULL);
    if (!Check(dc != NULL, "FAIL fullscreen screen DC", GetLastError()))
        return FALSE;
    const RECT green = {32, 32, 64, 64}, red = {32, 32, 40, 64}, blue = {56, 32, 64, 64};
    // Each stage changes every background stripe through GDI. An old cached
    // primary therefore cannot accidentally satisfy an untouched-pixel check.
    BOOL okay = FullscreenPaint(dc, green, FullscreenColor(FullscreenExpected(16, 31, phase))) &&
                FullscreenPaint(dc, red, FullscreenColor(FullscreenExpected(0, 31, phase))) &&
                FullscreenPaint(dc, blue, FullscreenColor(FullscreenExpected(31, 31, phase)));
    Check(GdiFlush(), "FAIL fullscreen GDI flush", GetLastError());
    Check(ReleaseDC(NULL, dc) != 0, "FAIL fullscreen screen DC release", GetLastError());
    return okay && !failed;
}
static BOOL FullscreenPixels(IDirectDrawSurface4 *surface, RECT rect, UINT phase) {
    DDSURFACEDESC2 mapped = {};
    mapped.dwSize = sizeof(mapped);
    if (!HR(IDirectDrawSurface4_Lock(surface, &rect, &mapped, DDLOCK_WAIT | DDLOCK_READONLY, NULL),
            "FAIL fullscreen pixel Lock"))
        return FALSE;
    if (Check(mapped.lpSurface && mapped.lPitch >= 64 &&
                  mapped.ddpfPixelFormat.dwRGBBitCount == 16 &&
                  mapped.ddpfPixelFormat.dwRBitMask == 0xf800 &&
                  mapped.ddpfPixelFormat.dwGBitMask == 0x07e0 &&
                  mapped.ddpfPixelFormat.dwBBitMask == 0x001f,
              "FAIL fullscreen mapped RGB565 layout", mapped.lPitch)) {
        for (UINT y = 0; y < 32 && !failed; ++y) {
            const WORD *row = reinterpret_cast<const WORD *>(
                static_cast<const BYTE *>(mapped.lpSurface) + y * mapped.lPitch);
            for (UINT x = 0; x < 32; ++x)
                if (!Check(row[x] == FullscreenExpected(x, y, phase),
                           "FAIL fullscreen exact pixel or untouched region", row[x])) {
                    Number("fullscreen pixel coordinates", x | (y << 16));
                    Number("fullscreen pixel phase", phase);
                    break;
                }
        }
    }
    HR(IDirectDrawSurface4_Unlock(surface, &rect), "FAIL fullscreen pixel Unlock");
    return !failed;
}
static BOOL FullscreenDcPixels(HDC dc, UINT phase) {
    for (UINT y = 0; y < 32 && !failed; ++y)
        for (UINT x = 0; x < 32; ++x) {
            COLORREF value = GetPixel(dc, 32 + x, 32 + y);
            if (!Check(value == FullscreenColor(FullscreenExpected(x, y, phase)),
                       "FAIL fullscreen DC exact pixel or untouched region", value)) {
                Number("fullscreen DC pixel coordinates", x | (y << 16));
                break;
            }
        }
    return !failed;
}
static BOOL FullscreenProbe() {
    IDirectDrawSurface4 *front = NULL, *source = NULL;
    HDC dc = NULL;
    BOOL exclusive = FALSE, mode_set = FALSE;
    DDSURFACEDESC2 desc = {}, mode = {};
    RECT original_window = {};
    if (!Check(GetWindowRect(window, &original_window), "FAIL fullscreen saved window geometry", GetLastError()))
        return FALSE;
    RECT region = {32, 32, 64, 64}, local = {0, 0, 32, 32};
    Log("STAGE fullscreen initial mode, GDI/primary read and partial-write lifecycle");
    do {
        if (!HR(IDirectDraw4_SetCooperativeLevel(draw, window, DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN),
                "FAIL fullscreen cooperative level"))
            break;
        exclusive = TRUE;
        if (!HR(IDirectDraw4_SetDisplayMode(draw, 640, 480, 16, 0, 0),
                "FAIL fullscreen set 640x480x16"))
            break;
        mode_set = TRUE;
        mode.dwSize = sizeof(mode);
        DEVMODEA actual = {};
        actual.dmSize = sizeof(actual);
        RECT client = {};
        if (!HR(IDirectDraw4_GetDisplayMode(draw, &mode), "FAIL fullscreen reported mode") ||
            !Check(EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &actual),
                   "FAIL fullscreen actual mode query", GetLastError()) ||
            !Check(GetClientRect(window, &client), "FAIL fullscreen client query", GetLastError()))
            break;
        Number("fullscreen actual width", actual.dmPelsWidth);
        Number("fullscreen actual height", actual.dmPelsHeight);
        Number("fullscreen actual bpp", actual.dmBitsPerPel);
        Number("fullscreen client width", client.right - client.left);
        Number("fullscreen client height", client.bottom - client.top);
        if (!Check(mode.dwWidth == 640 && mode.dwHeight == 480 &&
                       mode.ddpfPixelFormat.dwRGBBitCount == 16 && actual.dmPelsWidth == 640 &&
                       actual.dmPelsHeight == 480 && actual.dmBitsPerPel == 16 &&
                       GetSystemMetrics(SM_CXSCREEN) == 640 && GetSystemMetrics(SM_CYSCREEN) == 480 &&
                       client.right - client.left == 640 && client.bottom - client.top == 480,
                   "FAIL fullscreen initial guest mode/client mismatch", 0))
            break;
        desc.dwSize = sizeof(desc);
        desc.dwFlags = DDSD_CAPS;
        desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
        if (!HR(IDirectDraw4_CreateSurface(draw, &desc, &front, NULL), "FAIL fullscreen primary"))
            break;
        desc = {};
        desc.dwSize = sizeof(desc);
        if (!HR(IDirectDrawSurface4_GetSurfaceDesc(front, &desc), "FAIL fullscreen primary description") ||
            !Check(desc.dwWidth == 640 && desc.dwHeight == 480,
                   "FAIL fullscreen primary dimensions", desc.dwWidth))
            break;
        desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
        desc.dwWidth = desc.dwHeight = 32;
        desc.ddsCaps = {};
        desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
        if (!HR(IDirectDraw4_CreateSurface(draw, &desc, &source, NULL), "FAIL fullscreen copy surface"))
            break;
        Log("STAGE fullscreen GDI to primary Lock/GetDC/source Blt");
        if (!FullscreenSeed(0) || !FullscreenPixels(front, region, 0) ||
            !HR(IDirectDrawSurface4_GetDC(front, &dc), "FAIL fullscreen primary GetDC"))
            break;
        FullscreenDcPixels(dc, 0);
        HR(IDirectDrawSurface4_ReleaseDC(front, dc), "FAIL fullscreen primary read-only ReleaseDC");
        dc = NULL;
        if (failed || !HR(IDirectDrawSurface4_Blt(source, &local, front, &region, DDBLT_WAIT, NULL),
                          "FAIL fullscreen primary source Blt") || !FullscreenPixels(source, local, 0))
            break;
        Log("STAGE fullscreen partial GetDC/ReleaseDC preserves GDI pixels");
        if (!FullscreenSeed(1) || !HR(IDirectDrawSurface4_GetDC(front, &dc), "FAIL fullscreen write GetDC"))
            break;
        // PatBlt may remain batched. ReleaseDC must finish the GDI writes
        // before uploading its DIB; no caller flush or brush deletion masks it.
        Check(PatBlt(dc, 40, 40, 8, 8, WHITENESS), "FAIL fullscreen mapped PatBlt", GetLastError());
        HR(IDirectDrawSurface4_ReleaseDC(front, dc), "FAIL fullscreen partial ReleaseDC");
        dc = NULL;
        if (failed || !FullscreenPixels(front, region, 1))
            break;
        DDSURFACEDESC2 mapped = {};
        mapped.dwSize = sizeof(mapped);
        if (!HR(IDirectDrawSurface4_Lock(source, NULL, &mapped, DDLOCK_WAIT, NULL),
                "FAIL fullscreen keyed source Lock"))
            break;
        if (Check(mapped.lpSurface && mapped.lPitch >= 64, "FAIL fullscreen source pitch", mapped.lPitch))
            for (UINT y = 0; y < 32; ++y) {
                WORD *row = reinterpret_cast<WORD *>(static_cast<BYTE *>(mapped.lpSurface) + y * mapped.lPitch);
                for (UINT x = 0; x < 32; ++x)
                    row[x] = x < 16 && y < 16 ? 0xffff : 0;
            }
        HR(IDirectDrawSurface4_Unlock(source, NULL), "FAIL fullscreen keyed source Unlock");
        DDCOLORKEY key = {};
        Log("STAGE fullscreen keyed primary Blt preserves GDI destination");
        if (failed || !HR(IDirectDrawSurface4_SetColorKey(source, DDCKEY_SRCBLT, &key),
                          "FAIL fullscreen source color key") || !FullscreenSeed(2) ||
            !HR(IDirectDrawSurface4_Blt(front, &region, source, &local, DDBLT_WAIT | DDBLT_KEYSRC, NULL),
                "FAIL fullscreen keyed primary Blt") || !FullscreenPixels(front, region, 2))
            break;
        Log("STAGE fullscreen partial primary Blt preserves GDI destination");
        RECT destination = {44, 44, 52, 52}, from = {4, 4, 12, 12};
        if (!FullscreenSeed(3) ||
            !HR(IDirectDrawSurface4_Blt(front, &destination, source, &from, DDBLT_WAIT, NULL),
                "FAIL fullscreen partial primary Blt") || !FullscreenPixels(front, region, 3))
            break;
        HDC screen = GetDC(NULL);
        if (Check(screen != NULL, "FAIL fullscreen final screen DC", GetLastError())) {
            FullscreenDcPixels(screen, 3);
            Check(ReleaseDC(NULL, screen) != 0, "FAIL fullscreen final screen DC release", GetLastError());
        }
    } while (FALSE);
    if (dc && front)
        HR(IDirectDrawSurface4_ReleaseDC(front, dc), "FAIL fullscreen cleanup ReleaseDC");
    if (source)
        IDirectDrawSurface4_Release(source);
    if (front)
        IDirectDrawSurface4_Release(front);
    if (mode_set)
        HR(IDirectDraw4_RestoreDisplayMode(draw), "FAIL fullscreen restore original mode");
    if (exclusive)
        HR(IDirectDraw4_SetCooperativeLevel(draw, window, DDSCL_NORMAL), "FAIL fullscreen restore cooperative level");
    if (exclusive)
        Check(SetWindowPos(window, NULL, original_window.left, original_window.top,
                           original_window.right - original_window.left,
                           original_window.bottom - original_window.top, SWP_NOZORDER | SWP_NOACTIVATE),
              "FAIL fullscreen restore window geometry", GetLastError());
    if (!failed)
        Log("PASS fullscreen lifecycle: initial 640x480x16, exact primary/GDI reads and preserved partial/keyed writes");
    return !failed;
}
