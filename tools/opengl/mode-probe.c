/* SPDX-License-Identifier: GPL-2.0-or-later
 * Fixed display-mode transition with public WGL recreation and desktop reads.
 * Every ordinary failure path restores the entry display mode.
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
static HWND Window;
static HDC DC;
static HGLRC Context;
static DEVMODEA Original;
static BOOL Failed, RestoreNeeded;
static DWORD Verified;
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
static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM a, LPARAM b) {
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
static void Pump(void) {
    MSG message;
    UINT count = 0;
    while (count++ < 128 && PeekMessageA(&message, NULL, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    GdiFlush();
}
static BOOL CloseGpu(void) {
    BOOL okay = TRUE;
    if (Context) {
        if (!pwglMakeCurrent(NULL, NULL) || !pwglDeleteContext(Context))
            okay = FALSE;
        Context = NULL;
    }
    if (DC) {
        ReleaseDC(Window, DC);
        DC = NULL;
    }
    if (Window) {
        DestroyWindow(Window);
        Window = NULL;
    }
    Pump();
    return Check(okay, "FAIL GPU teardown before mode transition");
}
static BOOL CurrentMode(DWORD width, DWORD height, DWORD depth) {
    DEVMODEA mode = {0};
    mode.dmSize = sizeof(mode);
    if (!Check(EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &mode), "FAIL current mode query"))
        return FALSE;
    Number("current mode width", mode.dmPelsWidth);
    Number("current mode height", mode.dmPelsHeight);
    Number("current mode bits", mode.dmBitsPerPel);
    return Check(mode.dmPelsWidth == width && mode.dmPelsHeight == height &&
                     mode.dmBitsPerPel == depth,
                 "FAIL current mode mismatch");
}
static BOOL Restore(void) {
    LONG result;
    if (!RestoreNeeded)
        return TRUE;
    Log("STAGE restore entry display mode");
    result = ChangeDisplaySettingsA(&Original, 0);
    Number("restore result", (DWORD)result);
    Pump();
    if (!Check(result == DISP_CHANGE_SUCCESSFUL, "FAIL restore display mode"))
        return FALSE;
    if (!CurrentMode(Original.dmPelsWidth, Original.dmPelsHeight, Original.dmBitsPerPel))
        return FALSE;
    RestoreNeeded = FALSE;
    return TRUE;
}
static BOOL Frame(float red, float green, float blue, COLORREF expected) {
    PIXELFORMATDESCRIPTOR format = {0};
    HDC screen;
    UINT x, y;
    Window = CreateWindowExA(0, "DreamGPUModeGpu", "DreamGPU mode GPU", WS_POPUP | WS_VISIBLE, 64,
                             64, 160, 120, NULL, NULL, GetModuleHandleA(NULL), NULL);
    if (!Check(Window != NULL, "FAIL GPU window creation"))
        return FALSE;
    Pump();
    DC = GetDC(Window);
    if (!Check(DC != NULL, "FAIL GPU DC"))
        return FALSE;
    format.nSize = sizeof(format);
    format.nVersion = 1;
    format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    format.iPixelType = PFD_TYPE_RGBA;
    format.cColorBits = 32;
    format.cDepthBits = 24;
    format.cStencilBits = 8;
    if (!Check(pwglChoosePixelFormat(DC, &format) == 1 && pwglSetPixelFormat(DC, 1, &format),
               "FAIL pixel format"))
        return FALSE;
    Context = pwglCreateContext(DC);
    if (!Check(Context != NULL && pwglMakeCurrent(DC, Context), "FAIL recreated GL context"))
        return FALSE;
    pglViewport(0, 0, 160, 120);
    pglClearColor(red, green, blue, 1);
    pglClear(GL_COLOR_BUFFER_BIT);
    if (!Check(pglGetError() == GL_NO_ERROR && pwglSwapBuffers(DC), "FAIL recreated GPU draw"))
        return FALSE;
    screen = GetDC(NULL);
    if (!Check(screen != NULL, "FAIL desktop DC"))
        return FALSE;
    for (y = 80; y < 88; y++)
        for (x = 80; x < 88; x++) {
            COLORREF actual = GetPixel(screen, x, y);
            if (actual != expected) {
                Failed = TRUE;
                Number("FAIL exact desktop pixel", actual);
                Number("expected", expected);
                ReleaseDC(NULL, screen);
                return FALSE;
            }
            ++Verified;
        }
    ReleaseDC(NULL, screen);
    return CloseGpu();
}
static void Run(void) {
    DEVMODEA alternate = {0};
    WNDCLASSA cls = {0};
    LONG result;
    BOOL found = FALSE;
    DWORD index;
    Original.dmSize = sizeof(Original);
    if (!Check(EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &Original),
               "FAIL save original mode"))
        return;
    if (!Check(Original.dmBitsPerPel == 32, "FAIL NT GPU fixture requires32-bit entry mode"))
        return;
    /* Pick one supported alternate size at the same depth; do not guess a
     * resolution or request an unadvertised depth. No registry persistence. */
    for (index = 0; index < 256; index++) {
        ZeroMemory(&alternate, sizeof(alternate));
        alternate.dmSize = sizeof(alternate);
        if (!EnumDisplaySettingsA(NULL, index, &alternate))
            break;
        if (alternate.dmBitsPerPel == 32 && alternate.dmPelsWidth >= 640 &&
            alternate.dmPelsHeight >= 480 && alternate.dmPelsWidth <= 1024 &&
            alternate.dmPelsHeight <= 768 &&
            (alternate.dmPelsWidth != Original.dmPelsWidth ||
             alternate.dmPelsHeight != Original.dmPelsHeight)) {
            if (ChangeDisplaySettingsA(&alternate, CDS_TEST) == DISP_CHANGE_SUCCESSFUL) {
                found = TRUE;
                break;
            }
        }
    }
    if (!Check(found, "FAIL no supported alternate32-bit mode"))
        return;
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
    cls.hInstance = GetModuleHandleA(NULL);
    cls.lpfnWndProc = WindowProc;
    cls.style = CS_OWNDC;
    cls.lpszClassName = "DreamGPUModeGpu";
    if (!Check(RegisterClassA(&cls) != 0, "FAIL GPU class"))
        return;
    Log("STAGE entry-mode GPU and CPU readback");
    if (!Frame(0, 1, 1, RGB(0, 255, 255)))
        return;
    RestoreNeeded = TRUE;
    Log("STAGE switch to enumerated supported mode");
    result = ChangeDisplaySettingsA(&alternate, 0);
    Number("switch result", (DWORD)result);
    Pump();
    if (!Check(result == DISP_CHANGE_SUCCESSFUL, "FAIL mode switch") ||
        !CurrentMode(alternate.dmPelsWidth, alternate.dmPelsHeight, 32))
        return;
    Log("STAGE alternate-mode GPU recreation and readback");
    if (!Frame(1, 1, 0, RGB(255, 255, 0)) || !Restore())
        return;
    Log("STAGE restored-mode GPU recreation and readback");
    if (!Frame(1, 0, 1, RGB(255, 0, 255)))
        return;
    Number("verified desktop pixels", Verified);
}
void WINAPI WinMainCRTStartup(void) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    LogFile =
        CreateFileA("C:\\DGMODE.LOG", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    if (LogFile == INVALID_HANDLE_VALUE)
        ExitProcess(2);
    Run();
    if (Context || DC || Window)
        CloseGpu();
    Restore();
    if (!Failed)
        Log("PASS automated modes: supported32-bit mode switch/restore, three fresh GPU windows "
            "and192 exact desktop pixels");
    if (Library)
        FreeLibrary(Library);
    CloseHandle(LogFile);
    ExitProcess(Failed ? 1 : 0);
}
