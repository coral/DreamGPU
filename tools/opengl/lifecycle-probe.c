/* SPDX-License-Identifier: GPL-2.0-or-later
 * Retained GPU window across host VM/shader switches, then forced process exit.
 * The parent owns the child handle; no arbitrary PID, path or window is killed.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#define FN(result, name, args) static result(WINAPI *p##name) args
FN(HGLRC, wglCreateContext, (HDC));
FN(BOOL, wglMakeCurrent, (HDC, HGLRC));
FN(BOOL, wglSwapBuffers, (HDC));
FN(int, wglChoosePixelFormat, (HDC, const PIXELFORMATDESCRIPTOR *));
FN(BOOL, wglSetPixelFormat, (HDC, int, const PIXELFORMATDESCRIPTOR *));
FN(void, glClearColor, (GLfloat, GLfloat, GLfloat, GLfloat));
FN(void, glClear, (GLbitfield));
FN(GLenum, glGetError, (void));
static HANDLE LogFile = INVALID_HANDLE_VALUE;
static HBRUSH Brush;
static DWORD PixelsChecked;
static void Log(const char *s) {
    DWORD written;
    if (LogFile == INVALID_HANDLE_VALUE)
        return;
    WriteFile(LogFile, s, lstrlenA(s), &written, NULL);
    WriteFile(LogFile, "\r\n", 2, &written, NULL);
    FlushFileBuffers(LogFile);
}
static LRESULT CALLBACK WindowProc(HWND w, UINT m, WPARAM a, LPARAM b) {
    if (m == WM_PAINT) {
        PAINTSTRUCT p;
        RECT r;
        HDC dc = BeginPaint(w, &p);
        if (Brush) {
            GetClientRect(w, &r);
            FillRect(dc, &r, Brush);
        }
        EndPaint(w, &p);
        return 0;
    }
    if (m == WM_ERASEBKGND)
        return 1;
    return DefWindowProcA(w, m, a, b);
}
static DWORD PumpWait(HANDLE event, DWORD timeout) {
    DWORD start = GetTickCount(), elapsed = 0;
    while (elapsed < timeout) {
        DWORD result = MsgWaitForMultipleObjects(1, &event, FALSE, timeout - elapsed, QS_ALLINPUT);
        if (result != WAIT_OBJECT_0 + 1)
            return result;
        for (unsigned i = 0; i < 128; ++i) {
            MSG m;
            if (!PeekMessageA(&m, NULL, 0, 0, PM_REMOVE))
                break;
            TranslateMessage(&m);
            DispatchMessageA(&m);
        }
        elapsed = GetTickCount() - start;
    }
    return WAIT_TIMEOUT;
}
static HWND Window(BOOL child) {
    WNDCLASSA c = {0};
    c.hInstance = GetModuleHandleA(NULL);
    c.lpfnWndProc = WindowProc;
    c.style = CS_OWNDC;
    c.lpszClassName = child ? "DreamGPULifecycleGPU" : "DreamGPULifecycleBackdrop";
    if (!RegisterClassA(&c))
        return NULL;
    HWND w = CreateWindowExA(0, c.lpszClassName, c.lpszClassName, WS_POPUP, child ? 64 : 32,
                             child ? 64 : 32, child ? 320 : 640, child ? 240 : 480, NULL, NULL,
                             c.hInstance, NULL);
    if (w) {
        ShowWindow(w, SW_SHOW);
        UpdateWindow(w);
    }
    return w;
}
static BOOL Pixels(COLORREF expected) {
    HDC dc = GetDC(NULL);
    BOOL okay = dc != NULL;
    if (dc) {
        for (int y = 96; y < 112; ++y)
            for (int x = 96; x < 112; ++x) {
                if (GetPixel(dc, x, y) != expected)
                    okay = FALSE;
                else
                    ++PixelsChecked;
            }
        ReleaseDC(NULL, dc);
    }
    return okay;
}
static BOOL SignalPhase(const char *name) {
    HANDLE event = OpenEventA(EVENT_MODIFY_STATE, FALSE, name);
    if (!event)
        return FALSE;
    BOOL okay = SetEvent(event);
    CloseHandle(event);
    return okay;
}
static DWORD Child(void) {
    HMODULE gl = LoadLibraryA("C:\\SIERRA\\Half-Life\\dgpugl.dll");
    HWND window;
    HDC dc;
    HGLRC context;
    PIXELFORMATDESCRIPTOR pf = {0};
    HANDLE ready;
    if (!gl)
        return 2;
#define LOAD(name)                                                                                 \
    p##name = (void *)GetProcAddress(gl, #name);                                                   \
    if (!p##name)                                                                                  \
    return 3
    LOAD(wglCreateContext);
    LOAD(wglMakeCurrent);
    LOAD(wglSwapBuffers);
    LOAD(wglChoosePixelFormat);
    LOAD(wglSetPixelFormat);
    LOAD(glClearColor);
    LOAD(glClear);
    LOAD(glGetError);
#undef LOAD
    window = Window(TRUE);
    if (!window)
        return 4;
    dc = GetDC(window);
    if (!dc)
        return 5;
    pf.nSize = sizeof(pf);
    pf.nVersion = 1;
    pf.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pf.iPixelType = PFD_TYPE_RGBA;
    pf.cColorBits = 32;
    pf.cDepthBits = 24;
    pf.cStencilBits = 8;
    if (pwglChoosePixelFormat(dc, &pf) != 1 || !pwglSetPixelFormat(dc, 1, &pf))
        return 6;
    context = pwglCreateContext(dc);
    if (!context || !pwglMakeCurrent(dc, context))
        return 7;
    /* Three distinct exports, then no guest redraw: reentry must retain red. */
    for (unsigned i = 0; i < 3; ++i) {
        pglClearColor(i == 1 ? 0 : 1, 0, i == 1 ? 1 : 0, 1);
        pglClear(GL_COLOR_BUFFER_BIT);
        if (!pwglSwapBuffers(dc) || pglGetError() != GL_NO_ERROR)
            return 8;
    }
    ready = OpenEventA(EVENT_MODIFY_STATE, FALSE, "DreamGPULifecycleReady");
    if (!ready || !SetEvent(ready))
        return 9;
    CloseHandle(ready);
    HANDLE stop = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!stop)
        return 10;
    /* A safety deadline, not a render loop. Parent normally terminates us. */
    PumpWait(stop, 20000);
    CloseHandle(stop);
    return 11;
}
static BOOL Parent(void) {
    char path[MAX_PATH], command[MAX_PATH + 16], line[96];
    STARTUPINFOA startup = {0};
    PROCESS_INFORMATION process = {0};
    HANDLE ready = NULL, captured = NULL;
    HWND backdrop = NULL;
    BOOL okay = FALSE;
    ready = CreateEventA(NULL, TRUE, FALSE, "DreamGPULifecycleReady");
    captured = OpenEventA(SYNCHRONIZE, FALSE, "DreamGPUGameCaptureDone");
    if (!ready || !captured) {
        Log("FAIL lifecycle runner events");
        goto done;
    }
    ResetEvent(ready);
    Brush = CreateSolidBrush(RGB(16, 32, 48));
    backdrop = Window(FALSE);
    if (!Brush || !backdrop || !Pixels(RGB(16, 32, 48))) {
        Log("FAIL lifecycle initial desktop");
        goto done;
    }
    DWORD path_bytes = GetModuleFileNameA(NULL, path, sizeof(path));
    if (!path_bytes || path_bytes >= sizeof(path)) {
        Log("FAIL lifecycle own path");
        goto done;
    }
    lstrcpyA(command, "\"");
    lstrcatA(command, path);
    lstrcatA(command, "\" -child");
    startup.cb = sizeof(startup);
    if (!CreateProcessA(path, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process)) {
        Log("FAIL lifecycle owned child launch");
        goto done;
    }
    CloseHandle(process.hThread);
    process.hThread = NULL;
    DWORD ready_result = PumpWait(ready, 5000);
    if (ready_result != WAIT_OBJECT_0 || !Pixels(RGB(255, 0, 0))) {
        DWORD child_exit = STILL_ACTIVE;
        GetExitCodeProcess(process.hProcess, &child_exit);
        wsprintfA(line, "LIFECYCLE_CHILD wait=%lu exit=%lu error=%lu", ready_result, child_exit,
                  GetLastError());
        Log(line);
        Log("FAIL lifecycle initial GPU pixels");
        goto done;
    }
    if (!SignalPhase("DreamGPUGameMeasuring")) {
        Log("FAIL lifecycle measuring event");
        goto done;
    }
    if (PumpWait(process.hProcess, 6000) != WAIT_TIMEOUT || !Pixels(RGB(255, 0, 0))) {
        Log("FAIL lifecycle retained GPU window");
        goto done;
    }
    if (!SignalPhase("DreamGPUGameMeasured") || PumpWait(captured, 5000) != WAIT_OBJECT_0) {
        Log("FAIL lifecycle bounded capture acknowledgement");
        goto done;
    }
    /* Deliberately skip user-mode GL teardown; the OS/driver must retire it. */
    if (!TerminateProcess(process.hProcess, 77) ||
        WaitForSingleObject(process.hProcess, 3000) != WAIT_OBJECT_0) {
        Log("FAIL lifecycle forced owned process exit");
        goto done;
    }
    RedrawWindow(backdrop, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    if (!Pixels(RGB(16, 32, 48))) {
        Log("FAIL lifecycle desktop after forced exit");
        goto done;
    }
    wsprintfA(line, "LIFECYCLE_EXACT_PIXELS %lu", PixelsChecked);
    Log(line);
    Log("PASS automated lifecycle: retained GPU window, host phase handshake, forced owned process "
        "exit,1024 exact desktop pixels");
    okay = TRUE;
done:
    if (process.hProcess) {
        if (WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT) {
            TerminateProcess(process.hProcess, 1);
            WaitForSingleObject(process.hProcess, 3000);
        }
        CloseHandle(process.hProcess);
    }
    if (backdrop)
        DestroyWindow(backdrop);
    if (Brush) {
        DeleteObject(Brush);
        Brush = NULL;
    }
    if (ready)
        CloseHandle(ready);
    if (captured)
        CloseHandle(captured);
    return okay;
}
void WINAPI WinMainCRTStartup(void) {
    const char *args = GetCommandLineA();
    BOOL child = FALSE;
    while (*args) {
        if (args[0] == '-' && lstrcmpA(args, "-child") == 0) {
            child = TRUE;
            break;
        }
        ++args;
    }
    if (child)
        ExitProcess(Child());
    LogFile = CreateFileA("C:\\DGLOOP.LOG", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                          FILE_ATTRIBUTE_NORMAL, NULL);
    if (LogFile == INVALID_HANDLE_VALUE)
        ExitProcess(1);
    BOOL okay = Parent();
    CloseHandle(LogFile);
    ExitProcess(okay ? 0 : 1);
}
