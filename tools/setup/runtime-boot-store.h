// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win32-lifecycle.h"
#include "boot-queue.h"
#include "boot-wait.h"
#include "durable-record.h"
namespace setup::lifecycle {
// Bridge from the install journal to the OS queue. The queue can only reference
// exact destinations and source slots reconstructed from this owned generation.
// Aliases are private generated payloads; Microsoft originals are never embedded.
class RuntimeBootStore {
    Win32Store &store_;
    Journal &journal_;
    Os os_;
    bool undo_;
    char record_path_[MAX_PATH]{}, directory_[MAX_PATH]{};
    static boot::File file(const Image &image) {
        boot::File out;
        out.exists = image.exists;
        if (out.exists) {
            out.size = image.size;
            for (unsigned n = 0; n < 65; ++n)
                out.sha[n] = image.sha[n];
        }
        return out;
    }
    bool expected(unsigned n, boot::Entry &out) const {
        if (n >= journal_.count)
            return false;
        const auto &item = journal_.items[n];
        if (item.kind != Kind::file || item.phase == Phase::borrowed)
            return false;
        out = {};
        out.before = file(undo_ ? item.desired : item.before);
        out.desired = file(undo_ ? item.before : item.desired);
        if (boot::same(out.before, out.desired) || !store_.public_path(item, out.destination))
            return false;
        if (out.desired.exists &&
            !store_.private_slot(out.source, journal_.generation, n, undo_ ? "restore" : "new"))
            return false;
        if (os_ == Os::nt5)
            for (const char *name : public_runtime)
                if (!lstrcmpA(item.path, name))
                    out.protected_target = 1;
        return true;
    }
    static bool same_entry(const boot::Entry &a, const boot::Entry &b) {
        return boot::path_equal(a.source, b.source) &&
               boot::path_equal(a.destination, b.destination) && boot::same(a.before, b.before) &&
               boot::same(a.desired, b.desired) && a.protected_target == b.protected_target;
    }
    bool valid_record(const boot::Record &record) const {
        if (!boot::valid(record) || record.generation != journal_.generation)
            return false;
        for (unsigned n = 0; n < record.count; ++n)
            if (!approved(record.entries[n]))
                return false;
        return true;
    }

  public:
    RuntimeBootStore(Win32Store &store, Journal &journal, Os os, bool undo)
        : store_(store), journal_(journal), os_(os), undo_(undo) {
        char suffix[64];
        wsprintfA(suffix, "\\T%08lX", journal.generation);
        if (!store_.private_path(directory_, suffix))
            return;
        wsprintfA(suffix, undo ? "\\T%08lX\\RESTORE.BOOT" : "\\T%08lX\\APPLY.BOOT",
                  journal.generation);
        store_.private_path(record_path_, suffix);
    }
    RuntimeBootStore(const RuntimeBootStore &) = delete;
    const char *directory() const {
        return directory_;
    }
    bool approved(const boot::Entry &entry) const {
        boot::Entry allowed;
        for (unsigned n = 0; n < journal_.count; ++n)
            if (expected(n, allowed) && same_entry(allowed, entry))
                return true;
        return false;
    }
    bool inspect(const char *path, boot::File &out) const {
        bool owned = false;
        boot::Entry allowed;
        for (unsigned n = 0; n < journal_.count; ++n)
            if (expected(n, allowed) &&
                (boot::path_equal(path, allowed.destination) ||
                 (allowed.source[0] && boot::path_equal(path, allowed.source))))
                owned = true;
        Image actual;
        if (!owned || !store_.inspect_file(path, actual))
            return false;
        out = file(actual);
        return true;
    }
    bool load(boot::Record &record, bool &exists) {
        DurableRecord<boot::Record> log(record_path_);
        return record_path_[0] &&
               log.load(record, exists, [this](const auto &r) { return valid_record(r); });
    }
    bool save(const boot::Record &record) {
        DurableRecord<boot::Record> log(record_path_);
        boot::Record previous;
        bool exists;
        return record_path_[0] &&
               log.load(previous, exists, [this](const auto &r) { return valid_record(r); }) &&
               log.save(record, [this](const auto &r) { return valid_record(r); });
    }
    bool waiting(const boot::Record &record, const boot::RawQueue *foreign = nullptr,
                 boot::Permission permission = boot::Permission::absent) {
        struct Wait {
            uint32_t magic = 0x31574744, version = 1, generation = 0, waiting = 0;
            boot::Permission permission = boot::Permission::absent;
            boot::RawQueue queue{};
        };
        char suffix[64], path[MAX_PATH];
        wsprintfA(suffix, undo_ ? "\\T%08lX\\RESTORE.WAIT" : "\\T%08lX\\APPLY.WAIT",
                  journal_.generation);
        if (!store_.private_path(path, suffix))
            return false;
        auto valid = [&](const Wait &r) {
            return r.magic == 0x31574744 && r.version == 1 && r.generation == journal_.generation &&
                   r.waiting <= 1 &&
                   (r.permission == boot::Permission::absent ||
                    r.permission == boot::Permission::one) &&
                   boot::disjoint_foreign(record, r.queue);
        };
        DurableRecord<Wait> log(path);
        Wait previous;
        bool exists;
        if (!log.load(previous, exists, valid))
            return false;
        if (!foreign) {
            if (!exists || !previous.waiting)
                return true;
            previous.waiting = 0;
            return log.save(previous, valid);
        }
        if (exists && previous.waiting && previous.permission == permission &&
            boot::raw_equal(previous.queue, *foreign))
            return true;
        Wait next;
        next.generation = journal_.generation;
        next.waiting = 1;
        next.permission = permission;
        next.queue = *foreign;
        return log.save(next, valid);
    }
    // Called once before any queue registration. Every staged byte is checked
    // against the journal; source preparation never mutates public files.
    bool prepare(boot::Record &record, bool &needed) {
        needed = false;
        record = {};
        record.generation = journal_.generation;
        if (!valid(journal_) || !record_path_[0] || (os_ != Os::win98 && os_ != Os::nt5))
            return false;
        for (unsigned n = 0; n < journal_.count; ++n) {
            boot::Entry entry;
            if (!expected(n, entry))
                continue;
            boot::File current;
            if (!inspect(entry.destination, current))
                return false;
            if (boot::same(current, entry.desired))
                continue;
            if (!boot::same(current, entry.before) || record.count == boot::max_entries)
                return false;
            char payload[MAX_PATH];
            if (!store_.file_payload(journal_, n, undo_, payload))
                return false;
            if (entry.desired.exists) {
                boot::File actual;
                if (!inspect(entry.source, actual) || !boot::same(actual, entry.desired))
                    return false;
            }
            record.entries[record.count++] = entry;
        }
        if (!record.count)
            return true;
        needed = true;
        return save(record);
    }
};
} // namespace setup::lifecycle
