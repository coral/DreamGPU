/* SPDX-License-Identifier: GPL-2.0-or-later
 * Ask the fingerprint-verified retail engine to exit through its own WLog command.
 * Forced disposal bounds a failed test; it never establishes a clean exit.
 */
#ifndef DREAMGPU_UT_CLEANUP_H
#define DREAMGPU_UT_CLEANUP_H
static BOOL CleanupOwnedGame(HANDLE process, DWORD pid, HWND log) {
    DWORD status = WaitForSingleObject(process, 0), code = 0, owner = 0;
    if (status == WAIT_TIMEOUT) {
        if (log && GetWindowThreadProcessId(log, &owner) && owner == pid && RetailLogClass(log) &&
            PostMessageA(log, WM_COMMAND, 40004, 0)) {
            Record("CLEANUP_ENGINE_QUIT_REQUESTED", pid);
            status = WaitForSingleObject(process, 15000);
        } else {
            Record("FAIL CLEANUP_CLOSE_REQUEST", GetLastError());
        }
    }
    if (status == WAIT_OBJECT_0) {
        if (GetExitCodeProcess(process, &code) && code == 0) {
            Record("CLEANUP_GRACEFUL", code);
            return TRUE;
        }
        Record("FAIL CLEANUP_ENGINE_EXIT", code);
        return FALSE;
    }
    Record("FAIL CLEANUP_GRACEFUL_WAIT", status);
    CleanupFailureEvidence(process, pid);
    if (status == WAIT_TIMEOUT) {
        const BOOL killed = TerminateProcess(process, 125);
        const DWORD stopped = WaitForSingleObject(process, 3000);
        Record("CLEANUP_OWNED_TERMINATE", stopped);
        if (!killed && stopped != WAIT_OBJECT_0)
            Record("FAIL CLEANUP_TERMINATE", GetLastError());
    }
    return FALSE;
}
#endif
