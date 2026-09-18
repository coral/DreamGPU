// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "progress.h"
#include "policy.h"
#include "sha256.h"
namespace setup::staging {
enum class Presence { absent, present, error };
inline Presence ticket(const char *name) {
    char path[MAX_PATH];
    DWORD n = GetWindowsDirectoryA(path, MAX_PATH);
    if (!n || n >= MAX_PATH - 16)
        return Presence::error;
    lstrcatA(path, name);
    DWORD attributes = GetFileAttributesA(path);
    if (attributes == INVALID_FILE_ATTRIBUTES)
        return GetLastError() == ERROR_FILE_NOT_FOUND ? Presence::absent : Presence::error;
    return attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)
               ? Presence::error
               : Presence::present;
}
inline Presence cancelling() {
    return ticket("\\DGSETUP.CAN");
}
inline Presence pending() {
    auto journal = ticket("\\DGSETUP.JRN"), cancel = cancelling();
    if (journal == Presence::error || cancel == Presence::error ||
        (journal == Presence::present && cancel == Presence::present))
        return Presence::error;
    return journal == Presence::present || cancel == Presence::present ? Presence::present
                                                                       : Presence::absent;
}
// The process-wide installer lock serializes this store. Resource pointers stay
// immutable/live until finish; all entries are authenticated before begin.
class Store {
    static constexpr unsigned Limit = 2048;
    struct Node {
        char path[MAX_PATH]{};
        const BYTE *data = nullptr;
        DWORD size = 0;
        bool directory = false;
    } nodes_[Limit]{};
    struct Intent {
        uint32_t magic = 0x31534744, version = 1, os = 0, count = 0;
        char installer[68]{}, catalog[68]{};
    } intent_{};
    static_assert(sizeof(Intent) == 152, "Versioned staging receipt layout");
    struct File {
        HANDLE h;
        explicit File(HANDLE v) : h(v) {}
        ~File() {
            if (h != INVALID_HANDLE_VALUE)
                CloseHandle(h);
        }
        File(const File &) = delete;
    };
    char root_[MAX_PATH]{}, final_[MAX_PATH]{}, receipt_[MAX_PATH]{}, cancel_[MAX_PATH]{};
    unsigned count_ = 0;
    bool begun_ = false, committed_ = false, cancelled_ = false, cancel_only_ = false;
    static bool equal(const BYTE *a, const BYTE *b, DWORD n) {
        for (DWORD i = 0; i < n; ++i)
            if (a[i] != b[i])
                return false;
        return true;
    }
    static bool same_path(const char *a, const char *b) {
        for (unsigned i = 0; i < MAX_PATH; ++i) {
            auto x = static_cast<unsigned char>(a[i]), y = static_cast<unsigned char>(b[i]);
            if (x >= 'A' && x <= 'Z')
                x += 'a' - 'A';
            if (y >= 'A' && y <= 'Z')
                y += 'a' - 'A';
            if (x != y)
                return false;
            if (!x)
                return true;
        }
        return false;
    }
    static bool absent(const char *path, bool owned_descendant = false) {
        if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES)
            return false;
        const DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || (owned_descendant && error == ERROR_PATH_NOT_FOUND);
    }
    static bool directory(const char *path) {
        DWORD a = GetFileAttributesA(path);
        return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) &&
               !(a & FILE_ATTRIBUTE_REPARSE_POINT);
    }
    static bool join(const char *base, const char *relative, char out[MAX_PATH]) {
        if (lstrlenA(base) + lstrlenA(relative) + 2 >= MAX_PATH)
            return false;
        lstrcpyA(out, base);
        lstrcatA(out, "\\");
        lstrcatA(out, relative);
        return true;
    }
    // Read through a held non-sharing handle: only an exact immutable prefix
    // can be extended. Never truncate or replace a mismatching file.
    static bool contents(const char *path, const BYTE *bytes, DWORD size, bool complete,
                         bool write) {
        DWORD a = GetFileAttributesA(path);
        bool missing = a == INVALID_FILE_ATTRIBUTES;
        if ((missing && (!write || GetLastError() != ERROR_FILE_NOT_FOUND)) ||
            (!missing && (a & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))))
            return false;
        File f(CreateFileA(path, GENERIC_READ | (write ? GENERIC_WRITE : 0), 0, nullptr,
                           missing ? CREATE_NEW : OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (f.h == INVALID_HANDLE_VALUE)
            return false;
        DWORD high = 0, existing = GetFileSize(f.h, &high);
        if (high || existing > size || (complete && !write && existing != size))
            return false;
        BYTE buffer[4096];
        DWORD offset = 0;
        while (offset < existing) {
            DWORD n = existing - offset, got = 0;
            if (n > sizeof(buffer))
                n = sizeof(buffer);
            if (!ReadFile(f.h, buffer, n, &got, nullptr) || got != n ||
                !equal(buffer, bytes + offset, n))
                return false;
            offset += n;
        }
        if (!write)
            return true;
        while (offset < size) {
            DWORD n = size - offset, written = 0;
            if (n > 65536)
                n = 65536;
            if (!WriteFile(f.h, bytes + offset, n, &written, nullptr) || written != n)
                return false;
            offset += n;
        }
        return FlushFileBuffers(f.h);
    }
    int locate(const char *relative) const {
        for (unsigned n = 0; n < count_; ++n)
            if (same_path(relative, nodes_[n].path))
                return int(n);
        return -1;
    }
    bool node(const char *relative, bool dir, const BYTE *data, DWORD size) {
        int previous = locate(relative);
        if (previous >= 0)
            return dir && nodes_[previous].directory;
        if (count_ == Limit || lstrlenA(relative) >= MAX_PATH)
            return false;
        Node &n = nodes_[count_++];
        lstrcpyA(n.path, relative);
        n.directory = dir;
        n.data = data;
        n.size = size;
        return true;
    }
    // Enumerate each declared directory, refusing extra paths (including links).
    // No unbounded recursion: the catalog supplies all traversal nodes.
    bool tree(const char *base, bool complete) const {
        if (!directory(base))
            return false;
        for (unsigned d = 0; d <= count_; ++d) {
            if (d && !nodes_[d - 1].directory)
                continue;
            const char *relative = d ? nodes_[d - 1].path : "";
            char path[MAX_PATH], pattern[MAX_PATH];
            if (d) {
                if (!join(base, relative, path))
                    return false;
            } else
                lstrcpyA(path, base);
            if (absent(path, true)) {
                if (complete)
                    return false;
                else
                    continue;
            }
            if (!directory(path) || !join(path, "*", pattern))
                return false;
            WIN32_FIND_DATAA found{};
            HANDLE scan = FindFirstFileA(pattern, &found);
            if (scan == INVALID_HANDLE_VALUE) {
                if (GetLastError() != ERROR_FILE_NOT_FOUND)
                    return false;
                continue;
            }
            bool okay = true;
            unsigned entries = 0;
            do {
                if (!lstrcmpA(found.cFileName, ".") || !lstrcmpA(found.cFileName, ".."))
                    continue;
                char child[MAX_PATH];
                if (++entries > Limit || (d ? !join(relative, found.cFileName, child)
                                            : lstrlenA(found.cFileName) >= MAX_PATH)) {
                    okay = false;
                    break;
                }
                if (!d)
                    lstrcpyA(child, found.cFileName);
                int index = locate(child);
                if (index < 0 || (found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) ||
                    bool(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) !=
                        nodes_[index].directory) {
                    okay = false;
                    break;
                }
            } while (FindNextFileA(scan, &found));
            DWORD error = GetLastError();
            FindClose(scan);
            if (!okay || error != ERROR_NO_MORE_FILES)
                return false;
        }
        for (unsigned n = 0; n < count_; ++n) {
            char path[MAX_PATH];
            if (!join(base, nodes_[n].path, path))
                return false;
            if (absent(path, true)) {
                if (complete)
                    return false;
                else
                    continue;
            }
            if (nodes_[n].directory
                    ? !directory(path)
                    : !contents(path, nodes_[n].data, nodes_[n].size, complete, false))
                return false;
        }
        return true;
    }

  public:
    // Caller hashes the actual running executable, not its staged alias.
    bool reset(Os os, const char *installer_sha) {
        count_ = 0;
        begun_ = committed_ = cancelled_ = cancel_only_ = false;
        intent_ = {};
        root_[0] = final_[0] = receipt_[0] = cancel_[0] = 0;
        if ((os != Os::win98 && os != Os::nt5) || lstrlenA(installer_sha) != 64)
            return false;
        for (unsigned i = 0; i < 64; ++i)
            if (!((installer_sha[i] >= '0' && installer_sha[i] <= '9') ||
                  (installer_sha[i] >= 'a' && installer_sha[i] <= 'f')))
                return false;
        intent_.os = static_cast<uint32_t>(os);
        lstrcpyA(intent_.installer, installer_sha);
        char windows[MAX_PATH];
        DWORD n = GetWindowsDirectoryA(windows, MAX_PATH);
        return n && n < MAX_PATH - 20 && directory(windows) &&
               join(windows, "DGSETUP.NEW", root_) && join(windows, "DreamGPU", final_) &&
               join(windows, "DGSETUP.JRN", receipt_) && join(windows, "DGSETUP.CAN", cancel_);
    }
    bool add(const char *relative, const BYTE *data, DWORD bytes, const char *expected = nullptr) {
        if (begun_ || !safe_path(relative) || (!data && bytes))
            return false;
        Sha256 hash;
        char actual[65];
        hash.update(data, bytes);
        hash.finish(actual);
        if (expected && lstrcmpA(actual, expected))
            return false;
        char path[MAX_PATH], full[MAX_PATH];
        if (!root_[0] || lstrlenA(relative) >= MAX_PATH || !join(root_, relative, full))
            return false;
        lstrcpyA(path, relative);
        for (unsigned i = 0; path[i]; ++i) {
            if (path[i] != '/')
                continue;
            path[i] = 0;
            if (!node(path, true, nullptr, 0))
                return false;
            path[i] = '\\';
        }
        return node(path, false, data, bytes);
    }
    bool begin(bool cancel = false) {
        if (begun_ || !count_ || !receipt_[0])
            return false;
        Sha256 hash;
        for (unsigned n = 0; n < count_; ++n) {
            auto &v = nodes_[n];
            hash.update(reinterpret_cast<const BYTE *>(v.path), lstrlenA(v.path) + 1);
            BYTE dir = v.directory;
            hash.update(&dir, 1);
            hash.update(reinterpret_cast<const BYTE *>(&v.size), sizeof(v.size));
            if (!v.directory) {
                Sha256 content;
                char sha[65];
                content.update(v.data, v.size);
                content.finish(sha);
                hash.update(reinterpret_cast<const BYTE *>(sha), 64);
            }
        }
        hash.finish(intent_.catalog);
        intent_.count = count_;
        const auto *raw = reinterpret_cast<const BYTE *>(&intent_);
        if (!absent(cancel_)) {
            if (!cancel || !absent(receipt_) ||
                !contents(cancel_, raw, sizeof(intent_), true, false))
                return false;
            cancelled_ = true;
        } else if (absent(receipt_)) {
            if (cancel)
                return false;
            if (!absent(root_) || !absent(final_))
                return false;
            // Publish full binding before touching the staged tree. A foreign
            // or torn unbound first receipt is never treated as ownership.
            bool published = false;
            {
                File f(CreateFileA(receipt_, GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                   FILE_ATTRIBUTE_NORMAL, nullptr));
                if (f.h == INVALID_HANDLE_VALUE)
                    return false;
                DWORD wrote = 0;
                published = WriteFile(f.h, raw, sizeof(intent_), &wrote, nullptr) &&
                            wrote == sizeof(intent_) && FlushFileBuffers(f.h);
            }
            if (!published) {
                // This process exclusively created the metadata, so it may
                // clean its exact partial bytes. A future process cannot make
                // that claim for an unbound/torn first receipt.
                if (contents(receipt_, raw, sizeof(intent_), false, false))
                    DeleteFileA(receipt_);
                return false;
            }
        } else if (!contents(receipt_, raw, sizeof(intent_), true, false))
            return false;
        if (!contents(cancelled_ ? cancel_ : receipt_, raw, sizeof(intent_), true, false))
            return false;
        cancel_only_ = cancel;
        if (!absent(final_)) {
            if (!absent(root_) || !tree(final_, !cancel))
                return false;
            committed_ = true;
        } else {
            if (absent(root_) && !cancel && !CreateDirectoryA(root_, nullptr))
                return false;
            if (!absent(root_) && !tree(root_, false))
                return false;
        }
        begun_ = true;
        return true;
    }
    bool copy() {
        if (!begun_ || cancelled_ || cancel_only_)
            return false;
        if (committed_)
            return tree(final_, true);
        if (!tree(root_, false))
            return false;
        for (unsigned n = 0; n < count_; ++n) {
            char path[MAX_PATH];
            if (!join(root_, nodes_[n].path, path))
                return false;
            progress(nodes_[n].directory ? "Creating directory" : "Extracting file", path);
            if (nodes_[n].directory) {
                if (!directory(path) && (!absent(path) || !CreateDirectoryA(path, nullptr)))
                    return false;
            } else if (!contents(path, nodes_[n].data, nodes_[n].size, false, true))
                return false;
        }
        return tree(root_, true);
    }
    bool commit() {
        if (!begun_ || cancelled_ || cancel_only_)
            return false;
        if (committed_)
            return tree(final_, true);
        if (!tree(root_, true) || !absent(final_) || !MoveFileA(root_, final_))
            return false;
        committed_ = true;
        return tree(final_, true);
    }
    bool cancel() {
        if (!begun_)
            return false;
        const char *base = committed_ ? final_ : root_;
        if (!absent(base) && !tree(base, false))
            return false;
        if (!cancelled_) {
            if (!contents(receipt_, reinterpret_cast<const BYTE *>(&intent_), sizeof(intent_), true,
                          false) ||
                !absent(cancel_) || !MoveFileA(receipt_, cancel_))
                return false;
            cancelled_ = true;
        }
        // Catalog order creates parents before children, so reverse traversal
        // removes only verified children before their now-empty directories.
        for (unsigned n = count_; n; --n) {
            const auto &entry = nodes_[n - 1];
            char path[MAX_PATH];
            if (!join(base, entry.path, path))
                return false;
            if (absent(path, true))
                continue;
            if (entry.directory) {
                if (!directory(path) || !RemoveDirectoryA(path))
                    return false;
            } else if (!contents(path, entry.data, entry.size, false, false) || !DeleteFileA(path))
                return false;
        }
        if (!absent(base) && (!directory(base) || !RemoveDirectoryA(base)))
            return false;
        if (!contents(cancel_, reinterpret_cast<const BYTE *>(&intent_), sizeof(intent_), true,
                      false) ||
            !DeleteFileA(cancel_))
            return false;
        begun_ = false;
        return true;
    }
    // Call before component preparation adds its own journal files. A rename interrupted before
    // acknowledgement is recognized by the same exact final catalog on restart.
    bool finish() {
        return begun_ && committed_ && !cancelled_ && !cancel_only_ && tree(final_, true) &&
               contents(receipt_, reinterpret_cast<const BYTE *>(&intent_), sizeof(intent_), true,
                        false) &&
               DeleteFileA(receipt_);
    }
};
} // namespace setup::staging
