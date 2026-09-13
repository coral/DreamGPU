// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "driver-store.h"
#include "driver-lineage-store.h"
namespace setup::driver {
enum class GenerationRead { absent, present, error };
struct GenerationInfo {
    uint32_t id = 0, parent = 0, baseline = 0;
    Phase phase = Phase::capturing;
    char installer_sha[68]{};
};
inline bool same_generation(const GenerationInfo &a, const GenerationInfo &b) {
    return a.id == b.id && a.parent == b.parent && a.baseline == b.baseline && a.phase == b.phase &&
           !lstrcmpA(a.installer_sha, b.installer_sha);
}
inline bool generation_journal(Os os, const char *owner, const Lineage &lineage, uint32_t id,
                               Journal &journal, bool read_only = true) {
    char root[MAX_PATH];
    Win32Store store;
    return id && id <= lineage.count && generation_root(owner, id, root) &&
           store.init(os, root, id > 1) && store.load(journal, read_only) &&
           !lstrcmpA(journal.installer_sha, lineage.generations[id - 1].installer_sha);
}
inline GenerationRead read_generation_at(Os os, const char *owner, GenerationInfo &out,
                                         Journal &journal) {
    out = {};
    if ((os != Os::win98 && os != Os::nt5) || !bounded(owner, MAX_PATH - 48))
        return GenerationRead::error;
    Lineage lineage;
    bool exists;
    if (!load_lineage(owner, os, lineage, exists, true))
        return GenerationRead::error;
    if (!exists) {
        char path[MAX_PATH];
        lstrcpyA(path, owner);
        lstrcatA(path, "\\driver.bin");
        DWORD attributes = GetFileAttributesA(path);
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            DWORD error = GetLastError();
            return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND
                       ? GenerationRead::absent
                       : GenerationRead::error;
        }
        if (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
            return GenerationRead::error;
        Win32Store store;
        if (!store.init(os, owner) || !store.load(journal, true) ||
            !import_first_generation(journal, lineage))
            return GenerationRead::error;
    } else if (!generation_journal(os, owner, lineage, lineage.selected, journal))
        return GenerationRead::error;
    const auto &g = lineage.generations[lineage.selected - 1];
    out.id = g.id;
    out.parent = g.parent;
    out.baseline = g.baseline;
    out.phase = lineage.phase == LineagePhase::restored    ? Phase::restored
                : lineage.phase == LineagePhase::reversing ? Phase::restore_pending
                                                           : journal.phase;
    lstrcpyA(out.installer_sha, g.installer_sha);
    return GenerationRead::present;
}
inline GenerationRead query_generation_at(Os os, const char *root, GenerationInfo &out) {
    Journal journal;
    return read_generation_at(os, root, out, journal);
}
inline bool verify_generation_at(Os os, const char *owner, const GenerationInfo &expected,
                                 bool original) {
    if (expected.phase != (original ? Phase::restored : Phase::verified))
        return false;
    GenerationInfo current;
    Journal journal;
    if (read_generation_at(os, owner, current, journal) != GenerationRead::present ||
        !same_generation(current, expected))
        return false;
    Lineage lineage;
    bool exists;
    if (!load_lineage(owner, os, lineage, exists, true))
        return false;
    uint32_t physical = current.id;
    if (original && exists &&
        ((lineage.direction == Direction::uninstall && lineage.phase == LineagePhase::restored) ||
         (lineage.phase == LineagePhase::preparing &&
          lineage.prior_direction == Direction::uninstall &&
          lineage.prior_phase == LineagePhase::restored)))
        physical = current.baseline;
    if (exists && !generation_journal(os, owner, lineage, physical, journal))
        return false;
    char root[MAX_PATH];
    Win32Store store;
    return generation_root(owner, physical, root) && store.init(os, root, physical > 1) &&
           store.verify_readonly(journal, original);
}
} // namespace setup::driver
