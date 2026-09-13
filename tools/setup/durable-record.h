// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#ifdef DG_SETUP_ADAPTER_TEST
#include "test-win32.h"
#else
#include <windows.h>
#endif
#include "sha256.h"
namespace setup {
// Bounded append-only publication of a small typed record. The caller owns the
// directory and supplies semantic validation. Only an incomplete final append
// is discarded; a complete record with a bad hash is never silently ignored.
template <class Record> class DurableRecord {
    char path_[MAX_PATH]{};
    DWORD bytes_ = INVALID_FILE_SIZE;
    static constexpr DWORD Limit = 4u * 1024 * 1024;
    struct File {
        HANDLE h;
        explicit File(HANDLE handle) : h(handle) {}
        ~File() {
            if (h != INVALID_HANDLE_VALUE)
                CloseHandle(h);
        }
        File(const File &) = delete;
    };
    static void hash(const Record &record, char out[65]) {
        Sha256 h;
        h.update(reinterpret_cast<const BYTE *>(&record), sizeof(record));
        h.finish(out);
    }

  public:
    explicit DurableRecord(const char *path) {
        if (lstrlenA(path) < MAX_PATH)
            lstrcpyA(path_, path);
    }
    DurableRecord(const DurableRecord &) = delete;
    template <class Validate>
    bool load(Record &out, bool &exists, Validate validate, bool read_only = false) {
        exists = false;
        bytes_ = INVALID_FILE_SIZE;
        if (!path_[0])
            return false;
        DWORD attrs = GetFileAttributesA(path_);
        if (attrs == INVALID_FILE_ATTRIBUTES) {
            if (GetLastError() != ERROR_FILE_NOT_FOUND)
                return false;
            bytes_ = read_only ? INVALID_FILE_SIZE : 0;
            return true;
        }
        if (attrs & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
            return false;
        File file(CreateFileA(path_, GENERIC_READ | (read_only ? 0 : GENERIC_WRITE), 0, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (file.h == INVALID_HANDLE_VALUE)
            return false;
        DWORD high = 0, size = GetFileSize(file.h, &high);
        if (high || size > Limit)
            return false;
        constexpr DWORD step = sizeof(Record) + 64;
        DWORD good = 0;
        Record candidate{};
        while (size - good >= step) {
            DWORD got = 0;
            char stored[65]{}, actual[65];
            if (!ReadFile(file.h, &candidate, sizeof(candidate), &got, nullptr) ||
                got != sizeof(candidate) || !ReadFile(file.h, stored, 64, &got, nullptr) ||
                got != 64)
                return false;
            hash(candidate, actual);
            if (lstrcmpA(stored, actual) || !validate(candidate))
                return false;
            out = candidate;
            exists = true;
            good += step;
        }
        if (!read_only && good != size) {
            if (SetFilePointer(file.h, good, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
                !SetEndOfFile(file.h) || !FlushFileBuffers(file.h))
                return false;
        }
        bytes_ = read_only ? INVALID_FILE_SIZE : good;
        return true;
    }
    template <class Validate> bool save(const Record &record, Validate validate) {
        constexpr DWORD step = sizeof(Record) + 64;
        if (bytes_ == INVALID_FILE_SIZE || bytes_ > Limit - step || !validate(record))
            return false;
        DWORD attrs = GetFileAttributesA(path_);
        if (attrs != INVALID_FILE_ATTRIBUTES &&
            (attrs & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
            return false;
        if (attrs == INVALID_FILE_ATTRIBUTES && GetLastError() != ERROR_FILE_NOT_FOUND)
            return false;
        File file(CreateFileA(path_, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr));
        if (file.h == INVALID_HANDLE_VALUE)
            return false;
        DWORD high = 0;
        if (GetFileSize(file.h, &high) != bytes_ || high ||
            SetFilePointer(file.h, bytes_, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
            return false;
        char sha[65];
        hash(record, sha);
        DWORD wrote = 0;
        if (!WriteFile(file.h, &record, sizeof(record), &wrote, nullptr) ||
            wrote != sizeof(record) || !WriteFile(file.h, sha, 64, &wrote, nullptr) ||
            wrote != 64 || !FlushFileBuffers(file.h))
            return false;
        bytes_ += step;
        return true;
    }
};
} // namespace setup
