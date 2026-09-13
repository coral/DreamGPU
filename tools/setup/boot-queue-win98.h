// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#ifndef DG_BOOT_QUEUE_TEST
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "wininit-file.h"
namespace setup::boot {
// Windows 9x consumes DOS short paths in WININIT.INI before loading system
// DLLs. Unrelated sections and rename entries are preserved byte-for-byte.
// Queue's normalized representation is private bookkeeping, never an NT
// registry write. Source: Microsoft Programmer's Guide to Windows95, "Using a
// WININIT.INI File to Replace DLLs in Windows95" (1995), pp.165-166.
template <class Store> class Win98Ops {
    Store &store_;
    Record &record_;
    const char *root_;
    WininitFile file_;
    WininitDocument *input_ = nullptr, *output_ = nullptr;
    bool opened_ = false, input_exists_ = false;
    static bool local(const char *p) {
        if (!text(p, path_capacity) ||
            !((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) || p[1] != ':' ||
            p[2] != '\\')
            return false;
        for (unsigned i = 3; p[i]; ++i)
            if ((unsigned char)p[i] < 32 || p[i] == '/' || p[i] == ':' || p[i] == '=' ||
                p[i] == '"' || p[i] == '[' || p[i] == ']')
                return false;
        return true;
    }
    bool short_path(const char *p, char (&out)[path_capacity]) {
        if (!local(p))
            return false;
        // Resolve dot segments before DOS alias comparison. Preserve original INI
        // bytes separately; this canonical path is only an ownership identity.
        char full[path_capacity]{};
        DWORD n = GetFullPathNameA(p, path_capacity, full, nullptr);
        if (!n || n >= path_capacity || !local(full))
            return false;
        p = full;
        n = GetShortPathNameA(p, out, path_capacity);
        if (!n) {
            if (GetLastError() != ERROR_FILE_NOT_FOUND)
                return false;
            char parent[path_capacity];
            lstrcpyA(parent, p);
            unsigned end = lstrlenA(parent);
            while (end && parent[end - 1] != '\\')
                --end;
            if (!end || !p[end])
                return false;
            unsigned base = 0, ext = 0;
            bool dot = false;
            for (unsigned i = end; p[i]; ++i) {
                char c = p[i];
                if (c == '.') {
                    if (dot || !base)
                        return false;
                    dot = true;
                } else {
                    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9') || c == '_'))
                        return false;
                    if (dot)
                        ++ext;
                    else
                        ++base;
                }
            }
            if (!base || base > 8 || ext > 3 || (dot && !ext))
                return false;
            parent[end == 3 ? end : end - 1] = 0;
            n = GetShortPathNameA(parent, out, path_capacity);
            if (!n || n >= path_capacity - 14)
                return false;
            if (out[n - 1] != '\\')
                out[n++] = '\\';
            lstrcpyA(out + n, p + end);
            n = lstrlenA(out);
        }
        if (n >= path_capacity || !n)
            return false;
        // DOS OEM/ANSI translation is ambiguous for non-ASCII short names.
        // Reject before publishing a boot operation instead of renaming a
        // different file. Standard Windows-owned system/private paths are ASCII.
        for (unsigned i = 0; i < n; ++i) {
            if ((unsigned char)out[i] >= 127 || out[i] == ' ')
                return false;
            if (out[i] >= 'a' && out[i] <= 'z')
                out[i] -= 32;
        }
        return local(out);
    }
    bool pair(const Entry &e, char (&dest)[path_capacity], char (&src)[path_capacity]) {
        if (!short_path(e.desired.exists ? e.source : e.destination, src))
            return false;
        if (!e.desired.exists) {
            lstrcpyA(dest, "NUL");
            return true;
        }
        return short_path(e.destination, dest);
    }
    // -1 foreign, -2 collision/error, nonnegative exact journal entry.
    int owner(const char *dest, const char *source) {
        char canonical_dest[path_capacity]{}, canonical_source[path_capacity]{};
        if (!short_path(source, canonical_source))
            return -2;
        if (!lstrcmpiA(dest, "NUL"))
            lstrcpyA(canonical_dest, "NUL");
        else if (!short_path(dest, canonical_dest))
            return -2;
        dest = canonical_dest;
        source = canonical_source;
        int found = -1;
        for (unsigned n = 0; n < record_.count; ++n) {
            if (record_.completed & (1u << n))
                continue;
            char d[path_capacity]{}, s[path_capacity]{};
            if (!pair(record_.entries[n], d, s))
                return -2;
            bool sd = !lstrcmpiA(dest, d), ss = !lstrcmpiA(source, s);
            if (sd && ss) {
                if (found >= 0)
                    return -2;
                found = int(n);
            } else if (ss || (sd && lstrcmpiA(dest, "NUL") != 0) || !lstrcmpiA(source, d) ||
                       !lstrcmpiA(dest, s))
                return -2;
        }
        return found;
    }
    bool normalize(RawQueue &out, const char *dest, const char *source) {
        bool deletion = !lstrcmpiA(dest, "NUL");
        for (unsigned side = 0; side < 2; ++side) {
            if (out.words + path_capacity + 6 >= queue_words)
                return false;
            if (side && deletion) {
                out.data[out.words++] = 0;
                continue;
            }
            if (side)
                out.data[out.words++] = '!';
            out.data[out.words++] = '\\';
            out.data[out.words++] = '?';
            out.data[out.words++] = '?';
            out.data[out.words++] = '\\';
            const char *p = side ? dest : source;
            for (unsigned n = 0;; ++n) {
                char c = p[n];
                if (c >= 'a' && c <= 'z')
                    c -= 32;
                out.data[out.words++] = (uint8_t)c;
                if (!c)
                    break;
            }
        }
        return true;
    }

  public:
    Win98Ops(Store &store, Record &record, const char *root)
        : store_(store), record_(record), root_(root) {}
    Win98Ops(const Win98Ops &) = delete;
    ~Win98Ops() {
        if (input_)
            HeapFree(GetProcessHeap(), 0, input_);
        if (output_)
            HeapFree(GetProcessHeap(), 0, output_);
    }
    bool open(bool) {
        OSVERSIONINFOA os{};
        os.dwOSVersionInfoSize = sizeof(os);
        if (opened_ || input_ || output_ || !GetVersionExA(&os) ||
            os.dwPlatformId != VER_PLATFORM_WIN32_WINDOWS || os.dwMajorVersion != 4 ||
            os.dwMinorVersion != 10)
            return false;
        char path[MAX_PATH];
        UINT n = GetWindowsDirectoryA(path, MAX_PATH);
        if (!n || n >= MAX_PATH - 14)
            return false;
        lstrcatA(path, "\\WININIT.INI");
        input_ = wininit_object<WininitDocument>();
        output_ = wininit_object<WininitDocument>();
        if (!input_ || !output_ || !file_.open(root_, path))
            return false;
        opened_ = true;
        return true;
    }
    bool save(const Record &r) {
        return store_.save(r);
    }
    bool inspect(const char *p, File &out) {
        return store_.inspect(p, out);
    }
    bool approved(const Entry &e) {
        if (!opened_ || e.protected_target || !local(e.destination) ||
            (e.desired.exists && !local(e.source)) || !store_.approved(e))
            return false;
        char windows[path_capacity];
        UINT length = GetWindowsDirectoryA(windows, path_capacity);
        if (!length || length >= path_capacity - 24)
            return false;
        lstrcatA(windows, "\\SYSBCKUP\\");
        const unsigned prefix_length = lstrlenA(windows);
        for (const char *name : {"ddraw.dll", "d3d8.dll", "d3d9.dll"}) {
            windows[prefix_length] = 0;
            lstrcatA(windows, name);
            if (!lstrcmpiA(e.destination, windows))
                return true;
        }
        char system[path_capacity];
        UINT n = GetSystemDirectoryA(system, path_capacity);
        if (!n || n >= path_capacity || lstrlenA(e.destination) <= int(n) ||
            e.destination[n] != '\\')
            return false;
        char prefix[path_capacity];
        lstrcpyA(prefix, e.destination);
        prefix[n] = 0;
        if (lstrcmpiA(prefix, system))
            return false;
        for (unsigned i = n + 1; e.destination[i]; ++i)
            if (e.destination[i] == '\\')
                return false;
        return true;
    }
    unsigned encode(const char *p, uint16_t *out, unsigned capacity) {
        char shortname[path_capacity]{};
        if (!short_path(p, shortname))
            return 0;
        unsigned n = lstrlenA(shortname) + 1;
        if (n > capacity)
            return 0;
        for (unsigned i = 0; i < n; ++i)
            out[i] = (uint8_t)shortname[i];
        return n;
    }
    bool pending(RawQueue &out) {
        out = {};
        uint32_t seen = 0;
        if (!file_.read(*input_, input_exists_))
            return false;
        if (!input_->lines([&](const WininitDocument::Line &line) {
                char dest[path_capacity]{}, src[path_capacity]{};
                bool is_pair = false;
                if (!WininitDocument::pair(*input_, line, dest, src, is_pair))
                    return false;
                if (!is_pair)
                    return true;
                int n = owner(dest, src);
                if (n == -2)
                    return false;
                if (n < 0)
                    return true;
                if (seen & (1u << n))
                    return false;
                seen |= 1u << n;
                if (!pair(record_.entries[n], dest, src))
                    return false;
                return normalize(out, dest, src);
            }))
            return false;
        if (out.words) {
            out.exists = true;
            out.data[out.words++] = 0;
        }
        return true;
    }
    bool secondary_absent() {
        return true;
    }
    Permission permission() {
        return Permission::absent;
    }
    bool grant() {
        return false;
    }
    bool remove_permission() {
        return false;
    }
    bool enqueue(const Entry &e) {
        if (!approved(e))
            return false;
        RawQueue current;
        if (!pending(current))
            return false;
        // pending() validated this exact immutable snapshot. A second read
        // here could introduce an unchecked conflicting foreign rename.
        const bool existed = input_exists_;
        char dest[path_capacity]{}, src[path_capacity]{};
        if (!pair(e, dest, src))
            return false;
        unsigned insertion = input_->bytes;
        bool section = false;
        if (!input_->lines([&](const WininitDocument::Line &line) {
                if (line.rename) {
                    section = true;
                    insertion = line.end;
                }
                return true;
            }))
            return false;
        output_->bytes = 0;
        if (!output_->append(input_->data, insertion))
            return false;
        if (output_->bytes && output_->data[output_->bytes - 1] != '\n' &&
            !output_->append("\r\n", 2))
            return false;
        if (!section && !output_->append("[rename]\r\n", 10))
            return false;
        if (!output_->append(dest, lstrlenA(dest)) || !output_->append("=", 1) ||
            !output_->append(src, lstrlenA(src)) || !output_->append("\r\n", 2) ||
            !output_->append(input_->data + insertion, input_->bytes - insertion))
            return false;
        return file_.replace(*input_, existed, *output_);
    }
    bool remove_pending() {
        bool existed = false;
        if (!file_.read(*input_, existed))
            return false;
        output_->bytes = 0;
        if (!input_->lines([&](const WininitDocument::Line &line) {
                char dest[path_capacity]{}, src[path_capacity]{};
                bool is_pair = false;
                if (!WininitDocument::pair(*input_, line, dest, src, is_pair))
                    return false;
                int n = is_pair ? owner(dest, src) : -1;
                if (n == -2)
                    return false;
                return n >= 0 || output_->append(input_->data + line.start, line.end - line.start);
            }))
            return false;
        return file_.replace(*input_, existed, *output_);
    }
    bool flush() {
        return opened_;
    } // WininitFile flushes each staged file/receipt.
};
} // namespace setup::boot
