/* SPDX-License-Identifier: GPL-2.0-or-later
 * Real NT5 window-composition diagnostic. This uses the private driver ABI,
 * not an OpenGL DLL. Keyboard steps are deliberately outside FPS benchmarks.
 */
#define WIN32_LEAN_AND_MEAN
extern "C" {
#include <windows.h>
#include "dg-escape.h"
#include "dg-window.h"
#include "gl.h"
#include "gl-funcs.h"

typedef struct {
    HWND Window;
    HDC DC;
    ULONG Context, Drawable, Binding;
    BOOL Created;
} PANE;

static HWND MainWindow, CoverWindow;
static HINSTANCE Instance;
static HDC Display;
static HANDLE LogFile;
static PANE Panes[2];
static ULONG Client, Used, PendingPaint;
static BOOL Ready, Closing, Failed;
static struct {
    DG_ESCAPE_REQUEST Request;
    ULONG Words[2048];
} Packet;
static DG_ESCAPE_REPLY Reply;

static void Log(const char *text) {
    DWORD written;
    if (LogFile == INVALID_HANDLE_VALUE)
        return;
    WriteFile(LogFile, text, lstrlenA(text), &written, NULL);
    WriteFile(LogFile, "\r\n", 2, &written, NULL);
    FlushFileBuffers(LogFile);
}

static BOOL Fail(const char *text) {
    Failed = TRUE;
    Log(text);
    return FALSE;
}

static BOOL Request(ULONG operation, ULONG expected) {
    int result;
    CHAR line[192];
    Packet.Request.Version = DG_ESCAPE_VERSION;
    Packet.Request.Operation = operation;
    Packet.Request.Client = Client;
    Packet.Request.Bytes = operation == DG_ESCAPE_SUBMIT ? Used * 4 : 0;
    Packet.Request.Function = 0;
    Packet.Request.Reserved = 0;
    ZeroMemory(&Reply, sizeof(Reply));
    result = ExtEscape(Display, DG_ESCAPE, sizeof(Packet.Request) + Packet.Request.Bytes,
                       (LPCSTR)&Packet, sizeof(Reply), (LPSTR)&Reply);
    if (result == 1 && Reply.Version == DG_ESCAPE_VERSION && Reply.Status == expected)
        return TRUE;
    wsprintfA(line, "FAIL transport: op=%lu result=%d status=%lu host_error=%lu sequence=%lu",
              operation, result, Reply.Status, Reply.DeviceError, Reply.CompletedSequence);
    return Fail(line);
}

static ULONG *Record(ULONG operation, ULONG context, ULONG drawable, ULONG arguments) {
    ULONG *record;
    if (arguments > 32 || Used + 8 + arguments > sizeof(Packet.Words) / 4) {
        Fail("FAIL internal diagnostic packet bound");
        ExitProcess(2);
    }
    record = &Packet.Words[Used];
    ZeroMemory(record, (8 + arguments) * 4);
    record[0] = operation;
    record[1] = (8 + arguments) * 4;
    record[3] = context;
    record[4] = drawable;
    Used += 8 + arguments;
    return record + 8;
}

static BOOL Bind(PANE *pane) {
    DG_WINDOW_BIND request;
    DG_WINDOW_REPLY response;
    int result;
    CHAR line[160];
    ZeroMemory(&request, sizeof(request));
    ZeroMemory(&response, sizeof(response));
    request.Window = (ULONG)(ULONG_PTR)pane->Window;
    request.Magic = DG_WINDOW_MAGIC;
    request.Version = DG_WINDOW_VERSION;
    request.Client = Client;
    request.Context = pane->Context;
    request.Drawable = pane->Drawable;
    result = ExtEscape(pane->DC, WNDOBJ_SETUP, sizeof(request), (LPCSTR)&request, sizeof(response),
                       (LPSTR)&response);
    if (result <= 0 || response.Version != DG_WINDOW_VERSION || response.Status != DG_ESCAPE_OK ||
        !response.Binding || (response.Capabilities & ~DG_WINDOW_CAP_FRONT_ONLY)) {
        wsprintfA(line, "FAIL window bind: context=%lu result=%d status=%lu binding=%lu",
                  pane->Context, result, response.Status, response.Binding);
        return Fail(line);
    }
    pane->Binding = response.Binding;
    wsprintfA(line, "BOUND context=%lu drawable=%lu token=%lu", pane->Context, pane->Drawable,
              pane->Binding);
    Log(line);
    return TRUE;
}

static BOOL Present(PANE *pane) {
    DG_WINDOW_PRESENT present;
    if (!pane->Window || !pane->Created || !IsWindowVisible(pane->Window))
        return TRUE;
    present.Magic = DG_WINDOW_MAGIC;
    present.Version = DG_WINDOW_VERSION;
    present.Binding = pane->Binding;
    present.Flags = 0;
    if (DrawEscape(pane->DC, DG_DRAW_ESCAPE, sizeof(present), (LPCSTR)&present) != 1)
        return Fail("FAIL window DrawEscape present");
    return TRUE;
}

static BOOL Draw(PANE *pane) {
    ULONG *args;
    if (!pane->Window || !pane->Created || !IsWindowVisible(pane->Window))
        return TRUE;
    Used = 0;
    Record(DG_GL_MAKE_CURRENT, pane->Context, pane->Drawable, 0);
    args = Record(DG_GL_CALL, pane->Context, pane->Drawable, 5);
    args[0] = FEnum_glClearColor;
    args[1] = 0x3f800000;
    args[2] = pane->Context == 1 ? 0 : 0x3f800000;
    args[3] = 0;
    args[4] = 0x3f800000;
    args = Record(DG_GL_CALL, pane->Context, pane->Drawable, 2);
    args[0] = FEnum_glClear;
    args[1] = 0x4000;
    args = Record(DG_GL_CALL, pane->Context, pane->Drawable, 2);
    args[0] = FEnum_glEnable;
    args[1] = 0x0c11;
    args = Record(DG_GL_CALL, pane->Context, pane->Drawable, 5);
    args[0] = FEnum_glScissor;
    args[1] = args[2] = 0;
    args[3] = 320;
    args[4] = 120;
    args = Record(DG_GL_CALL, pane->Context, pane->Drawable, 5);
    args[0] = FEnum_glClearColor;
    args[1] = 0;
    args[2] = pane->Context == 1 ? 0 : 0x3f800000;
    args[3] = args[4] = 0x3f800000;
    args = Record(DG_GL_CALL, pane->Context, pane->Drawable, 2);
    args[0] = FEnum_glClear;
    args[1] = 0x4000;
    args = Record(DG_GL_CALL, pane->Context, pane->Drawable, 2);
    args[0] = FEnum_glDisable;
    args[1] = 0x0c11;
    if (!Request(DG_ESCAPE_SUBMIT, DG_ESCAPE_OK))
        return FALSE;
    return Present(pane);
}

static BOOL DrawAll(void) {
    return Draw(&Panes[0]) && Draw(&Panes[1]);
}

static void Geometry(ULONG stage) {
    ULONG i;
    CHAR line[160];
    POINT origin;
    wsprintfA(line, "STAGE %lu READY tick=%lu; host must verify composed pixels", stage,
              GetTickCount());
    Log(line);
    for (i = 0; i < 2; ++i) {
        if (!Panes[i].Window)
            continue;
        origin.x = origin.y = 0;
        ClientToScreen(Panes[i].Window, &origin);
        wsprintfA(line, "PANE %lu screen=%ld,%ld size=320,240 top=%s bottom=%s", i + 1, origin.x,
                  origin.y, i ? "yellow" : "red", i ? "cyan" : "blue");
        Log(line);
    }
}

static BOOL VerifyReadback(PANE *pane) {
    BITMAPINFO info;
    HBITMAP bitmap, old;
    HDC memory;
    ULONG *pixels, top, bottom, x = pane->Context == 1 ? 8 : 311;
    BOOL copied, valid;
    CHAR line[192];
    ZeroMemory(&info, sizeof(info));
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 320;
    info.bmiHeader.biHeight = -240;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    memory = CreateCompatibleDC(pane->DC);
    if (!memory)
        return Fail("FAIL readback memory DC allocation");
    bitmap = CreateDIBSection(pane->DC, &info, DIB_RGB_COLORS, (PVOID *)&pixels, NULL, 0);
    if (!bitmap) {
        DeleteDC(memory);
        return Fail("FAIL readback DIB allocation");
    }
    old = (HBITMAP)SelectObject(memory, bitmap);
    copied = BitBlt(memory, 0, 0, 320, 240, pane->DC, 0, 0, SRCCOPY);
    GdiFlush();
    /* After F3 pane1 covers pane2's lower-left corner. Sample pane2 on the
     * right, where both points are visible; BitBlt clips only its destination. */
    top = pixels[8 * 320 + x] & 0xffffff;
    bottom = pixels[231 * 320 + x] & 0xffffff;
    valid = copied && top == (pane->Context == 1 ? 0xff0000UL : 0xffff00UL) &&
            bottom == (pane->Context == 1 ? 0x0000ffUL : 0x00ffffUL);
    SelectObject(memory, old);
    DeleteObject(bitmap);
    DeleteDC(memory);
    wsprintfA(line, "%s GDI readback context=%lu topRGB=%06lx bottomRGB=%06lx",
              valid ? "PASS" : "FAIL", pane->Context, top, bottom);
    Log(line);
    if (!valid)
        Failed = TRUE;
    return valid;
}

static void DestroyPane(PANE *pane) {
    if (pane->DC) {
        ReleaseDC(pane->Window, pane->DC);
        pane->DC = NULL;
    }
    if (pane->Window) {
        DestroyWindow(pane->Window);
        pane->Window = NULL;
    }
    /* Deliberately retire the HWND first, exercising WOC_DELETE with a live
     * GL context. Closing resources afterward must not retain stale clips. */
    if (pane->Created && Client) {
        Used = 0;
        Record(DG_GL_DESTROY_CONTEXT, pane->Context, 0, 0);
        Record(DG_GL_DESTROY_DRAWABLE, 0, pane->Drawable, 0);
        Request(DG_ESCAPE_SUBMIT, DG_ESCAPE_OK);
    }
    pane->Created = FALSE;
    pane->Binding = 0;
}

static void Step(HWND window, WPARAM key) {
    ULONG frame;
    if (key == VK_ESCAPE) {
        DestroyWindow(window);
        return;
    }
    if (!Ready || Failed)
        return;
    if (key == VK_F1) {
        ShowWindow(CoverWindow, SW_HIDE);
    } else if (key == VK_F2) {
        ShowWindow(CoverWindow, SW_SHOWNOACTIVATE);
        SetWindowPos(CoverWindow, HWND_TOP, 208, 148, 176, 104, SWP_NOACTIVATE);
        UpdateWindow(CoverWindow);
    } else if (key == VK_F3) {
        SetWindowPos(Panes[0].Window, HWND_TOP, 96, 336, 320, 240, SWP_NOACTIVATE);
        SetWindowPos(CoverWindow, HWND_TOP, 480, 80, 176, 104, SWP_NOACTIVATE);
    } else if (key == VK_F4) {
        ShowWindow(CoverWindow, SW_HIDE);
        UpdateWindow(window);
        if (!DrawAll() || !VerifyReadback(&Panes[0]) ||
            (Panes[1].Window && !VerifyReadback(&Panes[1])))
            return;
    } else if (key == VK_F5) {
        DestroyPane(&Panes[1]);
        InvalidateRect(window, NULL, FALSE);
        UpdateWindow(window);
    } else if (key == VK_F7) {
        for (frame = 0; frame < 7; ++frame) {
            if (!DrawAll())
                return;
            Sleep(20); /* Finite lifecycle check; no idle rendering or spin. */
        }
    } else if (key == VK_F8) {
        /* Isolate DrawEscape/GDI synchronization from ExtEscape GL updates.
         * Keep this branch free of DrawAll, including the common final draw. */
        for (frame = 0; frame < 7; ++frame) {
            if (!Present(&Panes[0]) || !Present(&Panes[1]))
                return;
            Sleep(20);
        }
        Geometry(8);
        return;
    } else
        return;
    if (DrawAll())
        Geometry(static_cast<ULONG>(key) - VK_F1 + 1);
}

static LRESULT CALLBACK PaneProc(HWND window, UINT message, WPARAM key, LPARAM detail) {
    if (message == WM_ERASEBKGND)
        return 1;
    if (message == WM_PAINT) {
        PAINTSTRUCT paint;
        BeginPaint(window, &paint);
        EndPaint(window, &paint);
        if (Ready && !Closing && !Failed && !PendingPaint) {
            PendingPaint = 1;
            PostMessage(MainWindow, WM_APP, 0, 0);
        }
        return 0;
    }
    if (message == WM_KEYDOWN) {
        Step(MainWindow, key);
        return 0;
    }
    return DefWindowProc(window, message, key, detail);
}

static LRESULT CALLBACK CoverProc(HWND window, UINT message, WPARAM key, LPARAM detail) {
    if (message == WM_PAINT) {
        PAINTSTRUCT paint;
        RECT area;
        HBRUSH brush = CreateSolidBrush(RGB(0, 255, 0));
        HDC dc = BeginPaint(window, &paint);
        GetClientRect(window, &area);
        FillRect(dc, &area, brush);
        DeleteObject(brush);
        EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_KEYDOWN) {
        Step(MainWindow, key);
        return 0;
    }
    return DefWindowProc(window, message, key, detail);
}

static LRESULT CALLBACK MainProc(HWND window, UINT message, WPARAM key, LPARAM detail) {
    if (message == WM_KEYDOWN) {
        Step(window, key);
        return 0;
    }
    if (message == WM_APP) {
        PendingPaint = 0;
        if (Ready && !Closing && !Failed)
            DrawAll();
        return 0;
    }
    if (message == WM_DESTROY) {
        Closing = TRUE;
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(window, message, key, detail);
}

void WINAPI WinMainCRTStartup(void) {
    WNDCLASSA klass;
    MSG message;
    ULONG i, *args;
    Instance = GetModuleHandle(NULL);
    LogFile = CreateFileA("C:\\DGWIN.LOG", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                          FILE_ATTRIBUTE_NORMAL, NULL);
    if (LogFile == INVALID_HANDLE_VALUE)
        ExitProcess(1);
    Log("DreamGPU GPU window diagnostic: F1 uncovered, F2 GDI cover, F3 move, F4 GDI readback, F5 "
        "close pane2, F7 seven draws, F8 seven presents only, Escape exit");
    ZeroMemory(&klass, sizeof(klass));
    klass.hInstance = Instance;
    klass.lpfnWndProc = MainProc;
    klass.lpszClassName = "DgWindowProbe";
    klass.hbrBackground = (HBRUSH)GetStockObject(DKGRAY_BRUSH);
    if (!RegisterClassA(&klass))
        goto failed;
    klass.lpfnWndProc = PaneProc;
    klass.lpszClassName = "DgGpuPane";
    klass.style = CS_OWNDC;
    klass.hbrBackground = NULL;
    if (!RegisterClassA(&klass))
        goto failed;
    klass.lpfnWndProc = CoverProc;
    klass.lpszClassName = "DgCpuCover";
    klass.style = 0;
    if (!RegisterClassA(&klass))
        goto failed;
    MainWindow = CreateWindowA("DgWindowProbe", "DreamGPU GPU window diagnostic",
                               WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN, 40, 60, 768, 640, NULL,
                               NULL, Instance, NULL);
    if (!MainWindow)
        goto failed;
    Display = GetDC(NULL);
    if (!Display || !Request(DG_ESCAPE_OPEN, DG_ESCAPE_OK))
        goto failed;
    Client = Reply.Client;
    if (!Client || Reply.MaxBytes < sizeof(Packet.Words))
        goto failed;
    for (i = 0; i < 2; ++i) {
        PANE *pane = &Panes[i];
        pane->Context = pane->Drawable = i + 1;
        pane->Window =
            CreateWindowA("DgGpuPane", "GPU pane", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                          i ? 280 : 24, i ? 196 : 48, 320, 240, MainWindow, NULL, Instance, NULL);
        if (!pane->Window || !(pane->DC = GetDC(pane->Window)))
            goto failed;
        Used = 0;
        args = Record(DG_GL_CREATE_CONTEXT, pane->Context, 0, 1);
        args[0] = 0;
        args = Record(DG_GL_CREATE_DRAWABLE, 0, pane->Drawable, 2);
        args[0] = 320;
        args[1] = 240;
        Record(DG_GL_MAKE_CURRENT, pane->Context, pane->Drawable, 0);
        if (!Request(DG_ESCAPE_SUBMIT, DG_ESCAPE_OK))
            goto failed;
        pane->Created = TRUE;
        if (!Bind(pane))
            goto failed;
    }
    SetWindowPos(Panes[1].Window, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    CoverWindow = CreateWindowA("DgCpuCover", "GDI occluder", WS_CHILD | WS_CLIPSIBLINGS, 208, 148,
                                176, 104, MainWindow, NULL, Instance, NULL);
    if (!CoverWindow)
        goto failed;
    UpdateWindow(MainWindow);
    Ready = TRUE;
    SetForegroundWindow(MainWindow);
    SetFocus(MainWindow);
    if (!DrawAll())
        goto failed;
    Geometry(0);
    while (GetMessage(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessage(&message);
    }
    goto done;
failed:
    Fail("FAIL diagnostic setup; see preceding driver result");
done:
    Closing = TRUE;
    Ready = FALSE;
    DestroyPane(&Panes[0]);
    DestroyPane(&Panes[1]);
    if (Client) {
        Request(DG_ESCAPE_CLOSE, DG_ESCAPE_OK);
        Client = 0;
    }
    if (Display)
        ReleaseDC(NULL, Display);
    if (MainWindow && IsWindow(MainWindow))
        DestroyWindow(MainWindow);
    Log(Failed ? "FAIL diagnostic"
               : "CLOSED; host pixel checks and driver lifetime counters determine acceptance");
    CloseHandle(LogFile);
    ExitProcess(Failed ? 1 : 0);
}

} /* extern C */
