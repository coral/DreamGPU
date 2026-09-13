// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "lifecycle.h"
#include "providers.h"
#include "sha256.h"
namespace setup::lifecycle {
inline bool copy_text(char *out, size_t capacity, const char *text) {
    size_t n = 0;
    for (; text[n]; n++) {
        if (n + 1 >= capacity)
            return false;
        out[n] = text[n];
    }
    out[n] = 0;
    return true;
}
inline Image string_value(const char *text) {
    Image i;
    i.exists = 1;
    i.type = 1;
    size_t n = 0;
    while (text[n] && n + 1 < sizeof(i.value)) {
        i.value[n] = uint8_t(text[n]);
        ++n;
    }
    i.value[n] = 0;
    i.size = uint32_t(n + 1);
    Sha256 h;
    h.update(i.value, i.size);
    h.finish(i.sha);
    return i;
}
inline Image number_value(uint32_t value) {
    Image i;
    i.exists = 1;
    i.type = 4;
    i.size = 4;
    for (unsigned n = 0; n < 4; n++)
        i.value[n] = uint8_t(value >> (8 * n));
    Sha256 h;
    h.update(i.value, i.size);
    h.finish(i.sha);
    return i;
}
template <class Payloads> bool shared_plan(Os os, const Payloads &payloads, Journal &j) {
    j = {};
    j.generation = 1;
    for (const char *name : shared_runtime) {
        char source[128] = "application/";
        size_t offset = sizeof("application/") - 1;
        if (!copy_text(source + offset, sizeof(source) - offset, name))
            return false;
        bool found = false;
        for (const auto &p : payloads) {
            if (p.os != unsigned(os) || !text_equal(source, p.path, sizeof(source)))
                continue;
            if (found || j.count >= max_items)
                return false;
            found = true;
            auto &i = j.items[j.count++];
            i.kind = Kind::file;
            i.resource = p.id;
            if (!copy_text(i.path, sizeof(i.path), name) ||
                !copy_text(i.desired.sha, sizeof(i.desired.sha), p.sha))
                return false;
            i.desired.exists = 1;
            i.desired.size = p.size;
        }
        if (!found)
            return false;
    }
    return true;
}
// Registration hook only. Caller must append the verified ICD file operation
// before this, and production readiness must come from acceptance descriptors.
inline bool append_icd_registration(Os os, Journal &j, bool production_ready) {
    if (!production_ready)
        return false;
    if (j.count > max_items - 5)
        return false;
    if (os == Os::nt5) {
        auto &key = j.items[j.count++];
        key.kind = Kind::registry_key;
        copy_text(key.path, sizeof(key.path), nt_icd_key);
        key.desired.exists = 1;
        Sha256 h;
        h.finish(key.desired.sha);
        constexpr const char *names[] = {"Dll", "Version", "DriverVersion", "Flags"};
        for (unsigned n = 0; n < 4; n++) {
            auto &i = j.items[j.count++];
            i.kind = Kind::registry;
            copy_text(i.path, sizeof(i.path), nt_icd_key);
            copy_text(i.name, sizeof(i.name), names[n]);
            i.desired = n ? number_value(n == 1 ? 2 : 1) : string_value("dgpuicd.dll");
        }
        return true;
    }
    if (os == Os::win98) {
        auto &i = j.items[j.count++];
        i.kind = Kind::registry;
        copy_text(i.path, sizeof(i.path), win98_icd_key);
        copy_text(i.name, sizeof(i.name), "DGPUICD");
        i.desired = string_value("dgpuicd.dll");
        return true;
    }
    return false;
}
// Full public namespace layout. Native aliases have no embedded OS bytes:
// Win32Store derives them from captured original public-runtime operations.
// Registry operations deliberately follow every provider file operation.
template <class Payloads>
bool append_payload(Os os, const Payloads &payloads, Journal &j, const char *source,
                    const char *destination) {
    bool found = false;
    for (const auto &p : payloads) {
        if (p.os != unsigned(os) || !text_equal(source, p.path, 192))
            continue;
        if (found || j.count == max_items || !p.id || p.id >= derived_resource)
            return false;
        found = true;
        auto &i = j.items[j.count++];
        i.kind = Kind::file;
        i.resource = p.id;
        i.desired.exists = 1;
        i.desired.size = p.size;
        if (!copy_text(i.path, sizeof(i.path), destination) ||
            !copy_text(i.desired.sha, sizeof(i.desired.sha), p.sha))
            return false;
    }
    return found;
}
template <class Payloads>
bool system_plan(Os os, const Payloads &payloads, Journal &j, bool icd_contract_ready) {
    if ((os != Os::win98 && os != Os::nt5) || !icd_contract_ready ||
        !shared_plan(os, payloads, j) ||
        !append_payload(os, payloads, j, "application/dgpuicd.dll", "dgpuicd.dll"))
        return false;
    for (const char *name : native_aliases) {
        if (j.count == max_items)
            return false;
        auto &i = j.items[j.count++];
        i.kind = Kind::file;
        i.resource = derived_resource;
        if (!copy_text(i.path, sizeof(i.path), name))
            return false;
    }
    constexpr const char *win98[] = {"switchers/ddraw_98.dll", "switchers/d3d8_98.dll",
                                     "switchers/d3d9_98.dll"};
    constexpr const char *nt5[] = {"switchers/ddraw_xp.dll", "switchers/d3d8_xp.dll",
                                   "switchers/d3d9_xp.dll"};
    for (unsigned n = 0; n < 3; ++n)
        if (!append_payload(os, payloads, j, os == Os::win98 ? win98[n] : nt5[n],
                            public_runtime[n]))
            return false;
    return append_icd_registration(os, j, true);
}
// All before-images are captured before any derived payload is generated.
// No public path is changed during this preparation. A later Engine pass is
// required; a prepared record is never evidence of system activation.
template <class Store>
bool capture_system(Os os, Store &store, Journal &j, const Journal *previous = nullptr,
                    bool repair = false, bool publish = true) {
    if (!store.complete_system_plan(os, j, previous, repair) ||
        !store.prepare_generation(j, previous, false, repair))
        return false;
    for (unsigned n = 0; n < j.count; ++n) {
        if (j.items[n].resource != derived_resource)
            continue;
        unsigned public_index = j.count;
        for (unsigned mapping = 0; mapping < 3; ++mapping)
            if (text_equal(j.items[n].path, native_aliases[mapping], 192))
                for (unsigned k = 0; k < j.count; ++k)
                    if (text_equal(j.items[k].path, public_runtime[mapping], 192))
                        public_index = k;
        if (public_index == j.count || !store.derive_alias(os, j, n, public_index))
            return false;
    }
    return valid(j) && (!publish || store.publish_preparation(j));
}
inline bool is_system_journal(const Journal &j) {
    // A historical shared-only staging receipt keeps its original recovery
    // route. Full installation always has all three public runtime operations.
    unsigned found = 0;
    for (unsigned n = 0; n < j.count; ++n)
        if (j.items[n].kind == Kind::file)
            for (const char *name : public_runtime)
                if (!lstrcmpA(j.items[n].path, name))
                    ++found;
    return found == 3;
}
inline bool same_destination(const Item &a, const Item &b) {
    return a.kind == b.kind && destination_equal(a.path, b.path, sizeof(a.path)) &&
           destination_equal(a.name, b.name, sizeof(a.name));
}
// A rolled-back upgrade still has an installed ancestor. Authenticate each
// generation from the append log; never turn a failed journal into activation.
// Each edge preserves first-original backup identities and the exact bytes to
// which rollback returned. New destinations must retain their own before-image.
inline bool restored_parent(const Journal &child, const Journal &parent) {
    if (!valid(child) || !valid(parent) || child.state != State::failed || child.uninstall ||
        parent.uninstall || child.generation <= 1 || parent.generation != child.generation - 1 ||
        (parent.state != State::activated && parent.state != State::failed))
        return false;
    for (unsigned n = 0; n < child.count; ++n) {
        const auto &item = child.items[n];
        if (item.phase != Phase::restored && item.phase != Phase::borrowed)
            return false;
        const Item *prior = nullptr;
        for (unsigned k = 0; k < parent.count; ++k)
            if (same_destination(item, parent.items[k]))
                prior = &parent.items[k];
        if (prior) {
            if (prior->phase !=
                    (parent.state == State::failed ? Phase::restored : Phase::applied) &&
                prior->phase != Phase::borrowed)
                return false;
            bool known_original = false;
            if (item.kind == Kind::file && item.original.exists &&
                same(item.before, prior->original))
                for (unsigned k = 0; k < 3; ++k)
                    known_original |= destination_equal(item.path, public_runtime[k], 192) ||
                                      destination_equal(item.path, win98_runtime_cache[k], 192);
            if ((!same(item.before,
                       parent.state == State::failed ? prior->before : prior->desired) &&
                 !known_original) ||
                !same(item.original, prior->original) ||
                item.original_generation != prior->original_generation ||
                item.original_index != prior->original_index)
                return false;
        } else if (!same(item.before, item.original) ||
                   item.original_generation != child.generation || item.original_index != n)
            return false;
    }
    for (unsigned k = 0; k < parent.count; ++k) {
        bool retained = false;
        for (unsigned n = 0; n < child.count; ++n)
            retained |= same_destination(parent.items[k], child.items[n]);
        if (!retained)
            return false;
    }
    return true;
}
template <class Store> Result assess_uninstall(Store &store, const Journal &j) {
    if (!valid(j) || j.uninstall || j.generation == UINT32_MAX ||
        (j.state != State::activated && j.state != State::failed))
        return Result::invalid;
    static Journal current, parent;
    if (!store.load(current, 0, true))
        return Result::io_error;
    const auto *a = reinterpret_cast<const uint8_t *>(&current);
    const auto *b = reinterpret_cast<const uint8_t *>(&j);
    for (unsigned n = 0; n < sizeof(Journal); ++n)
        if (a[n] != b[n])
            return Result::conflict;
    for (unsigned n = 0; n < j.count; ++n) {
        const Actual actual = store.classify(j, n, j.state == State::failed);
        if (actual != Actual::after)
            return actual == Actual::error ? Result::io_error : Result::conflict;
    }
    if (j.state == State::activated)
        return Result::complete;
    for (unsigned depth = 0; depth < 32 && current.generation > 1; ++depth) {
        if (!store.load_generation(current.generation - 1, parent))
            return Result::io_error;
        if (!restored_parent(current, parent))
            return Result::invalid;
        if (parent.state == State::activated)
            return Result::complete;
        current = parent;
    }
    return Result::invalid; // A first-install rollback has no installed ancestor.
}
template <class Store>
Result make_uninstall_plan(Store &store, const Journal &installed, Journal &removal) {
    const auto allowed = assess_uninstall(store, installed);
    if (allowed != Result::complete)
        return allowed;
    return uninstall_plan(installed, removal, installed.state == State::failed) ? Result::complete
                                                                                : Result::invalid;
}
// A completed removal/rollback starts a new ownership cycle. It must first
// match the acknowledged restored bytes; historical backup references cannot
// silently become the originals for a new install.
template <class Payloads, class Store>
Result next_system_generation(Os os, const Payloads &payloads, Store &store,
                              const Journal &previous, Journal &next) {
    if (!valid(previous) || previous.generation == UINT32_MAX)
        return Result::invalid;
    const bool removed = previous.state == State::removed && previous.uninstall;
    const bool restored = previous.state == State::failed;
    const auto retained = restored ? assess_uninstall(store, previous) : Result::invalid;
    if (restored && (retained == Result::conflict || retained == Result::io_error))
        return retained;
    if (restored && retained == Result::invalid) {
        // Only a failed first install in an ownership cycle may start fresh.
        // Invalid inherited history must not turn installed bytes into originals.
        for (unsigned n = 0; n < previous.count; ++n)
            if (previous.items[n].original_generation != previous.generation ||
                previous.items[n].original_index != n ||
                !same(previous.items[n].before, previous.items[n].original))
                return Result::invalid;
    }
    const bool upgrade = (previous.state == State::activated && !previous.uninstall) ||
                         (restored && retained == Result::complete);
    if (!upgrade && !removed && !restored)
        return Result::invalid;
    for (unsigned n = 0; n < previous.count; ++n)
        if (store.classify(previous, n, restored) != Actual::after)
            return Result::conflict;
    if (!system_plan(os, payloads, next, true) ||
        !store.complete_system_plan(os, next, upgrade ? &previous : nullptr))
        return Result::invalid;
    next.generation = previous.generation + 1;
    if (upgrade)
        for (unsigned n = 0; n < previous.count; ++n) {
            bool retained = false;
            for (unsigned k = 0; k < next.count; ++k)
                if (previous.items[n].kind == next.items[k].kind &&
                    destination_equal(previous.items[n].path, next.items[k].path, 192) &&
                    destination_equal(previous.items[n].name, next.items[k].name, 64))
                    retained = true;
            if (!retained)
                return Result::invalid;
        }
    return capture_system(os, store, next, upgrade ? &previous : nullptr) ? Result::complete
                                                                          : Result::conflict;
}
// Diagnostic /stage from an older installer may have captured only the six
// shared files. Promote only a never-applied receipt whose before-images still
// match; partially applied historical operations keep their explicit recovery.
template <class Payloads, class Store>
Result promote_staged_system(Os os, const Payloads &payloads, Store &store, const Journal &previous,
                             Journal &next) {
    if (!valid(previous) || previous.state != State::staged || previous.uninstall ||
        previous.generation == UINT32_MAX || is_system_journal(previous))
        return Result::invalid;
    for (unsigned n = 0; n < previous.count; ++n) {
        if (previous.items[n].phase != Phase::prepared &&
            previous.items[n].phase != Phase::borrowed)
            return Result::invalid;
        if (store.classify(previous, n, true) != Actual::after)
            return Result::conflict;
    }
    if (!system_plan(os, payloads, next, true))
        return Result::invalid;
    next.generation = previous.generation + 1;
    return capture_system(os, store, next) ? Result::complete : Result::conflict;
}
struct RepairAssessment {
    Result result = Result::invalid;
    uint32_t changed = 0, proofs = 0;
};
// Read-only: servicing is recognized only when an owned public DirectX file
// or its captured Win98 cache has the exact, present OS original bytes.
// Other drift is foreign; cache paths are never inferred from arbitrary names.
template <class Store> RepairAssessment assess_repair(Store &store, const Journal &j) {
    RepairAssessment result;
    if (!valid(j) || !is_system_journal(j) || j.state != State::activated || j.uninstall)
        return result;
    for (unsigned n = 0; n < j.count; ++n) {
        if (store.classify(j, n, false) == Actual::after)
            continue;
        const auto &item = j.items[n];
        unsigned runtime = 3;
        if (item.kind == Kind::file && !item.name[0] && item.original.exists)
            for (unsigned k = 0; k < 3; ++k)
                if (destination_equal(item.path, public_runtime[k], sizeof(item.path)) ||
                    destination_equal(item.path, win98_runtime_cache[k], sizeof(item.path)))
                    runtime = k;
        if (runtime == 3 || !store.matches_original(item)) {
            result.result = Result::conflict;
            return result;
        }
        result.changed |= uint32_t(1) << n;
        result.proofs |= runtime == 0 ? 0x0cu : runtime == 1 ? 0x10u : 0x20u;
    }
    result.result = Result::complete;
    return result;
}
template <class Store> bool repairable(Store &store, const Journal &j) {
    return assess_repair(store, j).result == Result::complete;
}
// Prepare, but do not publish, a repair generation. Its independent
// verification receipt must be seeded before the lifecycle journal can select
// this target.
template <class Payloads, class Store>
Result prepare_system_repair(Os os, const Payloads &payloads, Store &store, const Journal &previous,
                             Journal &next, uint32_t &proofs) {
    const auto assessment = assess_repair(store, previous);
    proofs = assessment.proofs;
    if (assessment.result != Result::complete)
        return assessment.result;
    if (!assessment.changed) {
        next = previous;
        return Result::complete;
    }
    if (previous.generation == UINT32_MAX || !system_plan(os, payloads, next, true) ||
        !store.complete_system_plan(os, next, &previous, true) || next.count != previous.count)
        return Result::invalid;
    next.generation = previous.generation + 1;
    for (unsigned n = 0; n < next.count; ++n) {
        const auto &a = next.items[n];
        const auto &b = previous.items[n];
        if (a.kind != b.kind || !destination_equal(a.path, b.path, sizeof(a.path)) ||
            !destination_equal(a.name, b.name, sizeof(a.name)) ||
            (a.resource != derived_resource && !same(a.desired, b.desired)))
            return Result::conflict;
    }
    if (!capture_system(os, store, next, &previous, true, false))
        return Result::conflict;
    for (unsigned n = 0; n < next.count; ++n)
        if (!same(next.items[n].desired, previous.items[n].desired))
            return Result::conflict;
    return Result::complete;
}
} // namespace setup::lifecycle
