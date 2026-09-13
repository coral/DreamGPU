/* SPDX-License-Identifier: GPL-2.0-or-later
 * Failure-only retail loader oracle. Names match the original authorized
 * retail HL.EXE import table. The caller cannot supply a path or command.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static HANDLE log_file;
static const char *modules[] = {
    "WINMM.dll",   "DDRAW.dll",    "WONAu_W95.dll", "WONCr_W95.dll", "KERNEL32.dll", "USER32.dll",
    "GDI32.dll",   "comdlg32.dll", "WINSPOOL.DRV",  "ADVAPI32.dll",  "SHELL32.dll",  "COMCTL32.dll",
    "WSOCK32.dll", "SHLWAPI.dll",  "mpr.dll",       "mprui.dll",     "comdlg32.dll"};
static void Log(const char *text) {
    DWORD written;
    WriteFile(log_file, text, lstrlenA(text), &written, NULL);
    FlushFileBuffers(log_file);
}
int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show) {
    HMODULE loaded[sizeof(modules) / sizeof(modules[0])];
    DWORD i, error;
    char line[256];
    BOOL failed = FALSE;
    (void)instance;
    (void)previous;
    (void)command;
    (void)show;
    log_file = CreateFileA("C:\\DGLOAD.LOG", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (log_file == INVALID_HANDLE_VALUE)
        return 2;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    {
        DWORD slots[64], count = 0;
        while (count < 64) {
            slots[count] = TlsAlloc();
            if (slots[count] == TLS_OUT_OF_INDEXES)
                break;
            ++count;
        }
        wsprintfA(line, "TLS available bounded64=%lu\r\n", count);
        Log(line);
        while (count)
            TlsFree(slots[--count]);
    }
    for (i = 0; i < sizeof(modules) / sizeof(modules[0]); ++i) {
        wsprintfA(line, "LOAD begin %s\r\n", modules[i]);
        Log(line);
        if (i == sizeof(modules) / sizeof(modules[0]) - 1)
            SetErrorMode(0);
        SetLastError(0);
        loaded[i] = LoadLibraryA(modules[i]);
        error = GetLastError();
        wsprintfA(line, "LOAD result %s handle=%08lx error=%lu\r\n", modules[i], (DWORD)loaded[i],
                  error);
        Log(line);
        if (!loaded[i]) {
            if (i < 13)
                failed = TRUE;
            else
                Log("DIAGNOSTIC optional non-retail module failed\r\n");
        } else {
            char path[MAX_PATH];
            if (GetModuleFileNameA(loaded[i], path, sizeof(path))) {
                Log("LOAD path ");
                Log(path);
                Log("\r\n");
            }
        }
    }
    for (i = sizeof(modules) / sizeof(modules[0]); i; --i)
        if (loaded[i - 1])
            FreeLibrary(loaded[i - 1]);
    Log(failed ? "FAIL static retail DLL dependency load\r\n"
               : "PASS automated loader: all original retail static DLLs load\r\n");
    CloseHandle(log_file);
    return failed ? 1 : 0;
}
