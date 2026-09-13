// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "wininit-document.h"
#include "sha256.h"
#include <new>
#include <type_traits>
namespace setup::boot {
// HeapAlloc supplies storage, not C++ object construction. These bounded
// aggregates have trivial destruction; begin their lifetime explicitly.
template <class T> T *wininit_object() {
    static_assert(std::is_trivially_destructible_v<T>);
    void *storage = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(T));
    return storage ? ::new (storage) T{} : nullptr;
}
// Reserved W9* names live in the installer's authenticated private generation.
// Publish a flushed before/after receipt before touching WININIT.INI. MoveFileA
// cannot replace an existing file on Win98, so keep the exact old file under a
// private name until the replacement and completion marker are durable.
class WininitFile {
    struct Handle {
        HANDLE h;
        explicit Handle(HANDLE value) : h(value) {}
        Handle(const Handle &) = delete;
        ~Handle() {
            if (h != INVALID_HANDLE_VALUE)
                CloseHandle(h);
        }
    };
    struct Frame {
        uint32_t magic = 0x31464957, version = 1, existed = 0, attributes = FILE_ATTRIBUTE_NORMAL;
        WininitDocument before{}, after{};
    };
    Frame *frame_ = nullptr;
    char root_[MAX_PATH]{}, path_[MAX_PATH]{};
    unsigned slot_ = 0;
    static bool absent(const char *p) {
        return GetFileAttributesA(p) == INVALID_FILE_ATTRIBUTES &&
               GetLastError() == ERROR_FILE_NOT_FOUND;
    }
    bool name(char *p, const char *extension) const {
        if (lstrlenA(root_) > MAX_PATH - 24)
            return false;
        wsprintfA(p, "%s\\W9%02u.%s", root_, slot_, extension);
        return true;
    }
    static void hash(const void *data, unsigned size, char (&out)[65]) {
        setup::Sha256 h;
        h.update(static_cast<const uint8_t *>(data), size);
        h.finish(out);
    }
    static bool write_new(const char *path, const void *data, DWORD bytes) {
        Handle h(CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                             nullptr));
        DWORD written = 0;
        return h.h != INVALID_HANDLE_VALUE && WriteFile(h.h, data, bytes, &written, nullptr) &&
               written == bytes && FlushFileBuffers(h.h);
    }
    static bool read_exact(const char *path, void *data, DWORD bytes) {
        Handle h(
            CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
        DWORD high = 0, got = 0;
        return h.h != INVALID_HANDLE_VALUE && GetFileSize(h.h, &high) == bytes && !high &&
               ReadFile(h.h, data, bytes, &got, nullptr) && got == bytes;
    }
    static bool same(const WininitDocument &a, const WininitDocument &b) {
        if (a.bytes != b.bytes)
            return false;
        for (unsigned n = 0; n < a.bytes; ++n)
            if (a.data[n] != b.data[n])
                return false;
        return true;
    }
    bool matches(const char *path, const WininitDocument &expected, bool exists) {
        if (!exists)
            return absent(path);
        // Avoid two 64 KiB documents on the legacy thread stack.
        auto *data = static_cast<char *>(
            HeapAlloc(GetProcessHeap(), 0, expected.bytes ? expected.bytes : 1));
        if (!data)
            return false;
        bool ok = read_exact(path, data, expected.bytes);
        for (unsigned n = 0; ok && n < expected.bytes; ++n)
            ok = data[n] == expected.data[n];
        HeapFree(GetProcessHeap(), 0, data);
        return ok;
    }
    bool original_matches(const char *path) {
        return GetFileAttributesA(path) == frame_->attributes &&
               matches(path, frame_->before, true);
    }
    bool reject_original(const char *old, const char *conflict, const char *scratch,
                         const char (&digest)[65]) {
        // Restoring the original removes our private evidence of its mismatch.
        // Persist rejection first, so even coincidentally matching after bytes
        // can never be adopted by a later recovery attempt.
        if (absent(conflict)) {
            if (!absent(scratch) && !DeleteFileA(scratch))
                return false;
            if (!write_new(scratch, digest, sizeof(digest)) || !MoveFileA(scratch, conflict))
                return false;
        } else {
            char actual[65]{};
            if (!read_exact(conflict, actual, sizeof(actual)) || actual[64] ||
                lstrcmpA(actual, digest))
                return false;
        }
        if (absent(path_) && !absent(old))
            MoveFileA(old, path_);
        return false;
    }
    bool finish() {
        char old[MAX_PATH], next[MAX_PATH], marker[MAX_PATH], scratch[MAX_PATH], ready[MAX_PATH],
            conflict[MAX_PATH];
        if (!name(old, "old") || !name(next, "new") || !name(marker, "done") ||
            !name(scratch, "tmp") || !name(ready, "ready") || !name(conflict, "conflict"))
            return false;
        char digest[65];
        hash(frame_, sizeof(*frame_), digest);
        if (!absent(conflict))
            return reject_original(old, conflict, scratch, digest);
        if (!absent(marker)) {
            char actual[65];
            return read_exact(marker, actual, sizeof(actual)) && actual[64] == 0 &&
                   !lstrcmpA(actual, digest);
        }
        // A crash may have interrupted restoration of a foreign update that
        // won the race between our before check and the original-file move.
        // Never publish stale after bytes or acknowledge this edit in that case.
        if (frame_->existed && !absent(old) && !original_matches(old))
            return reject_original(old, conflict, scratch, digest);
        bool after = matches(path_, frame_->after, true);
        if (!after && absent(path_) && absent(next)) {
            // A reboot can consume the published queue before its completion
            // marker was flushed. WININIT retains the exact consumed INI as
            // WININIT.BAK; require those bytes rather than replaying stale paths.
            char consumed[MAX_PATH];
            lstrcpyA(consumed, path_);
            const unsigned length = lstrlenA(consumed);
            if (length < 3)
                return false;
            lstrcpyA(consumed + length - 3, "BAK");
            char staged[65]{};
            after = read_exact(ready, staged, sizeof(staged)) && staged[64] == 0 &&
                    !lstrcmpA(staged, digest) && matches(consumed, frame_->after, true);
        }
        if (!after) {
            bool before = matches(path_, frame_->before, frame_->existed != 0);
            bool moved = absent(path_) && frame_->existed && original_matches(old);
            if (!before && !moved)
                return false;
            if (!matches(next, frame_->after, true)) {
                // A torn private staging write has never been published. The
                // complete immutable receipt determines its exact replacement.
                if (!absent(next) && !DeleteFileA(next))
                    return false;
                if (!write_new(next, frame_->after.data, frame_->after.bytes))
                    return false;
            }
            // Reapply attributes even when a previous attempt wrote all bytes
            // but failed before setting them. Content equality alone is not
            // permission to publish staging with lost hidden/system flags.
            if (!SetFileAttributesA(next, frame_->attributes))
                return false;
            {
                Handle staged(CreateFileA(next, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                          OPEN_EXISTING, 0, nullptr));
                if (staged.h == INVALID_HANDLE_VALUE || !FlushFileBuffers(staged.h) ||
                    GetFileAttributesA(next) != frame_->attributes)
                    return false;
            }
            if (absent(ready)) {
                if (!absent(scratch) && !DeleteFileA(scratch))
                    return false;
                if (!write_new(scratch, digest, sizeof(digest)) || !MoveFileA(scratch, ready))
                    return false;
            } else {
                char staged[65]{};
                if (!read_exact(ready, staged, sizeof(staged)) || staged[64] ||
                    lstrcmpA(staged, digest))
                    return false;
            }
            if (before && frame_->existed) {
                if (!absent(old) || !MoveFileA(path_, old))
                    return false;
                // Verify the object actually moved, not the earlier unlocked
                // read. Restore a racing foreign edit without overwriting any
                // newer public queue; failed restoration is retried on open().
                if (!original_matches(old))
                    return reject_original(old, conflict, scratch, digest);
            }
            if (!absent(path_) || !MoveFileA(next, path_) || !matches(path_, frame_->after, true))
                return false;
        }
        // Completion is atomic publication of a small exact digest marker.
        if (!absent(scratch) && !DeleteFileA(scratch))
            return false;
        if (!write_new(scratch, digest, sizeof(digest)) || !MoveFileA(scratch, marker))
            return false;
        // Retain immutable snapshots until generation removal. In particular,
        // never delete a foreign/changed old file as part of a later edit.
        return true;
    }

  public:
    WininitFile() = default;
    WininitFile(const WininitFile &) = delete;
    ~WininitFile() {
        if (frame_)
            HeapFree(GetProcessHeap(), 0, frame_);
    }
    bool open(const char *root, const char *path) {
        if (frame_ || lstrlenA(root) >= MAX_PATH - 24 || lstrlenA(path) >= MAX_PATH)
            return false;
        lstrcpyA(root_, root);
        lstrcpyA(path_, path);
        frame_ = wininit_object<Frame>();
        if (!frame_)
            return false;
        for (slot_ = 0; slot_ < 32; ++slot_) {
            char receipt[MAX_PATH];
            if (!name(receipt, "bin"))
                return false;
            if (absent(receipt))
                return true;
            struct Stored {
                Frame value;
                char sha[65];
            };
            auto *stored = wininit_object<Stored>();
            if (!stored)
                return false;
            bool ok = read_exact(receipt, stored, sizeof(Stored));
            char digest[65];
            if (ok) {
                hash(&stored->value, sizeof(Frame), digest);
                ok = stored->value.magic == 0x31464957 && stored->value.version == 1 &&
                     stored->value.existed <= 1 &&
                     !(stored->value.attributes &
                       (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT |
                        FILE_ATTRIBUTE_READONLY)) &&
                     stored->value.before.bytes <= WininitDocument::capacity &&
                     stored->value.after.bytes <= WininitDocument::capacity &&
                     stored->sha[64] == 0 && !lstrcmpA(stored->sha, digest);
                if (ok)
                    *frame_ = stored->value;
            }
            HeapFree(GetProcessHeap(), 0, stored);
            if (!ok || !finish())
                return false;
        }
        return false;
    }
    bool read(WininitDocument &out, bool &exists) {
        out.bytes = 0;
        DWORD attr = GetFileAttributesA(path_);
        exists = attr != INVALID_FILE_ATTRIBUTES;
        if (!exists)
            return GetLastError() == ERROR_FILE_NOT_FOUND;
        if (attr &
            (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_READONLY))
            return false;
        Handle h(
            CreateFileA(path_, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
        if (h.h == INVALID_HANDLE_VALUE)
            return false;
        DWORD high = 0, got = 0, size = GetFileSize(h.h, &high);
        if (high || size > WininitDocument::capacity ||
            !ReadFile(h.h, out.data, size, &got, nullptr) || got != size)
            return false;
        out.bytes = size;
        return true;
    }
    bool replace(const WininitDocument &before, bool existed, const WininitDocument &after) {
        if (!frame_ || slot_ >= 32 || !matches(path_, before, existed))
            return false;
        if (existed && same(before, after))
            return true;
        *frame_ = {};
        frame_->existed = existed;
        frame_->attributes = existed ? GetFileAttributesA(path_) : FILE_ATTRIBUTE_NORMAL;
        if (frame_->attributes == INVALID_FILE_ATTRIBUTES ||
            (frame_->attributes &
             (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_READONLY)))
            return false;
        frame_->before = before;
        frame_->after = after;
        struct Stored {
            Frame value;
            char sha[65];
        };
        auto *stored = wininit_object<Stored>();
        if (!stored)
            return false;
        stored->value = *frame_;
        hash(frame_, sizeof(Frame), stored->sha);
        char receipt[MAX_PATH], temporary[MAX_PATH];
        bool ok = name(receipt, "bin") && name(temporary, "tmp") && absent(receipt);
        if (ok && !absent(temporary))
            ok = DeleteFileA(temporary) != FALSE;
        if (ok)
            ok = write_new(temporary, stored, sizeof(Stored)) && MoveFileA(temporary, receipt);
        HeapFree(GetProcessHeap(), 0, stored);
        if (!ok || !finish())
            return false;
        ++slot_;
        return true;
    }
};
} // namespace setup::boot
