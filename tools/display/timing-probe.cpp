/* SPDX-License-Identifier: GPL-2.0-or-later
 * Bounded selected-refresh and context-free display timing probe.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>
#define DG_TIMING_U32 DWORD
#include "../../guest/include/display-timing.h"
#undef DG_TIMING_U32
static HANDLE log_file, device = INVALID_HANDLE_VALUE;
static HDC display;
static BOOL win9x;
static unsigned failures;
static void Log(const char *format, ...) {
    char text[768];
    va_list args;
    va_start(args, format);
    wvsprintfA(text, format, args);
    va_end(args);
    DWORD written;
    WriteFile(log_file, text, lstrlenA(text), &written, nullptr);
    FlushFileBuffers(log_file);
}
static bool Query(DWORD operation, DG_TIMING_REPLY &reply) {
    DG_TIMING_REQUEST request{DG_TIMING_VERSION, operation, 0, 0};
    reply = {};
    DWORD returned = 0;
    bool ok = win9x ? DeviceIoControl(device, DG_TIMING_ESCAPE, &request, sizeof(request), &reply,
                                      sizeof(reply), &returned, nullptr) &&
                          returned == sizeof(reply)
                    : ExtEscape(display, DG_TIMING_ESCAPE, sizeof(request), (const char *)&request,
                                sizeof(reply), (char *)&reply) > 0;
    Log("timing op=%lu ok=%u error=%lu version=%lu status=%lu Hz=%lu line=%lu height=%lu blank=%lu "
        "begin_ns=%lu end_ns=%lu\r\n",
        operation, ok, GetLastError(), reply.Version, reply.Status, reply.RateHz, reply.ScanLine,
        reply.Height, reply.InVBlank, reply.UntilBeginNs, reply.UntilEndNs);
    return ok && reply.Version == DG_TIMING_VERSION && !reply.Status &&
           DgTimingRateValid(reply.RateHz) && reply.Height && reply.InVBlank <= 1 &&
           reply.InVBlank == (reply.ScanLine >= reply.Height);
}
static bool Mode(DWORD rate) {
    DEVMODEA mode{};
    mode.dmSize = sizeof(mode);
    if (!EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &mode))
        return false;
    mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY;
    mode.dmDisplayFrequency = rate;
    LONG status = ChangeDisplaySettingsA(&mode, 0);
    DEVMODEA actual{};
    actual.dmSize = sizeof(actual);
    BOOL queried = EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &actual);
    Log("mode requested=%lu result=%ld queried=%u actual=%lux%lux%lu@%lu\r\n", rate, status,
        queried, actual.dmPelsWidth, actual.dmPelsHeight, actual.dmBitsPerPel,
        actual.dmDisplayFrequency);
    return status == DISP_CHANGE_SUCCESSFUL && queried && actual.dmDisplayFrequency == rate;
}
extern "C" void WINAPI WinMainCRTStartup(void) {
    log_file = CreateFileA("C:\\DGRATE.LOG", GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                           0, nullptr);
    if (log_file == INVALID_HANDLE_VALUE)
        ExitProcess(1);
    win9x = (GetVersion() & 0x80000000UL) != 0;
    display = CreateDCA("DISPLAY", nullptr, nullptr, nullptr);
    if (win9x)
        device = CreateFileA("\\\\.\\DREAMGPU", 0, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    DEVMODEA current{};
    current.dmSize = sizeof(current);
    if (!display || (win9x && device == INVALID_HANDLE_VALUE) ||
        !EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &current)) {
        Log("FAIL initialization error=%lu\r\n", GetLastError());
        ExitProcess(2);
    }
    DWORD found = 0;
    const DWORD rates[]{60, 75, 85, 100, 120};
    for (DWORD index = 0; index < 4096; ++index) {
        DEVMODEA mode{};
        mode.dmSize = sizeof(mode);
        if (!EnumDisplaySettingsA(nullptr, index, &mode))
            break;
        if (mode.dmPelsWidth != current.dmPelsWidth || mode.dmPelsHeight != current.dmPelsHeight ||
            mode.dmBitsPerPel != current.dmBitsPerPel)
            continue;
        Log("enumerated %lux%lux%lu@%lu\r\n", mode.dmPelsWidth, mode.dmPelsHeight,
            mode.dmBitsPerPel, mode.dmDisplayFrequency);
        for (DWORD r = 0; r < 5; ++r)
            if (mode.dmDisplayFrequency == rates[r])
                found |= 1UL << r;
    }
    if (found != 31)
        ++failures;
    DG_TIMING_REPLY reply;
    if (!Query(0, reply))
        ++failures;
    if (!Mode(120))
        ++failures;
    if (!Query(0, reply) || reply.RateHz != 120)
        ++failures;
    LARGE_INTEGER frequency{}, before{}, after{};
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&before);
    for (unsigned i = 0; i < 8; ++i)
        if (!Query(DG_TIMING_WAIT_BEGIN, reply) || reply.RateHz != 120)
            ++failures;
    QueryPerformanceCounter(&after);
    Log("eight-waits ticks_lo=%lu frequency_lo=%lu\r\n", (DWORD)(after.QuadPart - before.QuadPart),
        frequency.LowPart);
    if (!Query(DG_TIMING_WAIT_END, reply))
        ++failures;
    bool restored = Mode(60) && Query(0, reply) && reply.RateHz == 60;
    if (!restored)
        ++failures;
    Log("RESULT failures=%u rates_mask=%lu restored60=%u\r\n", failures, found, restored);
    if (device != INVALID_HANDLE_VALUE)
        CloseHandle(device);
    DeleteDC(display);
    CloseHandle(log_file);
    ExitProcess(failures ? 3 : 0);
}
