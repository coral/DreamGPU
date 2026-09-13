/* SPDX-License-Identifier: GPL-2.0-or-later
 * Two actual GPU processes; work starts only after the host reports an observed
 * OS minimization. Fixed event-driven jobs, exact GPU/desktop pixels and cleanup.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#define FN(r, n, a) static r(WINAPI *p##n) a
FN(HGLRC, wglCreateContext, (HDC));
FN(BOOL, wglMakeCurrent, (HDC, HGLRC));
FN(BOOL, wglSwapBuffers, (HDC));
FN(int, wglChoosePixelFormat, (HDC, const PIXELFORMATDESCRIPTOR *));
FN(BOOL, wglSetPixelFormat, (HDC, int, const PIXELFORMATDESCRIPTOR *));
FN(void, glClearColor, (GLfloat, GLfloat, GLfloat, GLfloat));
FN(void, glClear, (GLbitfield));
FN(void, glReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *));
FN(GLenum, glGetError, (void));
static const char *ReadyNames[2] = {"DreamGPUDualReady0", "DreamGPUDualReady1"};
static const char *WorkNames[2] = {"DreamGPUDualWork0", "DreamGPUDualWork1"};
static const char *DoneNames[2] = {"DreamGPUDualDone0", "DreamGPUDualDone1"};
static HANDLE LogFile = INVALID_HANDLE_VALUE;
static HBRUSH Brush;
static DWORD Checked;
static void Log(const char *text) {
    DWORD n;
    if (LogFile == INVALID_HANDLE_VALUE)
        return;
    WriteFile(LogFile, text, lstrlenA(text), &n, NULL);
    WriteFile(LogFile, "\r\n", 2, &n, NULL);
    FlushFileBuffers(LogFile);
}
static LRESULT CALLBACK Proc(HWND w, UINT m, WPARAM a, LPARAM b) {
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
static DWORD Wait(HANDLE event, DWORD timeout) {
    DWORD began = GetTickCount();
    for (;;) {
        DWORD elapsed = GetTickCount() - began;
        if (elapsed >= timeout)
            return WAIT_TIMEOUT;
        DWORD result = MsgWaitForMultipleObjects(1, &event, FALSE, timeout - elapsed, QS_ALLINPUT);
        if (result != WAIT_OBJECT_0 + 1)
            return result;
        for (unsigned i = 0; i < 128; i++) {
            MSG m;
            if (!PeekMessageA(&m, NULL, 0, 0, PM_REMOVE))
                break;
            TranslateMessage(&m);
            DispatchMessageA(&m);
        }
    }
}
static HWND Window(int child) {
    WNDCLASSA c = {0};
    c.hInstance = GetModuleHandleA(NULL);
    c.lpfnWndProc = Proc;
    c.style = CS_OWNDC;
    c.lpszClassName = "DreamGPUDualWindow";
    if (!RegisterClassA(&c))
        return NULL;
    HWND w =
        CreateWindowExA(0, c.lpszClassName, c.lpszClassName, WS_POPUP,
                        child < 0 ? 32 : 64 + 192 * child, child < 0 ? 32 : 64,
                        child < 0 ? 560 : 128, child < 0 ? 360 : 96, NULL, NULL, c.hInstance, NULL);
    if (w) {
        ShowWindow(w, SW_SHOW);
        UpdateWindow(w);
    }
    return w;
}
static BOOL Pixels(int child, COLORREF expected) {
    HDC dc = GetDC(NULL);
    BOOL okay = dc != NULL;
    if (dc) {
        for (int y = 80; y < 88; y++)
            for (int x = 80 + 192 * child; x < 88 + 192 * child; x++) {
                if (GetPixel(dc, x, y) != expected)
                    okay = FALSE;
                else
                    Checked++;
            }
        ReleaseDC(NULL, dc);
    }
    return okay;
}
static BOOL Phase(const char *name) {
    HANDLE e = OpenEventA(EVENT_MODIFY_STATE, FALSE, name);
    if (!e)
        return FALSE;
    BOOL okay = SetEvent(e);
    CloseHandle(e);
    return okay;
}
static BOOL Draw(HDC dc, unsigned red, unsigned green, unsigned blue) {
    unsigned char pixels[8 * 8 * 4];
    pglClearColor(red ? 1 : 0, green ? 1 : 0, blue ? 1 : 0, 1);
    pglClear(GL_COLOR_BUFFER_BIT);
    pglReadPixels(0, 0, 8, 8, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    for (unsigned i = 0; i < 64; i++)
        if (pixels[i * 4] != red || pixels[i * 4 + 1] != green || pixels[i * 4 + 2] != blue ||
            pixels[i * 4 + 3] != 255)
            return FALSE;
    return pglGetError() == GL_NO_ERROR && pwglSwapBuffers(dc) && pglGetError() == GL_NO_ERROR;
}
static DWORD Child(unsigned child) {
    HMODULE gl = LoadLibraryA("C:\\SIERRA\\Half-Life\\dgpugl.dll");
    if (!gl)
        return 2;
#define LOAD(n)                                                                                    \
    p##n = (void *)GetProcAddress(gl, #n);                                                         \
    if (!p##n)                                                                                     \
    return 3
    LOAD(wglCreateContext);
    LOAD(wglMakeCurrent);
    LOAD(wglSwapBuffers);
    LOAD(wglChoosePixelFormat);
    LOAD(wglSetPixelFormat);
    LOAD(glClearColor);
    LOAD(glClear);
    LOAD(glReadPixels);
    LOAD(glGetError);
#undef LOAD
    HWND w = Window((int)child);
    if (!w)
        return 4;
    HDC dc = GetDC(w);
    PIXELFORMATDESCRIPTOR pf = {0};
    pf.nSize = sizeof(pf);
    pf.nVersion = 1;
    pf.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pf.iPixelType = PFD_TYPE_RGBA;
    pf.cColorBits = 32;
    pf.cDepthBits = 24;
    pf.cStencilBits = 8;
    if (!dc || pwglChoosePixelFormat(dc, &pf) != 1 || !pwglSetPixelFormat(dc, 1, &pf))
        return 5;
    HGLRC context = pwglCreateContext(dc);
    if (!context || !pwglMakeCurrent(dc, context))
        return 6;
    HANDLE ready = OpenEventA(EVENT_MODIFY_STATE, FALSE, ReadyNames[child]),
           work = OpenEventA(SYNCHRONIZE, FALSE, WorkNames[child]),
           done = OpenEventA(EVENT_MODIFY_STATE, FALSE, DoneNames[child]);
    if (!ready || !work || !done)
        return 7;
    if (!Draw(dc, child ? 0 : 255, child ? 255 : 0, 0) || !SetEvent(ready))
        return 8;
    for (unsigned job = 0; job < 2; job++) {
        if (Wait(work, 15000) != WAIT_OBJECT_0)
            return 9;
        if (!Draw(dc, child ? (job ? 0 : 255) : 0, child ? 255 : 0,
                  child ? (job ? 255 : 0) : 255) ||
            !SetEvent(done))
            return 10;
    }
    /* The parent deliberately forces process exit, exercising kernel retirement. */
    Wait(work, 10000);
    return 11;
}
static BOOL Kill(PROCESS_INFORMATION *p) {
    if (!p->hProcess)
        return TRUE;
    if (WaitForSingleObject(p->hProcess, 0) == WAIT_TIMEOUT &&
        (!TerminateProcess(p->hProcess, 77) ||
         WaitForSingleObject(p->hProcess, 3000) != WAIT_OBJECT_0))
        return FALSE;
    CloseHandle(p->hProcess);
    p->hProcess = NULL;
    return TRUE;
}
static BOOL Parent(void) {
    PROCESS_INFORMATION children[2] = {{0}, {0}};
    HANDLE ready[2] = {0}, work[2] = {0}, done[2] = {0};
    HANDLE minimized = NULL, captured = NULL;
    HWND backdrop = NULL;
    BOOL okay = FALSE;
    char line[128];
    minimized = OpenEventA(SYNCHRONIZE, FALSE, "DreamGPUHostMinimized");
    captured = OpenEventA(SYNCHRONIZE, FALSE, "DreamGPUGameCaptureDone");
    if (!minimized || !captured) {
        Log("FAIL dual runner handshake events");
        goto cleanup;
    }
    Brush = CreateSolidBrush(RGB(16, 32, 48));
    backdrop = Window(-1);
    if (!Brush || !backdrop)
        goto cleanup;
    for (unsigned i = 0; i < 2; i++) {
        ready[i] = CreateEventA(NULL, FALSE, FALSE, ReadyNames[i]);
        if (!ready[i] || GetLastError() == ERROR_ALREADY_EXISTS)
            goto cleanup;
        work[i] = CreateEventA(NULL, FALSE, FALSE, WorkNames[i]);
        if (!work[i] || GetLastError() == ERROR_ALREADY_EXISTS)
            goto cleanup;
        done[i] = CreateEventA(NULL, FALSE, FALSE, DoneNames[i]);
        if (!done[i] || GetLastError() == ERROR_ALREADY_EXISTS)
            goto cleanup;
        char path[MAX_PATH], command[MAX_PATH + 24];
        DWORD n = GetModuleFileNameA(NULL, path, sizeof(path));
        if (!n || n >= sizeof(path))
            goto cleanup;
        wsprintfA(command, "\"%s\" -child%u", path, i);
        STARTUPINFOA startup = {0};
        startup.cb = sizeof(startup);
        if (!CreateProcessA(path, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup,
                            &children[i]))
            goto cleanup;
        CloseHandle(children[i].hThread);
        children[i].hThread = NULL;
    }
    if (Wait(ready[0], 5000) != WAIT_OBJECT_0 || Wait(ready[1], 5000) != WAIT_OBJECT_0 ||
        !Pixels(0, RGB(255, 0, 0)) || !Pixels(1, RGB(0, 255, 0))) {
        Log("FAIL dual simultaneous initial GPU windows");
        goto cleanup;
    }
    Log("DUAL_INITIAL_TWO_GPU_PROCESSES_READY");
    if (!Phase("DreamGPUGameMeasuring") || Wait(minimized, 7000) != WAIT_OBJECT_0) {
        Log("FAIL dual observed host-minimize handshake");
        goto cleanup;
    }
    if (!SetEvent(work[0]) || !SetEvent(work[1]) || Wait(done[0], 5000) != WAIT_OBJECT_0 ||
        Wait(done[1], 5000) != WAIT_OBJECT_0 || !Pixels(0, RGB(0, 0, 255)) ||
        !Pixels(1, RGB(255, 255, 0))) {
        Log("FAIL dual GPU readback/swap/desktop completion while host minimized");
        goto cleanup;
    }
    Log("DUAL_MINIMIZED_GPU_READBACK_SWAP_AND_DESKTOP_PIXELS_PASS");
    if (!Phase("DreamGPUGameMeasured") || Wait(captured, 7000) != WAIT_OBJECT_0) {
        Log("FAIL dual restore/capture handshake");
        goto cleanup;
    }
    if (!Kill(&children[0]))
        goto cleanup;
    RedrawWindow(backdrop, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    if (!SetEvent(work[1]) || Wait(done[1], 5000) != WAIT_OBJECT_0 || !Pixels(0, RGB(16, 32, 48)) ||
        !Pixels(1, RGB(0, 255, 255))) {
        Log("FAIL dual survivor after independent forced exit");
        goto cleanup;
    }
    Log("DUAL_SURVIVOR_FRESH_GPU_WORK_PASS");
    if (!Kill(&children[1]))
        goto cleanup;
    RedrawWindow(backdrop, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    if (!Pixels(0, RGB(16, 32, 48)) || !Pixels(1, RGB(16, 32, 48)))
        goto cleanup;
    wsprintfA(line, "DUAL_EXACT_DESKTOP_PIXELS %lu", Checked);
    Log(line);
    Log("PASS automated dual: two concurrent GPU processes; minimized native readback/swap; "
        "independent forced exits; survivor redraw;512 exact desktop pixels and320 exact GPU "
        "readback pixels");
    okay = TRUE;
cleanup:
    for (unsigned i = 0; i < 2; i++) {
        if (children[i].hProcess) {
            DWORD code = 0;
            GetExitCodeProcess(children[i].hProcess, &code);
            wsprintfA(line, "DUAL_CHILD%u_EXIT %lu", i, code);
            Log(line);
        }
        if (!Kill(&children[i]))
            okay = FALSE;
        if (ready[i])
            CloseHandle(ready[i]);
        if (work[i])
            CloseHandle(work[i]);
        if (done[i])
            CloseHandle(done[i]);
    }
    if (backdrop)
        DestroyWindow(backdrop);
    if (Brush) {
        DeleteObject(Brush);
        Brush = NULL;
    }
    if (minimized)
        CloseHandle(minimized);
    if (captured)
        CloseHandle(captured);
    if (!okay)
        Log("FAIL dual owned resource or pixel contract");
    return okay;
}
void WINAPI WinMainCRTStartup(void) {
    const char *args = GetCommandLineA();
    while (*args) {
        if (args[0] == '-' && lstrcmpA(args, "-child0") == 0)
            ExitProcess(Child(0));
        if (args[0] == '-' && lstrcmpA(args, "-child1") == 0)
            ExitProcess(Child(1));
        args++;
    }
    LogFile =
        CreateFileA("C:\\DGDUAL.LOG", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    if (LogFile == INVALID_HANDLE_VALUE)
        ExitProcess(1);
    BOOL okay = Parent();
    CloseHandle(LogFile);
    ExitProcess(okay ? 0 : 1);
}
