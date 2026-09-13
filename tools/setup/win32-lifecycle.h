// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#ifdef DG_SETUP_ADAPTER_TEST
#include "test-win32.h"
#else
#include <windows.h>
#endif
#include "durable-record.h"
#include "lifecycle.h"
#include "native-alias.h"
#include "providers.h"
#include "sha256.h"
namespace setup::lifecycle {
// Win32 syscalls are confined here. All ordering/ownership decisions live in
// the source-tested Engine. Deferred file replacement has a separate owned
// queue.
class Win32Store {
    char private_[MAX_PATH] = {}, system_[MAX_PATH] = {};
    bool initialized_ = false;
    DWORD journal_bytes_ = INVALID_FILE_SIZE;
    uint32_t preparation_generation_ = 0;
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
    static bool durable(const char *path, DWORD expected = INVALID_FILE_ATTRIBUTES) {
        DWORD attrs = GetFileAttributesA(path);
        if (attrs == INVALID_FILE_ATTRIBUTES ||
            (attrs & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
            return false;
        if (expected == INVALID_FILE_ATTRIBUTES)
            expected = attrs;
        if (expected & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
            return false;
        const bool readonly = (attrs & FILE_ATTRIBUTE_READONLY) != 0;
        if (readonly && !SetFileAttributesA(path, (attrs & ~FILE_ATTRIBUTE_READONLY)
                                                      ? attrs & ~FILE_ATTRIBUTE_READONLY
                                                      : FILE_ATTRIBUTE_NORMAL))
            return false;
        File file(
            CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr));
        // Restore attributes with the write handle still open, then flush both
        // copied bytes and final metadata. Failure never changes a public file.
        const bool restored =
            (attrs == expected && !readonly) || SetFileAttributesA(path, expected);
        return file.h != INVALID_HANDLE_VALUE && restored && FlushFileBuffers(file.h) &&
               GetFileAttributesA(path) == expected;
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
    // Only a complete preparation receipt reserves these private names. A
    // failed write may leave a prefix of the verified source; unrelated bytes
    // never authorize deletion or replacement.
    static bool discard_prefix(const char *target, const char *source, const BYTE *expected,
                               DWORD length) {
        const DWORD attributes = GetFileAttributesA(target);
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
            return false;
        {
            File input(CreateFileA(target, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0,
                                   nullptr));
            File original(source ? CreateFileA(source, GENERIC_READ, FILE_SHARE_READ, nullptr,
                                               OPEN_EXISTING, 0, nullptr)
                                 : INVALID_HANDLE_VALUE);
            DWORD high = 0;
            if (input.h == INVALID_HANDLE_VALUE || (source && original.h == INVALID_HANDLE_VALUE))
                return false;
            const DWORD size = GetFileSize(input.h, &high);
            if (high || size > length)
                return false;
            BYTE a[4096], b[4096];
            for (DWORD offset = 0; offset < size;) {
                const DWORD count = size - offset < sizeof(a) ? size - offset : sizeof(a);
                DWORD got = 0;
                if (!ReadFile(input.h, a, count, &got, nullptr) || got != count)
                    return false;
                if (source && (!ReadFile(original.h, b, count, &got, nullptr) || got != count))
                    return false;
                for (DWORD n = 0; n < count; ++n)
                    if (a[n] != (source ? b[n] : expected[offset + n]))
                        return false;
                offset += count;
            }
        }
        return (!(attributes & FILE_ATTRIBUTE_READONLY) ||
                SetFileAttributesA(target, FILE_ATTRIBUTE_NORMAL)) &&
               DeleteFileA(target);
    }
    bool backup_before(const Journal &j, unsigned n) {
        const auto &i = j.items[n];
        if (i.kind != Kind::file || !i.before.exists)
            return true;
        char dest[MAX_PATH], backup[MAX_PATH];
        if (!destination(i, dest) || !slot(backup, j.generation, n, "before") ||
            !close_match(dest, i.before))
            return false;
        const DWORD attributes = GetFileAttributesA(dest);
        if (preparation_generation_ == j.generation && close_match(backup, i.before))
            return durable(backup, attributes);
        if (!absent(backup) && (preparation_generation_ != j.generation ||
                                !discard_prefix(backup, dest, nullptr, i.before.size)))
            return false;
        return CopyFileA(dest, backup, TRUE) && durable(backup, attributes) &&
               close_match(backup, i.before) && close_match(dest, i.before);
    }
    static bool write_resource(const char *target, uint32_t id, const Image &image) {
        if (!image.exists || !valid_image(image, Kind::file))
            return false;
        if (close_match(target, image))
            return durable(target, FILE_ATTRIBUTE_NORMAL);
        if (!absent(target))
            return false;
        HRSRC resource = FindResourceA(nullptr, MAKEINTRESOURCEA(id), RT_RCDATA);
        if (!resource || SizeofResource(nullptr, resource) != image.size)
            return false;
        HGLOBAL data = LoadResource(nullptr, resource);
        const BYTE *bytes = data ? static_cast<const BYTE *>(LockResource(data)) : nullptr;
        if (!bytes)
            return false;
        char hash[65];
        Sha256 h;
        h.update(bytes, image.size);
        h.finish(hash);
        if (lstrcmpA(hash, image.sha))
            return false;
        {
            File file(CreateFileA(target, GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL, nullptr));
            DWORD written = 0;
            if (file.h == INVALID_HANDLE_VALUE ||
                !WriteFile(file.h, bytes, image.size, &written, nullptr) || written != image.size ||
                !FlushFileBuffers(file.h))
                return false;
        }
        return close_match(target, image);
    }
    static int cache_index(const char *path) {
        for (unsigned n = 0; n < 3; ++n)
            if (!lstrcmpA(path, win98_runtime_cache[n]))
                return int(n);
        return -1;
    }
    bool allowed(const Item &i) const {
        if (i.kind == Kind::file) {
            if (i.name[0])
                return false;
            if (cache_index(i.path) >= 0)
                return true;
            if (!lstrcmpA(i.path, "dgpuicd.dll"))
                return true;
            for (auto name : shared_runtime)
                if (!lstrcmpA(i.path, name))
                    return true;
            for (auto name : public_runtime)
                if (!lstrcmpA(i.path, name))
                    return true;
            for (auto name : native_aliases)
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
        if (cache_index(i.path) >= 0) {
            // System/System32 is a direct child of the Windows directory.
            // Only the three exact cache paths above reach this branch.
            unsigned n = lstrlenA(out);
            while (n && out[n - 1] != '\\')
                --n;
            if (n <= 3)
                return false;
            out[n - 1] = 0;
        }
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
        journal_bytes_ = INVALID_FILE_SIZE;
        initialized_ = true;
        return true;
    }
    // Add only existing Win98 cache files, before their public counterpart.
    // Refuse an incoherent baseline before preparing backups or binding a
    // driver: restoring those two different images would trigger OS self-repair
    // and could not establish the promised original state after a cold boot.
    bool complete_system_plan(Os os, Journal &j, const Journal *previous = nullptr,
                              bool repair = false) {
        if (os != Os::win98)
            return os == Os::nt5;
        for (unsigned mapping = 0; mapping < 3; ++mapping) {
            unsigned public_index = j.count, cached_index = j.count;
            for (unsigned n = 0; n < j.count; ++n) {
                if (j.items[n].kind != Kind::file)
                    continue;
                if (!lstrcmpA(j.items[n].path, public_runtime[mapping]))
                    public_index = n;
                if (!lstrcmpA(j.items[n].path, win98_runtime_cache[mapping]))
                    cached_index = n;
            }
            if (public_index == j.count)
                return false;
            Item cache = j.items[public_index];
            lstrcpyA(cache.path, win98_runtime_cache[mapping]);
            Image cached, original;
            if (!current(cache, cached) || !current(j.items[public_index], original))
                return false;
            const Item *prior_cache = nullptr, *prior_public = nullptr;
            if (previous)
                for (unsigned n = 0; n < previous->count; ++n) {
                    const auto &item = previous->items[n];
                    if (item.kind == Kind::file && !lstrcmpA(item.path, cache.path))
                        prior_cache = &item;
                    if (item.kind == Kind::file && !lstrcmpA(item.path, public_runtime[mapping]))
                        prior_public = &item;
                }
            if (!cached.exists) {
                if (prior_cache || cached_index != j.count)
                    return false;
                continue;
            }
            const bool servicing =
                repair && prior_cache && prior_public &&
                (same(cached, prior_cache->desired) || same(cached, prior_cache->original)) &&
                (same(original, prior_public->desired) || same(original, prior_public->original));
            if (!same(cached, original) && !servicing)
                return false;
            if (cached_index != j.count) {
                if (j.items[cached_index].resource != cache.resource ||
                    !same(j.items[cached_index].desired, cache.desired))
                    return false;
                continue;
            }
            if (j.count == max_items)
                return false;
            for (unsigned n = j.count; n > public_index; --n)
                j.items[n] = j.items[n - 1];
            j.items[public_index] = cache;
            ++j.count;
        }
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
        if (size == INVALID_FILE_SIZE || size > 16 * 1024 * 1024 - record ||
            (size && size != journal_bytes_))
            return false;
        if (SetFilePointer(f.h, 0, nullptr, FILE_END) == INVALID_SET_FILE_POINTER)
            return false;
        char hash[65];
        Sha256 h;
        h.update(reinterpret_cast<const BYTE *>(&j), sizeof(j));
        h.finish(hash);
        DWORD written;
        bool okay = WriteFile(f.h, &j, sizeof(j), &written, nullptr) && written == sizeof(j) &&
                    WriteFile(f.h, hash, 64, &written, nullptr) && written == 64 &&
                    FlushFileBuffers(f.h);
        if (okay)
            journal_bytes_ = size + record;
        return okay;
    }
    bool load(Journal &out, uint32_t generation = 0, bool read_only = false) {
        char filename[MAX_PATH];
        if (!path(filename, "\\lifecycle.bin"))
            return false;
        File f(CreateFileA(filename, read_only ? GENERIC_READ : GENERIC_READ | GENERIC_WRITE,
                           read_only ? FILE_SHARE_READ : 0, nullptr, OPEN_EXISTING, 0, nullptr));
        if (f.h == INVALID_HANDLE_VALUE)
            return false;
        DWORD size = GetFileSize(f.h, nullptr), good = 0;
        if (size == INVALID_FILE_SIZE || size > 16 * 1024 * 1024)
            return false;
        static Journal candidate;
        bool found = false;
        while (size - good >= 8) {
            candidate = {};
            DWORD got;
            char expected[65] = {}, actual[65];
            if (!ReadFile(f.h, &candidate, 8, &got, nullptr) || got != 8 ||
                candidate.magic != 0x314a4744 || (candidate.version != 1 && candidate.version != 2))
                return false;
            const bool legacy = candidate.version == 1;
            const DWORD bytes = legacy ? 24 + legacy_items * sizeof(Item) : sizeof(Journal);
            if (size - good < bytes + 64)
                break; // incomplete last append only
            if (!ReadFile(f.h, reinterpret_cast<BYTE *>(&candidate) + 8, bytes - 8, &got,
                          nullptr) ||
                got != bytes - 8 || !ReadFile(f.h, expected, 64, &got, nullptr) || got != 64)
                return false;
            Sha256 h;
            h.update(reinterpret_cast<const BYTE *>(&candidate), bytes);
            h.finish(actual);
            if (lstrcmpA(actual, expected))
                return false;
            if (legacy) {
                if (candidate.count > legacy_items)
                    return false;
                for (unsigned i = 0; i < candidate.count; ++i)
                    if (candidate.items[i].original_index >= legacy_items)
                        return false;
                candidate.version = 2; // preserve original generations/backup slots
            }
            if (!valid(candidate))
                return false;
            for (unsigned i = 0; i < candidate.count; i++)
                if (!allowed(candidate.items[i]))
                    return false;
            if (!generation || candidate.generation == generation) {
                out = candidate;
                found = true;
            }
            good += bytes + 64;
        }
        if (!good || !found)
            return false;
        if (good != size && !read_only) {
            if (SetFilePointer(f.h, good, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
                !SetEndOfFile(f.h) || !FlushFileBuffers(f.h))
                return false;
        }
        if (!read_only)
            journal_bytes_ = good;
        return true;
    }
    bool load_generation(uint32_t generation, Journal &out) {
        return generation && load(out, generation, true);
    }
    bool matches_original(const Item &item) {
        Image found;
        return allowed(item) && current(item, found) && same(found, item.original);
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
        if (!backup_before(j, n))
            return false;
        if (same(i.before, i.desired) && (!prior || prior->phase == Phase::borrowed))
            i.phase = Phase::borrowed;
        return true;
    }
    // Build an alias from the immutable original public-runtime backup. An
    // upgrade reuses the first generation's original, never the installed Wine
    // switcher. Only an exact matching preexisting alias may be borrowed.
    bool derive_alias(Os os, Journal &j, unsigned alias_index, unsigned public_index) {
        if (alias_index >= j.count || public_index >= j.count)
            return false;
        auto &alias = j.items[alias_index];
        const auto &original = j.items[public_index];
        bool mapped = false;
        for (unsigned n = 0; n < 3; ++n)
            if (!lstrcmpA(alias.path, native_aliases[n]) &&
                !lstrcmpA(original.path, public_runtime[n]))
                mapped = true;
        if (!mapped || alias.resource != derived_resource || alias.kind != Kind::file ||
            original.kind != Kind::file)
            return false;
        if (!original.original.exists) {
            if (alias.before.exists)
                return false; // Never erase or adopt an unrelated existing alias.
            alias.desired = {};
            alias.phase = Phase::borrowed;
            return true;
        }
        char source[MAX_PATH], output[MAX_PATH];
        if (!slot(source, original.original_generation, original.original_index, "before") ||
            !close_match(source, original.original) ||
            !slot(output, j.generation, alias_index, "payload"))
            return false;
        struct Buffer {
            BYTE *data;
            explicit Buffer(DWORD size)
                : data(static_cast<BYTE *>(HeapAlloc(GetProcessHeap(), 0, size))) {}
            ~Buffer() {
                if (data)
                    HeapFree(GetProcessHeap(), 0, data);
            }
            Buffer(const Buffer &) = delete;
        } bytes(original.original.size);
        if (!bytes.data)
            return false;
        File input(
            CreateFileA(source, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
        DWORD got = 0;
        if (input.h == INVALID_HANDLE_VALUE ||
            !ReadFile(input.h, bytes.data, original.original.size, &got, nullptr) ||
            got != original.original.size)
            return false;
        char before[65];
        native_alias::hash({bytes.data, got}, before);
        if (lstrcmpA(before, original.original.sha) ||
            !native_alias::derive(os, original.path, {bytes.data, got}))
            return false;
        Image desired;
        desired.exists = 1;
        desired.size = got;
        native_alias::hash({bytes.data, got}, desired.sha);
        // An owned upgrade may replace its prior exact alias. Initial install
        // can only borrow a foreign alias when the bytes already match exactly.
        if (alias.before.exists && alias.original_generation == j.generation &&
            !same(alias.before, desired))
            return false;
        if (preparation_generation_ == j.generation && close_match(output, desired)) {
            if (!durable(output, FILE_ATTRIBUTE_NORMAL))
                return false;
        } else {
            if (!absent(output) && (preparation_generation_ != j.generation ||
                                    !discard_prefix(output, nullptr, bytes.data, got)))
                return false;
            File file(CreateFileA(output, GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL, nullptr));
            DWORD written = 0;
            if (file.h == INVALID_HANDLE_VALUE ||
                !WriteFile(file.h, bytes.data, got, &written, nullptr) || written != got ||
                !FlushFileBuffers(file.h))
                return false;
        }
        alias.desired = desired;
        alias.phase = same(alias.before, desired) && same(alias.original, alias.before)
                          ? Phase::borrowed
                          : Phase::prepared;
        return true;
    }
    // Record every before-image and the exact input plan before reserving the
    // generation directory. Repeated preparation can then finish only that
    // same plan, and a pre-existing unreceipted directory remains a conflict.
    bool prepare_generation(Journal &j, const Journal *previous = nullptr, bool removal = false,
                            bool repair = false) {
        struct Preparation {
            uint32_t magic = 0x31504744, version = 1;
            char intent[65]{};
            Journal snapshot{};
        };
        static Preparation record;
        char name[40], receipt[MAX_PATH], directory[MAX_PATH], digest[65];
        wsprintfA(name, "\\P%08lX.JRN", j.generation);
        if (!path(receipt, name))
            return false;
        wsprintfA(name, "\\T%08lX", j.generation);
        if (!path(directory, name))
            return false;
        Sha256 hash;
        hash.update(reinterpret_cast<const BYTE *>(&j), sizeof(j));
        if (previous)
            hash.update(reinterpret_cast<const BYTE *>(previous), sizeof(*previous));
        const BYTE mode = removal ? 1 : repair ? 2 : 0;
        hash.update(&mode, sizeof(mode));
        hash.finish(digest);
        DurableRecord<Preparation> disk(receipt);
        auto checked = [](const Preparation &p) {
            return p.magic == 0x31504744 && p.version == 1 && p.intent[64] == 0 &&
                   valid(p.snapshot) && p.snapshot.state == State::staged;
        };
        bool exists = false;
        if (!disk.load(record, exists, checked))
            return false;
        if (!exists) {
            if (!absent(directory))
                return false;
            record = {};
            lstrcpyA(record.intent, digest);
            record.snapshot = j;
            for (unsigned n = 0; n < j.count; ++n) {
                auto &i = record.snapshot.items[n];
                const Item retained = i;
                if (!allowed(i) || !current(i, i.before))
                    return false;
                if (removal) {
                    if (!same(i.before, retained.before))
                        return false;
                    i = retained;
                } else {
                    i.original = i.before;
                    i.original_generation = j.generation;
                    i.original_index = n;
                    const Item *prior = nullptr;
                    if (previous)
                        for (unsigned k = 0; k < previous->count; ++k)
                            if (i.kind == previous->items[k].kind &&
                                destination_equal(i.path, previous->items[k].path, 192) &&
                                destination_equal(i.name, previous->items[k].name, 64))
                                prior = &previous->items[k];
                    bool public_file = false;
                    if (repair && i.kind == Kind::file)
                        for (const char *name : public_runtime)
                            public_file |= destination_equal(i.path, name, sizeof(i.path));
                    if (repair && i.kind == Kind::file)
                        for (const char *name : win98_runtime_cache)
                            public_file |= destination_equal(i.path, name, sizeof(i.path));
                    if (prior && !inherit(i, *prior, public_file, previous->state == State::failed))
                        return false;
                    if (same(i.before, i.desired) && (!prior || prior->phase == Phase::borrowed))
                        i.phase = Phase::borrowed;
                }
            }
            if (!disk.save(record, checked))
                return false;
        } else if (lstrcmpA(record.intent, digest) || record.snapshot.generation != j.generation)
            return false;
        for (unsigned n = 0; n < record.snapshot.count; ++n) {
            Image current_image;
            if (!allowed(record.snapshot.items[n]) ||
                !current(record.snapshot.items[n], current_image) ||
                !same(current_image, record.snapshot.items[n].before))
                return false;
        }
        DWORD attributes = GetFileAttributesA(directory);
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            if (GetLastError() != ERROR_FILE_NOT_FOUND || !CreateDirectoryA(directory, nullptr))
                return false;
        } else if ((attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) !=
                   FILE_ATTRIBUTE_DIRECTORY)
            return false;
        j = record.snapshot;
        preparation_generation_ = j.generation;
        for (unsigned n = 0; n < j.count; ++n)
            if (!backup_before(j, n))
                return false;
        return true;
    }
    bool publish_preparation(const Journal &j) {
        if (preparation_generation_ != j.generation || j.state != State::staged || !valid(j))
            return false;
        static Journal last;
        if (load(last)) {
            if (last.generation == j.generation) {
                Sha256 a, b;
                char ah[65], bh[65];
                a.update(reinterpret_cast<const BYTE *>(&last), sizeof(last));
                b.update(reinterpret_cast<const BYTE *>(&j), sizeof(j));
                a.finish(ah);
                b.finish(bh);
                return !lstrcmpA(ah, bh);
            }
            if (last.generation + 1 != j.generation)
                return false;
            return persist(j);
        }
        char file[MAX_PATH];
        if (!path(file, "\\lifecycle.bin"))
            return false;
        if (!absent(file)) {
            if (j.generation != 1)
                return false;
            const DWORD attributes = GetFileAttributesA(file);
            if (attributes == INVALID_FILE_ATTRIBUTES ||
                (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
                return false;
            File output(CreateFileA(file, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                    0, nullptr));
            DWORD high = 0;
            if (output.h == INVALID_HANDLE_VALUE)
                return false;
            const DWORD size = GetFileSize(output.h, &high);
            if (high || size >= sizeof(Journal) + 64)
                return false;
            Sha256 hash;
            char digest[65];
            hash.update(reinterpret_cast<const BYTE *>(&j), sizeof(j));
            hash.finish(digest);
            BYTE bytes[4096];
            for (DWORD offset = 0; offset < size;) {
                const DWORD count = size - offset < sizeof(bytes) ? size - offset : sizeof(bytes);
                DWORD got = 0;
                if (!ReadFile(output.h, bytes, count, &got, nullptr) || got != count)
                    return false;
                for (DWORD n = 0; n < count; ++n) {
                    const DWORD index = offset + n;
                    const BYTE expected = index < sizeof(j)
                                              ? reinterpret_cast<const BYTE *>(&j)[index]
                                              : BYTE(digest[index - sizeof(j)]);
                    if (bytes[n] != expected)
                        return false;
                }
                offset += count;
            }
            if (SetFilePointer(output.h, 0, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
                !SetEndOfFile(output.h) || !FlushFileBuffers(output.h))
                return false;
            journal_bytes_ = 0;
        }
        return persist(j);
    }
    bool create_generation(uint32_t generation) {
        char suffix[40], directory[MAX_PATH];
        wsprintfA(suffix, "\\T%08lX", generation);
        return path(directory, suffix) && CreateDirectoryA(directory, nullptr);
    }
    // Prepare the exact immutable desired bytes for either an immediate rename
    // or the OS boot queue. This never changes a public destination.
    bool file_payload(const Journal &j, unsigned n, bool undo, char *temp) {
        if (n >= j.count || j.items[n].kind != Kind::file || !allowed(j.items[n]))
            return false;
        const auto &i = j.items[n];
        const auto &to = undo ? i.before : i.desired;
        if (!slot(temp, j.generation, n, undo ? "restore" : "new"))
            return false;
        if (to.exists && !close_match(temp, to)) {
            if (!absent(temp))
                return false;
            if (undo || j.uninstall) {
                char source[MAX_PATH];
                uint32_t gen = undo ? j.generation : i.original_generation;
                if (!slot(source, gen, (undo ? n : i.original_index), "before") ||
                    !close_match(source, to) || !CopyFileA(source, temp, TRUE) || !durable(temp))
                    return false;
            } else if (i.resource == derived_resource) {
                char source[MAX_PATH];
                if (!slot(source, j.generation, n, "payload") || !close_match(source, to) ||
                    !CopyFileA(source, temp, TRUE) || !durable(temp))
                    return false;
            } else {
                if (!write_resource(temp, i.resource, to))
                    return false;
            }
            if (!close_match(temp, to))
                return false;
        }
        if (to.exists) {
            DWORD attributes = FILE_ATTRIBUTE_NORMAL;
            if (undo || j.uninstall) {
                char source[MAX_PATH];
                const uint32_t gen = undo ? j.generation : i.original_generation;
                if (!slot(source, gen, undo ? n : i.original_index, "before"))
                    return false;
                attributes = GetFileAttributesA(source);
                if (attributes == INVALID_FILE_ATTRIBUTES)
                    return false;
            }
            // Reapply the source attributes even when recovery finds a complete
            // temp whose earlier attribute restoration failed before rename.
            if (!durable(temp, attributes))
                return false;
        }
        return true;
    }
    bool stage_helper(uint32_t generation, unsigned index, uint32_t resource, const Image &image,
                      char *executable, char *directory) {
        if (index >= 6)
            return false;
        char suffix[64];
        wsprintfA(suffix, "\\T%08lX\\PROOFS", generation);
        if (!path(directory, suffix))
            return false;
        DWORD attrs = GetFileAttributesA(directory);
        if (attrs == INVALID_FILE_ATTRIBUTES) {
            if (GetLastError() != ERROR_FILE_NOT_FOUND || !CreateDirectoryA(directory, nullptr))
                return false;
        } else if (!(attrs & FILE_ATTRIBUTE_DIRECTORY) || (attrs & FILE_ATTRIBUTE_REPARSE_POINT))
            return false;
        lstrcatA(suffix, "\\");
        lstrcatA(suffix, system_probes[index]);
        return path(executable, suffix) && write_resource(executable, resource, image);
    }
    bool retain_program(const char *source, const char *destination, const Image &expected) {
        Image actual;
        if (!hash_file(source, actual) || !same(actual, expected))
            return false;
        if (!close_match(destination, expected) &&
            (!absent(destination) || !CopyFileA(source, destination, TRUE)))
            return false;
        return durable(destination, FILE_ATTRIBUTE_NORMAL) && close_match(destination, expected);
    }
    bool public_path(const Item &i, char *out) const {
        return destination(i, out);
    }
    bool private_slot(char *out, uint32_t generation, unsigned n, const char *kind) const {
        return slot(out, generation, n, kind);
    }
    bool private_path(char *out, const char *suffix) const {
        return path(out, suffix);
    }
    bool inspect_file(const char *path, Image &image) const {
        return hash_file(path, image);
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
            // Win98 RegDeleteKey can recurse; NT also deletes remaining values.
            // Never erase foreign additions to an originally absent owned key.
            Key key;
            DWORD subkeys = 0, values = 0;
            if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, i.path, 0, KEY_READ, &key.h) != ERROR_SUCCESS ||
                RegQueryInfoKeyA(key.h, nullptr, nullptr, nullptr, &subkeys, nullptr, nullptr,
                                 &values, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS ||
                subkeys || values)
                return Change::error;
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
        if (!file_payload(j, n, undo, temp))
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
