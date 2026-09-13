// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "driver-generation.h"
namespace setup::driver {
inline bool executing_driver_installer(char out[68]) {
    char path[MAX_PATH];
    DWORD length = GetModuleFileNameA(nullptr, path, sizeof(path));
    if (!length || length >= sizeof(path))
        return false;
    HANDLE file =
        CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    DWORD high = 0, size = GetFileSize(file, &high), total = 0;
    bool good = !high && size && size <= 64u * 1024 * 1024;
    Sha256 digest;
    BYTE bytes[4096];
    while (good && total < size) {
        DWORD count = size - total, got = 0;
        if (count > sizeof(bytes))
            count = sizeof(bytes);
        good = ReadFile(file, bytes, count, &got, nullptr) && got == count;
        if (good) {
            digest.update(bytes, got);
            total += got;
        }
    }
    BOOL closed = CloseHandle(file);
    if (good && closed)
        digest.finish(out);
    return good && closed;
}
struct ReverseExecutor {
    const char *path;
    const char *sha;
};
// The caller has already authenticated the installer owner directory. Each
// publication is separate from private preparation; no old generation is ever
// reused as the target of a failed upgrade.
template <class Payloads>
Result act_at(Os os, int action, const char *owner, const Payloads &payloads,
              const ReverseExecutor *recovery = nullptr, bool managed_startup = false) {
    if (action < 0 || action > 4)
        return Result::invalid;
    Lineage lineage;
    bool exists;
    if (!load_lineage(owner, os, lineage, exists))
        return Result::invalid;
    if (recovery && (!exists || lineage.phase != LineagePhase::reversing ||
                     (action != 1 && action != 2 && action != 4)))
        return Result::invalid;
    Journal journal;
    if (!exists) {
        Win32Store first;
        if (!first.init(os, owner))
            return Result::invalid;
        if (!first.exists()) {
            if (action != 0 || !first.capture(journal, payloads)) {
                first.diagnostic(Result::invalid);
                return Result::invalid;
            }
        } else if (!first.load(journal))
            return Result::invalid;
        if (journal.phase == Phase::capturing && !first.complete_capture(journal))
            return Result::io_error;
        Lineage empty;
        if (!import_first_generation(journal, lineage) ||
            !save_lineage(owner, empty, lineage, true))
            return Result::io_error;
    }
    if (!generation_journal(os, owner, lineage, lineage.selected, journal, false))
        return Result::invalid;
    if (journal.child_pid)
        return Result::pending_reboot;
    if (action == 0 && lineage.phase == LineagePhase::restored)
        action = 3;
    if (lineage.pending) {
        char caller[68]{};
        if (action == 3 && !executing_driver_installer(caller))
            return Result::io_error;
        if (action == 2 || action == 4 ||
            (action == 3 &&
             lstrcmpA(caller, lineage.generations[lineage.pending - 1].installer_sha))) {
            const auto previous = lineage;
            if (!abandon_preparation(lineage) || !save_lineage(owner, previous, lineage))
                return Result::io_error;
        }
    }
    if (action == 3 || lineage.phase == LineagePhase::preparing) {
        if (action != 3)
            return Result::invalid;
        char installer[68]{};
        if (!executing_driver_installer(installer))
            return Result::io_error;
        if (!lineage.pending) {
            GenerationInfo before;
            if (query_generation_at(os, owner, before) != GenerationRead::present ||
                !verify_generation_at(os, owner, before, before.phase == Phase::restored) ||
                !GenerationStager::absent(owner, lineage.count + 1))
                return Result::conflict;
            const auto previous = lineage;
            if (!reserve_generation(lineage, before.phase, installer))
                return Result::invalid;
            if (!save_lineage(owner, previous, lineage))
                return Result::io_error;
        } else if (lstrcmpA(installer, lineage.generations[lineage.pending - 1].installer_sha))
            return Result::conflict;
        if (!GenerationStager::stage(owner, lineage, payloads))
            return Result::io_error;
        char staged[MAX_PATH];
        Win32Store next;
        if (!generation_root(owner, lineage.pending, staged) || !next.init(os, staged, true))
            return Result::invalid;
        if (next.exists()) {
            if (!next.load(journal))
                return Result::invalid;
        } else if (!next.capture(journal, payloads)) {
            next.diagnostic(Result::io_error);
            return Result::io_error;
        }
        if (journal.phase == Phase::capturing && !next.complete_capture(journal))
            return Result::io_error;
        const auto previous = lineage;
        if (!publish_generation(lineage, journal) || !save_lineage(owner, previous, lineage))
            return Result::io_error;
        action = 0;
    }
    if (action == 2 || action == 4) {
        const auto previous = lineage;
        if (!begin_driver_reverse(lineage, action == 4))
            return Result::invalid;
        if (!save_lineage(owner, previous, lineage))
            return Result::io_error;
    }
    if (lineage.phase == LineagePhase::restored) {
        GenerationInfo current;
        return query_generation_at(os, owner, current) == GenerationRead::present &&
                       verify_generation_at(os, owner, current, true)
                   ? Result::restored
                   : Result::conflict;
    }
    // All reverse steps resume through the selected installer, whose code knows
    // this catalogue. Never arm an older executable that predates generations.
    char selected_root[MAX_PATH], continuation[MAX_PATH];
    if (!generation_root(owner, lineage.selected, selected_root) ||
        lstrlenA(selected_root) + 11 >= MAX_PATH)
        return Result::invalid;
    lstrcpyA(continuation, selected_root);
    lstrcatA(continuation, "\\setup.exe");
    for (unsigned step = 0; step < MaxDriverGenerations; ++step) {
        const bool reverse = lineage.phase == LineagePhase::reversing;
        const uint32_t id = reverse ? lineage.cursor : lineage.selected;
        char root[MAX_PATH];
        Win32Store store;
        if (!generation_root(owner, id, root) || !store.init(os, root, id > 1) ||
            !store.load(journal) ||
            lstrcmpA(journal.installer_sha, lineage.generations[id - 1].installer_sha) ||
            !store.continuation(recovery ? recovery->path : continuation,
                                recovery ? recovery->sha
                                         : lineage.generations[lineage.selected - 1].installer_sha))
            return Result::invalid;
        if (journal.child_pid)
            return Result::pending_reboot;
        store.managed_startup(managed_startup);
        Engine engine(store, journal);
        Result result = reverse       ? engine.rollback()
                        : action == 0 ? engine.install()
                                      : engine.resume();
        store.diagnostic(result);
        if (!reverse || result != Result::restored)
            return result;
        const auto previous = lineage;
        if (!finish_driver_reverse(lineage, id, result) || !save_lineage(owner, previous, lineage))
            return Result::io_error;
        if (lineage.phase == LineagePhase::restored)
            return Result::restored;
    }
    return Result::invalid;
}
} // namespace setup::driver
