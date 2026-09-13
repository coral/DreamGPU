/* SPDX-License-Identifier: GPL-2.0-or-later
 * D3D6 HAL acceptance: real IDirectDraw4 / IDirect3D3 / Device3 / Viewport3.
 * Fixed CD helper protocol, 512 target + 512 presented pixels, no software device.
 */
#define WIN32_LEAN_AND_MEAN
#define CINTERFACE
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include "entry.h"
#include <ddraw.h>
#include <d3d.h>
#ifdef DG_SYSTEM_D3D
#include "system-loader.h"
#define DG_LOG_PATH "C:\\DGSYS6.LOG"
#define DG_PROBE_NAME "sysd3d6"
#else
#define DG_LOG_PATH "C:\\DGD3D6.LOG"
#define DG_PROBE_NAME "d3d6"
#endif

static HANDLE LogFile;
static IDirectDraw4 *draw;
static IDirect3D3 *d3d;
static IDirect3DDevice3 *device;
static IDirect3DViewport3 *view;
static IDirectDrawSurface4 *primary, *target;
static IDirectDrawClipper *clipper;
static HWND window;
static BOOL failed;
static unsigned int(WINAPI *ProbeGlError)(void);
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
    typedef HRESULT(WINAPI * CreateDraw)(GUID *, IDirectDraw **, IUnknown *);
    IDirectDraw *legacy_draw = NULL;
    HMODULE gl, runtime;
#ifndef DG_SYSTEM_D3D
    char path[MAX_PATH];
#endif
    WNDCLASSA klass = {};
    RECT bounds = {0, 0, 320, 240}, client;
    POINT origin = {0, 0};
    DDSURFACEDESC2 desc = {}, locked = {};
    D3DVIEWPORT2 viewport = {sizeof(D3DVIEWPORT2), 0, 0, 320, 240, -1, 1, 2, 2, 0, 1};
    D3DRECT clear_rect = {.x1 = 0, .y1 = 0, .x2 = 320, .y2 = 240};
    struct Vertex {
        float x, y, z, rhw;
        DWORD color;
    } vertices[3] = {{80, 60, .5f, 1, 0xffff0000},
                     {240, 60, .5f, 1, 0xffff0000},
                     {160, 180, .5f, 1, 0xffff0000}};
    UINT region, x, y;
#ifdef DG_SYSTEM_D3D
    Log("STAGE ordinary system DirectDraw loader");
    runtime = system_loader::load("ddraw.dll", Log);
    gl = nullptr;
    if (!Check(runtime != nullptr, "FAIL system DirectDraw loader or private neighbor",
               GetLastError()))
        return;
#else
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
    ProbeGlError = Entry<decltype(ProbeGlError)>(gl, "glGetError");
    runtime = LoadLibraryA("C:\\SIERRA\\Half-Life\\winedd.dll");
    if (!Check(runtime != NULL, "FAIL load Wine DirectDraw interface", GetLastError()))
        return;
    if (!Check(GetModuleHandleA("dgpugl.dll") == gl, "FAIL Wine OpenGL module identity", 0))
        return;
#endif
    const auto create = Entry<CreateDraw>(runtime, "DirectDrawCreate");
    if (!Check(create != nullptr, "FAIL DirectDraw factory", GetLastError()))
        return;
    Log("STAGE create DirectDraw4 and Direct3D3");
    if (!HR(create(NULL, &legacy_draw, NULL), "FAIL DirectDraw creation"))
        return;
    HRESULT query = IDirectDraw_QueryInterface(legacy_draw, IID_IDirectDraw4, (void **)&draw);
    IDirectDraw_Release(legacy_draw);
    if (!HR(query, "FAIL DirectDraw4 query") ||
        !HR(IDirectDraw4_QueryInterface(draw, IID_IDirect3D3, (void **)&d3d),
            "FAIL Direct3D3 query"))
        return;
#ifdef DG_SYSTEM_D3D
    if (!Check(system_loader::object(draw->lpVtbl, "winedd.dll"),
               "FAIL actual system Wine DirectDraw object/dependencies", 0))
        return;
    gl = GetModuleHandleA("dgpugl.dll");
    ProbeGlError = Entry<decltype(ProbeGlError)>(gl, "glGetError");
