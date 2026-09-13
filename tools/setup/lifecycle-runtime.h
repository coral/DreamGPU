// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "win32-lifecycle.h"
#include "plan.h"
namespace setup::lifecycle {
inline bool owner_path(char out[MAX_PATH]) {
    DWORD n = GetWindowsDirectoryA(out, MAX_PATH);
    if (!n || n > MAX_PATH - 16)
        return false;
    lstrcatA(out, "\\DreamGPU");
    return true;
}
// A current private staging receipt is mandatory before accessing its journal.
// No arbitrary /path is accepted, and these APIs never adopt a preexisting tree.
inline bool verified_owner(Win32Store &store) {
    char root[MAX_PATH], receipt[MAX_PATH];
    if (!owner_path(root))
        return false;
    lstrcpyA(receipt, root);
    lstrcatA(receipt, "\\RESULT.json");
    HANDLE file =
        CreateFileA(receipt, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    constexpr char expected[] = "{\"schema\":1,\"state\":\"staged\",\"system_activated\":false,"
                                "\"provider\":\"not_ready\"}\r\n";
    char text[sizeof(expected)] = {};
    DWORD bytes = 0;
    bool okay = GetFileSize(file, nullptr) == sizeof(expected) - 1 &&
                ReadFile(file, text, sizeof(text), &bytes, nullptr) &&
                bytes == sizeof(expected) - 1 && !lstrcmpA(text, expected);
    CloseHandle(file);
    return okay && store.init(root);
}
// Called after a verified fresh /stage directory is committed. Public paths are
// read/captured only; the persisted journal remains staged and unactivated.
template <class Payloads> bool prepare_shared(Os os, const Payloads &payloads) {
    Win32Store store;
    if (!verified_owner(store))
        return false;
    static Journal j;
    if (!shared_plan(os, payloads, j) || !store.create_generation(j.generation))
        return false;
    for (unsigned n = 0; n < j.count; n++)
        if (!store.capture(j, n))
            return false;
    return store.persist(j);
}
enum class Action { continue_install, rollback, uninstall, upgrade };
template <class Payloads> Result act(Os os, Action action, const Payloads &payloads) {
    // Upgrade always installs new providers. Continuation must first inspect
    // the owned journal: recovery and removal do not require ready providers.
    if (action == Action::upgrade && !providers(os).ready())
        return Result::provider_not_ready;
    Win32Store store;
    if (!verified_owner(store))
        return Result::invalid;
    static Journal j;
    if (!store.load(j))
        return Result::invalid;
    if (action == Action::continue_install && j.state == State::rolling_back)
        action = Action::rollback;
    if (action == Action::continue_install && !j.uninstall && !providers(os).ready())
        return Result::provider_not_ready;
    if (action == Action::rollback) {
        Engine engine(store, j);
        return engine.rollback();
    }
    if (action == Action::uninstall) {
        static Journal removal;
        if (!uninstall_plan(j, removal) || !store.create_generation(removal.generation))
            return Result::invalid;
        for (unsigned n = 0; n < removal.count; n++) {
            // Capture uninstall's immediate-before backup for its own rollback,
            // preserving each original-generation reference for desired restore.
            Item retained = removal.items[n];
            if (!store.capture(removal, n) || !same(removal.items[n].before, retained.before))
                return Result::conflict;
            removal.items[n] = retained;
        }
        if (!store.persist(removal))
            return Result::io_error;
        Engine engine(store, removal);
        return engine.continue_apply(false);
    }
    if (action == Action::upgrade) {
        if (j.state != State::activated || j.generation == 0xffffffff)
            return Result::invalid;
        static Journal next;
        if (!shared_plan(os, payloads, next))
            return Result::invalid;
        next.generation = j.generation + 1;
        // Never forget an older owned destination, even if a changed package
        // happens to contain the same total number of operations.
        for (unsigned k = 0; k < j.count; k++) {
            bool retained = false;
            for (unsigned n = 0; n < next.count; n++)
                if (next.items[n].kind == j.items[k].kind &&
                    destination_equal(next.items[n].path, j.items[k].path,
                                      sizeof(j.items[k].path)) &&
                    destination_equal(next.items[n].name, j.items[k].name, sizeof(j.items[k].name)))
                    retained = true;
            if (!retained)
                return Result::invalid;
        }
        if (!store.create_generation(next.generation))
            return Result::io_error;
        for (unsigned n = 0; n < next.count; n++) {
            const Item *prior = nullptr;
            for (unsigned k = 0; k < j.count; k++)
                if (next.items[n].kind == j.items[k].kind &&
                    destination_equal(next.items[n].path, j.items[k].path,
                                      sizeof(j.items[k].path)) &&
                    destination_equal(next.items[n].name, j.items[k].name, sizeof(j.items[k].name)))
                    prior = &j.items[k];
            if (!store.capture(next, n, prior))
                return Result::conflict;
        }
        // Removing an old provider is a separate removal transaction; do not
        // silently forget old owned destinations when changing package shape.
        if (!store.persist(next))
            return Result::io_error;
        j = next;
    }
    Engine engine(store, j);
    return engine.continue_apply(providers(os).ready());
}
} // namespace setup::lifecycle
