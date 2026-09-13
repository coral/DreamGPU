// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "global-record.h"
#include "runtime-resume.h"
namespace setup::global {
// An explicit replacement executor is independent of every payload generation.
// Its intent binds the exact old coordinator record; only reverse completion
// may subsequently change that record's phase. The old executables stay intact.
inline void copy_record_bytes(Record &to, const Record &from) {
    auto *out = reinterpret_cast<unsigned char *>(&to);
    const auto *in = reinterpret_cast<const unsigned char *>(&from);
    for (unsigned n = 0; n < sizeof(Record); ++n)
        out[n] = in[n];
}
struct RecoveryRecord {
    uint32_t magic = 0x31435244, version = 1, active = 0;
    Record origin{};
    lifecycle::Image executor{};
};
inline bool valid_recovery(const RecoveryRecord &r) {
    return r.magic == 0x31435244 && r.version == 1 && r.active <= 1 && valid_record(r.origin) &&
           r.origin.os == Os::nt5 && r.origin.flow.phase == Phase::restore_driver &&
           (r.origin.flow.intent == Intent::rollback ||
            r.origin.flow.intent == Intent::uninstall) &&
           r.origin.driver_target.id && r.executor.exists &&
           lifecycle::valid_image(r.executor, lifecycle::Kind::file) &&
           !lifecycle::same(r.executor, r.origin.installer);
}
inline bool recovery_origin(const RecoveryRecord &r, const Record &current) {
    if (!valid_recovery(r) || !valid_record(current) ||
        (current.flow.phase != Phase::restore_driver && current.flow.phase != Phase::restored))
        return false;
    Record normalized{};
    copy_record_bytes(normalized, current);
    normalized.flow.phase = Phase::restore_driver;
    // These are the same persisted POD bytes, not a reconstructed approximation
    // of the payload/driver/intent identity whose authority is being retained.
    const auto *a = reinterpret_cast<const unsigned char *>(&normalized);
    const auto *b = reinterpret_cast<const unsigned char *>(&r.origin);
    for (unsigned n = 0; n < sizeof(Record); ++n)
        if (a[n] != b[n])
            return false;
    return true;
}
class Recovery {
    lifecycle::Win32Store &owner_;
    char receipt_[MAX_PATH]{}, directory_[MAX_PATH]{}, executable_[MAX_PATH]{};
    RecoveryRecord record_{};
    bool configured_ = false, exists_ = false;
    bool path(char *out, const char *format, uint32_t epoch) {
        char suffix[40];
        wsprintfA(suffix, format, epoch);
        return owner_.private_path(out, suffix);
    }

  public:
    Recovery(lifecycle::Win32Store &owner, uint32_t epoch) : owner_(owner) {
        configured_ = path(receipt_, "\\REC%08lX.JRN", epoch) &&
                      path(directory_, "\\R%08lX", epoch) &&
                      path(executable_, "\\R%08lX\\setup.exe", epoch);
    }
    bool load() {
        if (!configured_)
            return false;
        DurableRecord<RecoveryRecord> disk(receipt_);
        return disk.load(record_, exists_, valid_recovery);
    }
    bool exists() const {
        return exists_;
    }
    bool active() const {
        return record_.active != 0;
    }
    const char *executable() const {
        return executable_;
    }
    const char *sha() const {
        return record_.executor.sha;
    }
    bool matches(const Record &origin, const lifecycle::Image &executing) const {
        return exists_ && recovery_origin(record_, origin) &&
               lifecycle::same(executing, record_.executor);
    }
    bool prepare(const Record &origin, const lifecycle::Image &executing, bool arm = true) {
        if (!configured_)
            return false;
        if (!exists_) {
            // Never adopt an already existing recovery program directory.
            DWORD attributes = GetFileAttributesA(directory_);
            if (attributes != INVALID_FILE_ATTRIBUTES || GetLastError() != ERROR_FILE_NOT_FOUND)
                return false;
            RecoveryRecord next{};
            copy_record_bytes(next.origin, origin);
            next.executor = executing;
            DurableRecord<RecoveryRecord> disk(receipt_);
            bool present;
            RecoveryRecord loaded;
            if (!disk.load(loaded, present, valid_recovery) || present ||
                !disk.save(next, valid_recovery))
                return false;
            record_ = next;
            exists_ = true;
        }
        if (!matches(origin, executing))
            return false;
        DWORD attributes = GetFileAttributesA(directory_);
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            if (GetLastError() != ERROR_FILE_NOT_FOUND || !CreateDirectoryA(directory_, nullptr))
                return false;
        } else if (!(attributes & FILE_ATTRIBUTE_DIRECTORY) ||
                   (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            return false;
        lifecycle::RuntimeResume resume(owner_, origin.flow.epoch, origin.os,
                                        lifecycle::ResumeScope::recovery);
        lifecycle::Image retained;
        return resume.prepare() && owner_.inspect_file(executable_, retained) &&
               lifecycle::same(retained, record_.executor) && (!arm || resume.arm());
    }
    bool publish() {
        DurableRecord<RecoveryRecord> disk(receipt_);
        RecoveryRecord loaded;
        bool exists;
        if (!disk.load(loaded, exists, valid_recovery) || !exists ||
            !recovery_origin(loaded, record_.origin) ||
            !lifecycle::same(loaded.executor, record_.executor))
            return false;
        loaded.active = 1;
        if (!disk.save(loaded, valid_recovery))
            return false;
        record_ = loaded;
        return true;
    }
};
} // namespace setup::global
