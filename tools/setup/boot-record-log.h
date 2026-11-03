// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#if !defined(DG_SETUP_ADAPTER_TEST) && !defined(DG_BOOT_QUEUE_TEST)
#include <windows.h>
#endif
#include "boot-queue.h"
#include "sha256.h"
namespace setup::boot {
// Append-only boot-queue record log (T<generation>\APPLY.BOOT and RESTORE.BOOT).
// Each append is one complete record in its own schema followed by the SHA256
// of exactly those bytes. Earlier installers wrote schema 1 (16 entries); this
// code reads it, widens it and appends schema 2 from then on, so a log holds
// schema 1 appends followed by schema 2 appends. Only an incomplete final
// append is discarded (as DurableRecord does); a complete append with a bad
// hash, an unknown schema, schema 1 after schema 2 or a failed validation
// stops the load without changing the file.
class RecordLog {
    char path_[MAX_PATH]{};
    DWORD bytes_ = INVALID_FILE_SIZE;
    static constexpr DWORD Limit = 4u * 1024 * 1024;
    static constexpr DWORD v1_step = sizeof(RecordV1) + 64, v2_step = sizeof(Record) + 64;
    struct File {
        HANDLE h;
        explicit File(HANDLE handle) : h(handle) {}
        ~File() {
            if (h != INVALID_HANDLE_VALUE)
                CloseHandle(h);
        }
        File(const File &) = delete;
    };
    static void hash(const void *data, DWORD bytes, char out[65]) {
        Sha256 h;
        h.update(static_cast<const BYTE *>(data), bytes);
        h.finish(out);
    }
    static bool read(HANDLE h, void *out, DWORD bytes) {
        DWORD got = 0;
        return ReadFile(h, out, bytes, &got, nullptr) && got == bytes;
    }

  public:
    explicit RecordLog(const char *path) {
        if (lstrlenA(path) < MAX_PATH)
            lstrcpyA(path_, path);
    }
    RecordLog(const RecordLog &) = delete;
    // The schema of the last complete append (0 when there is none).
    uint32_t schema = 0;
    template <class Validate>
    bool load(Record &out, bool &exists, Validate validate, bool read_only = false) {
        exists = false;
        schema = 0;
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
        static Record candidate;
        static RecordV1 stored;
        DWORD good = 0;
        // Anything shorter than the smallest append is an incomplete tail.
        while (size - good >= v1_step) {
            uint32_t header[2] = {};
            if (!read(file.h, header, sizeof(header)) || header[0] != record_magic ||
                (header[1] != 1 && header[1] != record_version) ||
                (header[1] == 1 && schema == record_version))
                return false;
            const DWORD step = header[1] == 1 ? v1_step : v2_step;
            if (size - good < step)
                break; // incomplete final schema 2 append
            void *image = header[1] == 1 ? static_cast<void *>(&stored) : &candidate;
            char expected[65]{}, actual[65];
            if (!read(file.h, static_cast<BYTE *>(image) + sizeof(header),
                      step - 64 - sizeof(header)) ||
                !read(file.h, expected, 64))
                return false;
            if (header[1] == 1) {
                stored.magic = header[0];
                stored.version = header[1];
            } else {
                candidate.magic = header[0];
                candidate.version = header[1];
            }
            hash(image, step - 64, actual);
            if (lstrcmpA(expected, actual) || (header[1] == 1 && !widen(stored, candidate)) ||
                !validate(candidate))
                return false;
            out = candidate;
            exists = true;
            schema = header[1];
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
    // Appends one schema 2 record after a successful non-read-only load.
    template <class Validate> bool save(const Record &record, Validate validate) {
        if (bytes_ == INVALID_FILE_SIZE || bytes_ > Limit - v2_step ||
            record.version != record_version || !validate(record))
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
        hash(&record, sizeof(record), sha);
        DWORD wrote = 0;
        if (!WriteFile(file.h, &record, sizeof(record), &wrote, nullptr) ||
            wrote != sizeof(record) || !WriteFile(file.h, sha, 64, &wrote, nullptr) ||
            wrote != 64 || !FlushFileBuffers(file.h))
            return false;
        bytes_ += v2_step;
        schema = record_version;
        return true;
    }
};
} // namespace setup::boot
