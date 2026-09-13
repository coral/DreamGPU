// SPDX-License-Identifier: GPL-2.0-or-later
#include "global-policy.h"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace setup::global;
using Result = setup::lifecycle::Result;
static bool equal(const Flow &a, const Flow &b) {
    return a.intent == b.intent && a.phase == b.phase &&
           a.provider_generation == b.provider_generation && a.epoch == b.epoch;
}
static Flow initial(Intent intent) {
    return {intent,
            intent == Intent::repair ? Phase::providers
            : forward(intent)        ? Phase::driver
                                     : Phase::restore_providers,
            37, 9};
}
struct Store {
    Flow durable;
    bool armed = false, first_done = false, second_done = false, identities = true;
    unsigned calls = 0, fail = 0, first_calls = 0, second_calls = 0;
    unsigned first_effects = 0, second_effects = 0, verify_calls = 0, disarm_calls = 0;
    bool fail_after = false;
    Result first_result = Result::complete, second_result = Result::complete;
    std::vector<Phase> published;
    explicit Store(Flow value) : durable(value), first_done(value.intent == Intent::repair) {}
    bool interrupt() {
        return ++calls == fail;
    }
    bool persist(const Flow &flow) {
        bool failed = interrupt();
        if (failed && !fail_after)
            return false;
        assert(flow.intent == durable.intent && flow.epoch == durable.epoch &&
               flow.provider_generation == durable.provider_generation);
        if (terminal(flow.phase))
            assert(first_done && second_done && identities && verify_calls);
        durable = flow;
        published.push_back(flow.phase);
        return !failed;
    }
    bool arm() {
        bool failed = interrupt();
        if (failed && !fail_after)
            return false;
        assert(!terminal(durable.phase));
        armed = true;
        return !failed;
    }
    bool disarm() {
        ++disarm_calls;
        assert(terminal(durable.phase) && first_done && second_done && identities);
        bool failed = interrupt();
        if (failed && !fail_after)
            return false;
        armed = false;
        return !failed;
    }
    Result apply(Intent intent, Phase phase, bool first, uint32_t generation) {
        assert(armed && durable.intent == intent && durable.phase == phase &&
               durable.provider_generation == generation);
        unsigned &invocations = first ? first_calls : second_calls;
        unsigned &effects = first ? first_effects : second_effects;
        bool &done = first ? first_done : second_done;
        ++invocations;
        if (!first)
            assert(first_done);
        bool failed = interrupt();
        if (failed && !fail_after)
            return Result::io_error;
        Result result = first ? first_result : second_result;
        if (result != Result::complete)
            return result;
        // Models the component's durable idempotent transaction. Retrying this
        // callback after a lost coordinator acknowledgement never repeats proof.
        if (!done) {
            done = true;
            ++effects;
        }
        return failed ? Result::io_error : Result::complete;
    }
    Result driver_apply(Intent intent) {
        return apply(intent, Phase::driver, true, durable.provider_generation);
    }
    Result providers_apply(Intent intent, uint32_t generation) {
        return apply(intent, Phase::providers, false, generation);
    }
    Result providers_restore(Intent intent, uint32_t generation) {
        return apply(intent, Phase::restore_providers, true, generation);
    }
    Result driver_restore(Intent intent) {
        return apply(intent, Phase::restore_driver, false, durable.provider_generation);
    }
    bool terminal_verify(const Flow &flow) {
        ++verify_calls;
        assert(terminal(flow.phase) && first_done && second_done);
        return !interrupt() && identities;
    }
};
static unsigned interrupted(Intent intent, unsigned failure, bool after) {
    Flow flow = initial(intent);
    Store store(flow);
    store.fail = failure;
    store.fail_after = after;
    Engine engine(store, flow);
    Result first = engine.resume();
    unsigned count = store.calls;
    assert((first == Result::complete) == !failure);
    // Restart from the actual durable image, including writes that succeeded
    // before their acknowledgement was lost.
    store.fail = 0;
    flow = store.durable;
    Engine recovered(store, flow);
    assert(recovered.resume() == Result::complete);
    assert(terminal(flow.phase) && equal(flow, store.durable) && !store.armed);
    assert(store.first_effects == unsigned(intent != Intent::repair) && store.second_effects == 1);
    if (intent == Intent::repair)
        assert(!store.first_calls);
    unsigned first_calls = store.first_calls, second_calls = store.second_calls;
    unsigned verify_calls = store.verify_calls;
    assert(recovered.resume() == Result::complete);
    assert(first_calls == store.first_calls && second_calls == store.second_calls);
    assert(store.verify_calls == verify_calls + 1);
    // An altered provider identity is never repaired/reprobed by terminal resume.
    store.identities = false;
    unsigned disarms = store.disarm_calls;
    assert(recovered.resume() == Result::conflict);
    assert(disarms == store.disarm_calls && first_calls == store.first_calls &&
           second_calls == store.second_calls);
    return count;
}
static void component_results(Intent intent) {
    for (Result result : {Result::pending_reboot, Result::conflict, Result::io_error,
                          Result::invalid, Result::provider_not_ready}) {
        for (bool second : {false, true}) {
            if (intent == Intent::repair && !second)
                continue;
            Flow flow = initial(intent);
            Store store(flow);
            (second ? store.second_result : store.first_result) = result;
            Engine engine(store, flow);
            assert(engine.resume() == result);
            Phase pending = second ? (forward(intent) ? Phase::providers : Phase::restore_driver)
                                   : initial(intent).phase;
            assert(flow.phase == pending && store.durable.phase == pending && store.armed);
            assert(!store.disarm_calls && !store.verify_calls);
            if (!second)
                assert(!store.second_calls);
            store.first_result = store.second_result = Result::complete;
            assert(engine.resume() == Result::complete);
            assert(store.first_effects == unsigned(intent != Intent::repair) &&
                   store.second_effects == 1);
        }
    }
}
static void failed_phase_save(Intent intent) {
    for (unsigned failure : {4u, 7u}) {
        Flow flow = initial(intent);
        Store store(flow);
        store.fail = failure;
        Engine engine(store, flow);
        assert(engine.resume() == Result::io_error);
        Phase previous = failure == 4
                             ? initial(intent).phase
                             : (forward(intent) ? Phase::providers : Phase::restore_driver);
        assert(flow.phase == previous && store.durable.phase == previous && store.armed);
        store.fail = 0;
        assert(engine.resume() == Result::complete);
        assert(store.first_effects == unsigned(intent != Intent::repair) &&
               store.second_effects == 1);
    }
    Flow flow = initial(intent);
    Store store(flow);
    store.identities = false;
    Engine engine(store, flow);
    assert(engine.resume() == Result::conflict);
    assert(!terminal(flow.phase) && !terminal(store.durable.phase) && store.armed);
    assert(!store.disarm_calls);
}
static void invalid_flows() {
    for (Flow flow :
         {Flow{Intent(99), Phase::driver, 1, 1}, Flow{Intent::install, Phase(99), 1, 1},
          Flow{Intent::install, Phase::restored, 1, 1}, Flow{Intent::rollback, Phase::driver, 1, 1},
          Flow{Intent::uninstall, Phase::activated, 1, 1},
          Flow{Intent::upgrade, Phase::restore_providers, 1, 1},
          Flow{Intent::repair, Phase::driver, 1, 1},
          Flow{Intent::repair, Phase::restore_driver, 1, 1},
          Flow{Intent::install, Phase::driver, 0, 1}, Flow{Intent::install, Phase::driver, 1, 0}}) {
        Store store(flow);
        Engine engine(store, flow);
        assert(engine.resume() == Result::invalid);
        assert(!store.calls);
    }
}
int main() {
    unsigned failure_cases = 0;
    for (Intent intent :
         {Intent::install, Intent::upgrade, Intent::rollback, Intent::uninstall, Intent::repair}) {
        unsigned count = interrupted(intent, 0, false);
        assert(count == (intent == Intent::repair ? 6u : 8u));
        for (unsigned fail = 1; fail <= count; ++fail)
            for (bool after : {false, true}) {
                interrupted(intent, fail, after);
                ++failure_cases;
            }
        component_results(intent);
        if (intent != Intent::repair)
            failed_phase_save(intent);
    }
    invalid_flows();
    printf("PASS global coordinator: %u syscall interruption cases, 45 component pending/error "
           "cases, forward/reverse order, durable phases and terminal readonly verification\n",
           failure_cases);
}
