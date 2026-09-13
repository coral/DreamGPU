// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#include <stddef.h>
namespace setup::lifecycle {
constexpr uint32_t max_items = 16;
enum class Kind : uint32_t { file = 1, registry = 2, registry_key = 3 };
enum class Phase : uint32_t { prepared, changing, applied, restoring, restored, borrowed };
enum class State : uint32_t {
    staged,
    applying,
    pending_reboot,
    activated,
    rolling_back,
    failed,
    removed
};
enum class Result { complete, pending_reboot, conflict, io_error, invalid, provider_not_ready };
enum class Actual { before, after, intermediate, conflict, error };
enum class Change { done, locked, error };
struct Image {
    uint32_t exists = 0, type = 0, size = 0;
    char sha[68] = {};
    uint8_t value[256] = {};
};
struct Item {
    Kind kind = Kind::file;
    Phase phase = Phase::prepared;
    uint32_t resource = 0, original_generation = 0, original_index = 0;
    char path[192] = {}, name[64] = {};
    Image before = {}, desired = {}, original = {};
};
struct Journal {
    uint32_t magic = 0x314a4744, version = 1, generation = 0, count = 0;
    State state = State::staged;
    uint32_t uninstall = 0;
    Item items[max_items] = {};
};
static_assert(sizeof(Image) == 336 && sizeof(Item) == 1284 && sizeof(Journal) == 20568,
              "persistent schema v1 layout");
inline bool text_equal(const char *a, const char *b, size_t limit) {
    for (size_t i = 0; i < limit; ++i) {
        if (a[i] != b[i])
            return false;
        if (!a[i])
            return true;
    }
    return false;
}
inline bool destination_equal(const char *a, const char *b, size_t limit) {
    for (size_t i = 0; i < limit; ++i) {
        char x = a[i], y = b[i];
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
inline bool same(const Image &a, const Image &b) {
    if (a.exists != b.exists)
        return false;
    if (!a.exists)
        return true;
    if (a.type != b.type || a.size != b.size || !text_equal(a.sha, b.sha, sizeof(a.sha)))
        return false;
    if (a.type && a.size > sizeof(a.value))
        return false;
    if (a.type)
        for (size_t i = 0; i < a.size; ++i)
            if (a.value[i] != b.value[i])
                return false;
    return true;
}
inline bool valid_image(const Image &i, Kind kind) {
    if (i.exists > 1 || (!i.exists && (i.size || i.type)))
        return false;
    if (!i.exists)
        return true;
    if (kind == Kind::file && (i.type || i.size > 64 * 1024 * 1024))
        return false;
    if (kind == Kind::registry && (i.size > sizeof(i.value) || (i.type != 1 && i.type != 4)))
        return false;
    if (kind == Kind::registry &&
        ((i.type == 4 && i.size != 4) || (i.type == 1 && (!i.size || i.value[i.size - 1]))))
        return false;
    if (kind == Kind::registry_key && (i.type || i.size))
        return false;
    for (unsigned n = 0; n < 64; n++)
        if (!((i.sha[n] >= '0' && i.sha[n] <= '9') || (i.sha[n] >= 'a' && i.sha[n] <= 'f')))
            return false;
    return !i.sha[64];
}
inline bool valid(const Journal &j) {
    if (j.magic != 0x314a4744 || j.version != 1 || !j.generation || !j.count ||
        j.count > max_items || j.uninstall > 1 || uint32_t(j.state) > uint32_t(State::removed))
        return false;
    for (unsigned n = 0; n < j.count; n++) {
        const auto &i = j.items[n];
        if ((i.kind != Kind::file && i.kind != Kind::registry && i.kind != Kind::registry_key) ||
            uint32_t(i.phase) > uint32_t(Phase::borrowed) || !i.original_generation ||
            i.original_index >= max_items)
            return false;
        if (!valid_image(i.before, i.kind) || !valid_image(i.desired, i.kind) ||
            !valid_image(i.original, i.kind))
            return false;
        if (!text_equal(i.path, i.path, sizeof(i.path)) ||
            !text_equal(i.name, i.name, sizeof(i.name)))
            return false;
        for (unsigned k = 0; k < n; k++)
            if (i.kind == j.items[k].kind &&
                destination_equal(i.path, j.items[k].path, sizeof(i.path)) &&
                destination_equal(i.name, j.items[k].name, sizeof(i.name)))
                return false;
    }
    return true;
}
// Store contract: persist is durable before returning true. Classify compares
// actual file/value identity; intermediate is permitted only for a journalled
// file rename gap with its exact verified backup. mutate is idempotent and does
// not overwrite a conflicting object. Backups remain immutable across upgrades.
template <class Store> class Engine {
    Store &store_;
    Journal &j_;
    Result save() {
        return store_.persist(j_) ? Result::complete : Result::io_error;
    }

  public:
    Engine(Store &s, Journal &j) : store_(s), j_(j) {}
    Engine(const Engine &) = delete;
    Engine &operator=(const Engine &) = delete;
    Result continue_apply(bool providers_ready) {
        if (!valid(j_))
            return Result::invalid;
        if (!j_.uninstall && !providers_ready)
            return Result::provider_not_ready;
        if (j_.state == State::rolling_back || j_.state == State::failed ||
            j_.state == State::removed)
            return Result::invalid;
        j_.state = State::applying;
        if (save() != Result::complete)
            return Result::io_error;
        for (unsigned n = 0; n < j_.count; n++) {
            auto &i = j_.items[n];
            if (i.phase == Phase::borrowed)
                continue;
            Actual state = store_.classify(j_, n, false);
            if (state == Actual::error)
                return Result::io_error;
            if (state == Actual::conflict)
                return Result::conflict;
            if (state == Actual::after) {
                if (!store_.confirm(j_, n, false))
                    return Result::io_error;
                i.phase = Phase::applied;
                if (save() != Result::complete)
                    return Result::io_error;
                continue;
            }
            if (i.phase == Phase::applied)
                return Result::conflict;
            if (i.phase != Phase::prepared && i.phase != Phase::changing)
                return Result::invalid;
            i.phase = Phase::changing;
            if (save() != Result::complete)
                return Result::io_error;
            Change changed = store_.mutate(j_, n, false);
            if (changed == Change::locked) {
                j_.state = State::pending_reboot;
                if (save() != Result::complete)
                    return Result::io_error;
                return Result::pending_reboot;
            }
            if (changed == Change::error)
                return Result::io_error;
            if (store_.classify(j_, n, false) != Actual::after)
                return Result::conflict;
            if (!store_.confirm(j_, n, false))
                return Result::io_error;
            i.phase = Phase::applied;
            if (save() != Result::complete)
                return Result::io_error;
        }
        // A driver/ICD registration is not proof that the driver is active.
        if (!j_.uninstall && !store_.verify_activation(j_)) {
            j_.state = State::pending_reboot;
            if (save() != Result::complete)
                return Result::io_error;
            return Result::pending_reboot;
        }
        j_.state = j_.uninstall ? State::removed : State::activated;
        return save();
    }
    Result rollback() {
        if (!valid(j_))
            return Result::invalid;
        j_.state = State::rolling_back;
        if (save() != Result::complete)
            return Result::io_error;
        for (unsigned n = j_.count; n; n--) {
            auto &i = j_.items[n - 1];
            if (i.phase == Phase::borrowed || i.phase == Phase::restored)
                continue;
            Actual state = store_.classify(j_, n - 1, true);
            if (state == Actual::error)
                return Result::io_error;
            if (state == Actual::conflict)
                return Result::conflict;
            if (state != Actual::after) {
                i.phase = Phase::restoring;
                if (save() != Result::complete)
                    return Result::io_error;
                Change result = store_.mutate(j_, n - 1, true);
                if (result == Change::locked)
                    return Result::pending_reboot;
                if (result == Change::error)
                    return Result::io_error;
                if (store_.classify(j_, n - 1, true) != Actual::after)
                    return Result::conflict;
            }
            if (!store_.confirm(j_, n - 1, true))
                return Result::io_error;
            i.phase = Phase::restored;
            if (save() != Result::complete)
                return Result::io_error;
        }
        j_.state = State::failed;
        return save();
    }
};
// Build a new generation without losing first-install originals. The caller
// captures each before image and its immutable backup BEFORE persisting this
// journal; no public destination changes until all entries are prepared.
inline bool inherit(Item &next, const Item &previous) {
    if (previous.phase != Phase::applied && previous.phase != Phase::borrowed)
        return false;
    if (next.kind != previous.kind || !text_equal(next.path, previous.path, sizeof(next.path)) ||
        !text_equal(next.name, previous.name, sizeof(next.name)) ||
        !same(next.before, previous.desired))
        return false;
    next.original = previous.original;
    next.original_generation = previous.original_generation;
    next.original_index = previous.original_index;
    return true;
}
inline bool uninstall_plan(const Journal &installed, Journal &removal) {
    if (!valid(installed) || installed.state != State::activated ||
        installed.generation == 0xffffffff)
        return false;
    removal = installed;
    removal.generation++;
    removal.uninstall = 1;
    removal.state = State::staged;
    for (unsigned n = 0; n < removal.count; n++) {
        removal.items[n] = installed.items[installed.count - 1 - n];
        auto &i = removal.items[n];
        i.before = i.desired;
        i.desired = i.original;
        i.phase = installed.items[installed.count - 1 - n].phase == Phase::borrowed
                      ? Phase::borrowed
                      : Phase::prepared;
    }
    return true;
}
} // namespace setup::lifecycle
