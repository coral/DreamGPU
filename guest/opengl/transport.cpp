/* SPDX-License-Identifier: GPL-2.0-or-later
 * Platform-specific kernel entry only. GL packet/state code stays shared.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "dg-escape.h"
#include "transport.h"

extern "C" {
static BOOL Windows9x;
static HANDLE Device = INVALID_HANDLE_VALUE;

void JglTransportInitialize(void) {
    Windows9x = (GetVersion() & 0x80000000UL) != 0;
}
int JglTransportRequest(HDC display, int input_bytes, LPCSTR input, int output_bytes,
                        LPSTR output) {
    DG_ESCAPE_REQUEST request;
    DG_ESCAPE_REPLY reply;
    DWORD returned = 0;
    BOOL ok;
    if (!Windows9x)
        return ExtEscape(display, DG_ESCAPE, input_bytes, input, output_bytes, output);
    if (!input || !output || input_bytes < (int)sizeof(request) ||
        output_bytes < (int)sizeof(DG_ESCAPE_REPLY))
        return 0;
    CopyMemory(&request, input, sizeof(request));
    /* OPEN/CLOSE are serialized by the frontend's process client lock.
     * Never silently reconnect a SUBMIT after loss of its owning handle. */
    if (request.Operation == DG_ESCAPE_OPEN && Device == INVALID_HANDLE_VALUE) {
        Device = CreateFileA("\\\\.\\DREAMGPU", 0, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (Device == INVALID_HANDLE_VALUE)
            return 0;
    }
    if (Device == INVALID_HANDLE_VALUE)
        return 0;
    ok = DeviceIoControl(Device, DG_ESCAPE, (void *)input, input_bytes, output, output_bytes,
                         &returned, NULL);
    if (!ok || returned < sizeof(DG_ESCAPE_REPLY) || returned > (DWORD)output_bytes)
        return 0;
    CopyMemory(&reply, output, sizeof(reply));
    if (reply.ResultBytes > (DWORD)output_bytes - sizeof(reply) ||
        returned != sizeof(reply) + reply.ResultBytes)
        return 0;
    if (request.Operation == DG_ESCAPE_CLOSE && reply.Version == DG_ESCAPE_VERSION &&
        reply.Status == DG_ESCAPE_OK && reply.Client == request.Client && !reply.ResultBytes &&
        !reply.ResultType) {
        CloseHandle(Device);
        Device = INVALID_HANDLE_VALUE;
    }
    return 1;
}

BOOL JglTransportIs9x(void) {
    return Windows9x;
}
HANDLE JglTransportDevice(void) {
    return Windows9x ? Device : INVALID_HANDLE_VALUE;
}

} /* extern C */
