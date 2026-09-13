// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#ifdef DG_SETUP_ADAPTER_TEST
#include "test-win32.h"
#else
#include <windows.h>
#endif
#include "lifecycle.h"
#include "providers.h"
#include "sha256.h"
namespace setup::lifecycle {
// Win32 syscalls are confined here. All ordering/ownership decisions live in the
// source-tested Engine. This adapter never schedules protected-DLL replacement.
class Win32Store {
    char private_[MAX_PATH] = {}, system_[MAX_PATH] = {};
    bool initialized_ = false;
    struct File {
        HANDLE h = INVALID_HANDLE_VALUE;
        File() = default;
        explicit File(HANDLE value) : h(value) {}
        File(const File &) = delete;
        ~File() {
            if (h != INVALID_HANDLE_VALUE)
                CloseHandle(h);
        }
    };
    struct Key {
        HKEY h = 0;
        Key() = default;
        Key(const Key &) = delete;
        ~Key() {
            if (h)
                RegCloseKey(h);
        }
    };
    static bool hash_file(const char *path, Image &out) {
        out = {};
        DWORD attrs = GetFileAttributesA(path);
        if (attrs == INVALID_FILE_ATTRIBUTES)
            return GetLastError() == ERROR_FILE_NOT_FOUND;
        if (attrs & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
            return false;
        File file(
            CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
        if (file.h == INVALID_HANDLE_VALUE)
            return false;
        DWORD high = 0, size = GetFileSize(file.h, &high);
        if (high || size > 64 * 1024 * 1024)
            return false;
        Sha256 hash;
        BYTE buffer[4096];
        DWORD total = 0, got = 0;
        do {
            if (!ReadFile(file.h, buffer, sizeof(buffer), &got, nullptr) || got > size - total)
                return false;
            hash.update(buffer, got);
            total += got;
        } while (got);
        if (total != size)
            return false;
        out.exists = 1;
        out.size = size;
        hash.finish(out.sha);
        return true;
    }
    static bool durable(const char *path) {
        File file(
            CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr));
        return file.h != INVALID_HANDLE_VALUE && FlushFileBuffers(file.h);
    }
    bool path(char *out, const char *suffix) const {
        if (!initialized_ || lstrlenA(private_) + lstrlenA(suffix) >= MAX_PATH)
            return false;
        lstrcpyA(out, private_);
        lstrcatA(out, suffix);
        return true;
    }
    bool slot(char *out, uint32_t generation, unsigned n, const char *kind) const {
        char suffix[64];
        wsprintfA(suffix, "\\T%08lX\\%02u.%s", generation, n, kind);
        return path(out, suffix);
    }
    static bool absent(const char *p) {
        DWORD attrs = GetFileAttributesA(p);
        return attrs == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND;
    }
    static bool close_match(const char *p, const Image &expected) {
        Image found;
        return hash_file(p, found) && same(found, expected);
    }
    static bool remove_match(const char *p, const Image &expected) {
        return close_match(p, expected) && (!expected.exists || DeleteFileA(p));
    }
    bool allowed(const Item &i) const {
        if (i.kind == Kind::file) {
            if (i.name[0])
                return false;
            if (!lstrcmpA(i.path, "dgpuicd.dll"))
                return true;
            for (auto name : shared_runtime)
                if (!lstrcmpA(i.path, name))
                    return true;
            return false;
        }
        if (i.kind == Kind::registry_key)
            return !lstrcmpA(i.path, nt_icd_key) && !i.name[0];
        if (!lstrcmpA(i.path, win98_icd_key))
            return !lstrcmpA(i.name, "DGPUICD");
        if (lstrcmpA(i.path, nt_icd_key))
            return false;
        return !lstrcmpA(i.name, "Dll") || !lstrcmpA(i.name, "Version") ||
               !lstrcmpA(i.name, "DriverVersion") || !lstrcmpA(i.name, "Flags");
    }
    bool destination(const Item &i, char *out) const {
        if (!allowed(i) || i.kind != Kind::file ||
            lstrlenA(system_) + lstrlenA(i.path) + 2 >= MAX_PATH)
            return false;
        lstrcpyA(out, system_);
        lstrcatA(out, "\\");
        lstrcatA(out, i.path);
        return true;
    }
    bool current(const Item &i, Image &out) const {
        if (!allowed(i))
            return false;
        if (i.kind == Kind::file) {
            char dest[MAX_PATH];
            return destination(i, dest) && hash_file(dest, out);
        }
        out = {};
        Key key;
        LONG result = RegOpenKeyExA(HKEY_LOCAL_MACHINE, i.path, 0, KEY_QUERY_VALUE, &key.h);
        if (result == ERROR_FILE_NOT_FOUND)
            return true;
        if (result != ERROR_SUCCESS)
            return false;
        if (i.kind == Kind::registry_key) {
            out.exists = 1;
            Sha256 h;
            h.finish(out.sha);
            return true;
        }
        DWORD bytes = sizeof(out.value), type = 0;
        result = RegQueryValueExA(key.h, i.name, nullptr, &type, out.value, &bytes);
        if (result == ERROR_FILE_NOT_FOUND) {
            out = {};
            return true;
        }
        if (result != ERROR_SUCCESS || bytes > sizeof(out.value) ||
            (type != REG_SZ && type != REG_DWORD))
            return false;
        out.exists = 1;
        out.size = bytes;
        out.type = type;
        Sha256 h;
        h.update(out.value, bytes);
        h.finish(out.sha);
        return true;
    }

  public:
    // Existing owner directory only. Caller must have verified its installation
    // receipt; init performs no mkdir, registry writes, or destination mutations.
    bool init(const char *owner) {
        if (lstrlenA(owner) > MAX_PATH - 80)
            return false;
        DWORD a = GetFileAttributesA(owner);
        if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY) ||
            (a & FILE_ATTRIBUTE_REPARSE_POINT))
            return false;
        unsigned n = GetSystemDirectoryA(system_, sizeof(system_));
        if (!n || n >= sizeof(system_) - 32)
            return false;
        lstrcpyA(private_, owner);
        initialized_ = true;
        return true;
    }
    bool persist(const Journal &j) {
        if (!valid(j))
            return false;
        char filename[MAX_PATH];
        if (!path(filename, "\\lifecycle.bin"))
            return false;
        File f(CreateFileA(filename, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr));
        if (f.h == INVALID_HANDLE_VALUE)
            return false;
        // Each append is a complete fixed schema + hash. No in-place metadata
        // overwrite. A partial tail is trimmed only after a validated load.
        DWORD size = GetFileSize(f.h, nullptr);
        constexpr DWORD record = sizeof(Journal) + 64;
        if (size == INVALID_FILE_SIZE || size % record || size > 16 * 1024 * 1024 - record)
            return false;
        if (SetFilePointer(f.h, 0, nullptr, FILE_END) == INVALID_SET_FILE_POINTER)
            return false;
        char hash[65];
        Sha256 h;
        h.update(reinterpret_cast<const BYTE *>(&j), sizeof(j));
        h.finish(hash);
        DWORD written;
        return WriteFile(f.h, &j, sizeof(j), &written, nullptr) && written == sizeof(j) &&
               WriteFile(f.h, hash, 64, &written, nullptr) && written == 64 &&
               FlushFileBuffers(f.h);
    }
    bool load(Journal &out) {
        char filename[MAX_PATH];
        if (!path(filename, "\\lifecycle.bin"))
            return false;
        File f(CreateFileA(filename, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0,
                           nullptr));
        if (f.h == INVALID_HANDLE_VALUE)
            return false;
        DWORD size = GetFileSize(f.h, nullptr), good = 0;
        if (size == INVALID_FILE_SIZE || size > 16 * 1024 * 1024)
            return false;
        static Journal candidate;
        while (size - good >= sizeof(Journal) + 64) {
            DWORD got;
            char expected[65] = {}, actual[65];
            if (!ReadFile(f.h, &candidate, sizeof(candidate), &got, nullptr) ||
                got != sizeof(candidate) || !ReadFile(f.h, expected, 64, &got, nullptr) ||
                got != 64)
                return false;
            Sha256 h;
            h.update(reinterpret_cast<const BYTE *>(&candidate), sizeof(candidate));
            h.finish(actual);
            if (lstrcmpA(actual, expected) || !valid(candidate))
                return false;
            for (unsigned i = 0; i < candidate.count; i++)
                if (!allowed(candidate.items[i]))
                    return false;
            out = candidate;
            good += sizeof(Journal) + 64;
        }
        if (!good)
            return false;
        if (good != size) {
            if (SetFilePointer(f.h, good, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
                !SetEndOfFile(f.h) || !FlushFileBuffers(f.h))
                return false;
        }
        return true;
    }
    Actual classify(const Journal &j, unsigned n, bool undo) {
        if (n >= j.count)
            return Actual::error;
        const Item &i = j.items[n];
        Image now;
        if (!current(i, now))
            return Actual::error;
        const Image &from = undo ? i.desired : i.before;
        const Image &to = undo ? i.before : i.desired;
        if (same(now, to))
            return Actual::after;
        if (same(now, from))
            return Actual::before;
        if (i.kind == Kind::file && !now.exists &&
            (i.phase == Phase::changing || i.phase == Phase::restoring)) {
            char backup[MAX_PATH];
            if (slot(backup, j.generation, n, undo ? "undo" : "old") && close_match(backup, from))
                return Actual::intermediate;
            if (undo && slot(backup, j.generation, n, "old") && close_match(backup, i.before))
                return Actual::intermediate;
        }
        return Actual::conflict;
    }
    // Seeing the desired value after a failed RegFlushKey is not evidence of
    // durability. Every observed-after recovery must flush again before its
    // applied/restored acknowledgement is persisted in the file journal.
    bool confirm(const Journal &j, unsigned n, bool undo) {
        if (n >= j.count || classify(j, n, undo) != Actual::after)
            return false;
        const auto &i = j.items[n];
        if (i.kind == Kind::file)
            return true; // source temp was flushed before rename
        Key key;
        LONG result = RegOpenKeyExA(HKEY_LOCAL_MACHINE, i.path, 0, KEY_QUERY_VALUE, &key.h);
        if (result == ERROR_FILE_NOT_FOUND)
            return RegFlushKey(HKEY_LOCAL_MACHINE) == ERROR_SUCCESS;
        return result == ERROR_SUCCESS && RegFlushKey(key.h) == ERROR_SUCCESS;
    }
    // Captures before-image and original backup without touching public paths.
    // Upgrade caller supplies prior ownership to inherit only after exact match.
    bool capture(Journal &j, unsigned n, const Item *prior = nullptr) {
        if (n >= j.count || !allowed(j.items[n]))
            return false;
        auto &i = j.items[n];
        if (!current(i, i.before))
            return false;
        i.original = i.before;
        i.original_generation = j.generation;
        i.original_index = n;
        if (prior && !inherit(i, *prior))
            return false;
        if (i.kind == Kind::file && i.before.exists) {
            char dest[MAX_PATH], backup[MAX_PATH];
            if (!destination(i, dest) || !slot(backup, j.generation, n, "before"))
                return false;
            if (!CopyFileA(dest, backup, TRUE) || !durable(backup) ||
                !close_match(backup, i.before))
                return false;
        }
        if (same(i.before, i.desired) && (!prior || prior->phase == Phase::borrowed))
            i.phase = Phase::borrowed;
        return true;
    }
    bool create_generation(uint32_t generation) {
        char suffix[40], directory[MAX_PATH];
        wsprintfA(suffix, "\\T%08lX", generation);
        return path(directory, suffix) && CreateDirectoryA(directory, nullptr);
    }
    Change mutate(const Journal &j, unsigned n, bool undo) {
        Actual state = classify(j, n, undo);
        if (state == Actual::after)
            return Change::done;
        if (state != Actual::before && state != Actual::intermediate)
            return Change::error;
        const Item &i = j.items[n];
        const Image &from = undo ? i.desired : i.before;
        const Image &to = undo ? i.before : i.desired;
        if (i.kind == Kind::registry_key) {
            if (to.exists) {
                Key key;
                DWORD disposition;
                return RegCreateKeyExA(HKEY_LOCAL_MACHINE, i.path, 0, nullptr, 0,
                                       KEY_READ | KEY_WRITE, nullptr, &key.h,
                                       &disposition) == ERROR_SUCCESS &&
                               RegFlushKey(key.h) == ERROR_SUCCESS
                           ? Change::done
                           : Change::error;
            }
            return RegDeleteKeyA(HKEY_LOCAL_MACHINE, i.path) == ERROR_SUCCESS &&
                           RegFlushKey(HKEY_LOCAL_MACHINE) == ERROR_SUCCESS
                       ? Change::done
                       : Change::error;
        }
        if (i.kind == Kind::registry) {
            Key key;
            // The separately journalled key operation owns key creation. Shared Win98
            // OpenGLDrivers parent is opened; it is never deleted on uninstall.
            LONG status = RegOpenKeyExA(HKEY_LOCAL_MACHINE, i.path, 0,
                                        KEY_QUERY_VALUE | KEY_SET_VALUE, &key.h);
            if (status != ERROR_SUCCESS)
                return !to.exists && status == ERROR_FILE_NOT_FOUND ? Change::done : Change::error;
            status = to.exists ? RegSetValueExA(key.h, i.name, 0, to.type, to.value, to.size)
                               : RegDeleteValueA(key.h, i.name);
            if (status != ERROR_SUCCESS && !(status == ERROR_FILE_NOT_FOUND && !to.exists))
                return Change::error;
            return RegFlushKey(key.h) == ERROR_SUCCESS ? Change::done : Change::error;
        }
        char dest[MAX_PATH], old[MAX_PATH], temp[MAX_PATH];
        if (!destination(i, dest) || !slot(old, j.generation, n, undo ? "undo" : "old") ||
            !slot(temp, j.generation, n, undo ? "restore" : "new"))
            return Change::error;
        if (to.exists && !close_match(temp, to)) {
            if (!absent(temp))
                return Change::error;
            if (undo || j.uninstall) {
                char source[MAX_PATH];
                uint32_t gen = undo ? j.generation : i.original_generation;
                if (!slot(source, gen, (undo ? n : i.original_index), "before") ||
                    !close_match(source, to) || !CopyFileA(source, temp, TRUE) || !durable(temp))
                    return Change::error;
            } else {
                HRSRC resource = FindResourceA(nullptr, MAKEINTRESOURCEA(i.resource), RT_RCDATA);
                if (!resource || SizeofResource(nullptr, resource) != to.size)
                    return Change::error;
                HGLOBAL data = LoadResource(nullptr, resource);
                const BYTE *bytes = data ? static_cast<const BYTE *>(LockResource(data)) : nullptr;
                if (!bytes)
                    return Change::error;
                char hash[65];
                Sha256 h;
                h.update(bytes, to.size);
                h.finish(hash);
                if (lstrcmpA(hash, to.sha))
                    return Change::error;
                File file(CreateFileA(temp, GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                      FILE_ATTRIBUTE_NORMAL, nullptr));
                DWORD written;
                if (file.h == INVALID_HANDLE_VALUE ||
                    !WriteFile(file.h, bytes, to.size, &written, nullptr) || written != to.size ||
                    !FlushFileBuffers(file.h))
                    return Change::error;
            }
            if (!close_match(temp, to))
                return Change::error;
        }
        if (to.exists && !durable(temp))
            return Change::error;
        if (state == Actual::before && from.exists) {
            if (!absent(old))
                return Change::error;
            if (!MoveFileA(dest, old)) {
                DWORD error = GetLastError();
                return error == ERROR_SHARING_VIOLATION || error == ERROR_ACCESS_DENIED
                           ? Change::locked
                           : Change::error;
            }
        }
        if (to.exists && !MoveFileA(temp, dest))
            return Change::error;
        return Change::done;
    }
    bool verify_activation(const Journal &) {
        return false;
    }
};
} // namespace setup::lifecycle
