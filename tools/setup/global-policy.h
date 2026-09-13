// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "lifecycle.h"
namespace setup::global {
enum class Intent : uint32_t { install, upgrade, rollback, uninstall, repair };
enum class Phase : uint32_t {
    driver,
    providers,
    activated,
    restore_providers,
    restore_driver,
    restored
};
struct Flow {
    Intent intent = Intent::install;
    Phase phase = Phase::driver;
    uint32_t provider_generation = 1;
    uint32_t epoch = 1;
};
static_assert(sizeof(Flow) == 16, "Persistent global coordinator flow layout");
inline bool forward(Intent intent) {
    return intent == Intent::install || intent == Intent::upgrade || intent == Intent::repair;
}
inline bool terminal(Phase phase) {
    return phase == Phase::activated || phase == Phase::restored;
}
inline bool valid(const Flow &flow) {
    if (uint32_t(flow.intent) > uint32_t(Intent::repair) || !flow.provider_generation ||
        !flow.epoch)
        return false;
    if (flow.intent == Intent::repair)
        return flow.phase == Phase::providers || flow.phase == Phase::activated;
    if (forward(flow.intent))
        return flow.phase == Phase::driver || flow.phase == Phase::providers ||
               flow.phase == Phase::activated;
    return flow.phase == Phase::restore_providers || flow.phase == Phase::restore_driver ||
           flow.phase == Phase::restored;
}
// The adapter owns the exclusive operation lock and authenticates its complete
// journal before calling resume(). persist atomically saves that whole journal,
// including Flow, installer identity and prior RunOnce ownership. arm/disarm are
// idempotent exact-owned RunOnce operations; components must not remove that arm.
// Component callbacks are durable and idempotent for intent/generation/epoch:
// complete means their own activation/restore proof has succeeded, while every
// other Result leaves this component pending. They own API probes and may not
// repeat completed probes merely because the coordinator's next save failed.
// terminal_verify only inspects identities; it does not launch probes/mutations.
// A failed syscall may already have taken effect. Reload the durable journal on
// process recovery; the policy never treats an unacknowledged save as progress.
// Epoch/generation are chosen by the adapter for a new operation, never advanced
// implicitly here. Reverse operations start at restore_providers explicitly.
template <class Store> class Engine {
    Store &store_;
    Flow &flow_;
    using Result = lifecycle::Result;
    Result component() {
        switch (flow_.phase) {
            case Phase::driver:
                return store_.driver_apply(flow_.intent);
            case Phase::providers:
                return store_.providers_apply(flow_.intent, flow_.provider_generation);
            case Phase::restore_providers:
                return store_.providers_restore(flow_.intent, flow_.provider_generation);
            case Phase::restore_driver:
                return store_.driver_restore(flow_.intent);
            default:
                return Result::invalid;
        }
    }
    Phase next() const {
        switch (flow_.phase) {
            case Phase::driver:
                return Phase::providers;
            case Phase::providers:
                return Phase::activated;
            case Phase::restore_providers:
                return Phase::restore_driver;
            case Phase::restore_driver:
                return Phase::restored;
            default:
                return flow_.phase;
        }
    }

  public:
    Engine(Store &store, Flow &flow) : store_(store), flow_(flow) {}
    Engine(const Engine &) = delete;
    Engine &operator=(const Engine &) = delete;
    Result resume() {
        if (!valid(flow_))
            return Result::invalid;
        if (terminal(flow_.phase)) {
            if (!store_.terminal_verify(flow_))
                return Result::conflict;
            return store_.disarm() ? Result::complete : Result::io_error;
        }
        // Initial/current intent must be durable before RunOnce or components.
        if (!store_.persist(flow_) || !store_.arm())
            return Result::io_error;
        // At most two components remain in either direction.
        for (unsigned step = 0; step < 2; ++step) {
            Result result = component();
            if (result != Result::complete)
                return result;
            Flow proposed = flow_;
            proposed.phase = next();
            if (terminal(proposed.phase) && !store_.terminal_verify(proposed))
                return Result::conflict;
            if (!store_.persist(proposed))
                return Result::io_error;
            flow_ = proposed;
            if (terminal(flow_.phase))
                return store_.disarm() ? Result::complete : Result::io_error;
        }
        return Result::invalid;
    }
};
} // namespace setup::global
