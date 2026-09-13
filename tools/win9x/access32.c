/* SPDX-License-Identifier: GPL-2.0-or-later
 * Source-built Win98 primary-access oracle. IOCTL markers delimit synchronous
 * GDI calls; the diagnostic driver records actual DDI/BeginAccess context.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../../guest/win9x/dg-identity.h"
static HANDLE Device, Serial, Log;
static unsigned Failures;
static void Report(const char *name, BOOL ok) {
    char line[160];
    DWORD written;
    int bytes = wsprintfA(line, "DGACCESS %s pass=%u\r\n", name, !!ok);
    if (!ok)
        ++Failures;
    WriteFile(Log, line, bytes, &written, NULL);
    if (Serial != INVALID_HANDLE_VALUE)
        WriteFile(Serial, line, bytes, &written, NULL);
}
static void Mark(unsigned marker) {
    DWORD returned;
    DeviceIoControl(Device, DG_IDENTITY_BASE | 0x6000 | marker, NULL, 0, NULL, 0, &returned, NULL);
}
static LRESULT CALLBACK Window(HWND window, UINT message, WPARAM w, LPARAM l) {
    return DefWindowProcA(window, message, w, l);
}
int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show) {
    WNDCLASSA wc;
    HWND window;
    HDC dc, memory;
    HBITMAP bitmap, old;
    HDC compatible;
    HBITMAP device_bitmap, old_device;
    HWND occluder;
    HRGN second;
    HBRUSH brush;
    HPEN pen, old_pen;
    HRGN region;
    RECT rect;
    BITMAPINFO info;
    DWORD *pixels;
    DWORD payload[4] = {0x4a524739, 0x31444242, 0x13579bdf, 0x2468ace0};
    unsigned i;
    DCB state;
    COMMTIMEOUTS timeouts;
    (void)previous;
    (void)command;
    (void)show;
    Log =
        CreateFileA("C:\\DGACC.LOG", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    Serial = CreateFileA("COM1", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (Serial != INVALID_HANDLE_VALUE) {
        ZeroMemory(&state, sizeof(state));
        state.DCBlength = sizeof(state);
        if (GetCommState(Serial, &state)) {
            state.BaudRate = CBR_115200;
            state.ByteSize = 8;
            state.Parity = NOPARITY;
            state.StopBits = ONESTOPBIT;
            state.fOutxCtsFlow = state.fOutxDsrFlow = state.fOutX = state.fInX = FALSE;
            SetCommState(Serial, &state);
        }
        ZeroMemory(&timeouts, sizeof(timeouts));
        timeouts.WriteTotalTimeoutConstant = 250;
        SetCommTimeouts(Serial, &timeouts);
    }
    Device = CreateFileA("\\\\.\\DREAMGPU", 0, 0, NULL, OPEN_EXISTING, 0, NULL);
    Report("device", Device != INVALID_HANDLE_VALUE);
    if (Device == INVALID_HANDLE_VALUE)
        goto done;
    ZeroMemory(&wc, sizeof(wc));
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = Window;
    wc.hInstance = instance;
    wc.lpszClassName = "DgAccessOracle";
    RegisterClassA(&wc);
    window =
        CreateWindowA(wc.lpszClassName, "DreamGPU access oracle", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                      50, 50, 400, 300, NULL, NULL, instance, NULL);
    Report("window", window != NULL);
    if (!window)
        goto device_done;
    UpdateWindow(window);
    dc = GetDC(window);
    memory = CreateCompatibleDC(dc);
    ZeroMemory(&info, sizeof(info));
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = 128;
    info.bmiHeader.biHeight = -128;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, (void **)&pixels, NULL, 0);
    Report("dib", bitmap != NULL);
    if (!bitmap)
        goto window_done;
    old = SelectObject(memory, bitmap);
    for (i = 0; i < 128 * 128; ++i)
        pixels[i] = 0x00336699;
    brush = CreateSolidBrush(RGB(70, 90, 110));
    pen = CreatePen(PS_SOLID, 1, RGB(50, 20, 100));
    old_pen = SelectObject(dc, pen);
    rect.left = 10;
    rect.top = 10;
    rect.right = 90;
    rect.bottom = 90;
    Mark(1);
    Report("solid-fill", FillRect(dc, &rect, brush));
    Mark(0);
    Mark(2);
    Report("text", TextOutA(dc, 10, 100, "Primary access", 14));
    Mark(0);
    Mark(3);
    MoveToEx(dc, 10, 10, NULL);
    Report("line", LineTo(dc, 100, 100));
    Mark(0);
    Mark(4);
    Report("screen-read", GetPixel(dc, 12, 12) != CLR_INVALID);
    Mark(0);
    Mark(5);
    Report("screen-copy", BitBlt(dc, 100, 10, 50, 50, dc, 10, 10, SRCCOPY));
    Mark(0);
    Mark(6);
    Report("dib-to-screen", BitBlt(dc, 10, 10, 128, 128, memory, 0, 0, SRCCOPY));
    Mark(0);
    Mark(7);
    Report("screen-to-dib", BitBlt(memory, 0, 0, 64, 64, dc, 10, 10, SRCCOPY));
    Mark(0);
    Mark(8);
    Report("stretch", StretchBlt(dc, 40, 40, 80, 80, memory, 0, 0, 120, 120, SRCCOPY));
    Mark(0);
    Mark(9);
    Report("stretch-dib", StretchDIBits(dc, 40, 40, 80, 80, 0, 0, 120, 120, pixels, &info,
                                        DIB_RGB_COLORS, SRCCOPY) != (int)GDI_ERROR);
    Mark(0);
    region = CreateRectRgn(10, 10, 40, 40);
    SelectClipRgn(dc, region);
    Mark(10);
    Report("clipped-dib", BitBlt(dc, 0, 0, 128, 128, memory, 0, 0, SRCCOPY));
    Mark(0);
    SelectClipRgn(dc, NULL);
    DeleteObject(region);
    Mark(11);
    Report("cursor", SetCursorPos(100, 100));
    Mark(0);
    compatible = CreateCompatibleDC(dc);
    device_bitmap = CreateCompatibleBitmap(dc, 128, 128);
    old_device = SelectObject(compatible, device_bitmap);
    Report("compatible-bitmap", device_bitmap != NULL);
    BitBlt(compatible, 0, 0, 128, 128, memory, 0, 0, SRCCOPY);
    Mark(12);
    Report("compatible-to-screen", BitBlt(dc, 10, 10, 128, 128, compatible, 0, 0, SRCCOPY));
    Mark(0);
    region = CreateRectRgn(10, 10, 40, 40);
    second = CreateRectRgn(60, 60, 90, 90);
    CombineRgn(region, region, second, RGN_OR);
    SelectClipRgn(dc, region);
    Mark(13);
    Report("compatible-complex-clip", BitBlt(dc, 0, 0, 128, 128, compatible, 0, 0, SRCCOPY));
    Mark(0);
    SelectClipRgn(dc, NULL);
    DeleteObject(region);
    DeleteObject(second);
    occluder = CreateWindowA(wc.lpszClassName, "Occluder", WS_POPUP | WS_VISIBLE, 100, 100, 40, 40,
                             NULL, NULL, instance, NULL);
    UpdateWindow(occluder);
    Mark(14);
    Report("compatible-occluded", BitBlt(dc, 10, 10, 128, 128, compatible, 0, 0, SRCCOPY));
    Mark(0);
    DestroyWindow(occluder);
    Mark(15);
    Report("display-control", ExtEscape(dc, 0x4a01, 0, NULL, 0, NULL) == 1);
    Mark(0);
    Mark(16);
    Report("compatible-control-rejected", ExtEscape(compatible, 0x4a01, 0, NULL, 0, NULL) <= 0);
    Mark(0);
    Mark(17);
    Report("dib-control-rejected", ExtEscape(memory, 0x4a01, 0, NULL, 0, NULL) <= 0);
    Mark(0);
    Report("compatible-token-write",
           SetBitmapBits(device_bitmap, sizeof(payload), payload) == sizeof(payload));
    Mark(18);
    Report("compatible-token-blit", BitBlt(dc, 10, 10, 128, 128, compatible, 0, 0, SRCCOPY));
    Mark(0);
    SelectObject(compatible, old_device);
    DeleteObject(device_bitmap);
    DeleteDC(compatible);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
    DeleteObject(brush);
    SelectObject(memory, old);
    DeleteObject(bitmap);
window_done:
    DeleteDC(memory);
    ReleaseDC(window, dc);
    DestroyWindow(window);
device_done:
    CloseHandle(Device);
done:
    Report("DONE", Failures == 0);
    CloseHandle(Log);
    if (Serial != INVALID_HANDLE_VALUE)
        CloseHandle(Serial);
    return Failures != 0;
}
