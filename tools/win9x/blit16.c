/* SPDX-License-Identifier: GPL-2.0-or-later
 * Win16 native-2D oracle: full-screen black/white fills, both overlapping-copy
 * directions, and bounded CPU readback checks. At 1280x1024x32 a full-screen
 * fill exceeds the host inline budget and exercises the sleeping IRQ path.
 */
#include <windows.h>
static HFILE output;
static int width, height, stage, failures;
static DWORD start;
static void log_line(LPCSTR text) {
    _lwrite(output, text, lstrlen(text));
    _lwrite(output, "\r\n", 2);
}
static COLORREF pattern(int x, int y) {
    return ((x / 64 + y / 64) & 1) ? RGB(255, 255, 255) : RGB(0, 0, 0);
}
static COLORREF expected(int x, int y, int copy_count) {
    /* Invert each completed copy in reverse order. Outside its destination
     * pixels are unchanged; overlap always uses the pre-copy source. */
    if (copy_count >= 2 && x < width - 16 && y < height - 16) {
        x += 16;
        y += 16;
    }
    if (copy_count >= 1 && x >= 16 && y >= 16) {
        x -= 16;
        y -= 16;
    }
    return pattern(x, y);
}
static BOOL verify(HDC dc, int mode) {
    int x, y, bad = 0;
    COLORREF want, got;
    char text[160];
    for (y = 0; y < height; y += 17)
        for (x = 0; x < width; x += 19) {
            want = mode < 2 ? (mode ? RGB(255, 255, 255) : RGB(0, 0, 0)) : expected(x, y, mode - 2);
            got = GetPixel(dc, x, y);
            if (got != want && bad++ == 0) {
                wsprintf(text, "pixel mismatch stage=%d x=%d y=%d got=%08lX expected=%08lX", mode,
                         x, y, got, want);
                log_line(text);
            }
        }
    if (bad)
        failures++;
    wsprintf(text, "stage=%d mismatches=%d elapsed_ms=%lu", mode, bad, GetTickCount() - start);
    log_line(text);
    return !bad;
}
static LRESULT CALLBACK dispatch(HWND window, UINT message, WPARAM key, LPARAM detail) {
    HDC dc;
    int x, y;
    BOOL ok = TRUE;
    char text[160];
    switch (message) {
        case WM_CREATE:
            SetTimer(window, 1, 250, NULL);
            return 0;
        case WM_TIMER:
            KillTimer(window, 1);
            dc = GetDC(window);
            start = GetTickCount();
            if (stage < 2)
                ok = BitBlt(dc, 0, 0, width, height, NULL, 0, 0, stage ? WHITENESS : BLACKNESS);
            else if (stage == 2) {
                for (y = 0; y < height; y += 64)
                    for (x = 0; x < width; x += 64)
                        if (!BitBlt(dc, x, y, width - x < 64 ? width - x : 64,
                                    height - y < 64 ? height - y : 64, NULL, 0, 0,
                                    pattern(x, y) ? WHITENESS : BLACKNESS))
                            ok = FALSE;
            } else if (stage == 3)
                ok = BitBlt(dc, 16, 16, width - 16, height - 16, dc, 0, 0, SRCCOPY);
            else
                ok = BitBlt(dc, 0, 0, width - 16, height - 16, dc, 16, 16, SRCCOPY);
            if (!ok) {
                failures++;
                log_line("BitBlt returned FALSE");
            }
            verify(dc, stage);
            ReleaseDC(window, dc);
            if (++stage < 5)
                SetTimer(window, 1, 250, NULL);
            else {
                wsprintf(text, "%s: 5 stages, %d failures, %dx%d",
                         (LPCSTR)(failures ? "FAIL" : "PASS"), failures, width, height);
                log_line(text);
                _lclose(output);
                output = HFILE_ERROR;
                MessageBox(window, text, "DreamGPU Win9x native 2D oracle", MB_OK);
            }
            return 0;
        case WM_KEYDOWN:
            if (key == VK_ESCAPE)
                DestroyWindow(window);
            return 0;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(window, &ps);
            EndPaint(window, &ps);
            return 0;
        }
        case WM_DESTROY:
            KillTimer(window, 1);
            if (output != HFILE_ERROR)
                _lclose(output);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(window, message, key, detail);
}
int PASCAL WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show) {
    WNDCLASS klass;
    HWND window;
    MSG message;
    HDC dc;
    char text[128];
    (void)command;
    (void)show;
    output = _lcreat("C:\\DG9BLT.LOG", 0);
    if (output == HFILE_ERROR)
        return 1;
    width = GetSystemMetrics(SM_CXSCREEN);
    height = GetSystemMetrics(SM_CYSCREEN);
    dc = GetDC(NULL);
    wsprintf(text, "Desktop %dx%d %d bpp", width, height,
             GetDeviceCaps(dc, BITSPIXEL) * GetDeviceCaps(dc, PLANES));
    ReleaseDC(NULL, dc);
    log_line(text);
    if (!previous) {
        klass.style = 0;
        klass.lpfnWndProc = dispatch;
        klass.cbClsExtra = klass.cbWndExtra = 0;
        klass.hInstance = instance;
        klass.hIcon = NULL;
        klass.hCursor = NULL;
        klass.hbrBackground = NULL;
        klass.lpszMenuName = NULL;
        klass.lpszClassName = "DreamGPU9Blt";
        if (!RegisterClass(&klass))
            return 2;
    }
    window = CreateWindow("DreamGPU9Blt", "DreamGPU Win9x 2D oracle", WS_POPUP | WS_VISIBLE, 0, 0,
                          width, height, NULL, NULL, instance, NULL);
    if (!window)
        return 3;
    ShowCursor(FALSE);
    SetFocus(window);
    while (GetMessage(&message, NULL, 0, 0)) {
        TranslateMessage(&message);
        DispatchMessage(&message);
    }
    ShowCursor(TRUE);
    return 0;
}
