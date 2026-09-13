// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "driver-actions.h"
#include "lifecycle-runtime.h"
namespace setup::driver {
inline GenerationRead query_generation(Os os, GenerationInfo &out) {
    char root[MAX_PATH];
    if (!lifecycle::owner_path(root))
        return GenerationRead::error;
    return query_generation_at(os, root, out);
}
inline bool verify_generation(Os os, const GenerationInfo &expected, bool original) {
    char root[MAX_PATH];
    return lifecycle::owner_path(root) && verify_generation_at(os, root, expected, original);
}
inline bool reverse_inputs_ready(Os os) {
    char owner[MAX_PATH], root[MAX_PATH];
    Lineage lineage;
    bool exists;
    if (!lifecycle::owner_path(owner) || !load_lineage(owner, os, lineage, exists, true) ||
        !exists || lineage.phase != LineagePhase::reversing)
        return false;
    uint32_t id = lineage.cursor;
    for (unsigned step = 0; step < MaxDriverGenerations; ++step) {
        Win32Store store;
        Journal journal;
        if (!id || !generation_root(owner, id, root) || !store.init(os, root, id > 1) ||
            !store.load(journal, true) ||
            lstrcmpA(journal.installer_sha, lineage.generations[id - 1].installer_sha) ||
            !store.recovery_inputs(journal))
            return false;
        if (lineage.direction == Direction::rollback ||
            id == lineage.generations[lineage.selected - 1].baseline)
            return true;
        id = lineage.generations[id - 1].parent;
    }
    return false;
}
// Called only after GLOBAL has durably bound a reverse recovery executor.
// Retire the exact old selected-driver startup command before publishing the
// new continuation. Unknown values and changed generation payloads conflict.
inline bool retire_reverse_resume(Os os) {
    char owner[MAX_PATH], root[MAX_PATH], executable[MAX_PATH];
    Lineage lineage;
    bool exists;
    if (!lifecycle::owner_path(owner) || !load_lineage(owner, os, lineage, exists) || !exists ||
        lineage.phase != LineagePhase::reversing ||
        !generation_root(owner, lineage.selected, root) || lstrlenA(root) + 11 >= MAX_PATH)
        return false;
    lstrcpyA(executable, root);
    lstrcatA(executable, "\\setup.exe");
    Win32Store store;
    Journal journal;
    return store.init(os, root, lineage.selected > 1) && store.load(journal) &&
           !lstrcmpA(journal.installer_sha,
                     lineage.generations[lineage.selected - 1].installer_sha) &&
           store.continuation(executable, journal.installer_sha) && store.disarm_resume(journal);
}
// Separate driver mechanics are available while the overall provider descriptor
// is incomplete. Default setup continues to report provider_not_ready; none of
// these actions claims system OpenGL/Direct3D activation.
template <class Payloads>
Result act(Os os, int action, const Payloads &payloads, const ReverseExecutor *recovery = nullptr) {
    lifecycle::Win32Store owner;
    char root[MAX_PATH];
    if (!lifecycle::verified_owner(owner) || !lifecycle::owner_path(root))
        return Result::invalid;
    return act_at(os, action, root, payloads, recovery);
}
} // namespace setup::driver