#endif
    klass.lpfnWndProc = WindowProc;
    klass.hInstance = GetModuleHandleA(NULL);
    klass.lpszClassName = "DreamGPUD3D6Probe";
    if (!Check(RegisterClassA(&klass) != 0, "FAIL class registration", GetLastError()) ||
        !Check(AdjustWindowRect(&bounds, WS_OVERLAPPEDWINDOW, FALSE), "FAIL window bounds",
               GetLastError()))
        return;
    window = CreateWindowA(klass.lpszClassName, "DreamGPU D3D6 probe",
                           WS_OVERLAPPEDWINDOW | WS_VISIBLE, 64, 64, bounds.right - bounds.left,
                           bounds.bottom - bounds.top, NULL, NULL, klass.hInstance, NULL);
    if (!Check(window != NULL, "FAIL window creation", GetLastError()) ||
        !Check(GetClientRect(window, &client) && client.right == 320 && client.bottom == 240,
               "FAIL drawable size", 0))
        return;
    if (!HR(IDirectDraw4_SetCooperativeLevel(draw, window, DDSCL_NORMAL), "FAIL cooperative level"))
        return;
    {
        DDSCAPS2 caps = {};
        DWORD total = 0, available = 0;
        DDSURFACEDESC2 mode = {};
        caps.dwCaps = DDSCAPS_VIDEOMEMORY;
        mode.dwSize = sizeof(mode);
        if (HR(IDirectDraw4_GetAvailableVidMem(draw, &caps, &total, &available),
               "FAIL video-memory query")) {
            Number("reported video memory total", total);
            Number("reported video memory available", available);
        }
        if (HR(IDirectDraw4_GetDisplayMode(draw, &mode), "FAIL display-mode query")) {
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
    if (!HR(IDirectDraw4_CreateSurface(draw, &desc, &primary, NULL), "FAIL primary surface") ||
        !HR(IDirectDraw4_CreateClipper(draw, 0, &clipper, NULL), "FAIL clipper") ||
        !HR(IDirectDrawClipper_SetHWnd(clipper, 0, window), "FAIL clipper window") ||
        !HR(IDirectDrawSurface4_SetClipper(primary, clipper), "FAIL primary clipper"))
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
    if (!HR(IDirectDraw4_CreateSurface(draw, &desc, &target, NULL), "FAIL render target") ||
        !HR(IDirect3D3_CreateDevice(d3d, IID_IDirect3DHALDevice, target, &device, NULL),
            "FAIL HAL device") ||
        !HR(IDirect3D3_CreateViewport(d3d, &view, NULL), "FAIL viewport creation") ||
        !HR(IDirect3DDevice3_AddViewport(device, view), "FAIL attach viewport") ||
        !HR(IDirect3DViewport3_SetViewport2(view, &viewport), "FAIL viewport2") ||
        !HR(IDirect3DDevice3_SetCurrentViewport(device, view), "FAIL current viewport"))
        return;
    if (!HR(IDirect3DDevice3_SetRenderState(device, D3DRENDERSTATE_ZENABLE, FALSE), "FAIL depth") ||
        !HR(IDirect3DDevice3_SetRenderState(device, D3DRENDERSTATE_CULLMODE, D3DCULL_NONE),
            "FAIL culling") ||
        !HR(IDirect3DDevice3_SetRenderState(device, D3DRENDERSTATE_DITHERENABLE, FALSE),
            "FAIL dither") ||
        !HR(IDirect3DDevice3_SetTextureStageState(device, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1),
            "FAIL color operation") ||
        !HR(IDirect3DDevice3_SetTextureStageState(device, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE),
            "FAIL diffuse argument"))
        return;
    if (ProbeGlError)
        Number("GL error before draw", ProbeGlError());
    Log("STAGE clear and fixed triangle");
    if (!HR(IDirect3DViewport3_Clear2(view, 1, &clear_rect, D3DCLEAR_TARGET, 0xff102030, 1, 0),
            "FAIL clear") ||
        !HR(IDirect3DDevice3_BeginScene(device), "FAIL begin scene") ||
        !HR(IDirect3DDevice3_DrawPrimitive(device, D3DPT_TRIANGLELIST,
                                           D3DFVF_XYZRHW | D3DFVF_DIFFUSE, vertices, 3, 0),
            "FAIL draw") ||
        !HR(IDirect3DDevice3_EndScene(device), "FAIL end scene"))
        return;
    if (ProbeGlError)
        Number("GL error after draw", ProbeGlError());
    locked.dwSize = sizeof(locked);
    Log("STAGE real render-target readback");
    if (!HR(IDirectDrawSurface4_Lock(target, NULL, &locked, DDLOCK_READONLY | DDLOCK_WAIT, NULL),
            "FAIL readback lock"))
        return;
    if (Check(locked.lpSurface && locked.lPitch >= 1280, "FAIL readback layout", locked.lPitch)) {
        for (region = 0; region < 2 && !failed; region++)
            for (y = 0; y < 16 && !failed; y++)
                for (x = 0; x < 16; x++) {
                    UINT xx = x + (region ? 152 : 0), yy = y + (region ? 92 : 0);
                    DWORD expected = region ? 0xff0000 : 0x102030;
                    DWORD actual =
                        *(DWORD *)((BYTE *)locked.lpSurface + yy * locked.lPitch + xx * 4) &
                        0xffffff;
                    if (!Check(actual == expected, "FAIL exact D3D6 pixel", actual)) {
                        Number("pixel x", xx);
                        Number("pixel y", yy);
                        Number("expected", expected);
                        break;
                    }
                }
    }
    if (!HR(IDirectDrawSurface4_Unlock(target, NULL), "FAIL unlock") || failed)
        return;
    if (ProbeGlError)
        Number("GL error before Blt", ProbeGlError());
    Log("STAGE clipped window presentation");
    if (!Check(ClientToScreen(window, &origin), "FAIL window origin", GetLastError()))
        return;
    OffsetRect(&client, origin.x, origin.y);
    if (!HR(IDirectDrawSurface4_Blt(primary, &client, target, NULL, DDBLT_WAIT, NULL),
            "FAIL present blit"))
        return;
    if (ProbeGlError)
        Number("GL error after Blt", ProbeGlError());
    {
        HDC dc = GetDC(window);
        if (!Check(dc != NULL, "FAIL presented window DC", GetLastError()))
            return;
        Log("STAGE actual clipped GPU front-buffer GDI readback");
        for (region = 0; region < 2 && !failed; region++)
            for (y = 0; y < 16 && !failed; y++)
                for (x = 0; x < 16; x++) {
                    UINT xx = x + (region ? 152 : 0), yy = y + (region ? 92 : 0);
                    COLORREF expected = region ? RGB(255, 0, 0) : RGB(16, 32, 48);
                    COLORREF actual = GetPixel(dc, xx, yy);
                    if (!Check(actual == expected, "FAIL exact presented D3D6 pixel", actual)) {
                        Number("pixel x", xx);
                        Number("pixel y", yy);
                        Number("expected", expected);
                        {
                            typedef HDC(WINAPI * CurrentDC)(void);
                            typedef void(WINAPI * ReadBuffer)(unsigned int);
                            typedef void(WINAPI * ReadPixels)(int, int, int, int, unsigned int,
                                                              unsigned int, void *);
                            typedef unsigned int(WINAPI * GetError)(void);
                            CurrentDC current = Entry<CurrentDC>(gl, "wglGetCurrentDC");
                            ReadBuffer buffer = Entry<ReadBuffer>(gl, "glReadBuffer");
                            ReadPixels pixels = Entry<ReadPixels>(gl, "glReadPixels");
                            GetError error = Entry<GetError>(gl, "glGetError");
                            HWND fg = GetForegroundWindow(),
                                 active = current ? WindowFromDC(current()) : NULL;
                            POINT point = {(int)xx, (int)yy};
                            RECT bounds;
                            DWORD pid = 0;
                            BYTE rgba[4] = {};
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
extern "C" void WINAPI WinMainCRTStartup(void) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    LogFile =
        CreateFileA(DG_LOG_PATH, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    if (LogFile == INVALID_HANDLE_VALUE)
        ExitProcess(2);
    Run();
    if (device && view)
        IDirect3DDevice3_DeleteViewport(device, view);
    if (view)
        IDirect3DViewport3_Release(view);
    if (device)
        IDirect3DDevice3_Release(device);
    if (target)
        IDirectDrawSurface4_Release(target);
    if (primary)
        IDirectDrawSurface4_Release(primary);
    if (clipper)
        IDirectDrawClipper_Release(clipper);
    if (d3d)
        IDirect3D3_Release(d3d);
    if (draw)
        IDirectDraw4_Release(draw);
    if (window)
        DestroyWindow(window);
    if (!failed)
        Log("PASS automated " DG_PROBE_NAME
            ": D3D6 HAL, 1024 exact target/front pixels, viewport3 and "
            "independent release");
    CloseHandle(LogFile);
    ExitProcess(failed ? 1 : 0);
}
