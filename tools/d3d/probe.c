/* SPDX-License-Identifier: GPL-2.0-or-later
 * Deterministic public D3D8/9 acceptance through source-built Wine9x.
 * HAL rendering only. Fixed pretransformed vertices avoid shader requirements.
 * Compile once with DG_D3D_VERSION=8 and once with =9. No CRT dependency.
 */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#if DG_D3D_VERSION == 8
#include <d3d8.h>
typedef IDirect3D8 D3D;
typedef IDirect3DDevice8 Device;
typedef IDirect3DSurface8 Surface;
#define D3D_CALL(name, ...) IDirect3D8_##name(__VA_ARGS__)
#define DEVICE(name, ...) IDirect3DDevice8_##name(__VA_ARGS__)
#define SURFACE(name, ...) IDirect3DSurface8_##name(__VA_ARGS__)
#define INTERFACE_DLL "C:\\SIERRA\\Half-Life\\wined8.dll"
#define CREATE_NAME "Direct3DCreate8"
#define LOG_PATH "C:\\DGD3D8.LOG"
#define PASS_LINE                                                                                  \
    "PASS automated d3d8: Wine HAL triangle, 512 exact GPU pixels, present and clean release"
#else
#include <d3d9.h>
typedef IDirect3D9 D3D;
typedef IDirect3DDevice9 Device;
typedef IDirect3DSurface9 Surface;
#define D3D_CALL(name, ...) IDirect3D9_##name(__VA_ARGS__)
#define DEVICE(name, ...) IDirect3DDevice9_##name(__VA_ARGS__)
#define SURFACE(name, ...) IDirect3DSurface9_##name(__VA_ARGS__)
#define INTERFACE_DLL "C:\\SIERRA\\Half-Life\\wined9.dll"
#define CREATE_NAME "Direct3DCreate9"
#define LOG_PATH "C:\\DGD3D9.LOG"
#define PASS_LINE                                                                                  \
    "PASS automated d3d9: Wine HAL triangle, 512 exact GPU pixels, present and clean release"
#endif
#define GL_PATH "C:\\SIERRA\\Half-Life\\dgpugl.dll"
static HANDLE LogFile = INVALID_HANDLE_VALUE;
static Device *device;
static D3D *d3d;
static Surface *surface;
static HWND window;
static BOOL failed;
static HMODULE gl_module;

