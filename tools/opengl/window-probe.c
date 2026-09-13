/* SPDX-License-Identifier: GPL-2.0-or-later
 * Fixed public WGL/GDI window-coherence acceptance. No menu or screenshot loop.
 * Every visible result is read through the guest desktop GetPixel contract.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#define FN(result, name, args) static result(WINAPI *p##name) args
FN(HGLRC, wglCreateContext, (HDC));
FN(BOOL, wglDeleteContext, (HGLRC));
FN(BOOL, wglMakeCurrent, (HDC, HGLRC));
FN(BOOL, wglSwapBuffers, (HDC));
FN(int, wglChoosePixelFormat, (HDC, const PIXELFORMATDESCRIPTOR *));
FN(BOOL, wglSetPixelFormat, (HDC, int, const PIXELFORMATDESCRIPTOR *));
FN(void, glClearColor, (GLfloat, GLfloat, GLfloat, GLfloat));
FN(void, glClear, (GLbitfield));
FN(void, glViewport, (GLint, GLint, GLsizei, GLsizei));
FN(GLenum, glGetError, (void));
static HANDLE LogFile = INVALID_HANDLE_VALUE;
static HMODULE Library;
static HWND Backdrop, GpuWindow, Cover;
static HDC GpuDC, ScreenDC;
static HGLRC Context;
static HBRUSH BackgroundBrush, CoverBrush;
static BOOL Failed;
static DWORD CheckedPixels;
static void Log(const char *text) {
    DWORD written;
    if (LogFile == INVALID_HANDLE_VALUE)
        return;
    WriteFile(LogFile, text, lstrlenA(text), &written, NULL);
    WriteFile(LogFile, "\r\n", 2, &written, NULL);
    FlushFileBuffers(LogFile);
}
static void Number(const char *name, DWORD value) {
    static const char hex[] = "0123456789abcdef";
    char line[] = "value=0x00000000";
    int i;
    Log(name);
    for (i = 0; i < 8; i++)
        line[8 + i] = hex[(value >> (28 - i * 4)) & 15];
    Log(line);
}
static BOOL Check(BOOL okay, const char *stage) {
    if (okay)
        return TRUE;
    Failed = TRUE;
    Number(stage, GetLastError());
    return FALSE;
}
static LRESULT CALLBACK GdiProc(HWND window, UINT message, WPARAM a, LPARAM b) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint;
        RECT rect;
        HDC dc = BeginPaint(window, &paint);
        GetClientRect(window, &rect);
        FillRect(dc, &rect, window == Cover ? CoverBrush : BackgroundBrush);
        EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_ERASEBKGND)
        return 1;
    return DefWindowProcA(window, message, a, b);
}
static LRESULT CALLBACK GpuProc(HWND window, UINT message, WPARAM a, LPARAM b) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint;
        BeginPaint(window, &paint);
        EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_ERASEBKGND)
        return 1;
    return DefWindowProcA(window, message, a, b);
}
static void PaintWindows(void) {
    MSG message;
    UINT count = 0;
    /* Drain currently pending messages once, bounded even under external input. */
    while (count++ < 128 && PeekMessageA(&message, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    if (Backdrop)
        UpdateWindow(Backdrop);
    if (Cover)
        UpdateWindow(Cover);
    if (GpuWindow)
        UpdateWindow(GpuWindow);
    GdiFlush();
}
static BOOL Pixels(const char *stage, int left, int top, COLORREF expected) {
    int x, y;
    Log(stage);
    for (y = top; y < top + 8; y++)
        for (x = left; x < left + 8; x++) {
            COLORREF actual = GetPixel(ScreenDC, x, y);
            if (actual != expected) {
                Failed = TRUE;
                Log("FAIL exact desktop pixel");
                Number("x", x);
                Number("y", y);
                Number("expected COLORREF", expected);
                Number("actual COLORREF", actual);
                return FALSE;
            }
            ++CheckedPixels;
        }
    return TRUE;
}
static BOOL Draw(float red, float green, float blue, int width, int height) {
    pglViewport(0, 0, width, height);
    pglClearColor(red, green, blue, 1);
    pglClear(GL_COLOR_BUFFER_BIT);
    if (!Check(pglGetError() == GL_NO_ERROR, "FAIL GL clear"))
        return FALSE;
    if (!Check(pwglSwapBuffers(GpuDC), "FAIL WGL swap"))
        return FALSE;
    return Check(pglGetError() == GL_NO_ERROR, "FAIL GL after swap");
}
static void Run(void) {
    HINSTANCE instance = GetModuleHandleA(NULL);
    WNDCLASSA cls = {0};
    PIXELFORMATDESCRIPTOR format = {0};
    RECT rect;
    Library = LoadLibraryA("C:\\SIERRA\\Half-Life\\dgpugl.dll");
    if (!Check(Library != NULL, "FAIL explicit DreamGPU frontend load"))
        return;
#define LOAD(name)                                                                                 \
    p##name = (void *)GetProcAddress(Library, #name);                                              \
    if (!Check(p##name != NULL, "FAIL export " #name))                                             \
    return
    LOAD(wglCreateContext);
    LOAD(wglDeleteContext);
    LOAD(wglMakeCurrent);
    LOAD(wglSwapBuffers);
    LOAD(wglChoosePixelFormat);
    LOAD(wglSetPixelFormat);
    LOAD(glClearColor);
    LOAD(glClear);
    LOAD(glViewport);
    LOAD(glGetError);
#undef LOAD
    BackgroundBrush = CreateSolidBrush(RGB(16, 32, 48));
    CoverBrush = CreateSolidBrush(RGB(0, 255, 0));
    if (!Check(BackgroundBrush && CoverBrush, "FAIL GDI brushes"))
        return;
    cls.hInstance = instance;
    cls.lpfnWndProc = GdiProc;
    cls.lpszClassName = "DreamGPUWindowGdi";
    if (!Check(RegisterClassA(&cls) != 0, "FAIL GDI class"))
        return;
    cls.lpfnWndProc = GpuProc;
    cls.style = CS_OWNDC;
    cls.lpszClassName = "DreamGPUWindowGpu";
    if (!Check(RegisterClassA(&cls) != 0, "FAIL GPU class"))
        return;
    Backdrop = CreateWindowExA(0, "DreamGPUWindowGdi", "DreamGPU stability backdrop", WS_POPUP, 32,
                               32, 640, 480, NULL, NULL, instance, NULL);
    GpuWindow = CreateWindowExA(0, "DreamGPUWindowGpu", "DreamGPU stability GPU", WS_POPUP, 64, 64,
                                320, 240, NULL, NULL, instance, NULL);
    Cover = CreateWindowExA(0, "DreamGPUWindowGdi", "DreamGPU stability occluder", WS_POPUP, 160,
                            112, 128, 96, NULL, NULL, instance, NULL);
    if (!Check(Backdrop && GpuWindow && Cover, "FAIL owned windows"))
        return;
    ShowWindow(Backdrop, SW_SHOWNOACTIVATE);
    ShowWindow(GpuWindow, SW_SHOWNOACTIVATE);
    PaintWindows();
    GpuDC = GetDC(GpuWindow);
    ScreenDC = GetDC(NULL);
    if (!Check(GpuDC && ScreenDC, "FAIL DC acquisition"))
        return;
    format.nSize = sizeof(format);
    format.nVersion = 1;
    format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    format.iPixelType = PFD_TYPE_RGBA;
    format.cColorBits = 32;
    format.cDepthBits = 24;
    format.cStencilBits = 8;
    if (!Check(pwglChoosePixelFormat(GpuDC, &format) == 1 && pwglSetPixelFormat(GpuDC, 1, &format),
               "FAIL DreamGPU pixel format"))
        return;
    Context = pwglCreateContext(GpuDC);
    if (!Check(Context != NULL && pwglMakeCurrent(GpuDC, Context), "FAIL current GPU context"))
        return;
    Log("STAGE visible GPU ownership and desktop read");
    if (!Draw(1, 0, 0, 320, 240) || !Pixels("red GPU before occlusion", 96, 96, RGB(255, 0, 0)))
        return;
    ShowWindow(Cover, SW_SHOWNOACTIVATE);
    SetWindowPos(Cover, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    PaintWindows();
    Log("STAGE GDI occluder survives GPU swap");
    if (!Draw(1, 0, 0, 320, 240) || !Pixels("unoccluded red", 96, 96, RGB(255, 0, 0)) ||
        !Pixels("green GDI occluder", 176, 128, RGB(0, 255, 0)))
        return;
    Log("STAGE fully hidden GPU drawing");
    ShowWindow(GpuWindow, SW_HIDE);
    PaintWindows();
    if (!Pixels("backdrop after hide", 96, 96, RGB(16, 32, 48)) || !Draw(0, 0, 1, 320, 240) ||
        !Pixels("hidden swap preserves backdrop", 96, 96, RGB(16, 32, 48)) ||
        !Pixels("hidden swap preserves occluder", 176, 128, RGB(0, 255, 0)))
        return;
    Log("STAGE show and present after hidden drawing");
    ShowWindow(GpuWindow, SW_SHOWNOACTIVATE);
    SetWindowPos(Cover, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    PaintWindows();
    if (!Draw(0, 0, 1, 320, 240) || !Pixels("shown blue", 96, 96, RGB(0, 0, 255)) ||
        !Pixels("shown occluder green", 176, 128, RGB(0, 255, 0)))
        return;
    Log("STAGE move resize and explicit rebind");
    if (!Check(pwglMakeCurrent(NULL, NULL), "FAIL release before resize") ||
        !Check(MoveWindow(GpuWindow, 400, 80, 160, 120, TRUE), "FAIL MoveWindow"))
        return;
    PaintWindows();
    if (!Check(GetClientRect(GpuWindow, &rect) && rect.right == 160 && rect.bottom == 120,
               "FAIL resized client") ||
        !Check(pwglMakeCurrent(GpuDC, Context), "FAIL rebind resized drawable") ||
        !Draw(1, 1, 0, 160, 120) || !Pixels("resized yellow", 416, 96, RGB(255, 255, 0)) ||
        !Pixels("old location repainted", 96, 96, RGB(16, 32, 48)))
        return;
    Log("STAGE context cleanup returns desktop ownership");
    if (!Check(pwglMakeCurrent(NULL, NULL) && pwglDeleteContext(Context), "FAIL context cleanup"))
        return;
    Context = NULL;
    ReleaseDC(GpuWindow, GpuDC);
    GpuDC = NULL;
    DestroyWindow(GpuWindow);
    GpuWindow = NULL;
    PaintWindows();
    if (!Pixels("destroyed GPU region repainted", 416, 96, RGB(16, 32, 48)) ||
        !Pixels("occluder after GPU cleanup", 176, 128, RGB(0, 255, 0)))
        return;
    Number("verified desktop pixels", CheckedPixels);
}
void WINAPI WinMainCRTStartup(void) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    LogFile =
        CreateFileA("C:\\DGWIN.LOG", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    if (LogFile == INVALID_HANDLE_VALUE)
        ExitProcess(2);
    Run();
    if (Context) {
        pwglMakeCurrent(NULL, NULL);
        pwglDeleteContext(Context);
    }
    if (GpuDC)
        ReleaseDC(GpuWindow, GpuDC);
    if (ScreenDC)
        ReleaseDC(NULL, ScreenDC);
    if (Cover)
        DestroyWindow(Cover);
    if (GpuWindow)
        DestroyWindow(GpuWindow);
    if (Backdrop)
        DestroyWindow(Backdrop);
    if (BackgroundBrush)
        DeleteObject(BackgroundBrush);
    if (CoverBrush)
        DeleteObject(CoverBrush);
    if (!Failed)
        Log("PASS automated windows: 768 exact desktop pixels, GDI occlusion, hidden drawing, "
            "show, resize/rebind and cleanup");
    if (Library)
        FreeLibrary(Library);
    CloseHandle(LogFile);
    ExitProcess(Failed ? 1 : 0);
}
