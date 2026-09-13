// SPDX-License-Identifier: GPL-2.0-or-later
// Fixed NT5 boot-rename feasibility test on an independent fixture. This is not
// the production installer: it changes only system DDRAW from two manifest-pinned
// diagnostic inputs. A durable receipt is written before enqueueing. No WFP DLL,
// runtime cache, or persistent SFC policy is changed. Post-boot runtime inventory
// supplies the actual acceptance evidence; enqueue success alone is insufficient.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../setup/sha256.h"
namespace {
constexpr const char *Session = "SYSTEM\\CurrentControlSet\\Control\\Session Manager";
constexpr const char *Pending = "PendingFileRenameOperations";
constexpr const char *Allow = "AllowProtectedRenames";
#ifdef DG_RESTORE_RUNTIME
constexpr const char *Source = "C:\\DGNTTEST\\original.dll";
constexpr const char *Before = "C:\\DGNTTEST\\wine.dll";
constexpr const char *LogPath = "C:\\DGRS.LOG";
constexpr const char *Passed =
    "PASS automated ntrestore: exact DDRAW replacement queued; reboot required";
#else
constexpr const char *Source = "C:\\DGNTTEST\\wine.dll";
constexpr const char *Before = "C:\\DGNTTEST\\original.dll";
constexpr const char *LogPath = "C:\\DGRP.LOG";
constexpr const char *Passed =
    "PASS automated ntrename: exact DDRAW replacement queued; reboot required";
#endif
constexpr const char *Staged = "C:\\DGNTTEST\\boot.dll";
class File {
  public:
    HANDLE h = INVALID_HANDLE_VALUE;
    File() = default;
    File(const File &) = delete;
    ~File() {
        if (h != INVALID_HANDLE_VALUE)
            CloseHandle(h);
    }
};
class Key {
  public:
    HKEY h = nullptr;
    Key() = default;
    Key(const Key &) = delete;
    ~Key() {
        if (h)
            RegCloseKey(h);
    }
};
class Log {
    File file;
    bool ok_ = true;

