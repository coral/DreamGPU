// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "driver-lifecycle.h"
namespace setup::driver {
// Separate from Journal V2: desired generations never overwrite the original
// node, before-images or ownership receipts of an earlier installation.
constexpr uint32_t MaxDriverGenerations = 32;
enum class Direction : uint32_t { forward, rollback, uninstall };
enum class LineagePhase : uint32_t { current, preparing, reversing, restored };
enum class GenerationState : uint32_t { published, preparing, abandoned };
struct DriverGeneration {
    uint32_t id = 0, parent = 0, baseline = 0;
    GenerationState state = GenerationState::published;
    char installer_sha[68]{};
};
struct Lineage {
    uint32_t magic = 0x4c474744, version = 1;
    Os os = Os::unsupported;
    uint32_t count = 0, selected = 0, pending = 0, cursor = 0;
    Direction direction = Direction::forward;
    LineagePhase phase = LineagePhase::current;
    Direction prior_direction = Direction::forward;
    LineagePhase prior_phase = LineagePhase::current;
    DriverGeneration generations[MaxDriverGenerations]{};
};
static_assert(sizeof(DriverGeneration) == 84, "driver generation descriptor");
static_assert(sizeof(Lineage) == 2732, "driver lineage durable layout");
inline bool valid_lineage(const Lineage &l) {
    if (l.magic != 0x4c474744 || l.version != 1 || (l.os != Os::win98 && l.os != Os::nt5) ||
        !l.count || l.count > MaxDriverGenerations || !l.selected || l.selected > l.count ||
        uint32_t(l.direction) > 2 || uint32_t(l.phase) > 3 || uint32_t(l.prior_direction) > 2 ||
        (l.prior_phase != LineagePhase::current && l.prior_phase != LineagePhase::restored))
        return false;
    for (unsigned n = 0; n < l.count; ++n) {
        const auto &g = l.generations[n];
        if (g.id != n + 1 || (n ? (!g.parent || g.parent >= g.id) : g.parent != 0) || !g.baseline ||
            g.baseline > g.id || !hash(g.installer_sha) || uint32_t(g.state) > 2 ||
            l.generations[g.baseline - 1].baseline != g.baseline ||
            (g.parent && l.generations[g.parent - 1].state != GenerationState::published) ||
            (g.baseline != g.id &&
             l.generations[g.baseline - 1].state != GenerationState::published))
            return false;
        if (n && g.baseline != g.id && g.baseline != l.generations[g.parent - 1].baseline)
            return false;
    }
    if (l.generations[l.selected - 1].state != GenerationState::published)
        return false;
    unsigned preparing = 0;
    for (unsigned n = 0; n < l.count; ++n)
        if (l.generations[n].state == GenerationState::preparing)
            ++preparing;
    if (preparing != (l.phase == LineagePhase::preparing ? 1u : 0u))
        return false;
    if (l.phase == LineagePhase::preparing)
        return l.direction == Direction::forward && l.pending == l.count &&
               l.generations[l.pending - 1].parent == l.selected &&
               l.generations[l.pending - 1].state == GenerationState::preparing && !l.cursor;
    if (l.pending)
        return false;
    if (l.phase == LineagePhase::reversing) {
        uint32_t ancestor = l.selected;
        while (ancestor && ancestor != l.cursor)
            ancestor = l.generations[ancestor - 1].parent;
        if (!ancestor)
            return false;
    }
    if (l.phase == LineagePhase::reversing)
        return l.direction != Direction::forward && l.cursor <= l.selected &&
               l.cursor >= l.generations[l.selected - 1].baseline &&
               l.generations[l.cursor - 1].state == GenerationState::published &&
               (l.direction == Direction::uninstall || l.cursor == l.selected);
    return !l.cursor && (l.phase != LineagePhase::restored || l.direction != Direction::forward);
}
inline bool import_first_generation(const Journal &j, Lineage &out) {
    if (!valid(j))
        return false;
    out = {};
    out.os = j.os;
    out.count = out.selected = 1;
    auto &g = out.generations[0];
    g.id = g.baseline = 1;
    for (unsigned n = 0; n < sizeof(g.installer_sha); ++n)
        g.installer_sha[n] = j.installer_sha[n];
    if (j.phase == Phase::restoring || j.phase == Phase::restore_pending) {
        out.direction = Direction::rollback;
        out.phase = LineagePhase::reversing;
        out.cursor = 1;
    } else if (j.phase == Phase::restored) {
        out.direction = Direction::rollback;
        out.phase = LineagePhase::restored;
    }
    return valid_lineage(out);
}
// The caller durably saves this reservation before creating its private tree.
// selected remains unchanged until capture completes: GLOBAL must not mistake a
// failed private preparation for a new driver operation that needs rollback.
inline bool reserve_generation(Lineage &l, Phase selected_phase, const char installer[68]) {
    if (!valid_lineage(l) || l.pending || l.phase == LineagePhase::reversing ||
        l.count == MaxDriverGenerations || !hash(installer) ||
        (selected_phase != Phase::verified && selected_phase != Phase::restored))
        return false;
    const auto previous = l.generations[l.selected - 1];
    bool fresh = selected_phase == Phase::restored &&
                 (l.direction == Direction::uninstall || previous.id == previous.baseline);
    auto &next = l.generations[l.count];
    next = {};
    next.id = ++l.count;
    next.state = GenerationState::preparing;
    next.parent = previous.id;
    next.baseline = fresh ? next.id : previous.baseline;
    for (unsigned n = 0; n < sizeof(next.installer_sha); ++n)
        next.installer_sha[n] = installer[n];
    l.pending = next.id;
    l.prior_direction = l.direction;
    l.prior_phase = l.phase;
    l.cursor = 0;
    l.direction = Direction::forward;
    l.phase = LineagePhase::preparing;
    return valid_lineage(l);
}
inline bool publish_generation(Lineage &l, const Journal &captured) {
    if (!valid_lineage(l) || l.phase != LineagePhase::preparing || !valid(captured) ||
        captured.os != l.os || captured.phase != Phase::captured)
        return false;
    const auto &g = l.generations[l.pending - 1];
    for (unsigned n = 0; n < sizeof(g.installer_sha); ++n)
        if (g.installer_sha[n] != captured.installer_sha[n])
            return false;
    l.selected = l.pending;
    l.generations[l.selected - 1].state = GenerationState::published;
    l.pending = 0;
    l.phase = LineagePhase::current;
    return valid_lineage(l);
}
// An unpublished tree has made no device changes. Preserve its reservation and
// bytes as abandoned evidence; a later operation receives a fresh never-reused ID.
inline bool abandon_preparation(Lineage &l) {
    if (!valid_lineage(l) || l.phase != LineagePhase::preparing)
        return false;
    l.generations[l.pending - 1].state = GenerationState::abandoned;
    l.pending = 0;
    l.phase = l.prior_phase;
    l.direction = l.prior_direction;
    return valid_lineage(l);
}
inline bool begin_driver_reverse(Lineage &l, bool uninstall) {
    if (!valid_lineage(l) || l.pending)
        return false;
    const Direction wanted = uninstall ? Direction::uninstall : Direction::rollback;
    if (l.phase == LineagePhase::reversing)
        return l.direction == wanted;
    if (l.phase == LineagePhase::restored && l.direction == wanted)
        return true;
    l.direction = wanted;
    l.phase = LineagePhase::reversing;
    l.cursor = l.selected;
    return valid_lineage(l);
}
// Advance only after the actual driver adapter proves this generation's before
// image. A pending reboot never advances this cursor or skips a prior baseline.
inline bool finish_driver_reverse(Lineage &l, uint32_t completed, Result result) {
    if (!valid_lineage(l) || l.phase != LineagePhase::reversing || completed != l.cursor ||
        result != Result::restored)
        return false;
    if (l.direction == Direction::rollback || completed == l.generations[l.selected - 1].baseline) {
        l.cursor = 0;
        l.phase = LineagePhase::restored;
    } else {
        l.cursor = l.generations[completed - 1].parent;
    }
    return valid_lineage(l);
}
} // namespace setup::driver
