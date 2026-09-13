/* Fixed clean poweroff for an owned disposable Win98/NT5 fixture. No forced exit. */
#define WIN32_LEAN_AND_MEAN
extern "C" {
#include <windows.h>
static HANDLE Log;
static void Record(const char *text) {
    DWORD written;
    WriteFile(Log, text, lstrlenA(text), &written, NULL);
    WriteFile(Log, "\r\n", 2, &written, NULL);
    FlushFileBuffers(Log);
}
void WINAPI WinMainCRTStartup(void) {
    HANDLE token;
    TOKEN_PRIVILEGES privileges;
    DWORD error;
    Log =
        CreateFileA("C:\\DGSTOP.LOG", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    if (Log == INVALID_HANDLE_VALUE)
        ExitProcess(1);
    if (!(GetVersion() & 0x80000000UL)) {
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
            goto failed;
        privileges.PrivilegeCount = 1;
        privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        if (!LookupPrivilegeValueA(NULL, SE_SHUTDOWN_NAME, &privileges.Privileges[0].Luid)) {
            CloseHandle(token);
            goto failed;
        }
        SetLastError(0);
        if (!AdjustTokenPrivileges(token, FALSE, &privileges, 0, NULL, NULL) ||
            GetLastError() != 0) {
            CloseHandle(token);
            goto failed;
        }
        CloseHandle(token);
    }
    Record("STAGE clean fixture poweroff; no force flags");
    if (!ExitWindowsEx(EWX_POWEROFF | ((GetVersion() & 0x80000000UL) ? EWX_SHUTDOWN : 0), 0))
        goto failed;
    Record("POWEROFF_ACCEPTED");
    CloseHandle(Log);
    ExitProcess(0);
failed:
    error = GetLastError();
    Record("FAIL clean fixture poweroff");
    CloseHandle(Log);
    ExitProcess(error ? error : 1);
}

} /* extern C */
