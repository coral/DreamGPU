// SPDX-License-Identifier: GPL-2.0-or-later
// Fixed diagnostic fixture bootstrap; never enables installer readiness.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../setup/sha256.h"
namespace {
#ifdef DG_ICD_INSPECT
constexpr bool Inspect = true;
#else
constexpr bool Inspect = false;
#endif
constexpr char KeyPath[] = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\OpenGLDrivers";
constexpr char ValueName[] = "DGPUICD";
constexpr char Provider[] = "dgpuicd.dll";
constexpr char BackupPath[] = "C:\\DGICD.BAK";
constexpr char DisabledPath[] = "C:\\DGICD.OFF";
struct File {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~File() {
        if (value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
    File() = default;
    File(const File &) = delete;
};
struct Key {
    HKEY value = nullptr;
    ~Key() {
        if (value)
            RegCloseKey(value);
    }
    Key() = default;
    Key(const Key &) = delete;
};
struct Original {
    DWORD magic = 0x31494344, key = 0, exists = 0, type = 0, size = 0;
    BYTE bytes[512] = {};
    char sha[68] = {};
};
bool equal(const void *a, const void *b, DWORD bytes) {
    const auto *x = static_cast<const BYTE *>(a), *y = static_cast<const BYTE *>(b);
    for (DWORD n = 0; n < bytes; ++n)
        if (x[n] != y[n])
            return false;
    return true;
}
void hash(const Original &value, char out[68]) {
    setup::Sha256 h;
    h.update(reinterpret_cast<const BYTE *>(&value), offsetof(Original, sha));
    h.finish(out);
}
bool read_value(Original &out) {
    out = {};
    Key key;
    LONG rc = RegOpenKeyExA(HKEY_LOCAL_MACHINE, KeyPath, 0, KEY_QUERY_VALUE, &key.value);
    if (rc == ERROR_FILE_NOT_FOUND)
        return true;
    if (rc)
        return false;
    out.key = 1;
    out.size = sizeof(out.bytes);
    rc = RegQueryValueExA(key.value, ValueName, nullptr, &out.type, out.bytes, &out.size);
    if (rc == ERROR_FILE_NOT_FOUND) {
        out.type = out.size = 0;
        return true;
    }
    if (rc || out.size > sizeof(out.bytes))
        return false;
    out.exists = 1;
    return true;
}
bool desired(const Original &value) {
    return value.exists && value.type == REG_SZ && value.size == sizeof(Provider) &&
           value.bytes[value.size - 1] == 0 &&
           !lstrcmpiA(reinterpret_cast<const char *>(value.bytes), Provider);
}
bool same(const Original &a, const Original &b) {
    return a.exists == b.exists &&
           (!a.exists || (a.type == b.type && a.size == b.size && equal(a.bytes, b.bytes, a.size)));
}
bool backup(Original &original) {
    File file;
    file.value =
        CreateFileA(BackupPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file.value != INVALID_HANDLE_VALUE) {
        DWORD bytes = 0;
        if (GetFileSize(file.value, nullptr) != sizeof(original) ||
            !ReadFile(file.value, &original, sizeof(original), &bytes, nullptr) ||
            bytes != sizeof(original))
            return false;
        char expected[68] = {};
        hash(original, expected);
        return original.magic == 0x31494344 && original.key <= 1 &&
               original.exists <= original.key && original.size <= sizeof(original.bytes) &&
               equal(original.sha, expected, sizeof(expected)) &&
               (!original.exists || desired(original));
    }
    if (GetLastError() != ERROR_FILE_NOT_FOUND || !read_value(original))
        return false;
    // A different provider owns this name. Never adopt or replace it.
    if (original.exists && !desired(original))
        return false;
    hash(original, original.sha);
    file.value = CreateFileA(BackupPath, GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD bytes = 0;
    return file.value != INVALID_HANDLE_VALUE &&
           WriteFile(file.value, &original, sizeof(original), &bytes, nullptr) &&
           bytes == sizeof(original) && FlushFileBuffers(file.value);
}
bool read_backup(Original &original) {
    if (GetFileAttributesA(BackupPath) == INVALID_FILE_ATTRIBUTES)
        return false;
    return backup(original);
}
bool win98() {
    DWORD version = GetVersion();
    if (!(version & 0x80000000UL) || LOBYTE(LOWORD(version)) != 4 || HIBYTE(LOWORD(version)) != 10)
        return false;
    return true;
}
void diagnostic(HANDLE log, const char *label, DWORD value) {
    DWORD written;
    WriteFile(log, label, lstrlenA(label), &written, nullptr);
    char hex[11] = {};
    for (unsigned n = 0; n < 8; ++n)
        hex[n] = "0123456789abcdef"[(value >> (28 - n * 4)) & 15];
    hex[8] = '\r';
    hex[9] = '\n';
    WriteFile(log, hex, 10, &written, nullptr);
    FlushFileBuffers(log);
}
bool active_diagnostic(HANDLE log) {
    // The Win16 display escape uses ANSI270 bytes, not the NT wide532 ABI.
    struct Info {
        DWORD version, driver;
        char name[262];
    };
    static_assert(offsetof(Info, name) == 8);
    BYTE output[532] = {};
    HDC dc = GetDC(nullptr);
    if (!dc)
        return false;
    int result = Escape(dc, 0x1101, 0, nullptr, output);
    Info info{};
    CopyMemory(&info, output, 270);
    diagnostic(log, "OS_VERSION ", GetVersion());
    diagnostic(log, "ESCAPE_RESULT ", result);
    diagnostic(log, "ESCAPE_VERSION ", info.version);
    diagnostic(log, "ESCAPE_DRIVER ", info.driver);
    DWORD name = 0;
    CopyMemory(&name, info.name, 4);
    diagnostic(log, "ESCAPE_NAME_FIRST4 ", name);
    BYTE extended[270] = {};
    DWORD query = 0;
    int ext = ExtEscape(dc, 0x1101, sizeof(query), reinterpret_cast<const char *>(&query),
                        sizeof(extended), reinterpret_cast<char *>(extended));
    Info extended_info{};
    CopyMemory(&extended_info, extended, 270);
    diagnostic(log, "EXTESCAPE_RESULT ", ext);
    diagnostic(log, "EXTESCAPE_VERSION ", extended_info.version);
    diagnostic(log, "EXTESCAPE_DRIVER ", extended_info.driver);
    CopyMemory(&name, extended_info.name, 4);
    diagnostic(log, "EXTESCAPE_NAME_FIRST4 ", name);
    ReleaseDC(nullptr, dc);
    // Win98's generic Escape thunk rejects this private escape. ExtEscape
    // supplies the output capacity required to marshal the Win16 reply.
    return ext > 0 && extended_info.version == 2 && extended_info.driver == 1 &&
           equal(extended_info.name, "DGPUICD", sizeof("DGPUICD"));
}
bool restore(const Original &original) {
    Original current;
    if (!read_value(current) || (!same(current, original) && !desired(current)))
        return false;
    // Durable disabling marker prevents a reboot from re-registering after a
    // partial restore. The fixed /restore command can finish interrupted work.
    File off;
    off.value = CreateFileA(DisabledPath, GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL, nullptr);
    if (off.value == INVALID_HANDLE_VALUE || !FlushFileBuffers(off.value))
        return false;
    Key key;
    LONG rc =
        RegOpenKeyExA(HKEY_LOCAL_MACHINE, KeyPath, 0, KEY_SET_VALUE | KEY_QUERY_VALUE, &key.value);
    if (rc != ERROR_FILE_NOT_FOUND && rc)
        return false;
    if (!rc) {
        rc = original.exists ? RegSetValueExA(key.value, ValueName, 0, original.type,
                                              original.bytes, original.size)
                             : RegDeleteValueA(key.value, ValueName);
        if ((rc && rc != ERROR_FILE_NOT_FOUND) || RegFlushKey(key.value))
            return false;
        if (!original.key) {
            // Win9x RegDeleteKey can delete a subtree. Never delete a parent
            // containing another writer's values or subkeys.
            DWORD subkeys = 0, values = 0;
            if (RegQueryInfoKeyA(key.value, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr,
                                 &values, nullptr, nullptr, nullptr, nullptr) ||
                subkeys || values)
                return false;
            RegCloseKey(key.value);
            key.value = nullptr;
            rc = RegDeleteKeyA(HKEY_LOCAL_MACHINE, KeyPath);
            if (rc && rc != ERROR_FILE_NOT_FOUND)
                return false;
        }
    }
    Original result;
    return read_value(result) && same(result, original) && result.key == original.key;
}
bool apply(const Original &original) {
    Original current;
    if (!read_value(current) || (!same(current, original) && !desired(current)))
        return false;
    Key key;
    DWORD disposition;
    if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, KeyPath, 0, nullptr, 0, KEY_SET_VALUE, nullptr,
                        &key.value, &disposition) ||
        RegSetValueExA(key.value, ValueName, 0, REG_SZ, reinterpret_cast<const BYTE *>(Provider),
                       sizeof(Provider)) ||
        RegFlushKey(key.value))
        return false;
    Original result;
    return read_value(result) && desired(result);
}
const char *arguments() {
    const char *p = GetCommandLineA();
    if (*p == '"') {
        ++p;
        while (*p && *p != '"')
            ++p;
        if (*p)
            ++p;
    } else
        while (*p && *p != ' ' && *p != '\t')
            ++p;
    while (*p == ' ' || *p == '\t')
        ++p;
    return p;
}
DWORD body() {
    File log;
    log.value = CreateFileA("C:\\DGICDBT.LOG", GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log.value == INVALID_HANDLE_VALUE)
        return 2;
    auto finish = [&](const char *message, DWORD code) {
        DWORD bytes;
        WriteFile(log.value, message, lstrlenA(message), &bytes, nullptr);
        FlushFileBuffers(log.value);
        return code;
    };
    const char *args = arguments();
    bool removal = !lstrcmpA(args, "/restore");
    bool registration = !lstrcmpA(args, "/register");
    if (*args && !removal && !registration)
        return finish("FAIL fixed command requires no args, /register or /restore\r\n", 2);
    if (!win98())
        return finish("FAIL requires Windows 98\r\n", 3);
    bool active = removal ? true : active_diagnostic(log.value);
    diagnostic(log.value, "ACTIVE_DIAGNOSTIC ", active ? 1 : 0);
    if ((!Inspect && !active) || (Inspect && removal))
        return finish("FAIL Win98 active diagnostic ICD escape\r\n", 3);
    if (!Inspect) {
        Original original;
        if (removal) {
            if (!read_backup(original) || !restore(original))
                return finish("FAIL diagnostic registration restore; prior journal preserved\r\n",
                              4);
            return finish("PASS diagnostic registration restored; future bootstrap disabled; not "
                          "production installation\r\n",
                          0);
        }
        if (GetFileAttributesA(DisabledPath) != INVALID_FILE_ATTRIBUTES ||
            GetLastError() != ERROR_FILE_NOT_FOUND)
            return finish("FAIL diagnostic bootstrap disabled or marker inaccessible\r\n", 5);
        if (!backup(original) || !apply(original))
            return finish("FAIL diagnostic registration; exact prior backup preserved\r\n", 6);
    }
    if (registration)
        return finish("PASS diagnostic registration staged; system pixel proof pending; "
                      "production_ready=false\r\n",
                      0);
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    char command[] = "C:\\DGPUBEN.EXE";
    if (!CreateProcessA(command, command, nullptr, nullptr, FALSE, 0, nullptr, "C:\\", &startup,
                        &process))
        return finish("FAIL registration staged but fixed runner launch failed\r\n", 7);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    if (Inspect)
        return finish(
            "PASS read-only diagnostic discovery and runner launched; registry unchanged\r\n", 0);
    return finish("PASS diagnostic registration staged and runner launched; system pixel proof "
                  "pending; production_ready=false\r\n",
                  0);
}
} // namespace
extern "C" void WINAPI WinMainCRTStartup() {
    ExitProcess(body());
}
