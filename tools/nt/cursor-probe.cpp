/* SPDX-License-Identifier: GPL-2.0-or-later
 * Real NT5 pointer-plane diagnostic. Every change is keyboard stepped; idle
 * never paints or moves the cursor. Host captures must include the cursor.
 */
#define WIN32_LEAN_AND_MEAN
extern "C" {
#include <windows.h>
static HWND Window;
static HINSTANCE Instance;
static HCURSOR Mono, Alpha, Masked, Active;
static HANDLE LogFile;
static BOOL Visible = TRUE;
static ULONG Stage = 1;
static POINT Position = {263, 201}; /* hotspot => top-left 256,192 */
static void Log(const char *text) {
    DWORD written;
    if (LogFile == INVALID_HANDLE_VALUE)
        return;
    WriteFile(LogFile, text, lstrlenA(text), &written, NULL);
    WriteFile(LogFile, "\r\n", 2, &written, NULL);
    FlushFileBuffers(LogFile);
}
static void State(void) {
    POINT origin = {0, 0}, position = Position;
    CHAR line[256];
    ClientToScreen(Window, &origin);
    ClientToScreen(Window, &position);
    SetCursor(Visible ? Active : NULL);
    SetCursorPos(position.x, position.y);
    wsprintfA(
        line,
        "STAGE %lu origin=%ld,%ld cursor_hotspot=%ld,%ld visible=%d shape=%s; HOST PIXELS REQUIRED",
        Stage, origin.x, origin.y, position.x, position.y, Visible,
        Active == Mono    ? "mono"
        : Active == Alpha ? "alpha"
                          : "masked");
    Log(line);
}
static HCURSOR ColorCursor(BOOL alpha) {
    BITMAPINFO info;
    ICONINFO icon;
    HDC dc = GetDC(NULL);
    ULONG *pixels, x, y;
    BYTE mask[128];
    HCURSOR cursor;
    ZeroMemory(&info, sizeof(info));
    ZeroMemory(&icon, sizeof(icon));
    ZeroMemory(mask, sizeof(mask));
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = 32;
    info.bmiHeader.biHeight = -32;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    icon.hbmColor = CreateDIBSection(dc, &info, DIB_RGB_COLORS, (void **)&pixels, NULL, 0);
    ReleaseDC(NULL, dc);
    if (!icon.hbmColor)
        return NULL;
    for (y = 0; y < 32; ++y)
        for (x = 0; x < 32; ++x) {
            if (alpha)
                pixels[y * 32 + x] =
                    y < 16 ? (x < 16 ? 0 : 0x80400000UL) : (x < 16 ? 0xff00ff00UL : 0x80004000UL);
            else {
                if (y >= 16)
                    mask[y * 4 + x / 8] |= 0x80 >> (x & 7);
                pixels[y * 32 + x] = x < 16 ? 0x00123456UL : 0x00654321UL;
            }
        }
    icon.fIcon = FALSE;
    icon.xHotspot = 7;
    icon.yHotspot = 9;
    icon.hbmMask = CreateBitmap(32, 32, 1, 1, mask);
    cursor = (HCURSOR)CreateIconIndirect(&icon);
    DeleteObject(icon.hbmMask);
    DeleteObject(icon.hbmColor);
    return cursor;
}
static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM key, LPARAM extra) {
    if (message == WM_SETCURSOR) {
        SetCursor(Visible ? Active : NULL);
        return TRUE;
    }
    if (message == WM_PAINT) {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(window, &paint);
        HBRUSH brushes[2] = {CreateSolidBrush(RGB(32, 64, 96)),
                             CreateSolidBrush(RGB(192, 128, 64))};
        int x, y;
        for (y = 0; y < 384; y += 16)
            for (x = 0; x < 512; x += 16) {
                RECT rect = {x, y, x + 16, y + 16};
                FillRect(dc, &rect, brushes[((x / 16) ^ (y / 16)) & 1]);
            }
        DeleteObject(brushes[0]);
        DeleteObject(brushes[1]);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
        TextOutA(dc, 8, 8, "F1 mono F2 hide F3 show F4 move F5 alpha F6 masked Esc exit", 58);
        EndPaint(window, &paint);
        return 0;
    }
    if (message == WM_KEYDOWN) {
        if (key == VK_ESCAPE) {
            DestroyWindow(window);
            return 0;
        }
        switch (key) {
            case VK_F1:
                Active = Mono;
                Visible = TRUE;
                Position.x = 263;
                Position.y = 201;
                break;
            case VK_F2:
                Visible = FALSE;
                break;
            case VK_F3:
                Visible = TRUE;
                break;
            case VK_F4:
                Position.x = 295;
                Position.y = 233;
                break;
            case VK_F5:
                Active = Alpha;
                Visible = TRUE;
                break;
            case VK_F6:
                Active = Masked;
                Visible = TRUE;
                break;
            default:
                return 0;
        }
        Stage = static_cast<ULONG>(key) - VK_F1 + 1;
        State();
        return 0;
    }
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(window, message, key, extra);
}
void __stdcall WinMainCRTStartup(void) {
    WNDCLASSA cls;
    MSG message;
    BYTE and_mask[128], xor_mask[128];
    ULONG x, y;
    Instance = GetModuleHandleA(NULL);
    LogFile = CreateFileA("C:\\DGCURSOR.LOG", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                          FILE_ATTRIBUTE_NORMAL, NULL);
    ZeroMemory(and_mask, sizeof(and_mask));
    ZeroMemory(xor_mask, sizeof(xor_mask));
    for (y = 0; y < 32; ++y)
        for (x = 0; x < 32; ++x) {
            if (y >= 16)
                and_mask[y * 4 + x / 8] |= 0x80 >> (x & 7);
            if (x >= 16)
                xor_mask[y * 4 + x / 8] |= 0x80 >> (x & 7);
        }
    Mono = CreateCursor(Instance, 7, 9, 32, 32, and_mask, xor_mask);
    Alpha = ColorCursor(TRUE);
    Masked = ColorCursor(FALSE);
    if (!Mono || !Alpha || !Masked) {
        Log("FAIL cursor allocation");
        ExitProcess(2);
    }
    Active = Mono;
    ZeroMemory(&cls, sizeof(cls));
    cls.lpfnWndProc = WindowProc;
    cls.hInstance = Instance;
    cls.lpszClassName = "DreamGPUCursorProbe";
    RegisterClassA(&cls);
    Window = CreateWindowExA(0, cls.lpszClassName, "DreamGPU native pointer diagnostic",
                             WS_POPUP | WS_VISIBLE, 64, 64, 512, 384, NULL, NULL, Instance, NULL);
    if (!Window) {
        Log("FAIL window allocation");
        ExitProcess(3);
    }
    ShowWindow(Window, SW_SHOW);
    UpdateWindow(Window);
    SetForegroundWindow(Window);
    SetFocus(Window);
    State();
    while (GetMessageA(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    SetCursor(LoadCursorA(NULL, IDC_ARROW));
    DestroyCursor(Mono);
    DestroyCursor(Alpha);
    DestroyCursor(Masked);
    Log("DONE closed");
    CloseHandle(LogFile);
    ExitProcess(0);
}

} /* extern C */
