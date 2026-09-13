// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "global-policy.h"
#include "policy.h"
#include "driver-lifecycle.h"
namespace setup::global {
struct DriverIdentity {
    uint32_t id = 0, parent = 0, baseline = 0;
    driver::Phase phase = driver::Phase::capturing;
    char installer_sha[68]{};
};
inline bool valid_driver(const DriverIdentity &d) {
    if (!d.id)
        return !d.parent && !d.baseline && d.phase == driver::Phase::capturing &&
               driver::empty(d.installer_sha);
    return d.parent < d.id && d.baseline && d.baseline <= d.id &&
           uint32_t(d.phase) <= uint32_t(driver::Phase::restored) && driver::hash(d.installer_sha);
}
inline bool same_driver(const DriverIdentity &a, const DriverIdentity &b) {
    return a.id == b.id && a.parent == b.parent && a.baseline == b.baseline &&
           lifecycle::text_equal(a.installer_sha, b.installer_sha, sizeof(a.installer_sha));
}
struct Record {
    uint32_t magic = 0x314c4744, version = 3;
    Os os = Os::unsupported;
    Flow flow{};
    lifecycle::Image installer{};
    // Reverse recovery must distinguish an upgrade that never published its
    // target provider generation from an installed target that needs undoing.
    Intent origin_intent = Intent::install;
    uint32_t origin_provider_generation = 1;
    lifecycle::State origin_provider_state = lifecycle::State::staged;
    uint32_t origin_provider_uninstall = 0;
    DriverIdentity driver_before{}, driver_target{};
    char driver_installer_sha[68]{};
};
inline bool provider_terminal(lifecycle::State state, uint32_t uninstall) {
    return (state == lifecycle::State::activated && !uninstall) ||
           (state == lifecycle::State::removed && uninstall == 1) ||
           state == lifecycle::State::failed;
}
inline bool valid_record(const Record &r) {
    if (r.magic != 0x314c4744 || r.version != 3 || (r.os != Os::win98 && r.os != Os::nt5) ||
        !valid(r.flow) || !r.installer.exists ||
        !lifecycle::valid_image(r.installer, lifecycle::Kind::file) ||
        !r.origin_provider_generation || !valid_driver(r.driver_before) ||
        !valid_driver(r.driver_target) || !driver::hash(r.driver_installer_sha) ||
        r.origin_provider_uninstall > 1 ||
        uint32_t(r.origin_provider_state) > uint32_t(lifecycle::State::removed) ||
        (r.origin_intent != Intent::install && r.origin_intent != Intent::upgrade &&
         r.origin_intent != Intent::uninstall && r.origin_intent != Intent::repair))
        return false;
    if (r.flow.intent != r.origin_intent &&
        (r.flow.intent != Intent::rollback || r.origin_intent == Intent::uninstall))
        return false;
    if (r.origin_intent == Intent::upgrade &&
        !provider_terminal(r.origin_provider_state, r.origin_provider_uninstall))
        return false;
    if (r.origin_intent == Intent::repair &&
        (r.origin_provider_state != lifecycle::State::activated || r.origin_provider_uninstall))
        return false;
    if (r.origin_intent == Intent::uninstall &&
        ((r.origin_provider_state != lifecycle::State::activated &&
          r.origin_provider_state != lifecycle::State::failed) ||
         r.origin_provider_uninstall))
        return false;
    // Provider repair never acquires a driver generation, including its rollback.
    if (r.origin_intent == Intent::repair && (r.driver_target.id || !r.driver_before.id ||
                                              r.driver_before.phase != driver::Phase::verified))
        return false;
    if (r.driver_target.id) {
        const bool retained = same_driver(r.driver_target, r.driver_before);
        const bool child =
            r.driver_target.id > r.driver_before.id &&
            r.driver_target.parent == r.driver_before.id &&
            lifecycle::text_equal(r.driver_target.installer_sha, r.driver_installer_sha, 68) &&
            (r.driver_target.baseline == r.driver_before.baseline ||
             ((!r.driver_before.id || r.driver_before.phase == driver::Phase::restored) &&
              r.driver_target.baseline == r.driver_target.id));
        if (!retained && !child)
            return false;
    }
    const bool increment = r.origin_intent != Intent::install;
    return (!increment || r.origin_provider_generation != UINT32_MAX) &&
           r.flow.provider_generation == r.origin_provider_generation + unsigned(increment);
}
} // namespace setup::global
