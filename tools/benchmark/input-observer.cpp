/* SPDX-License-Identifier: GPL-2.0-or-later
 * Read-only guest foreground/key-state observation for host-origin input checks.
 * No window, keyboard hook, injected event, process attach or focus request.
 * The existing ntupdate serial probe exports C:\DGDRV.LOG after key activity
 * settles, with a bounded timeout when no input arrives.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern "C" void WINAPI WinMainCRTStartup() {
    HANDLE log = CreateFileA("C:\\DGDRV.LOG", GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE)
        ExitProcess(1);
    const BYTE keys[] = {'W',         'A',         'S',      'D',      VK_LSHIFT, VK_RSHIFT,
                         VK_LCONTROL, VK_RCONTROL, VK_LMENU, VK_RMENU, VK_LWIN,   VK_RWIN};
    const char header[] = "READ_ONLY_INPUT_OBSERVER\r\n"
                          "bits=W,A,S,D,LShift,RShift,LCtrl,RCtrl,LAlt,RAlt,LWin,RWin\r\n";
    DWORD wrote = 0;
    WriteFile(log, header, sizeof(header) - 1, &wrote, nullptr);
    const DWORD began = GetTickCount();
    DWORD previous = ~DWORD(0);
    HWND old_foreground = nullptr;
    unsigned changes = 0;
    bool activity = false;
    DWORD last_key = began;
    while (GetTickCount() - began < 60000) {
        DWORD bits = 0;
        for (unsigned i = 0; i < sizeof(keys); ++i)
            if (GetAsyncKeyState(keys[i]) & 0x8000)
                bits |= DWORD(1) << i;
        HWND foreground = GetForegroundWindow();
        if (bits) {
            activity = true;
            last_key = GetTickCount();
        }
        if (bits != previous || foreground != old_foreground) {
            DWORD pid = 0;
            GetWindowThreadProcessId(foreground, &pid);
            char klass[96]{};
            GetClassNameA(foreground, klass, sizeof(klass));
            char line[256];
            wsprintfA(line, "ms=%lu keys=%04lx foreground=%08lx pid=%lu class=%s\r\n",
                      GetTickCount() - began, bits, reinterpret_cast<DWORD>(foreground), pid,
                      klass);
            WriteFile(log, line, static_cast<DWORD>(lstrlenA(line)), &wrote, nullptr);
            FlushFileBuffers(log);
            previous = bits;
            old_foreground = foreground;
            if (++changes >= 512)
                break;
        }
        if (activity && !bits && GetTickCount() - last_key >= 2000)
            break;
        Sleep(10);
    }
    const char done[] = "DIAGNOSTIC_COMPLETE\r\n"
                        "PASS automated ntupdate: read-only observation completed; "
                        "input acceptance requires recorded edges and foreground\r\n";
    WriteFile(log, done, sizeof(done) - 1, &wrote, nullptr);
    CloseHandle(log);
    ExitProcess(changes < 512 ? 0 : 2);
}