void *memset(void *destination, int value, unsigned int count) {
    volatile BYTE *out = destination;
    while (count--)
        *out++ = (BYTE)value;
    return destination;
}
static void Log(const char *text) {
    DWORD written;
    if (LogFile == INVALID_HANDLE_VALUE)
        return;
    WriteFile(LogFile, text, (DWORD)lstrlenA(text), &written, NULL);
    WriteFile(LogFile, "\r\n", 2, &written, NULL);
    FlushFileBuffers(LogFile);
}
static void Number(const char *stage, DWORD value) {
    static const char hex[] = "0123456789abcdef";
    char message[] = "value=0x00000000";
    int i;
    Log(stage);
    for (i = 0; i < 8; ++i)
        message[8 + i] = hex[(value >> (28 - i * 4)) & 15];
    Log(message);
}
static void GLState(const char *stage) {
    typedef DWORD(WINAPI * GetError)(void);
    typedef void(WINAPI * GetInteger)(DWORD, int *);
    typedef HDC(WINAPI * CurrentDC)(void);
    union {
        FARPROC generic;
        GetError error;
        GetInteger integer;
        CurrentDC dc;
    } call;
    GetError error;
    GetInteger integer;
    HDC dc;
    RECT rect;
    int value[4] = {0};
    UINT i;
    static const DWORD queries[] = {0x0c01, 0x0c02, 0x0ba2, 0x0c10, 0x0c23};
    if (!gl_module)
        return;
    Log(stage);
    call.generic = GetProcAddress(gl_module, "glGetError");
    error = call.error;
    call.generic = GetProcAddress(gl_module, "glGetIntegerv");
    integer = call.integer;
    call.generic = GetProcAddress(gl_module, "wglGetCurrentDC");
    if (!error || !integer || !call.dc)
        return;
    dc = call.dc();
    Number("current GL DC", (DWORD)(ULONG_PTR)dc);
    if (dc && GetClientRect(WindowFromDC(dc), &rect)) {
        Number("current drawable width", rect.right);
        Number("current drawable height", rect.bottom);
    }
    Number("pending GL error", error());
    if (!dc)
        return;
    for (i = 0; i < sizeof(queries) / sizeof(queries[0]); i++) {
        ZeroMemory(value, sizeof(value));
        integer(queries[i], value);
        Number("GL query", queries[i]);
        Number("value0", value[0]);
        if (queries[i] == 0x0ba2 || queries[i] == 0x0c10 || queries[i] == 0x0c23) {
            Number("value1", value[1]);
            Number("value2", value[2]);
            Number("value3", value[3]);
        }
    }
    Number("GL diagnostic query error", error());
}
static BOOL Check(BOOL condition, const char *stage, DWORD value) {
    static const char hex[] = "0123456789abcdef";
    char message[32] = "value=0x00000000";
    int i;
    if (condition)
        return TRUE;
    failed = TRUE;
    Log(stage);
    for (i = 0; i < 8; ++i)
        message[8 + i] = hex[(value >> (28 - i * 4)) & 15];
    Log(message);
    return FALSE;
}
static BOOL HR(HRESULT result, const char *stage) {
    return Check(SUCCEEDED(result), stage, (DWORD)result);
}
static LRESULT CALLBACK WindowProc(HWND w, UINT message, WPARAM key, LPARAM detail) {
    return DefWindowProcA(w, message, key, detail);
}
static BOOL Pixels(const D3DLOCKED_RECT *locked) {
    UINT region, x, y;
    if (!locked->pBits || locked->Pitch < 320 * 4) {
        Check(FALSE, "FAIL readback layout", (DWORD)locked->Pitch);
        return FALSE;
    }
    for (region = 0; region < 2; ++region) {
        UINT left = region ? 152 : 0, top = region ? 92 : 0;
        DWORD expected = region ? 0x00ff0000 : 0x00102030;
        for (y = top; y < top + 16; ++y)
            for (x = left; x < left + 16; ++x) {
                DWORD actual =
                    *(DWORD *)((BYTE *)locked->pBits + y * locked->Pitch + x * 4) & 0x00ffffff;
                if (!Check(actual == expected, "FAIL exact D3D pixel", actual)) {
                    Number("pixel x", x);
                    Number("pixel y", y);
                    Number("pixel expected", expected);
                    Number("surface pitch", locked->Pitch);
                    {
                        UINT yy, xx, count = 0;
                        for (yy = 0; yy < 240; yy++)
                            for (xx = 0; xx < 320; xx++)
                                if (*(DWORD *)((BYTE *)locked->pBits + yy * locked->Pitch +
                                               xx * 4) &
                                    0xffffff)
                                    count++;
                        Number("nonblack backbuffer pixels", count);
                        Number("center pixel",
                               *(DWORD *)((BYTE *)locked->pBits + 120 * locked->Pitch + 160 * 4));
                    }
                    return FALSE;
                }
            }
    }
    return TRUE;
}
static void Run(void) {
    typedef D3D *(WINAPI * CreateD3D)(UINT);
    HMODULE gl, runtime;
    char loaded[MAX_PATH];
    CreateD3D create;
    union {
        FARPROC generic;
        CreateD3D factory;
    } entry;
    WNDCLASSA klass = {0};
    D3DPRESENT_PARAMETERS pp = {0};
    D3DLOCKED_RECT locked;
    RECT bounds = {0, 0, 320, 240}, client;
    struct Vertex {
        float x, y, z, rhw;
        DWORD color;
    } vertices[3] = {{80, 60, .5f, 1, 0xffff0000},
                     {240, 60, .5f, 1, 0xffff0000},
                     {160, 180, .5f, 1, 0xffff0000}};
    Log("STAGE explicit application-local DreamGPU OpenGL loader");
    gl = LoadLibraryA(GL_PATH);
    gl_module = gl;
    if (!Check(gl != NULL, "FAIL load application-local dgpugl.dll", GetLastError()))
        return;
    loaded[0] = 0;
    GetModuleFileNameA(gl, loaded, sizeof(loaded));
    Log(loaded);
    if (!Check(lstrcmpiA(loaded, GL_PATH) == 0, "FAIL system OpenGL fallback is forbidden", 0))
        return;
    Log("STAGE source-built Wine D3D interface");
    runtime = LoadLibraryA(INTERFACE_DLL);
    if (!Check(runtime != NULL, "FAIL load Wine interface", GetLastError()))
        return;
    if (!Check(GetModuleHandleA("dgpugl.dll") == gl, "FAIL Wine loaded another OpenGL module", 0))
        return;
    entry.generic = GetProcAddress(runtime, CREATE_NAME);
    create = entry.factory;
    if (!create) {
        Check(FALSE, "FAIL Direct3D factory export", GetLastError());
        return;
    }
    Log("STAGE create D3D object");
    d3d = create(D3D_SDK_VERSION);
    if (!Check(d3d != NULL, "FAIL create D3D object", GetLastError()))
        return;
    klass.lpfnWndProc = WindowProc;
    klass.hInstance = GetModuleHandleA(NULL);
    klass.lpszClassName = "DreamGPUD3DProbe";
    if (!Check(RegisterClassA(&klass) != 0, "FAIL window registration", GetLastError()))
        return;
    if (!Check(AdjustWindowRect(&bounds, WS_OVERLAPPEDWINDOW, FALSE), "FAIL window bounds",
               GetLastError()))
        return;
    window = CreateWindowA(klass.lpszClassName, "DreamGPU D3D probe",
                           WS_OVERLAPPEDWINDOW | WS_VISIBLE, 64, 64, bounds.right - bounds.left,
                           bounds.bottom - bounds.top, NULL, NULL, klass.hInstance, NULL);
    if (!Check(window != NULL, "FAIL window creation", GetLastError()))
        return;
    if (!Check(GetClientRect(window, &client) && client.right == 320 && client.bottom == 240,
               "FAIL exact drawable size", GetLastError()))
        return;
    pp.BackBufferWidth = 320;
    pp.BackBufferHeight = 240;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = window;
    pp.Windowed = TRUE;
    pp.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
#if DG_D3D_VERSION == 9
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
#endif
    Log("STAGE create HAL device");
    if (!HR(D3D_CALL(CreateDevice, d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, window,
                     D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device),
            "FAIL HAL device creation"))
        return;
    if (!HR(DEVICE(SetRenderState, device, D3DRS_LIGHTING, FALSE), "FAIL disable lighting") ||
        !HR(DEVICE(SetRenderState, device, D3DRS_ZENABLE, FALSE), "FAIL disable depth") ||
        !HR(DEVICE(SetRenderState, device, D3DRS_CULLMODE, D3DCULL_NONE), "FAIL disable culling") ||
        !HR(DEVICE(SetRenderState, device, D3DRS_DITHERENABLE, FALSE), "FAIL disable dither") ||
        !HR(DEVICE(SetTextureStageState, device, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1),
            "FAIL diffuse operation") ||
        !HR(DEVICE(SetTextureStageState, device, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE),
            "FAIL diffuse argument"))
        return;
#if DG_D3D_VERSION == 8
    if (!HR(DEVICE(SetVertexShader, device, D3DFVF_XYZRHW | D3DFVF_DIFFUSE),
            "FAIL fixed vertex format"))
        return;
#else
    if (!HR(DEVICE(SetFVF, device, D3DFVF_XYZRHW | D3DFVF_DIFFUSE), "FAIL fixed vertex format"))
        return;
#endif
    GLState("GL diagnostic before draw");
    Log("STAGE clear and fixed pretransformed triangle");
    if (!HR(DEVICE(Clear, device, 0, NULL, D3DCLEAR_TARGET, 0xff102030, 1, 0), "FAIL clear") ||
        !HR(DEVICE(BeginScene, device), "FAIL begin scene") ||
        !HR(DEVICE(DrawPrimitiveUP, device, D3DPT_TRIANGLELIST, 1, vertices, sizeof(vertices[0])),
            "FAIL draw triangle") ||
        !HR(DEVICE(EndScene, device), "FAIL end scene"))
        return;
#if DG_D3D_VERSION == 8
    if (!HR(DEVICE(GetBackBuffer, device, 0, D3DBACKBUFFER_TYPE_MONO, &surface),
            "FAIL acquire backbuffer"))
        return;
#else
    if (!HR(DEVICE(GetBackBuffer, device, 0, 0, D3DBACKBUFFER_TYPE_MONO, &surface),
            "FAIL acquire backbuffer"))
        return;
#endif
    GLState("GL diagnostic after draw");
    Log("STAGE real backbuffer readback");
    if (!HR(SURFACE(LockRect, surface, &locked, NULL, D3DLOCK_READONLY), "FAIL lock backbuffer"))
        return;
    GLState("GL diagnostic after LockRect");
    Pixels(&locked);
    if (!HR(SURFACE(UnlockRect, surface), "FAIL unlock backbuffer"))
        return;
    if (failed)
        return;
    Log("STAGE present");
    HR(DEVICE(Present, device, NULL, NULL, NULL, NULL), "FAIL present");
}
void WINAPI WinMainCRTStartup(void) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    LogFile = CreateFileA(LOG_PATH, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                          FILE_ATTRIBUTE_NORMAL, NULL);
    if (LogFile == INVALID_HANDLE_VALUE)
        ExitProcess(2);
    Run();
    if (surface)
        SURFACE(Release, surface);
    if (device)
        DEVICE(Release, device);
    if (d3d)
        D3D_CALL(Release, d3d);
    if (window)
        DestroyWindow(window);
    if (!failed)
        Log(PASS_LINE);
    CloseHandle(LogFile);
    ExitProcess(failed ? 1 : 0);
}
