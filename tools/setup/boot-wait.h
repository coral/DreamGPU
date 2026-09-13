// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "boot-queue.h"
namespace setup::boot {
enum class Deferral { none, pending, conflict, io_error };
// Only unambiguous DOS paths can prove disjointness without resolving a future
// rename. In particular, do not guess whether an 8.3 alias names an owned file.
inline bool waiting_path(const uint16_t *text, unsigned length, bool target,
                         char (&out)[path_capacity]) {
    unsigned start = target && length && text[0] == u'!' ? 1 : 0;
    if (length < start + 7 || text[start] != u'\\' || text[start + 1] != u'?' ||
        text[start + 2] != u'?' || text[start + 3] != u'\\')
        return false;
    start += 4;
    const unsigned count = length - start;
    if (count >= path_capacity ||
        !((text[start] >= u'A' && text[start] <= u'Z') ||
          (text[start] >= u'a' && text[start] <= u'z')) ||
        text[start + 1] != u':' || text[start + 2] != u'\\')
        return false;
    unsigned component = 0;
    for (unsigned n = 0; n < count; ++n) {
        const uint16_t c = text[start + n];
        if (c < 32 || c > 126 || c == u'/' || c == u'~' || c == u'"' || c == u'<' || c == u'>' ||
            c == u'|' || c == u'?' || c == u'*' || (c == u':' && n != 1))
            return false;
        if (n > 2 && c == u'\\') {
            if (!component || out[n - 1] == '.' || out[n - 1] == ' ')
                return false;
            component = 0;
        } else if (n > 2) {
            if (!component && (c == u'.' || c == u' '))
                return false;
            ++component;
        }
        out[n] = char(c);
    }
    out[count] = 0;
    return component && out[count - 1] != '.' && out[count - 1] != ' ';
}
inline bool disjoint_foreign(const Record &record, const RawQueue &raw) {
    if (!valid(record) || !raw.exists || raw.words < 2 || raw.words > queue_words ||
        raw.data[raw.words - 1] || raw.data[raw.words - 2])
        return false;
    unsigned position = 0, pairs = 0;
    while (position < raw.words && raw.data[position]) {
        for (unsigned side = 0; side < 2; ++side) {
            const unsigned start = position;
            while (position < raw.words && raw.data[position])
                ++position;
            if (position == raw.words)
                return false;
            const unsigned length = position++ - start;
            if (!length) {
                if (!side)
                    return false;
                continue; // Native delayed deletion has an empty target member.
            }
            char path[path_capacity]{};
            if (!waiting_path(raw.data + start, length, side != 0, path))
                return false;
            for (unsigned n = 0; n < record.count; ++n)
                if (path_equal(path, record.entries[n].source) ||
                    path_equal(path, record.entries[n].destination))
                    return false;
        }
        ++pairs;
    }
    for (; position < raw.words; ++position)
        if (raw.data[position])
            return false;
    return pairs != 0;
}
// Read-only admission before our first registration. Queue itself still never
// adopts/extends a foreign queue; after boot the complete owned inventory is
// checked again before any registration can occur.
template <class Ops>
Deferral wait_before_registration(Ops &ops, const Record &record, RawQueue &observed,
                                  Permission &permission) {
    observed = {};
    permission = Permission::absent;
    if (record.phase != Phase::prepared)
        return Deferral::none;
    if (!valid(record) || !ops.pending(observed) || !ops.secondary_absent())
        return Deferral::conflict;
    if (!observed.exists)
        return Deferral::none;
    permission = ops.permission();
    if (permission == Permission::error)
        return Deferral::io_error;
    if (permission == Permission::foreign || !disjoint_foreign(record, observed) ||
        record.completed)
        return Deferral::conflict;
    for (unsigned n = 0; n < record.count; ++n) {
        const auto &entry = record.entries[n];
        File source, target;
        if (!ops.approved(entry) || !ops.inspect(entry.destination, target) ||
            (entry.desired.exists && !ops.inspect(entry.source, source)))
            return Deferral::io_error;
        if (!same(target, entry.before) || !same(source, entry.desired))
            return Deferral::conflict;
    }
    RawQueue current;
    if (!ops.pending(current) || !raw_equal(current, observed) || !ops.secondary_absent() ||
        ops.permission() != permission)
        return Deferral::conflict;
    return Deferral::pending;
}
} // namespace setup::boot
