// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>
namespace setup::boot {
constexpr unsigned max_entries = 16, path_capacity = 260;
constexpr unsigned queue_words = max_entries * (path_capacity * 2 + 10) + 1;
enum class Phase : uint32_t {
    prepared,
    registering,
    arming,
    queued,
    cancelling,
    cancelled,
    complete
};
enum class Result { pending, complete, cancelled, needs_restore, conflict, io_error, invalid };
enum class Permission { absent, one, foreign, error };
struct File {
    uint32_t exists = 0, size = 0;
    char sha[68]{};
};
struct Entry {
    char source[path_capacity]{}, destination[path_capacity]{};
    File before{}, desired{};
    uint32_t protected_target = 0;
};
struct Record {
    uint32_t magic = 0x51424744, version = 1, generation = 0, count = 0;
    Phase phase = Phase::prepared;
    uint32_t completed = 0;
    Entry entries[max_entries]{};
};
struct RawQueue {
    bool exists = false;
    unsigned words = 0;
    uint16_t data[queue_words]{};
};
inline bool text(const char *s, unsigned capacity) {
    for (unsigned n = 0; n < capacity; ++n)
        if (!s[n])
            return n != 0;
    return false;
}
inline bool equal(const char *a, const char *b, unsigned capacity) {
    for (unsigned n = 0; n < capacity; ++n) {
        if (a[n] != b[n])
            return false;
        if (!a[n])
            return true;
    }
    return false;
}
inline bool path_equal(const char *a, const char *b) {
    for (unsigned n = 0; n < path_capacity; ++n) {
        char x = a[n], y = b[n];
        if (x >= 'a' && x <= 'z')
            x -= 32;
        if (y >= 'a' && y <= 'z')
            y -= 32;
        if (x != y)
            return false;
        if (!x)
            return true;
    }
    return false;
}
inline bool valid_file(const File &f) {
    if (f.exists > 1 || f.size > 64u * 1024u * 1024u)
        return false;
    if (!f.exists)
        return !f.size && !f.sha[0];
    if (!f.size || f.sha[64])
        return false;
    for (unsigned i = 0; i < 64; ++i)
        if (!((f.sha[i] >= '0' && f.sha[i] <= '9') || (f.sha[i] >= 'a' && f.sha[i] <= 'f')))
            return false;
    return true;
}
inline bool same(const File &a, const File &b) {
    return a.exists == b.exists && (!a.exists || (a.size == b.size && equal(a.sha, b.sha, 68)));
}
inline bool valid(const Record &r) {
    if (r.magic != 0x51424744 || r.version != 1 || !r.generation || !r.count ||
        r.count > max_entries || uint32_t(r.phase) > uint32_t(Phase::complete) ||
        (r.completed >> r.count))
        return false;
    for (unsigned n = 0; n < r.count; ++n) {
        const auto &e = r.entries[n];
        if (!text(e.destination, path_capacity) || !valid_file(e.before) ||
            !valid_file(e.desired) ||
            (e.desired.exists ? !text(e.source, path_capacity) : e.source[0] != 0) ||
            same(e.before, e.desired) || e.protected_target > 1)
            return false;
        if (path_equal(e.source, e.destination))
            return false;
        for (unsigned k = 0; k < n; ++k) {
            const auto &other = r.entries[k];
            if ((e.source[0] &&
                 (path_equal(e.source, other.source) || path_equal(e.source, other.destination))) ||
                (other.source[0] && path_equal(e.destination, other.source)) ||
                path_equal(e.destination, other.destination))
                return false;
        }
    }
    return true;
}
static_assert(sizeof(File) == 76 && sizeof(Entry) == 676 && sizeof(Record) == 10840,
              "Independent persistent boot queue schema v1");
inline bool raw_equal(const RawQueue &a, const RawQueue &b) {
    if (a.exists != b.exists || a.words != b.words)
        return false;
    for (unsigned n = 0; n < a.words; ++n)
        if (a.data[n] != b.data[n])
            return false;
    return true;
}
// Ops: save(record) must atomically persist and flush a complete owned record;
// files/paths must be classified against immutable staging/rollback identities.
// All methods run under the installer's exclusive operation lock. This cannot
// lock unrelated Windows installers: exact registry readback detects conflicts,
// but the NT5 global registry API has no compare-and-swap transaction.
//
// Ops methods: save, inspect(path,File&), approved(Entry), encode(path,out,cap),
// pending(RawQueue&), secondary_absent(), permission(), enqueue(Entry), grant(),
// remove_permission(), remove_pending(), flush(). All failures are fail-closed.
template <class Ops> class Queue {
    Ops &ops_;
    Record &r_;
    bool expected(RawQueue &out, unsigned prefix) {
        out = {};
        if (!prefix)
            return true;
        out.exists = true;
        unsigned added = 0;
        for (unsigned n = 0; n < r_.count && added < prefix; ++n) {
            if (r_.completed & (1u << n))
                continue;
            const auto &e = r_.entries[n];
            for (unsigned side = 0; side < 2; ++side) {
                if (side && !e.desired.exists) {
                    // A deletion has an empty second member, not the end of the
                    // entire MULTI_SZ. Later entries remain part of this batch.
                    out.data[out.words++] = 0;
                    continue;
                }
                if (side)
                    out.data[out.words++] = u'!';
                out.data[out.words++] = u'\\';
                out.data[out.words++] = u'?';
                out.data[out.words++] = u'?';
                out.data[out.words++] = u'\\';
                unsigned count = ops_.encode(side || !e.desired.exists ? e.destination : e.source,
                                             out.data + out.words, path_capacity);
                if (!count || count > path_capacity || out.data[out.words + count - 1])
                    return false;
                out.words += count;
            }
            ++added;
        }
        if (added != prefix)
            return false;
        out.data[out.words++] = 0;
        return true;
    }
    unsigned remaining() const {
        unsigned total = 0;
        for (unsigned n = 0; n < r_.count; ++n)
            if (!(r_.completed & (1u << n)))
                ++total;
        return total;
    }
    bool needs_permission() const {
        for (unsigned n = 0; n < r_.count; ++n)
            if (!(r_.completed & (1u << n)) && r_.entries[n].protected_target)
                return true;
        return false;
    }
    Result inventory(bool queue_exists) {
        uint32_t completed = r_.completed;
        for (unsigned n = 0; n < r_.count; ++n) {
            const auto &e = r_.entries[n];
            if (!ops_.approved(e))
                return Result::invalid;
            File source{}, target{};
            if ((e.desired.exists && !ops_.inspect(e.source, source)) ||
                !ops_.inspect(e.destination, target))
                return Result::io_error;
            if (same(target, e.desired) && !source.exists) {
                if (queue_exists && !(completed & (1u << n)))
                    return Result::conflict;
                completed |= 1u << n;
            } else if ((completed & (1u << n)) || !same(target, e.before) ||
                       !same(source, e.desired)) {
                return Result::conflict;
            }
        }
        if (completed != r_.completed) {
            if (r_.phase == Phase::prepared)
                return Result::conflict;
            r_.completed = completed;
            if (!ops_.save(r_))
                return Result::io_error;
        }
        return Result::complete;
    }
    Result read(RawQueue &raw, unsigned &prefix, Permission &permission) {
        if (!ops_.pending(raw) || !ops_.secondary_absent())
            return Result::conflict;
        if (raw.words > queue_words)
            return Result::conflict;
        permission = ops_.permission();
        if (permission == Permission::error)
            return Result::io_error;
        if (permission == Permission::foreign)
            return Result::conflict;
        if (permission == Permission::one && r_.phase != Phase::arming &&
            r_.phase != Phase::queued && r_.phase != Phase::cancelling)
            return Result::conflict;
        auto state = inventory(raw.exists);
        if (state != Result::complete)
            return state;
        RawQueue wanted;
        for (prefix = 0; prefix <= remaining(); ++prefix) {
            if (!expected(wanted, prefix))
                return Result::invalid;
            if (raw_equal(raw, wanted))
                return Result::complete;
        }
        return Result::conflict;
    }
    bool unchanged(const RawQueue &expected_queue) {
        RawQueue now;
        return ops_.secondary_absent() && ops_.pending(now) && raw_equal(now, expected_queue);
    }

  public:
    Queue(Ops &ops, Record &record) : ops_(ops), r_(record) {}
    Queue(const Queue &) = delete;
    Result resume() {
        if (!valid(r_) || r_.phase == Phase::cancelled || r_.phase == Phase::cancelling)
            return Result::invalid;
        RawQueue raw;
        unsigned prefix = 0;
        Permission permission;
        // A new plan may never claim a byte-identical preexisting queue.
        if (r_.phase == Phase::prepared) {
            if (!ops_.pending(raw) || raw.exists || !ops_.secondary_absent() ||
                ops_.permission() != Permission::absent)
                return Result::conflict;
            auto state = inventory(false);
            if (state != Result::complete || r_.completed)
                return state == Result::complete ? Result::conflict : state;
            r_.phase = Phase::registering;
            if (!ops_.save(r_))
                return Result::io_error;
        }
        auto state = read(raw, prefix, permission);
        if (state != Result::complete)
            return state;
        if (!remaining()) {
            if (raw.exists || permission != Permission::absent)
                return Result::conflict;
            r_.phase = Phase::complete;
            return ops_.save(r_) ? Result::complete : Result::io_error;
        }
        if (permission == Permission::one && (prefix != remaining() || !needs_permission()))
            return Result::conflict;
        unsigned ordinal = 0;
        for (unsigned n = 0; n < r_.count; ++n) {
            if (r_.completed & (1u << n))
                continue;
            if (ordinal++ < prefix)
                continue;
            if (permission != Permission::absent || ops_.permission() != Permission::absent ||
                !unchanged(raw))
                return Result::conflict;
            // Recheck all immutable inputs before every externally visible append.
            state = inventory(raw.exists);
            if (state != Result::complete)
                return state;
            if (!ops_.enqueue(r_.entries[n]))
                return Result::io_error;
            if (!expected(raw, ++prefix) || !unchanged(raw))
                return Result::conflict;
            if (!ops_.flush())
                return Result::io_error;
        }
        if (needs_permission() && permission == Permission::absent) {
            r_.phase = Phase::arming;
            if (!ops_.save(r_))
                return Result::io_error;
            if (!unchanged(raw) || ops_.permission() != Permission::absent)
                return Result::conflict;
            if (!ops_.grant() || !ops_.flush())
                return Result::io_error;
            if (!unchanged(raw) || ops_.permission() != Permission::one)
                return Result::conflict;
        }
        r_.phase = Phase::queued;
        return ops_.save(r_) ? Result::pending : Result::io_error;
    }
    Result cancel() {
        if (!valid(r_) || r_.phase == Phase::prepared)
            return Result::invalid;
        RawQueue raw;
        unsigned prefix = 0;
        Permission permission;
        auto state = read(raw, prefix, permission);
        if (state != Result::complete)
            return state;
        r_.phase = Phase::cancelling;
        if (!ops_.save(r_))
            return Result::io_error;
        if (permission == Permission::one) {
            if (!raw.exists || !needs_permission() || prefix != remaining() || !unchanged(raw))
                return Result::conflict;
            if (ops_.permission() != Permission::one)
                return Result::conflict;
            if (!ops_.remove_permission() || !ops_.flush())
                return Result::io_error;
        }
        if (ops_.permission() != Permission::absent || !unchanged(raw))
            return Result::conflict;
        if (raw.exists && (!ops_.remove_pending() || !ops_.flush()))
            return Result::io_error;
        RawQueue absent;
        if (!unchanged(absent))
            return Result::conflict;
        r_.phase = Phase::cancelled;
        if (!ops_.save(r_))
            return Result::io_error;
        return r_.completed ? Result::needs_restore : Result::cancelled;
    }
};
} // namespace setup::boot
