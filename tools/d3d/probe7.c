/* SPDX-License-Identifier: GPL-2.0-or-later
 * D3D7 HAL fixed-work acceptance through the pinned Wine DirectDraw interface.
 */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>

static HANDLE LogFile;
static IDirectDraw7 *draw;
static IDirect3D7 *d3d;
static IDirect3DDevice7 *device;
static IDirectDrawSurface7 *primary, *target;
static IDirectDrawClipper *clipper;
static HWND window;
static BOOL failed;
static unsigned int(WINAPI *ProbeGlError)(void);
void *memset(void *destination, int value, unsigned int count) {
    volatile BYTE *out = destination;
    while (count--)
        *out++ = (BYTE)value;
    return destination;
}
static void Log(const char *text) {
    DWORD written;
    WriteFile(LogFile, text, lstrlenA(text), &written, NULL);
    WriteFile(LogFile, "\r\n", 2, &written, NULL);
    FlushFileBuffers(LogFile);
}
static void Number(const char *stage, DWORD value) {
    static const char hex[] = "0123456789abcdef";
    char message[] = "value=0x00000000";
    int i;
    Log(stage);
    for (i = 0; i < 8; i++)
        message[8 + i] = hex[(value >> (28 - i * 4)) & 15];
    Log(message);
}
static BOOL Check(BOOL condition, const char *stage, DWORD code) {
    if (condition)
        return TRUE;
    failed = TRUE;
    Number(stage, code);
    return FALSE;
}
static BOOL HR(HRESULT result, const char *stage) {
    return Check(SUCCEEDED(result), stage, (DWORD)result);
}
static LRESULT CALLBACK WindowProc(HWND w, UINT message, WPARAM a, LPARAM b) {
    return DefWindowProcA(w, message, a, b);
}
static void Run(void) {
    typedef HRESULT(WINAPI * CreateDraw)(GUID *, void **, REFIID, IUnknown *);
    union {
        FARPROC generic;
        CreateDraw factory;
    } entry;
    HMODULE gl, runtime;
    char path[MAX_PATH];
    WNDCLASSA klass = {0};
    RECT bounds = {0, 0, 320, 240}, client;
    POINT origin = {0, 0};
    DDSURFACEDESC2 desc = {0}, locked = {0};
    D3DVIEWPORT7 viewport = {0, 0, 320, 240, 0, 1};
    struct Vertex {
        float x, y, z, rhw;
        DWORD color;
    } vertices[3] = {{80, 60, .5f, 1, 0xffff0000},
                     {240, 60, .5f, 1, 0xffff0000},
                     {160, 180, .5f, 1, 0xffff0000}};
    const UINT region_x[5] = {0, 152, 304, 0, 304};
    const UINT region_y[5] = {0, 92, 0, 224, 224};
    const DWORD region_color[5] = {0x102030, 0xff0000, 0x00ff00, 0x00ffff, 0xffff00};
    UINT region, x, y;
    Log("STAGE explicit application-local DreamGPU OpenGL loader");
    gl = LoadLibraryA("C:\\SIERRA\\Half-Life\\dgpugl.dll");
    if (!Check(gl != NULL, "FAIL load DreamGPU OpenGL", GetLastError()))
        return;
    path[0] = 0;
    GetModuleFileNameA(gl, path, sizeof(path));
    Log(path);
    if (!Check(lstrcmpiA(path, "C:\\SIERRA\\Half-Life\\dgpugl.dll") == 0,
               "FAIL system OpenGL fallback is forbidden", 0))
        return;
    ProbeGlError = (void *)GetProcAddress(gl, "glGetError");
    runtime = LoadLibraryA("C:\\SIERRA\\Half-Life\\winedd.dll");
    if (!Check(runtime != NULL, "FAIL load Wine DirectDraw interface", GetLastError()))
        return;
    if (!Check(GetModuleHandleA("dgpugl.dll") == gl, "FAIL Wine OpenGL module identity", 0))
        return;
    entry.generic = GetProcAddress(runtime, "DirectDrawCreateEx");
    if (!Check(entry.generic != NULL, "FAIL DirectDraw factory", GetLastError()))
        return;
    Log("STAGE create DirectDraw7 and Direct3D7");
    if (!HR(entry.factory(NULL, (void **)&draw, &IID_IDirectDraw7, NULL),
            "FAIL DirectDraw7 creation") ||
        !HR(IDirectDraw7_QueryInterface(draw, &IID_IDirect3D7, (void **)&d3d),
            "FAIL Direct3D7 query"))
        return;
    klass.lpfnWndProc = WindowProc;
    klass.hInstance = GetModuleHandleA(NULL);
    klass.lpszClassName = "DreamGPUD3D7Probe";
    if (!Check(RegisterClassA(&klass) != 0, "FAIL class registration", GetLastError()) ||
        !Check(AdjustWindowRect(&bounds, WS_OVERLAPPEDWINDOW, FALSE), "FAIL window bounds",
               GetLastError()))
        return;
    window = CreateWindowA(klass.lpszClassName, "DreamGPU D3D7 probe",
                           WS_OVERLAPPEDWINDOW | WS_VISIBLE, 64, 64, bounds.right - bounds.left,
                           bounds.bottom - bounds.top, NULL, NULL, klass.hInstance, NULL);
    if (!Check(window != NULL, "FAIL window creation", GetLastError()) ||
        !Check(GetClientRect(window, &client) && client.right == 320 && client.bottom == 240,
               "FAIL drawable size", 0))
        return;
    if (!HR(IDirectDraw7_SetCooperativeLevel(draw, window, DDSCL_NORMAL), "FAIL cooperative level"))
        return;
    {
        DDSCAPS2 caps = {0};
        DWORD total = 0, available = 0;
        DDSURFACEDESC2 mode = {0};
        caps.dwCaps = DDSCAPS_VIDEOMEMORY;
        mode.dwSize = sizeof(mode);
        if (HR(IDirectDraw7_GetAvailableVidMem(draw, &caps, &total, &available),
               "FAIL video-memory query")) {
            Number("reported video memory total", total);
            Number("reported video memory available", available);
        }
        if (HR(IDirectDraw7_GetDisplayMode(draw, &mode), "FAIL display-mode query")) {
            Number("desktop width", mode.dwWidth);
            Number("desktop height", mode.dwHeight);
            Number("desktop bits per pixel", mode.ddpfPixelFormat.dwRGBBitCount);
        }
        if (failed)
            return;
    }
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS;
    desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    if (!HR(IDirectDraw7_CreateSurface(draw, &desc, &primary, NULL), "FAIL primary surface") ||
        !HR(IDirectDraw7_CreateClipper(draw, 0, &clipper, NULL), "FAIL clipper") ||
        !HR(IDirectDrawClipper_SetHWnd(clipper, 0, window), "FAIL clipper window") ||
        !HR(IDirectDrawSurface7_SetClipper(primary, clipper), "FAIL primary clipper"))
        return;
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    desc.dwWidth = 320;
    desc.dwHeight = 240;
    desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_3DDEVICE | DDSCAPS_VIDEOMEMORY;
    desc.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
    desc.ddpfPixelFormat.dwFlags = DDPF_RGB;
    desc.ddpfPixelFormat.dwRGBBitCount = 32;
    desc.ddpfPixelFormat.dwRBitMask = 0xff0000;
    desc.ddpfPixelFormat.dwGBitMask = 0xff00;
    desc.ddpfPixelFormat.dwBBitMask = 0xff;
    if (ProbeGlError)
        Number("GL error before HAL", ProbeGlError());
    Log("STAGE create HAL render target and device");
    if (!HR(IDirectDraw7_CreateSurface(draw, &desc, &target, NULL), "FAIL render target") ||
        !HR(IDirect3D7_CreateDevice(d3d, &IID_IDirect3DHALDevice, target, &device),
            "FAIL HAL device") ||
        !HR(IDirect3DDevice7_SetViewport(device, &viewport), "FAIL viewport"))
        return;
    if (!HR(IDirect3DDevice7_SetRenderState(device, D3DRENDERSTATE_LIGHTING, FALSE),
            "FAIL lighting") ||
        !HR(IDirect3DDevice7_SetRenderState(device, D3DRENDERSTATE_ZENABLE, FALSE), "FAIL depth") ||
        !HR(IDirect3DDevice7_SetRenderState(device, D3DRENDERSTATE_CULLMODE, D3DCULL_NONE),
            "FAIL culling") ||
        !HR(IDirect3DDevice7_SetRenderState(device, D3DRENDERSTATE_DITHERENABLE, FALSE),
            "FAIL dither") ||
        !HR(IDirect3DDevice7_SetTextureStageState(device, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1),
            "FAIL color operation") ||
        !HR(IDirect3DDevice7_SetTextureStageState(device, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE),
            "FAIL diffuse argument"))
        return;
    if (ProbeGlError)
        Number("GL error before draw", ProbeGlError());
    Log("STAGE clear and fixed triangle");
    if (!HR(IDirect3DDevice7_Clear(device, 0, NULL, D3DCLEAR_TARGET, 0xff102030, 1, 0),
            "FAIL clear") ||
        !HR(IDirect3DDevice7_BeginScene(device), "FAIL begin scene") ||
        !HR(IDirect3DDevice7_DrawPrimitive(device, D3DPT_TRIANGLELIST,
                                           D3DFVF_XYZRHW | D3DFVF_DIFFUSE, vertices, 3, 0),
            "FAIL draw") ||
        !HR(IDirect3DDevice7_EndScene(device), "FAIL end scene"))
        return;
    /* Distinct far-edge colors detect clipping of the desktop-sized primary
     * through a smaller window backing, including clamped-edge replication. */
    for (region = 2; region < 5; ++region) {
        D3DRECT edge = {0};
        edge.x1 = (LONG)region_x[region];
        edge.y1 = (LONG)region_y[region];
        edge.x2 = edge.x1 + 16;
        edge.y2 = edge.y1 + 16;
        if (!HR(IDirect3DDevice7_Clear(device, 1, &edge, D3DCLEAR_TARGET,
                                       0xff000000 | region_color[region], 1, 0),
                "FAIL distinct far-edge clear"))
            return;
    }
    if (ProbeGlError)
        Number("GL error after draw", ProbeGlError());
    locked.dwSize = sizeof(locked);
    Log("STAGE real render-target readback");
    if (!HR(IDirectDrawSurface7_Lock(target, NULL, &locked, DDLOCK_READONLY | DDLOCK_WAIT, NULL),
            "FAIL readback lock"))
        return;
    if (Check(locked.lpSurface && locked.lPitch >= 1280, "FAIL readback layout", locked.lPitch)) {
        for (region = 0; region < 5 && !failed; region++)
            for (y = 0; y < 16 && !failed; y++)
                for (x = 0; x < 16; x++) {
                    UINT xx = x + region_x[region], yy = y + region_y[region];
                    DWORD expected = region_color[region];
                    DWORD actual =
                        *(DWORD *)((BYTE *)locked.lpSurface + yy * locked.lPitch + xx * 4) &
                        0xffffff;
                    if (!Check(actual == expected, "FAIL exact D3D7 pixel", actual)) {
                        Number("pixel x", xx);
                        Number("pixel y", yy);
                        Number("expected", expected);
                        break;
                    }
                }
    }
    if (!HR(IDirectDrawSurface7_Unlock(target, NULL), "FAIL unlock") || failed)
        return;
    if (ProbeGlError)
        Number("GL error before Blt", ProbeGlError());
    Log("STAGE clipped window presentation");
    if (!Check(ClientToScreen(window, &origin), "FAIL window origin", GetLastError()))
        return;
    OffsetRect(&client, origin.x, origin.y);
    if (!HR(IDirectDrawSurface7_Blt(primary, &client, target, NULL, DDBLT_WAIT, NULL),
            "FAIL present blit"))
        return;
    if (ProbeGlError)
        Number("GL error after Blt", ProbeGlError());
    {
        HDC dc = GetDC(window);
        if (!Check(dc != NULL, "FAIL presented window DC", GetLastError()))
            return;
        Log("STAGE actual clipped GPU front-buffer GDI readback");
        for (region = 0; region < 5 && !failed; region++)
            for (y = 0; y < 16 && !failed; y++)
                for (x = 0; x < 16; x++) {
                    UINT xx = x + region_x[region], yy = y + region_y[region];
                    DWORD color = region_color[region];
                    COLORREF expected = RGB((color >> 16) & 255, (color >> 8) & 255, color & 255);
                    COLORREF actual = GetPixel(dc, xx, yy);
                    if (!Check(actual == expected, "FAIL exact presented D3D7 pixel", actual)) {
                        Number("pixel x", xx);
                        Number("pixel y", yy);
                        Number("expected", expected);
                        {
                            typedef HDC(WINAPI * CurrentDC)(void);
                            typedef void(WINAPI * ReadBuffer)(unsigned int);
                            typedef void(WINAPI * ReadPixels)(int, int, int, int, unsigned int,
                                                              unsigned int, void *);
                            typedef unsigned int(WINAPI * GetError)(void);
                            CurrentDC current =
                                (CurrentDC)(void *)GetProcAddress(gl, "wglGetCurrentDC");
                            ReadBuffer buffer =
                                (ReadBuffer)(void *)GetProcAddress(gl, "glReadBuffer");
                            ReadPixels pixels =
                                (ReadPixels)(void *)GetProcAddress(gl, "glReadPixels");
                            GetError error = (GetError)(void *)GetProcAddress(gl, "glGetError");
                            HWND fg = GetForegroundWindow(),
                                 active = current ? WindowFromDC(current()) : NULL;
                            POINT point = {(int)xx, (int)yy};
                            RECT bounds;
                            DWORD pid = 0;
                            BYTE rgba[4] = {0};
                            HDC screen;
                            Number("owned HWND", (DWORD)window);
                            Number("foreground HWND", (DWORD)fg);
                            GetWindowThreadProcessId(fg, &pid);
                            Number("foreground PID", pid);
                            Number("own PID", GetCurrentProcessId());
                            Number("current GL HWND", (DWORD)active);
                            ClientToScreen(window, &point);
                            Number("screen x", point.x);
                            Number("screen y", point.y);
                            Number("WindowFromPoint", (DWORD)WindowFromPoint(point));
                            Number("visible", IsWindowVisible(window));
                            screen = GetDC(NULL);
                            Number("screen pixel before GL query",
                                   GetPixel(screen, point.x, point.y));
                            ReleaseDC(NULL, screen);
                            if (active && buffer && pixels && error &&
                                GetClientRect(active, &bounds)) {
                                Number("GL drawable width", bounds.right);
                                Number("GL drawable height", bounds.bottom);
                                buffer(0x0404);
                                pixels(xx, bounds.bottom - 1 - yy, 1, 1, 0x1908, 0x1401, rgba);
                                Number("GL FRONT RGBA", *(DWORD *)rgba);
                                Number("GL diagnostic error", error());
                                Number("window pixel after GL query", GetPixel(dc, xx, yy));
                            }
                        }
                        break;
                    }
                }
        ReleaseDC(window, dc);
    }
}
void WINAPI WinMainCRTStartup(void) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    LogFile =
        CreateFileA("C:\\DGD3D7.LOG", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    if (LogFile == INVALID_HANDLE_VALUE)
        ExitProcess(2);
    Run();
    if (device)
        IDirect3DDevice7_Release(device);
    if (target)
        IDirectDrawSurface7_Release(target);
    if (primary)
        IDirectDrawSurface7_Release(primary);
    if (clipper)
        IDirectDrawClipper_Release(clipper);
    if (d3d)
        IDirect3D7_Release(d3d);
    if (draw)
        IDirectDraw7_Release(draw);
    if (window)
        DestroyWindow(window);
    if (!failed)
        Log("PASS automated d3d7: Wine HAL triangle, 1280 exact GPU pixels, 1280 exact front "
            "pixels including far edges and "
            "clean release");
    CloseHandle(LogFile);
    ExitProcess(failed ? 1 : 0);
}
