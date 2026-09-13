/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Local logon can precede first-boot NT serial enumeration, or Win98 serial
 * detection can temporarily own COM1. Wait for these transient startup states
 * once, with a fixed deadline.
 * Normal command processing remains a blocking ReadFile loop. */
static HANDLE OpenStartupSerial(void) {
    DWORD begin = GetTickCount(), error;
    HANDLE serial;
    for (;;) {
        serial = CreateFileA("COM1", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (serial != INVALID_HANDLE_VALUE)
            return serial;
        error = GetLastError();
        if ((error != ERROR_ACCESS_DENIED && error != ERROR_SHARING_VIOLATION &&
             error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) ||
            (DWORD)(GetTickCount() - begin) >= 20000) {
            SetLastError(error);
            return INVALID_HANDLE_VALUE;
        }
        Sleep(250);
    }
}
