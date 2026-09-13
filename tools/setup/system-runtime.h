// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "lifecycle-runtime.h"
#include "runtime-files.h"
#include "runtime-resume.h"
#include "runtime-verify.h"
#include "system-engine.h"
namespace setup::lifecycle {
template <class Payloads> Result system_act(Os os, Action action, const Payloads &payloads) {
    VerifiedRuntimeStore store(os, payloads);
    if (!verified_owner(store))
        return Result::invalid;
    static Journal j;
    if (!store.load(j))
        return Result::invalid;
    if (!is_system_journal(j))
        return act(os, action, payloads);
    if (action == Action::continue_install && j.state == State::rolling_back)
        action = Action::rollback;
    if ((action == Action::upgrade || action == Action::repair ||
         (action == Action::continue_install && !j.uninstall)) &&
        !providers(os).ready())
        return Result::provider_not_ready;
    if (action == Action::repair) {
        static Journal next;
        uint32_t proofs = 0;
        const Result prepared = prepare_system_repair(os, payloads, store, j, next, proofs);
        if (prepared != Result::complete || !proofs)
            return prepared;
        if (!store.seed_repair(j, next, proofs) || !store.publish_preparation(next))
            return Result::io_error;
        j = next;
    } else if (action == Action::uninstall) {
        static Journal removal;
        const auto allowed = make_uninstall_plan(store, j, removal);
        if (allowed != Result::complete)
            return allowed;
        if (!store.prepare_generation(removal, nullptr, true))
            return Result::invalid;
        if (!store.publish_preparation(removal))
            return Result::io_error;
        j = removal;
    } else if (action == Action::upgrade) {
        static Journal next;
        const Result prepared = next_system_generation(os, payloads, store, j, next);
        if (prepared != Result::complete)
            return prepared;
        j = next;
    }
    RuntimeFiles files(store, j, os);
    RuntimeResume resume(store, j.generation, os);
    SystemEngine engine(store, j, files, resume);
    return action == Action::rollback ? engine.rollback() : engine.apply(providers(os).ready());
}
} // namespace setup::lifecycle
