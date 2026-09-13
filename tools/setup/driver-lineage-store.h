// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "driver-lineage.h"
#include "durable-record.h"
namespace setup::driver {
inline bool generation_root(const char *owner, uint32_t id, char out[MAX_PATH]) {
    if (!id || id > MaxDriverGenerations || !bounded(owner, MAX_PATH - 48))
        return false;
    lstrcpyA(out, owner);
    if (id != 1) {
        char suffix[24];
        wsprintfA(suffix, "\\D%08lX", static_cast<unsigned long>(id));
        lstrcatA(out, suffix);
    }
    return true;
}
inline bool lineage_path(const char *owner, char out[MAX_PATH]) {
    if (!bounded(owner, MAX_PATH - 48))
        return false;
    lstrcpyA(out, owner);
    lstrcatA(out, "\\driver-lineage.bin");
    return true;
}
inline bool load_lineage(const char *owner, Os os, Lineage &out, bool &exists,
                         bool read_only = false) {
    char path[MAX_PATH];
    if (!lineage_path(owner, path))
        return false;
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES &&
        GetLastError() == ERROR_PATH_NOT_FOUND) {
        exists = false;
        return true;
    }
    DurableRecord<Lineage> disk(path);
    return disk.load(
        out, exists, [os](const Lineage &l) { return l.os == os && valid_lineage(l); }, read_only);
}
inline bool save_lineage(const char *owner, const Lineage &previous, const Lineage &next,
                         bool creating = false) {
    char path[MAX_PATH];
    if (!lineage_path(owner, path) || !valid_lineage(next))
        return false;
    DurableRecord<Lineage> disk(path);
    Lineage stored;
    bool exists;
    if (!disk.load(stored, exists, valid_lineage) || exists == creating)
        return false;
    if (exists) {
        const auto *a = reinterpret_cast<const BYTE *>(&stored);
        const auto *b = reinterpret_cast<const BYTE *>(&previous);
        for (unsigned n = 0; n < sizeof(stored); ++n)
            if (a[n] != b[n])
                return false; // Never replace a concurrently advanced selection.
    }
    return disk.save(next, valid_lineage);
}
// Fixed resources only. Every byte is authenticated before any directory or file
// write. A reserved generation may resume only an exact resource prefix; another
// complete file or a mismatching partial write is never overwritten.
class GenerationStager {
    struct Handle {
        HANDLE h;
        explicit Handle(HANDLE v) : h(v) {}
        ~Handle() {
            if (h != INVALID_HANDLE_VALUE)
                CloseHandle(h);
        }
        Handle(const Handle &) = delete;
    };
    static bool directory(const char *path) {
        DWORD attr = GetFileAttributesA(path);
        if (attr == INVALID_FILE_ATTRIBUTES)
            return GetLastError() == ERROR_FILE_NOT_FOUND && CreateDirectoryA(path, nullptr);
        return (attr & FILE_ATTRIBUTE_DIRECTORY) && !(attr & FILE_ATTRIBUTE_REPARSE_POINT);
    }
    static bool write(const char *path, const BYTE *bytes, DWORD size) {
        DWORD attr = GetFileAttributesA(path);
        if (attr != INVALID_FILE_ATTRIBUTES &&
            (attr & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
            return false;
        if (attr == INVALID_FILE_ATTRIBUTES && GetLastError() != ERROR_FILE_NOT_FOUND)
            return false;
        Handle file(CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                attr == INVALID_FILE_ATTRIBUTES ? CREATE_NEW : OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr));
        if (file.h == INVALID_HANDLE_VALUE)
            return false;
        DWORD high = 0, existing = GetFileSize(file.h, &high);
        if (high || existing > size)
            return false;
        BYTE buffer[4096];
        DWORD offset = 0;
        while (offset < existing) {
            DWORD count = existing - offset, got = 0;
            if (count > sizeof(buffer))
                count = sizeof(buffer);
            if (!ReadFile(file.h, buffer, count, &got, nullptr) || got != count)
                return false;
            for (DWORD n = 0; n < count; ++n)
                if (buffer[n] != bytes[offset + n])
                    return false;
            offset += count;
        }
        while (offset < size) {
            DWORD count = size - offset, written = 0;
            if (count > 65536)
                count = 65536;
            if (!WriteFile(file.h, bytes + offset, count, &written, nullptr) || written != count)
                return false;
            offset += count;
        }
        return FlushFileBuffers(file.h);
    }

  public:
    static bool absent(const char *owner, uint32_t id) {
        char path[MAX_PATH];
        return id > 1 && generation_root(owner, id, path) &&
               GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES &&
               GetLastError() == ERROR_FILE_NOT_FOUND;
    }
    template <class Payloads>
    static bool stage(const char *owner, const Lineage &l, const Payloads &payloads) {
        if (!valid_lineage(l) || l.phase != LineagePhase::preparing)
            return false;
        const char *names[5];
        if (l.os == Os::nt5) {
            names[0] = "drivers/nt5/dreamgpu.inf";
            names[1] = "drivers/nt5/dgpudisp.dll";
            names[2] = "drivers/nt5/dgpumini.sys";
            names[3] = "tools/nt5/DGDRV.EXE";
            names[4] = "tools/nt5/DGAUDIT.EXE";
        } else {
            names[0] = "drivers/win98/dg9x.inf";
            names[1] = "drivers/win98/dgpumini.drv";
            names[2] = "drivers/win98/dgpumini.vxd";
            names[3] = "tools/common/DG9INST.EXE";
            names[4] = "tools/common/DG9AUDIT.EXE";
        }
        const BYTE *data[5]{};
        DWORD sizes[5]{};
        for (unsigned n = 0; n < 5; ++n) {
            unsigned found = 0;
            for (const auto &p : payloads) {
                if (p.os != unsigned(l.os) || lstrcmpA(p.path, names[n]))
                    continue;
                if (++found != 1 || !p.size || p.size > 64u * 1024 * 1024 || !p.id || p.id > 65535)
                    return false;
                HRSRC resource = FindResourceA(nullptr, MAKEINTRESOURCEA(p.id), RT_RCDATA);
                if (!resource || SizeofResource(nullptr, resource) != p.size)
                    return false;
                HGLOBAL loaded = LoadResource(nullptr, resource);
                data[n] = loaded ? static_cast<const BYTE *>(LockResource(loaded)) : nullptr;
                if (!data[n])
                    return false;
                char digest[65];
                Sha256 sha;
                sha.update(data[n], p.size);
                sha.finish(digest);
                if (lstrcmpA(digest, p.sha))
                    return false;
                sizes[n] = p.size;
            }
            if (found != 1)
                return false;
        }
        char root[MAX_PATH];
        if (!generation_root(owner, l.pending, root) || !directory(root))
            return false;
        for (unsigned n = 0; n < 5; ++n) {
            char path[MAX_PATH];
            lstrcpyA(path, root);
            size_t offset = lstrlenA(path);
            if (offset + lstrlenA(names[n]) + 2 > sizeof(path))
                return false;
            path[offset++] = '\\';
            for (const char *p = names[n];; ++p) {
                path[offset] = 0;
                if (!*p)
                    break;
                if (*p == '/') {
                    if (!directory(path))
                        return false;
                    path[offset++] = '\\';
                } else
                    path[offset++] = *p;
            }
            if (!write(path, data[n], sizes[n]))
                return false;
        }
        return true;
    }
};
} // namespace setup::driver
