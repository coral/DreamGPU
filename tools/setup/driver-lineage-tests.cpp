// SPDX-License-Identifier: GPL-2.0-or-later
#include "driver-test-win32.h"
#define main BoundLegacyMain
#include "driver-tests.cpp"
#undef main
#include "driver-lineage.h"
int main() {
    setup_case(Os::nt5, false);
    Win32Store store;
    Journal journal;
    assert(store.init(Os::nt5, root) && store.capture(journal, payloads));
    journal.phase = Phase::verified;
    Lineage lineage;
    assert(import_first_generation(journal, lineage));
    assert(lineage.selected == 1 && lineage.generations[0].baseline == 1);
    char next_hash[68]{};
    memset(next_hash, 'a', 64);
    assert(reserve_generation(lineage, Phase::verified, next_hash));
    assert(lineage.selected == 1 && lineage.pending == 2);
    assert(!begin_driver_reverse(lineage, false));
    Journal next = journal;
    next.phase = Phase::captured;
    assert(!publish_generation(lineage, next));
    memcpy(next.installer_sha, next_hash, sizeof(next_hash));
    assert(publish_generation(lineage, next));
    assert(lineage.selected == 2 && lineage.generations[1].parent == 1);
    auto before = lineage;
    assert(begin_driver_reverse(lineage, false));
    assert(!finish_driver_reverse(lineage, 2, Result::pending_reboot));
    assert(lineage.cursor == 2);
    assert(finish_driver_reverse(lineage, 2, Result::restored));
    assert(lineage.selected == 2 && lineage.phase == LineagePhase::restored);
    // Retrying an upgrade after rollback keeps the first installation baseline.
    assert(reserve_generation(lineage, Phase::restored, next_hash));
    assert(lineage.generations[2].parent == 2 && lineage.generations[2].baseline == 1);
    assert(publish_generation(lineage, next));
    assert(begin_driver_reverse(lineage, true));
    assert(!finish_driver_reverse(lineage, 1, Result::restored));
    assert(finish_driver_reverse(lineage, 3, Result::restored) && lineage.cursor == 2);
    assert(finish_driver_reverse(lineage, 2, Result::restored) && lineage.cursor == 1);
    assert(finish_driver_reverse(lineage, 1, Result::restored));
    assert(lineage.selected == 3 && lineage.phase == LineagePhase::restored);
    // Reinstall after baseline removal owns a new cycle, even for the same exe.
    assert(reserve_generation(lineage, Phase::restored, next_hash));
    assert(lineage.generations[3].parent == 3 && lineage.generations[3].baseline == 4);
    assert(publish_generation(lineage, next));
    assert(begin_driver_reverse(lineage, true));
    assert(finish_driver_reverse(lineage, 4, Result::restored));
    assert(lineage.phase == LineagePhase::restored); // Does not revisit the previous cycle.
    auto corrupt = before;
    corrupt.generations[1].parent = 2;
    assert(!valid_lineage(corrupt));
    corrupt = before;
    corrupt.generations[1].baseline = 3;
    assert(!valid_lineage(corrupt));
    lineage = before;
    while (lineage.count < MaxDriverGenerations) {
        assert(reserve_generation(lineage, Phase::verified, next_hash));
        assert(publish_generation(lineage, next));
    }
    assert(!reserve_generation(lineage, Phase::verified, next_hash));
    puts("PASS driver lineage: unpublished upgrade isolation, prior-version rollback, baseline "
         "uninstall, fresh cycle, corruption and bounded generations");
}
