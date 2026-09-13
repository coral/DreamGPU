// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "policy.h"
#include <stdint.h>
namespace setup::driver {
enum class Phase : uint32_t {
    capturing,
    captured,
    binding,
    pending_reboot,
    verified,
    restoring,
    restore_pending,
    restored
};
enum class Result { verified, pending_reboot, restored, invalid, conflict, io_error };
enum class Actual { original, desired, pending, other, error };
enum class Binding : uint32_t { stock = 0, dreamgpu = 1, unbound = 2 };
struct Node {
    char device[256] = {}, inf[260] = {}, section[256] = {}, provider[256] = {},
         description[256] = {};
    char inf_sha[68] = {};
};
struct File {
    char path[260] = {}, backup[64] = {}, sha[68] = {};
    uint32_t size = 0, exists = 0, replaced = 0;
    char desired_sha[68] = {};
    uint32_t desired_size = 0;
};
struct Journal {
    uint32_t magic = 0x31424744, version = 2;
    Os os = Os::unsupported;
    Phase phase = Phase::capturing;
    uint32_t count = 0;
    Node original{}, desired{};
    File files[16]{};
    uint32_t resume_existed = 0, resume_type = 0, resume_bytes = 0;
    Binding original_binding = Binding::stock;
    uint32_t child_pid = 0;
    uint8_t resume_value[512] = {};
    char installer_sha[68] = {}, helper_sha[68] = {}, audit_sha[68] = {};
};
static_assert(sizeof(Journal) == 11076, "driver journal V1/V2/V3 durable layout");
static_assert(offsetof(Journal, original_binding) == 10352, "V1 binding field offset");
inline bool bounded(const char *text, size_t capacity) {
    for (size_t n = 0; n < capacity; ++n)
        if (!text[n])
            return n != 0;
    return false;
}
inline bool hash(const char value[68]) {
    for (unsigned n = 0; n < 64; ++n)
        if (!((value[n] >= '0' && value[n] <= '9') || (value[n] >= 'a' && value[n] <= 'f')))
            return false;
    return value[64] == 0;
}
inline bool equal_fold(const char *a, const char *b) {
    while (*a && *b)
        if (upper(*a++) != upper(*b++))
            return false;
    return *a == *b;
}
template <size_t N> inline bool empty(const char (&value)[N]) {
    for (char byte : value)
        if (byte)
            return false;
    return true;
}
// V2 preserves V1's byte layout; only the formerly boolean binding field gains
// an explicit unbound kind. Old V1 records accept only stock/owned bindings.
// V3 has the same layout and uses persistent Run for Win98 continuations.
// Earlier journals retain their RunOnce baseline for exact terminal cleanup.
inline bool valid(const Journal &j) {
    if (j.magic != 0x31424744 || (j.version != 1 && j.version != 2 && j.version != 3) ||
        (j.os != Os::win98 && j.os != Os::nt5) || uint32_t(j.phase) > uint32_t(Phase::restored) ||
        j.count > 16 || uint32_t(j.original_binding) > uint32_t(Binding::unbound) ||
        (j.original_binding == Binding::unbound && (j.version < 2 || j.os != Os::nt5)) ||
        j.resume_existed > 1 || j.resume_bytes > sizeof(j.resume_value) || !hash(j.installer_sha) ||
        !hash(j.helper_sha) || !hash(j.audit_sha))
        return false;
    const Node *nodes[2] = {&j.original, &j.desired};
    for (const auto *node : nodes) {
        if (!bounded(node->device, sizeof(node->device)) ||
            !pci(node->device, sizeof(node->device)))
            return false;
        if (node == &j.original && j.original_binding == Binding::unbound) {
            // Explicit absence, never a synthetic previous INF or provider.
            if (!empty(node->inf) || !empty(node->section) || !empty(node->provider) ||
                !empty(node->description) || !empty(node->inf_sha))
                return false;
            continue;
        }
        if (!bounded(node->inf, sizeof(node->inf)) ||
            !bounded(node->section, sizeof(node->section)) ||
            !bounded(node->description, sizeof(node->description)) ||
            !bounded(node->provider, sizeof(node->provider)) || !hash(node->inf_sha))
            return false;
    }
    if (!equal_fold(j.original.device, j.desired.device))
        return false;
    for (uint32_t n = 0; n < j.count; ++n) {
        const auto &f = j.files[n];
        if (f.exists > 1 || f.replaced > 1 || !bounded(f.path, sizeof(f.path)) ||
            (f.exists && (!hash(f.sha) || f.size > 64u * 1024 * 1024)) ||
            (f.replaced &&
             (!hash(f.desired_sha) || !f.desired_size || f.desired_size > 64u * 1024 * 1024)) ||
            (f.replaced && f.exists && !bounded(f.backup, sizeof(f.backup))))
            return false;
        for (uint32_t k = 0; k < n; ++k)
            if (equal_fold(f.path, j.files[k].path))
                return false;
    }
    return true;
}
// Store owns durable journal/file/registry APIs and the existing checked driver
// helpers. No operation after an uncertain install blindly replays it: current
// binding and untouched-original identities are reconciled first.
template <class Store> class Engine {
    Store &store_;
    Journal &j_;
    Result save() {
        return store_.persist(j_) ? Result::verified : Result::io_error;
    }

  public:
    Engine(Store &store, Journal &journal) : store_(store), j_(journal) {}
    Engine(const Engine &) = delete;
    Result install() {
        if (!valid(j_) || j_.phase == Phase::capturing || j_.phase == Phase::restoring ||
            j_.phase == Phase::restore_pending || j_.phase == Phase::restored)
            return Result::invalid;
        if (j_.child_pid)
            return Result::pending_reboot;
        Actual current = store_.observe(j_);
        if (current == Actual::error)
            return Result::io_error;
        if (current == Actual::other || !store_.unchanged_originals(j_))
            return Result::conflict;
        if (j_.phase == Phase::verified && current != Actual::desired)
            return Result::conflict;
        if (!store_.arm_resume(j_, false))
            return Result::io_error;
        if (current == Actual::pending) {
            j_.phase = Phase::pending_reboot;
            return save() == Result::verified ? Result::pending_reboot : Result::io_error;
        }
        if (current == Actual::desired) {
            // Returning from the first bind is never activation proof. A later
            // continuation checks the actual started adapter and driver channel.
            if (j_.phase == Phase::pending_reboot || j_.phase == Phase::verified) {
                if (!store_.verify(j_, false))
                    return Result::pending_reboot;
                j_.phase = Phase::verified;
                if (save() != Result::verified || !store_.disarm_resume(j_))
                    return Result::io_error;
                return Result::verified;
            }
            j_.phase = Phase::pending_reboot;
            return save() == Result::verified ? Result::pending_reboot : Result::io_error;
        }
        // If binding was interrupted but the exact original node/files remain,
        // rerunning the fixed installer is safe; no partially changed node is adopted.
        j_.phase = Phase::binding;
        if (save() != Result::verified)
            return Result::io_error;
        if (!store_.install(j_))
            return Result::io_error;
        current = store_.observe(j_);
        if ((current != Actual::desired && current != Actual::pending) ||
            !store_.unchanged_originals(j_))
            return Result::conflict;
        j_.phase = Phase::pending_reboot;
        return save() == Result::verified ? Result::pending_reboot : Result::io_error;
    }
    Result rollback() {
        const bool resuming_restore =
            j_.phase == Phase::restoring || j_.phase == Phase::restore_pending;
        if (!valid(j_) || j_.phase == Phase::capturing)
            return Result::invalid;
        if (j_.child_pid)
            return Result::pending_reboot;
        Actual current = store_.observe(j_);
        if (current == Actual::error)
            return Result::io_error;
        if (current == Actual::other || !store_.unchanged_originals(j_))
            return Result::conflict;
        // Record the requested recovery direction before arming RunOnce: a
        // registry flush failure must never turn a rebooted rollback into an
        // installation continuation.
        if (j_.phase != Phase::restoring && j_.phase != Phase::restore_pending &&
            j_.phase != Phase::restored) {
            j_.phase = current == Actual::original ? Phase::restore_pending : Phase::restoring;
            if (save() != Result::verified)
                return Result::io_error;
            // Identical before/desired images are classified as desired while
            // moving forward. Re-observe with the durable reverse direction so
            // an already-original binding does not launch a redundant installer.
            const Actual reverse = store_.observe(j_);
            if (reverse == Actual::error)
                return Result::io_error;
            if (reverse == Actual::other)
                return Result::conflict;
            if (reverse == Actual::original)
                current = reverse;
        }
        if (!store_.arm_resume(j_, true))
            return Result::io_error;
        if (current == Actual::pending && resuming_restore)
            return Result::pending_reboot;
        if (current == Actual::original &&
            (j_.phase == Phase::restoring || j_.phase == Phase::restore_pending ||
             j_.phase == Phase::restored || j_.phase == Phase::captured)) {
            if (!store_.verify(j_, true))
                return Result::pending_reboot;
            j_.phase = Phase::restored;
            if (save() != Result::verified || !store_.disarm_resume(j_))
                return Result::io_error;
            return Result::restored;
        }
        j_.phase = Phase::restoring;
        if (save() != Result::verified || !store_.restore(j_))
            return Result::io_error;
        current = store_.observe(j_);
        if (current != Actual::original && current != Actual::pending)
            return Result::conflict;
        j_.phase = Phase::restore_pending;
        return save() == Result::verified ? Result::pending_reboot : Result::io_error;
    }
    Result resume() {
        return j_.phase == Phase::restoring || j_.phase == Phase::restore_pending ||
                       j_.phase == Phase::restored
                   ? rollback()
                   : install();
    }
};
} // namespace setup::driver
