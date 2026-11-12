/* SPDX-License-Identifier: GPL-2.0-or-later
 * Win9x DeviceIoControl for the DreamGPU VxD, retrying only its retryable
 * refusal. Shared by transport.cpp (DG_ESCAPE) and transport9-window.cpp
 * (DG9_WINDOW_IOCTL); both include <windows.h> first.
 *
 * Retrying is valid only because of the VxD's contract (dg-refusal9.h):
 * DG9_ERROR_BUSY means the VxD refused before executing any part of the
 * request, so resending the identical request on the same handle cannot
 * repeat a submission or a resource change. The refusal is transient (the
 * calling thread briefly owns the VMM critical section, the VMM is
 * reentered, or a delayed critical-section release is still pending), so
 * yield a few times, then sleep 1 ms, for at most about 1.5 s. After that
 * cap the caller fails exactly as for any other DeviceIoControl failure.
 * Every other error, including DG9_ERROR_INVALID, is returned at once.
 */
#ifndef DG_OPENGL_TRANSPORT9_RETRY_H
#define DG_OPENGL_TRANSPORT9_RETRY_H
#include "../win9x/dg-refusal9.h"
#define JGL9_REFUSAL_YIELDS 16UL /* Sleep(0) retries before Sleep(1) */
#define JGL9_REFUSAL_LIMIT_MS 1500UL
/* Backstop should GetTickCount not advance: each later retry sleeps >= 1 ms. */
#define JGL9_REFUSAL_RETRIES (JGL9_REFUSAL_YIELDS + JGL9_REFUSAL_LIMIT_MS)
static BOOL Jgl9DeviceControl(HANDLE device, DWORD code, void *input, DWORD input_bytes,
                              void *output, DWORD output_bytes, DWORD *returned) {
    DWORD start = 0, retries = 0;
    for (;;) {
        *returned = 0;
        if (DeviceIoControl(device, code, input, input_bytes, output, output_bytes, returned, NULL))
            return TRUE;
        if (GetLastError() != DG9_ERROR_BUSY)
            return FALSE;
        if (!retries)
            start = GetTickCount();
        else if (retries >= JGL9_REFUSAL_RETRIES || GetTickCount() - start >= JGL9_REFUSAL_LIMIT_MS)
            return FALSE; /* GetLastError() is still DG9_ERROR_BUSY */
        Sleep(retries < JGL9_REFUSAL_YIELDS ? 0 : 1);
        ++retries;
    }
}
#endif