  public:
    Log() {
        file.h = CreateFileA(LogPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    bool ok() const {
        return ok_ && file.h != INVALID_HANDLE_VALUE;
    }
    void line(const char *text) {
        DWORD count = 0, size = static_cast<DWORD>(lstrlenA(text));
        ok_ = ok() && WriteFile(file.h, text, size, &count, nullptr) && size == count;
        ok_ = ok() && WriteFile(file.h, "\r\n", 2, &count, nullptr) && count == 2;
    }
    bool flush() {
        return ok() && FlushFileBuffers(file.h);
    }
};
bool digest(const char *path, char (&hash)[65]) {
    File file;
    file.h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file.h == INVALID_HANDLE_VALUE)
        return false;
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(file.h, &info) || info.nFileSizeHigh || !info.nFileSizeLow ||
        info.nFileSizeLow > 16u * 1024u * 1024u ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
        return false;
    setup::Sha256 state;
    BYTE bytes[4096];
    DWORD total = 0;
    while (total < info.nFileSizeLow) {
        DWORD count = 0, left = info.nFileSizeLow - total;
        if (!ReadFile(file.h, bytes, left < sizeof(bytes) ? left : sizeof(bytes), &count,
                      nullptr) ||
            !count || count > left)
            return false;
        state.update(bytes, count);
        total += count;
    }
    state.finish(hash);
    return true;
}
bool absent(HKEY key, const char *name) {
    const LONG status = RegQueryValueExA(key, name, nullptr, nullptr, nullptr, nullptr);
    return status == ERROR_FILE_NOT_FOUND;
}
bool run(Log &log) {
    OSVERSIONINFOA os{};
    os.dwOSVersionInfoSize = sizeof(os);
    if (!GetVersionExA(&os) || os.dwPlatformId != VER_PLATFORM_WIN32_NT || os.dwMajorVersion != 5 ||
        os.dwMinorVersion > 1)
        return false;
    File mutex;
    mutex.h = CreateMutexA(nullptr, TRUE, "Global\\DreamGPU.RuntimeRenameProbe");
    if (!mutex.h || GetLastError() == ERROR_ALREADY_EXISTS)
        return false;
    Key key;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, Session, 0, KEY_QUERY_VALUE | KEY_SET_VALUE, &key.h))
        return false;
    // Do not widen permission for someone else's queued operation or policy.
    if (!absent(key.h, Pending) || !absent(key.h, "PendingFileRenameOperations2") ||
        !absent(key.h, Allow)) {
        log.line("FAIL foreign pending rename or permission value");
        return false;
    }
    char destination[MAX_PATH];
    UINT n = GetSystemDirectoryA(destination, MAX_PATH);
    if (!n || n + 11 >= MAX_PATH)
        return false;
    lstrcatA(destination, "\\ddraw.dll");
    char from[65], before[65], current[65], staged[65];
    if (!digest(Source, from) || !digest(Before, before) || !digest(destination, current) ||
        lstrcmpA(current, before) || !lstrcmpA(from, before)) {
        log.line("FAIL input or current runtime identity");
        return false;
    }
    log.line("destination");
    log.line(destination);
    log.line("before_sha256");
    log.line(before);
    log.line("after_sha256");
    log.line(from);
    if (!CopyFileA(Source, Staged, TRUE))
        return false;
    if (!SetFileAttributesA(Staged, FILE_ATTRIBUTE_NORMAL))
        return false;
    {
        File out;
        out.h = CreateFileA(Staged, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0,
                            nullptr);
        if (out.h == INVALID_HANDLE_VALUE || !FlushFileBuffers(out.h))
            return false;
    }
    if (!digest(Staged, staged) || lstrcmpA(staged, from))
        return false;
    log.line("STAGE durable payload and pending-operation intent");
    if (!log.flush())
        return false;
    // Windows writes the canonical native source/destination pair. Read it back
    // before setting the one-boot permission; no global persistent SFC disable.
    if (!absent(key.h, Pending) || !absent(key.h, Allow) ||
        !MoveFileExA(Staged, destination, MOVEFILE_DELAY_UNTIL_REBOOT | MOVEFILE_REPLACE_EXISTING))
        return false;
    wchar_t expected[MAX_PATH * 2 + 16]{};
    unsigned used = 0;
    const wchar_t *prefix = L"\\??\\";
    for (unsigned i = 0; prefix[i]; ++i)
        expected[used++] = prefix[i];
    const int first =
        MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, Staged, -1, expected + used, MAX_PATH);
    if (!first)
        return false;
    used += first;
    expected[used++] = L'!';
    for (unsigned i = 0; prefix[i]; ++i)
        expected[used++] = prefix[i];
    const int second = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, destination, -1,
                                           expected + used, MAX_PATH);
    if (!second)
        return false;
    used += second;
    expected[used++] = 0;
    wchar_t actual[MAX_PATH * 2 + 16]{};
    DWORD type = 0, size = sizeof(actual);
    if (RegQueryValueExW(key.h, L"PendingFileRenameOperations", nullptr, &type,
                         reinterpret_cast<BYTE *>(actual), &size) ||
        type != REG_MULTI_SZ || size != used * sizeof(wchar_t)) {
        log.line("FAIL pending representation");
        return false;
    }
    for (unsigned i = 0; i < used; ++i)
        if (actual[i] != expected[i]) {
            log.line("FAIL foreign queued operation");
            return false;
        }
    const DWORD one = 1;
    if (!absent(key.h, Allow) ||
        RegSetValueExA(key.h, Allow, 0, REG_DWORD, reinterpret_cast<const BYTE *>(&one),
                       sizeof(one)) ||
        RegFlushKey(key.h))
        return false;
    DWORD check = 0;
    size = sizeof(check);
    type = 0;
    if (RegQueryValueExA(key.h, Allow, nullptr, &type, reinterpret_cast<BYTE *>(&check), &size) ||
        type != REG_DWORD || size != sizeof(check) || check != 1)
        return false;
    log.line("STAGE exact pair and one-boot permission persisted");
    return true;
}
} // namespace
extern "C" void WINAPI WinMainCRTStartup() {
    DWORD result = 1;
    {
        Log log;
        if (log.ok()) {
            const bool ok = run(log);
            log.line(ok ? Passed : "FAIL runtime boot rename; inspect receipt before reboot");
            if (log.flush() && ok)
                result = 0;
        }
    }
    ExitProcess(result);
}
