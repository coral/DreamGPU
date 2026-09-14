/* SPDX-License-Identifier: LGPL-2.1-or-later
 * DreamGPU virtual scanout query / interrupt-backed vertical blank wait.
 * Process-owned GDI/VxD handles are reclaimed by the OS on process exit.
 */
#ifndef DG_WINE_DISPLAY_TIMING_H
#define DG_WINE_DISPLAY_TIMING_H
#define DG_TIMING_U32 DWORD
#include "display-timing.h"
#undef DG_TIMING_U32
static BOOL dg_wine_display_timing(DWORD operation, DG_TIMING_REPLY *reply) {
    static LONG device_value, display_value;
    DG_TIMING_REQUEST request = {DG_TIMING_VERSION, 0, 0, 0};
    DWORD returned = 0;
    BOOL ok;
    request.Operation = operation;
    memset(reply, 0, sizeof(*reply));
    if (GetVersion() & 0x80000000u) {
        HANDLE device = (HANDLE)InterlockedCompareExchange(&device_value, 0, 0);
        if (!device) {
            HANDLE created = CreateFileA("\\\\.\\DREAMGPU", 0, 0, NULL, OPEN_EXISTING, 0, NULL);
            LONG previous;
            if (created == INVALID_HANDLE_VALUE)
                return FALSE;
            previous = InterlockedCompareExchange(&device_value, (LONG)created, 0);
            if (previous)
                CloseHandle(created);
            device = previous ? (HANDLE)previous : created;
        }
        ok = DeviceIoControl(device, DG_TIMING_ESCAPE, &request, sizeof(request), reply,
                             sizeof(*reply), &returned, NULL) &&
             returned == sizeof(*reply);
    } else {
        HDC display = (HDC)InterlockedCompareExchange(&display_value, 0, 0);
        if (!display) {
            HDC created = CreateDCA("DISPLAY", NULL, NULL, NULL);
            LONG previous;
            if (!created)
                return FALSE;
            previous = InterlockedCompareExchange(&display_value, (LONG)created, 0);
            if (previous)
                DeleteDC(created);
            display = previous ? (HDC)previous : created;
        }
        ok = ExtEscape(display, DG_TIMING_ESCAPE, sizeof(request), (const char *)&request,
                       sizeof(*reply), (char *)reply) > 0;
    }
    return ok && reply->Version == DG_TIMING_VERSION && reply->Status == DG_TIMING_REPLY_OK &&
           DgTimingRateValid(reply->RateHz) && reply->Height && reply->Height <= 16384 &&
           reply->ScanLine < reply->Height + DG_TIMING_BLANK_LINES && reply->InVBlank <= 1 &&
           reply->InVBlank == (reply->ScanLine >= reply->Height);
}
#endif
