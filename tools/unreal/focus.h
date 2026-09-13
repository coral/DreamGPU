#include <string.h>
/* Fixed owned-window activation. Keep input queues independent so a hung game
 * cannot hang its supervisor. WM_NULL acknowledges the asynchronous activation
 * with a deadline; GetForegroundWindow remains the authoritative result.
 * https://devblogs.microsoft.com/oldnewthing/20161118-00/?p=94745 */
static BOOL ActivateOwnedViewport(HWND window, DWORD expectedPid) {
    DWORD pid = 0, thread;
    DWORD_PTR unused = 0;
    BOOL requested, acknowledged;
    thread = GetWindowThreadProcessId(window, &pid);
    if (!window || !expectedPid || pid != expectedPid || !thread || thread == GetCurrentThreadId())
        return FALSE;
    requested = GetForegroundWindow() == window ? TRUE : SetForegroundWindow(window);
    Record("SET_OWNED_VIEWPORT_FOREGROUND", requested);
    acknowledged = SendMessageTimeoutA(window, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000,
                                       &unused) != 0;
    Record("OWNED_VIEWPORT_ACTIVATION_ACK", acknowledged);
    pid = 0;
    return acknowledged && GetWindowThreadProcessId(window, &pid) == thread && pid == expectedPid &&
           GetForegroundWindow() == window;
}

/* Optional on Win98, exported on NT5. Grant only the child returned by our
 * suspended CreateProcess. A denied grant is evidence, never a reason to alter
 * the system foreground lock or attach another application's input queue. */
static void GrantOwnedForeground(DWORD pid) {
    typedef BOOL(WINAPI * ALLOW_FOREGROUND)(DWORD);
    HMODULE user = GetModuleHandleA("user32.dll");
    FARPROC address = user ? GetProcAddress(user, "AllowSetForegroundWindow") : NULL;
    ALLOW_FOREGROUND allow = NULL;
    /* Win32 exports use one function-pointer representation; copy its bits
     * without an incompatible function-type cast. */
    static_assert(sizeof(allow) == sizeof(address), "Win32 procedure pointer size");
    memcpy(&allow, &address, sizeof(allow));
    Record("OWNED_FOREGROUND_GRANT_AVAILABLE", allow != NULL);
    if (allow)
        Record("OWNED_FOREGROUND_GRANT", allow(pid));
}
