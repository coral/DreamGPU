// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
namespace setup {
// Only busy28 is retryable: it exits before the installer acquires ownership
// and before any transaction mutation. Every such launch is retained in the
// helper's log. All waits and children share one deadline and a launch bound.
template <class Ops> bool request_setup(Ops &ops, uint32_t &code) {
    constexpr uint32_t budget = 330000;
    const uint32_t started = ops.now();
    for (unsigned attempt = 1; attempt <= 16; ++attempt) {
        uint32_t elapsed = ops.now() - started;
        if (elapsed >= budget || !ops.wait((budget - elapsed < 90000) ? budget - elapsed : 90000))
            return false;
        elapsed = ops.now() - started;
        if (elapsed >= budget || !ops.launch(budget - elapsed, code))
            return false;
        if (code != 28)
            return true;
        elapsed = ops.now() - started;
        if (!ops.busy(attempt, elapsed))
            return false;
        if (attempt == 16 || elapsed >= budget)
            return true; // Retain busy as the final failure, not a completed operation.
        ops.pause(budget - elapsed < 250 ? budget - elapsed : 250);
    }
    return false;
}
} // namespace setup
