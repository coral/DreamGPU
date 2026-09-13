// SPDX-License-Identifier: GPL-2.0-or-later
// Read-only NT5 system runtime/WFP inventory. No protection policy or runtime
// is changed. Used before/after installer and reboot to identify actual files.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../d3d/entry.h"
#include "../setup/sha256.h"
namespace {
class Handle {
  public:
    HANDLE value = INVALID_HANDLE_VALUE;
    Handle() = default;
    Handle(const Handle &) = delete;
    ~Handle() {
        if (value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
};
class Module {
  public:
    HMODULE value = nullptr;
    Module() = default;
    Module(const Module &) = delete;
    ~Module() {
        if (value)
            FreeLibrary(value);
    }
};
class Log {
    Handle file;
    bool good = true;

  public:
    Log() {
        file.value = CreateFileA("C:\\DGRT.LOG", GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    bool valid() const {
        return good && file.value != INVALID_HANDLE_VALUE;
    }
    void line(const char *s) {
        DWORD count = 0;
        const auto size = static_cast<DWORD>(lstrlenA(s));
        good = valid() && WriteFile(file.value, s, size, &count, nullptr) && count == size;
        good = valid() && WriteFile(file.value, "\r\n", 2, &count, nullptr) && count == 2;
    }
    void number(const char *name, DWORD value) {
        line(name);
        char text[9];
        for (unsigned i = 0; i < 8; ++i)
            text[i] = "0123456789abcdef"[(value >> ((7 - i) * 4)) & 15];
        text[8] = 0;
        line(text);
    }
    bool flush() {
        return valid() && FlushFileBuffers(file.value);
    }
};
bool join(char (&out)[MAX_PATH], const char *directory, const char *name) {
    const int n = lstrlenA(directory), m = lstrlenA(name);
    if (n <= 0 || n + m + 2 > MAX_PATH)
        return false;
    CopyMemory(out, directory, n);
    unsigned end = static_cast<unsigned>(n);
    if (out[end - 1] != '\\')
        out[end++] = '\\';
    CopyMemory(out + end, name, m + 1);
    return true;
}
using IsProtected = BOOL(WINAPI *)(HANDLE, LPCWSTR);
bool inventory(Log &log, const char *directory, const char *name, IsProtected protected_file) {
    char path[MAX_PATH];
    if (!join(path, directory, name))
        return false;
    log.line(path);
    wchar_t wide[MAX_PATH];
    if (!MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, path, -1, wide, MAX_PATH))
        return false;
    log.number("protected", protected_file(nullptr, wide));
    Handle input;
    // Exclude concurrent writers while hashing; shared deletion is unnecessary.
    input.value = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (input.value == INVALID_HANDLE_VALUE) {
        const DWORD error = GetLastError();
        log.number("open_error", error);
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(input.value, &info) || info.nFileSizeHigh ||
        info.nFileSizeLow > 64u * 1024u * 1024u)
        return false;
    log.number("attributes", info.dwFileAttributes);
    log.number("size", info.nFileSizeLow);
    setup::Sha256 hash;
    BYTE data[4096];
    DWORD total = 0;
    while (total < info.nFileSizeLow) {
        DWORD count = 0;
        const DWORD left = info.nFileSizeLow - total;
        if (!ReadFile(input.value, data, left < sizeof(data) ? left : sizeof(data), &count,
                      nullptr) ||
            !count || count > left)
            return false;
        hash.update(data, count);
        total += count;
    }
    char digest[65];
    hash.finish(digest);
    log.line(digest);
    return true;
}
void policy(Log &log) {
    HKEY key = nullptr;
    const LONG opened = RegOpenKeyExA(HKEY_LOCAL_MACHINE,
                                      "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon",
                                      0, KEY_QUERY_VALUE, &key);
    if (opened != ERROR_SUCCESS) {
        log.number("policy_open_error", opened);
        return;
    }
    static constexpr const char *names[] = {"SFCDisable", "SFCScan"};
    for (const char *name : names) {
        DWORD type = 0, value = 0, size = sizeof(value);
        const LONG result =
            RegQueryValueExA(key, name, nullptr, &type, reinterpret_cast<BYTE *>(&value), &size);
        log.line(name);
        log.number("query_status", result);
        if (result == ERROR_SUCCESS && type == REG_DWORD && size == sizeof(value))
            log.number("value", value);
    }
    RegCloseKey(key);
    key = nullptr;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Control\\Session Manager", 0,
                      KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        static constexpr const char *session_names[] = {"AllowProtectedRenames",
                                                        "PendingFileRenameOperations"};
        for (const char *name : session_names) {
            DWORD type = 0, size = 0;
            const LONG status = RegQueryValueExA(key, name, nullptr, &type, nullptr, &size);
            log.line(name);
            log.number("query_status", status);
            log.number("bytes", size);
        }
        RegCloseKey(key);
    }
}
bool run(Log &log) {
    OSVERSIONINFOA os{};
    os.dwOSVersionInfoSize = sizeof(os);
    if (!GetVersionExA(&os) || os.dwPlatformId != VER_PLATFORM_WIN32_NT || os.dwMajorVersion != 5 ||
        os.dwMinorVersion > 1)
        return false;
    log.number("os_major", os.dwMajorVersion);
    log.number("os_minor", os.dwMinorVersion);
    log.number("os_build", os.dwBuildNumber);
    char system[MAX_PATH], path[MAX_PATH];
    const UINT n = GetSystemDirectoryA(system, MAX_PATH);
    if (!n || n >= MAX_PATH || !join(path, system, "sfc.dll"))
        return false;
    Module sfc;
    sfc.value = LoadLibraryA(path);
    if (!sfc.value)
        return false;
    const auto protected_file = Entry<IsProtected>(sfc.value, "SfcIsFileProtected");
    if (!protected_file)
        return false;
    policy(log);
    static constexpr const char *files[] = {"ddraw.dll",
                                            "d3d8.dll",
                                            "d3d9.dll",
                                            "opengl32.dll",
                                            "ddsys.dll",
                                            "msd3d8.dll",
                                            "msd3d9.dll",
                                            "winedd.dll",
                                            "wined8.dll",
                                            "wined9.dll",
                                            "wined3d.dll",
                                            "dgpugl.dll",
                                            "dgpuicd.dll",
                                            "sfc.dll",
                                            "sfc_os.dll",
                                            "sfcfiles.dll",
                                            "dllcache\\ddraw.dll",
                                            "dllcache\\d3d8.dll",
                                            "dllcache\\d3d9.dll"};
    for (const char *name : files)
        if (!inventory(log, system, name, protected_file))
            return false;
    return log.valid();
}
} // namespace
extern "C" void WINAPI WinMainCRTStartup() {
    DWORD result = 1;
    {
        Log log;
        if (log.valid()) {
            const bool ok = run(log);
            log.line(ok ? "PASS automated ntruntime: read-only system runtime inventory"
                        : "FAIL automated ntruntime: incomplete runtime inventory");
            if (log.flush() && ok)
                result = 0;
        }
    }
    ExitProcess(result);
}
