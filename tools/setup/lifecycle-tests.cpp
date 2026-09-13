// SPDX-License-Identifier: GPL-2.0-or-later
#include "lifecycle.h"
#include "sha256.h"
#include <cassert>
#include <initializer_list>
#include <cstring>
using namespace setup::lifecycle;
static Image image(const char *bytes, bool registry = false) {
    Image i;
    i.exists = 1;
    i.size = unsigned(strlen(bytes)) + (registry ? 1 : 0);
    i.type = registry ? 1 : 0;
    if (registry)
        memcpy(i.value, bytes, i.size);
    setup::Sha256 h;
    h.update(reinterpret_cast<const uint8_t *>(bytes), i.size);
    h.finish(i.sha);
    return i;
}
struct Store {
    Journal disk;
    Image actual[max_items];
    unsigned calls = 0, fail = 0;
    bool after = false, locked = false, verified = true;
    bool hit() {
        return ++calls == fail;
    }
    bool persist(const Journal &j) {
        bool failure = hit();
        if (!failure || after)
            disk = j;
        return !failure;
    }
    Actual classify(const Journal &j, unsigned n, bool undo) {
        const auto &i = j.items[n];
        if (same(actual[j.items[n].original_index], undo ? i.before : i.desired))
            return Actual::after;
        if (same(actual[j.items[n].original_index], undo ? i.desired : i.before))
            return Actual::before;
        return Actual::conflict;
    }
    Change mutate(const Journal &j, unsigned n, bool undo) {
        if (locked)
            return Change::locked;
        bool failure = hit();
        if (!failure || after)
            actual[j.items[n].original_index] = undo ? j.items[n].before : j.items[n].desired;
        return failure ? Change::error : Change::done;
    }
    bool confirm(const Journal &, unsigned, bool) {
        return !hit();
    }
    bool verify_activation(const Journal &) {
        return verified;
    }
};
static Journal make() {
    Journal j;
    j.generation = 1;
    j.count = 3;
    for (unsigned n = 0; n < j.count; n++) {
        auto &i = j.items[n];
        i.kind = n == 2 ? Kind::registry : Kind::file;
        i.original_generation = 1;
        i.original_index = n;
        strcpy(i.path, n == 0 ? "dgpugl.dll" : n == 1 ? "glide2x.dll" : "ICD");
        i.before = n ? image("native", n == 2) : Image{};
        i.original = i.before;
        i.desired = image("dreamgpu", n == 2);
    }
    return j;
}
static Store store(const Journal &j) {
    Store s;
    s.disk = j;
    for (unsigned n = 0; n < j.count; n++)
        s.actual[n] = j.items[n].before;
    return s;
}
int main() {
    {
        Journal j = make();
        Store s = store(j);
        Engine e(s, j);
        assert(e.continue_apply(false) == Result::provider_not_ready);
        assert(s.calls == 0);
    }
    unsigned points;
    {
        Journal j = make();
        Store s = store(j);
        Engine e(s, j);
        assert(e.continue_apply(true) == Result::complete);
        assert(j.state == State::activated);
        points = s.calls;
    }
    for (unsigned fail = 1; fail <= points; fail++)
        for (bool after : {false, true}) {
            Journal j = make();
            Store s = store(j);
            s.fail = fail;
            s.after = after;
            Engine e(s, j);
            assert(e.continue_apply(true) == Result::io_error);
            // Fresh process reads only last durable journal; no in-memory phase trust.
            j = s.disk;
            s.fail = 0;
            Engine recovery(s, j);
            assert(recovery.continue_apply(true) == Result::complete);
            for (unsigned n = 0; n < j.count; n++)
                assert(same(s.actual[n], j.items[n].desired));
        }
    {
        Journal j = make();
        Store s = store(j);
        s.locked = true;
        Engine e(s, j);
        assert(e.continue_apply(true) == Result::pending_reboot);
        assert(j.state == State::pending_reboot);
        s.locked = false;
        j = s.disk;
        assert(e.continue_apply(true) == Result::complete);
    }
    {
        Journal j = make();
        Store s = store(j);
        s.verified = false;
        Engine e(s, j);
        assert(e.continue_apply(true) == Result::pending_reboot);
        assert(j.state != State::activated);
        s.verified = true;
        assert(e.continue_apply(true) == Result::complete);
    }
    {
        Journal j = make();
        Store s = store(j);
        s.actual[0] = image("foreign");
        Engine e(s, j);
        assert(e.continue_apply(true) == Result::conflict);
        assert(same(s.actual[0], image("foreign")));
    }
    unsigned rollback_points;
    {
        Journal j = make();
        Store s = store(j);
        Engine e(s, j);
        assert(e.continue_apply(true) == Result::complete);
        s.calls = 0;
        assert(e.rollback() == Result::complete);
        rollback_points = s.calls;
    }
    for (unsigned fail = 1; fail <= rollback_points; fail++)
        for (bool after : {false, true}) {
            Journal j = make();
            Store s = store(j);
            Engine e(s, j);
            assert(e.continue_apply(true) == Result::complete);
            s.calls = 0;
            s.fail = fail;
            s.after = after;
            assert(e.rollback() == Result::io_error);
            j = s.disk;
            s.fail = 0;
            Engine recovery(s, j);
            assert(recovery.rollback() == Result::complete);
            for (unsigned n = 0; n < j.count; n++)
                assert(same(s.actual[n], j.items[n].before));
        }
    {
        Journal j = make();
        Store s = store(j);
        Engine e(s, j);
        assert(e.continue_apply(true) == Result::complete);
        Journal removal;
        assert(uninstall_plan(j, removal));
        for (unsigned n = 0; n < removal.count; n++)
            assert(same(removal.items[n].desired, j.items[j.count - 1 - n].original));
        Engine uninstall(s, removal);
        assert(uninstall.continue_apply(false) == Result::complete);
        assert(removal.state == State::removed);
    }
    {
        Journal j = make();
        Store s = store(j);
        Engine e(s, j);
        assert(e.continue_apply(true) == Result::complete);
        Item upgrade = j.items[1];
        upgrade.before = upgrade.desired;
        upgrade.desired = image("v2");
        upgrade.original = upgrade.before;
        assert(inherit(upgrade, j.items[1]));
        assert(same(upgrade.original, image("native")));
        assert(upgrade.original_generation == 1 && upgrade.original_index == 1);
        upgrade.before = image("externally modified");
        assert(!inherit(upgrade, j.items[1]));
    }
    {
        Journal j = make();
        j.items[0].before = j.items[0].desired;
        j.items[0].original = j.items[0].desired;
        j.items[0].phase = Phase::borrowed;
        Store s = store(j);
        Engine e(s, j);
        assert(e.continue_apply(true) == Result::complete);
        Journal removal;
        assert(uninstall_plan(j, removal));
        Engine u(s, removal);
        assert(u.continue_apply(false) == Result::complete);
        assert(same(s.actual[0], image("dreamgpu")));
    }
    {
        Journal j = make();
        j.count = max_items + 1;
        assert(!valid(j));
        j = make();
        j.items[1].before.size = 0xffffffff;
        j.items[1].before.type = 1;
        assert(!valid(j));
    }
}
