/* SPDX-License-Identifier: GPL-2.0-or-later
 * Run the exact NT typed-query/texture/credit oracle through Win98's checked
 * VxD boundary. Only transport and result delivery differ; no GUI dialog.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
extern "C" {
static HANDLE Serial = INVALID_HANDLE_VALUE;
static BOOL __stdcall Dg9ProbeWrite(HANDLE file, const void *data, DWORD bytes, DWORD *written,
                                    OVERLAPPED *overlapped) {
    DWORD ignored;
    BOOL result = WriteFile(file, data, bytes, written, overlapped);
    if (Serial != INVALID_HANDLE_VALUE)
        WriteFile(Serial, data, bytes, &ignored, NULL);
    return result;
}
static HDC __stdcall Dg9ProbeOpen(const char *driver, const char *device, const char *output,
                                  const DEVMODEA *mode) {
    HANDLE file;
    (void)driver;
    (void)device;
    (void)output;
    (void)mode;
    file = CreateFileA("\\\\.\\DREAMGPU", 0, 0, NULL, OPEN_EXISTING, 0, NULL);
    return file == INVALID_HANDLE_VALUE ? NULL : (HDC)file;
}
static BOOL __stdcall Dg9ProbeClose(HDC dc) {
    return CloseHandle((HANDLE)dc);
}
static int __stdcall Dg9ProbeEscape(HDC dc, int code, int input_bytes, LPCSTR input,
                                    int output_bytes, LPSTR output) {
    DWORD returned = 0;
    return DeviceIoControl((HANDLE)dc, code, (void *)input, input_bytes, output, output_bytes,
                           &returned, NULL)
               ? 1
               : 0;
}
static int __stdcall Dg9ProbeMessage(HWND window, LPCSTR message, LPCSTR title, UINT flags) {
    (void)window;
    (void)message;
    (void)title;
    (void)flags;
    return IDOK;
}
#define WriteFile Dg9ProbeWrite
#define CreateDCA Dg9ProbeOpen
#define DeleteDC Dg9ProbeClose
#define ExtEscape Dg9ProbeEscape
#define MessageBoxA Dg9ProbeMessage
#define WinMainCRTStartup Dg9ChannelMain
#include "../nt/channel-probe.cpp"
#undef WriteFile
int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show) {
    DCB state;
    COMMTIMEOUTS timeouts;
    (void)instance;
    (void)previous;
    (void)command;
    (void)show;
    Serial = CreateFileA("COM1", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (Serial != INVALID_HANDLE_VALUE) {
        ZeroMemory(&state, sizeof(state));
        state.DCBlength = sizeof(state);
        if (GetCommState(Serial, &state)) {
            state.BaudRate = CBR_115200;
            state.ByteSize = 8;
            state.Parity = NOPARITY;
            state.StopBits = ONESTOPBIT;
            state.fOutxCtsFlow = state.fOutxDsrFlow = FALSE;
            state.fOutX = state.fInX = FALSE;
            SetCommState(Serial, &state);
        }
        ZeroMemory(&timeouts, sizeof(timeouts));
        timeouts.WriteTotalTimeoutConstant = 250;
        SetCommTimeouts(Serial, &timeouts);
    }
    Dg9ChannelMain();
    return 0;
}

} /* extern C */
