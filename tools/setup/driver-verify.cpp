// SPDX-License-Identifier: GPL-2.0-or-later
// Fixed fixture-only driver transaction gate. No arbitrary executable/path input.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "driver-lifecycle.h"
#include "sha256.h"
#ifndef DG_EXPECTED_INSTALLER_HASH
#error Exact installer artifact hash required
#endif
#ifndef DG_DRIVER_PHASE
#error Fixed verification phase required
#endif
namespace {
constexpr const char *Name = DG_DRIVER_PHASE == 0   ? "drvbind"
                             : DG_DRIVER_PHASE == 1 ? "drvcheck"
                                                    : "drvrestore";
constexpr const char *LogPath = DG_DRIVER_PHASE == 0   ? "C:\\DGDRVB.LOG"
                                : DG_DRIVER_PHASE == 1 ? "C:\\DGDRVC.LOG"
                                                       : "C:\\DGDRVR.LOG";
HANDLE Log = INVALID_HANDLE_VALUE;
void line(const char *s) {
    DWORD wrote;
    WriteFile(Log, s, lstrlenA(s), &wrote, nullptr);
    WriteFile(Log, "\r\n", 2, &wrote, nullptr);
    FlushFileBuffers(Log);
}
bool hash_file(const char *p, char out[65]) {
    HANDLE f = CreateFileA(p, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    DWORD high = 0, size = GetFileSize(f, &high), total = 0, bytes = 0;
    setup::Sha256 hash;
    BYTE buffer[4096];
    bool okay = !high && size <= 128u * 1024 * 1024;
    while (okay) {
        okay = ReadFile(f, buffer, sizeof(buffer), &bytes, nullptr) && bytes <= size - total;
        if (!okay || !bytes)
            break;
        hash.update(buffer, bytes);
        total += bytes;
    }
    CloseHandle(f);
    if (!okay || total != size)
        return false;
    hash.finish(out);
    return true;
}
bool execute(const char *args, DWORD &code) {
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    char command[96];
    wsprintfA(command, "\"C:\\dreamgpu.exe\" %s /silent", args);
    line(command);
    if (!CreateProcessA("C:\\dreamgpu.exe", command, nullptr, nullptr, FALSE, 0, nullptr, "C:\\",
                        &si, &pi))
        return false;
    CloseHandle(pi.hThread);
    DWORD wait = WaitForSingleObject(pi.hProcess, 115000);
    bool okay = wait == WAIT_OBJECT_0 && GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    if (okay) {
        char text[64];
        wsprintfA(text, "INSTALLER_EXIT %lu", code);
        line(text);
    }
    return okay;
}
bool journal(setup::driver::Journal &out) {
    char p[MAX_PATH];
    DWORD n = GetWindowsDirectoryA(p, sizeof(p));
    if (!n || n >= MAX_PATH - 30)
        return false;
    lstrcatA(p, "\\DreamGPU\\driver.bin");
    HANDLE f = CreateFileA(p, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    constexpr DWORD record = sizeof(out) + 64;
    DWORD size = GetFileSize(f, nullptr);
    bool okay = size >= record && size % record == 0 && size <= 16u * 1024 * 1024;
    char expected[65]{}, actual[65];
    DWORD got;
    okay = okay &&
           SetFilePointer(f, size - record, nullptr, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
           ReadFile(f, &out, sizeof(out), &got, nullptr) && got == sizeof(out) &&
           ReadFile(f, expected, 64, &got, nullptr) && got == 64;
    CloseHandle(f);
    if (!okay || !setup::driver::valid(out))
        return false;
    setup::Sha256 hash;
    hash.update(reinterpret_cast<const BYTE *>(&out), sizeof(out));
    hash.finish(actual);
    if (lstrcmpA(expected, actual))
        return false;
    line("ORIGINAL_INF");
    line(out.original.inf);
    line(out.original.inf_sha);
    line("ORIGINAL_PROVIDER");
    line(out.original.provider);
    line("JOURNAL_SHA256");
    line(actual);
    char text[80];
    wsprintfA(text, "DRIVER_PHASE %u ORIGINAL_DREAMGPU %u", unsigned(out.phase),
              unsigned(out.original_binding));
    line(text);
    for (unsigned k = 0; k < out.count; ++k) {
        const auto &file = out.files[k];
        line(file.path);
        if (hash_file(file.path, actual))
            line(actual);
        else
            line("FILE_ABSENT_OR_UNREADABLE");
    }
    return true;
}
bool run() {
    char hash[65];
    if (!hash_file("C:\\dreamgpu.exe", hash) || lstrcmpA(hash, DG_EXPECTED_INSTALLER_HASH))
        return false;
    line("INSTALLER_SHA256");
    line(hash);
    DWORD code = 0;
    if constexpr (DG_DRIVER_PHASE == 0) {
        char root[MAX_PATH];
        DWORD n = GetWindowsDirectoryA(root, sizeof(root));
        if (!n || n >= MAX_PATH - 16)
            return false;
        lstrcatA(root, "\\DreamGPU");
        DWORD attr = GetFileAttributesA(root);
        if (attr == INVALID_FILE_ATTRIBUTES) {
            if (GetLastError() != ERROR_FILE_NOT_FOUND || !execute("/stage", code) || code != 10)
                return false;
        }
        if (!execute("/driver-install", code) || code != 16)
            return false;
    } else if constexpr (DG_DRIVER_PHASE == 1) {
        if (!execute("/driver-resume", code) || (code != 14 && code != 15))
            return false;
    } else {
        if (!execute("/driver-restore", code) || (code != 16 && code != 15))
            return false;
    }
    static setup::driver::Journal j;
    if (!journal(j))
        return false;
    if constexpr (DG_DRIVER_PHASE == 0)
        return j.phase == setup::driver::Phase::pending_reboot;
    if constexpr (DG_DRIVER_PHASE == 1)
        return j.phase == setup::driver::Phase::verified ||
               j.phase == setup::driver::Phase::restored;
    return j.phase == setup::driver::Phase::restore_pending ||
           j.phase == setup::driver::Phase::restored;
}
} // namespace
extern "C" void WINAPI WinMainCRTStartup() {
    Log = CreateFileA(LogPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (Log == INVALID_HANDLE_VALUE)
        ExitProcess(2);
    bool okay = run();
    char diagnostic[MAX_PATH];
    DWORD length = GetWindowsDirectoryA(diagnostic, sizeof(diagnostic));
    if (length && length < MAX_PATH - 40) {
        lstrcatA(diagnostic, "\\DreamGPU\\driver-status.log");
        HANDLE source = CreateFileA(diagnostic, GENERIC_READ, FILE_SHARE_READ, nullptr,
                                    OPEN_EXISTING, 0, nullptr);
        if (source != INVALID_HANDLE_VALUE) {
            char buffer[4096];
            DWORD bytes = 0, wrote = 0;
            if (ReadFile(source, buffer, sizeof(buffer), &bytes, nullptr))
                WriteFile(Log, buffer, bytes, &wrote, nullptr);
            CloseHandle(source);
        }
    }
    char result[160];
    wsprintfA(result,
              "%s automated %s: driver transaction gate; system GPU readiness is not asserted",
              okay ? "PASS" : "FAIL", Name);
    line(result);
    CloseHandle(Log);
    ExitProcess(okay ? 0 : 1);
}
