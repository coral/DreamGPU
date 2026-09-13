// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#ifndef DG_BOOT_QUEUE_TEST
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "boot-queue.h"
namespace setup::boot {
// Store owns crash-safe record publication, exact file hashes, durable staging,
// and the manifest allowlist. No backup or staged payload is deleted here.
// MoveFileEx documents enqueue success separately from post-boot success:
// https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw
// AllowProtectedRenames is an NT5 mechanism separately runtime-gated on each OS;
// no SFCDisable or dllcache mutation is made by this adapter.
template <class Store> class Win32Ops {
    Store &store_;
    HKEY key_ = {};
    bool protected_renames_verified_ = false;
    using Query = LONG(WINAPI *)(HKEY, const wchar_t *, DWORD *, DWORD *, BYTE *, DWORD *);
    using Set = LONG(WINAPI *)(HKEY, const wchar_t *, DWORD, DWORD, const BYTE *, DWORD);
    using Delete = LONG(WINAPI *)(HKEY, const wchar_t *);
    using Move = BOOL(WINAPI *)(const wchar_t *, const wchar_t *, DWORD);
    Query query_ = nullptr;
    Set set_ = nullptr;
    Delete delete_ = nullptr;
    Move move_ = nullptr;
    template <class Function> static bool resolve(Function &out, HMODULE module, const char *name) {
        FARPROC address = GetProcAddress(module, name);
        if (!address)
            return false;
        static_assert(sizeof(out) == sizeof(address), "Windows function pointer ABI");
        auto *to = reinterpret_cast<unsigned char *>(&out);
        const auto *from = reinterpret_cast<const unsigned char *>(&address);
        for (unsigned i = 0; i < sizeof(out); ++i)
            to[i] = from[i];
        return true;
    }

    static constexpr const char *Session = "SYSTEM\\CurrentControlSet\\Control\\Session Manager";
    static constexpr const wchar_t *Pending = L"PendingFileRenameOperations";
    static constexpr const wchar_t *Allow = L"AllowProtectedRenames";
    static bool local_path(const char *p) {
        if (!text(p, path_capacity) ||
            !((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) || p[1] != ':' ||
            p[2] != '\\')
            return false;
        unsigned component = 3;
        for (unsigned i = 3;; ++i) {
            char c = p[i];
            if (c == '/' || c == ':' || c == '"' || c == '*' || c == '?' ||
                (c && (unsigned char)c < 32))
                return false;
            if (!c || c == '\\') {
                unsigned size = i - component;
                if (!size || p[i - 1] == '.' || p[i - 1] == ' ')
                    return false;
                component = i + 1;
                if (!c)
                    return true;
            }
        }
    }

  public:
    explicit Win32Ops(Store &store) : store_(store) {}
    Win32Ops(const Win32Ops &) = delete;
    ~Win32Ops() {
        if (key_)
            RegCloseKey(key_);
    }
    bool open(bool protected_renames_verified) {
        if (key_)
            return false;
        OSVERSIONINFOA os{};
        os.dwOSVersionInfoSize = sizeof(os);
        if (!GetVersionExA(&os) || os.dwPlatformId != VER_PLATFORM_WIN32_NT ||
            os.dwMajorVersion != 5 || os.dwMinorVersion > 1)
            return false;
        // Resolve NT-only Unicode exports after OS detection. The same installer
        // remains loadable on Win98 without importing those entry points.
        HMODULE kernel = GetModuleHandleA("kernel32.dll");
        HMODULE registry = GetModuleHandleA("advapi32.dll");
        if (!kernel || !registry || !resolve(move_, kernel, "MoveFileExW") ||
            !resolve(query_, registry, "RegQueryValueExW") ||
            !resolve(set_, registry, "RegSetValueExW") ||
            !resolve(delete_, registry, "RegDeleteValueW"))
            return false;
        protected_renames_verified_ = protected_renames_verified;
        return RegOpenKeyExA(HKEY_LOCAL_MACHINE, Session, 0, KEY_QUERY_VALUE | KEY_SET_VALUE,
                             &key_) == ERROR_SUCCESS;
    }
    bool save(const Record &r) {
        return store_.save(r);
    }
    bool inspect(const char *p, File &out) {
        return store_.inspect(p, out);
    }
    bool approved(const Entry &e) {
        if (!key_ || !local_path(e.destination) ||
            (e.desired.exists &&
             (!local_path(e.source) || lstrcmpiA(e.source, e.destination) == 0)) ||
            !store_.approved(e))
            return false;
        char system[path_capacity]{};
        UINT n = GetSystemDirectoryA(system, path_capacity);
        if (!n || n >= path_capacity || system[n])
            return false;
        unsigned length = 0;
        while (e.destination[length] && length < n)
            ++length;
        if (length != n || e.destination[n] != '\\')
            return false;
        char prefix[path_capacity]{};
        for (unsigned i = 0; i < n; ++i)
            prefix[i] = e.destination[i];
        if (lstrcmpiA(prefix, system))
            return false;
        const char *name = e.destination + n + 1;
        for (unsigned i = 0; name[i]; ++i)
            if (name[i] == '\\')
                return false;
        bool protected_target = !lstrcmpiA(name, "ddraw.dll") || !lstrcmpiA(name, "d3d8.dll") ||
                                !lstrcmpiA(name, "d3d9.dll");
        if (bool(e.protected_target) != protected_target ||
            (protected_target && !protected_renames_verified_))
            return false;
        return true;
    }
    unsigned encode(const char *p, uint16_t *out, unsigned cap) {
        static_assert(sizeof(wchar_t) == sizeof(uint16_t), "Windows UTF16 ABI");
        if (!local_path(p))
            return 0;
        int count = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, p, -1,
                                        reinterpret_cast<wchar_t *>(out), int(cap));
        return count > 0 ? unsigned(count) : 0;
    }
    bool pending(RawQueue &out) {
        out = {};
        DWORD type = 0, bytes = sizeof(out.data);
        LONG status =
            query_(key_, Pending, nullptr, &type, reinterpret_cast<BYTE *>(out.data), &bytes);
        if (status == ERROR_FILE_NOT_FOUND)
            return true;
        if (status != ERROR_SUCCESS || type != REG_MULTI_SZ || bytes < 2 * sizeof(uint16_t) ||
            bytes > sizeof(out.data) || bytes % sizeof(uint16_t))
            return false;
        out.exists = true;
        out.words = bytes / sizeof(uint16_t);
        return out.data[out.words - 1] == 0 && out.data[out.words - 2] == 0;
    }
    bool secondary_absent() {
        return query_(key_, L"PendingFileRenameOperations2", nullptr, nullptr, nullptr, nullptr) ==
               ERROR_FILE_NOT_FOUND;
    }
    Permission permission() {
        DWORD type = 0, bytes = sizeof(DWORD), one = 0;
        LONG status = query_(key_, Allow, nullptr, &type, reinterpret_cast<BYTE *>(&one), &bytes);
        if (status == ERROR_FILE_NOT_FOUND)
            return Permission::absent;
        if (status == ERROR_MORE_DATA)
            return Permission::foreign;
        if (status != ERROR_SUCCESS)
            return Permission::error;
        return type == REG_DWORD && bytes == sizeof(one) && one == 1 ? Permission::one
                                                                     : Permission::foreign;
    }
    bool enqueue(const Entry &e) {
        if (!approved(e))
            return false;
        uint16_t source[path_capacity]{}, target[path_capacity]{};
        if (!e.desired.exists) {
            if (!encode(e.destination, source, path_capacity))
                return false;
            return move_(reinterpret_cast<const wchar_t *>(source), nullptr,
                         MOVEFILE_DELAY_UNTIL_REBOOT) != FALSE;
        }
        if (!encode(e.source, source, path_capacity) ||
            !encode(e.destination, target, path_capacity))
            return false;
        return move_(reinterpret_cast<const wchar_t *>(source),
                     reinterpret_cast<const wchar_t *>(target),
                     MOVEFILE_DELAY_UNTIL_REBOOT | MOVEFILE_REPLACE_EXISTING) != FALSE;
    }
    bool grant() {
        if (!protected_renames_verified_)
            return false;
        const DWORD one = 1;
        return set_(key_, Allow, 0, REG_DWORD, reinterpret_cast<const BYTE *>(&one), sizeof(one)) ==
               ERROR_SUCCESS;
    }
    bool remove_permission() {
        return delete_(key_, Allow) == ERROR_SUCCESS;
    }
    bool remove_pending() {
        return delete_(key_, Pending) == ERROR_SUCCESS;
    }
    bool flush() {
        return RegFlushKey(key_) == ERROR_SUCCESS;
    }
};
} // namespace setup::boot
