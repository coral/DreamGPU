// SPDX-License-Identifier: GPL-2.0-or-later
#define DG_SETUP_ADAPTER_TEST
#include "win32-lifecycle.h"
#include "plan.h"
#include <array>
#include <cassert>
#include <string>
#include <vector>
using namespace setup;
using namespace setup::lifecycle;
struct Payload {
    unsigned os, id, size;
    const char *path;
    char sha[65];
};
constexpr const char *Sources[] = {"application/dgpugl.dll",  "application/glide2x.dll",
                                   "application/wined3d.dll", "application/winedd.dll",
                                   "application/wined8.dll",  "application/wined9.dll",
                                   "application/dgpuicd.dll", "switchers/ddraw_xp.dll",
                                   "switchers/d3d8_xp.dll",   "switchers/d3d9_xp.dll"};
static std::array<Payload, 10> payloads;
static std::vector<BYTE> native() {
    std::vector<BYTE> bytes(128);
    bytes[0] = 'M';
    bytes[1] = 'Z';
    bytes[60] = 64;
    bytes[64] = 'P';
    bytes[65] = 'E';
    bytes[68] = 0x4c;
    bytes[69] = 1;
    bytes[88] = 0x0b;
    bytes[89] = 1;
    return bytes;
}
static void initial() {
    using namespace fake_win32;
    reset();
    files[canon("C:\\WINDOWS\\DreamGPU")].directory = true;
    for (unsigned n = 0; n < 10; ++n) {
        std::string content = "verified payload " + std::to_string(n);
        resources[100 + n] = {content.begin(), content.end()};
        payloads[n] = {unsigned(Os::nt5), 100 + n, unsigned(content.size()), Sources[n], {}};
        native_alias::hash(resources[100 + n], payloads[n].sha);
    }
    for (const char *name : {"ddraw.dll", "d3d8.dll"}) {
        auto &node = files[canon((std::string("C:\\WINDOWS\\SYSTEM\\") + name).c_str())];
        node.bytes = native();
        node.attributes = FILE_ATTRIBUTE_READONLY;
        node.flushed = true;
    }
}
static bool prepare(Win32Store &store, Journal &j) {
    return store.init("C:\\WINDOWS\\DreamGPU") && system_plan(Os::nt5, payloads, j, true) &&
           capture_system(Os::nt5, store, j);
}
static void original_files() {
    using namespace fake_win32;
    for (const char *name : {"ddraw.dll", "d3d8.dll"}) {
        const auto &node = files.at(canon((std::string("C:\\WINDOWS\\SYSTEM\\") + name).c_str()));
        assert(node.bytes == native() && node.attributes == FILE_ATTRIBUTE_READONLY);
    }
    assert(!files.count(canon("C:\\WINDOWS\\SYSTEM\\d3d9.dll")));
}
static Journal restored_upgrade(Win32Store &store, bool repeated = false, bool repair = false) {
    initial();
    Journal first, next;
    assert(prepare(store, first));
    Engine install(store, first);
    assert(install.continue_apply(true) == Result::pending_reboot);
    first.state = State::activated;
    assert(store.persist(first));
    for (unsigned pass = 0; pass < (repeated ? 2u : 1u); ++pass) {
        if (repair) {
            auto &repaired = fake_win32::files[fake_win32::canon("C:\\WINDOWS\\SYSTEM\\ddraw.dll")];
            repaired.bytes = native();
            repaired.attributes = FILE_ATTRIBUTE_READONLY;
            uint32_t proofs = 0;
            assert(prepare_system_repair(Os::nt5, payloads, store, first, next, proofs) ==
                   Result::complete);
            assert(proofs == 0xcu && store.publish_preparation(next));
        } else {
            auto &bytes = fake_win32::resources[payloads[0].id];
            bytes.push_back(BYTE(0x41 + pass));
            payloads[0].size = unsigned(bytes.size());
            native_alias::hash(bytes, payloads[0].sha);
            assert(next_system_generation(Os::nt5, payloads, store, first, next) ==
                   Result::complete);
        }
        Engine upgrade(store, next);
        assert(upgrade.continue_apply(true) == Result::pending_reboot);
        next.state = State::activated;
        assert(store.persist(next));
        assert(upgrade.rollback() == Result::complete && next.state == State::failed);
        for (unsigned n = 0; n < next.count; ++n)
            assert(next.items[n].original_generation == 1);
        first = next;
    }
    return next;
}
static void uninstall_restored_tests() {
    using namespace fake_win32;
    unsigned boundaries = 0;
    for (unsigned failure = 0;; ++failure) {
        Win32Store store;
        Journal restored = restored_upgrade(store), removal;
        const auto reads_before = mutation;
        assert(assess_uninstall(store, restored) == Result::complete && mutation == reads_before);
        assert(make_uninstall_plan(store, restored, removal) == Result::complete);
        mutation = 0;
        fail = failure;
        bool prepared =
            store.prepare_generation(removal, nullptr, true) && store.publish_preparation(removal);
        Result result = Result::io_error;
        if (prepared) {
            Engine remove(store, removal);
            result = remove.continue_apply(false);
        }
        if (!failure) {
            assert(result == Result::complete);
            boundaries = mutation;
        } else if (result != Result::complete) {
            fail = 0;
            Win32Store fresh;
            assert(fresh.init("C:\\WINDOWS\\DreamGPU"));
            Journal current;
            assert(fresh.load(current));
            if (current.generation == restored.generation) {
                assert(make_uninstall_plan(fresh, current, removal) == Result::complete);
                assert(fresh.prepare_generation(removal, nullptr, true) &&
                       fresh.publish_preparation(removal));
            } else
                removal = current;
            if (removal.state == State::removed) {
                for (unsigned n = 0; n < removal.count; ++n)
                    assert(fresh.classify(removal, n, false) == Actual::after);
            } else {
                Engine resume(fresh, removal);
                assert(resume.continue_apply(false) == Result::complete);
            }
        }
        original_files();
        if (failure == boundaries)
            break;
    }
    for (bool repair : {false, true}) {
        Win32Store store;
        Journal restored = restored_upgrade(store, !repair, repair), removal;
        assert(assess_uninstall(store, restored) == Result::complete);
        assert(make_uninstall_plan(store, restored, removal) == Result::complete);
        assert(store.prepare_generation(removal, nullptr, true) &&
               store.publish_preparation(removal));
        Engine remove(store, removal);
        assert(remove.continue_apply(false) == Result::complete);
        original_files();
    }
    {
        Win32Store store;
        Journal restored = restored_upgrade(store), removal;
        auto &log = files.at(canon("C:\\WINDOWS\\DreamGPU\\lifecycle.bin"));
        const auto saved = log.bytes;
        log.bytes.push_back(0x44); // Read-only eligibility must not trim a torn tail.
        unsigned writes = mutation;
        assert(assess_uninstall(store, restored) == Result::complete);
        assert(mutation == writes && log.bytes.size() == saved.size() + 1);
        log.bytes = saved;
        log.bytes[40] ^= 1;
        assert(assess_uninstall(store, restored) == Result::io_error && mutation == writes);
        log.bytes = saved;
        files[canon("C:\\WINDOWS\\SYSTEM\\ddraw.dll")].bytes.push_back(0xaa);
        assert(make_uninstall_plan(store, restored, removal) == Result::conflict &&
               mutation == writes);
    }
    {
        Win32Store store;
        Journal restored = restored_upgrade(store), next;
        restored.items[0].original_index = 31; // Valid schema, unproven inherited backup.
        assert(store.persist(restored));
        const unsigned writes = mutation;
        assert(assess_uninstall(store, restored) == Result::invalid);
        assert(next_system_generation(Os::nt5, payloads, store, restored, next) == Result::invalid);
        assert(mutation == writes);
    }
    {
        initial();
        Win32Store store;
        Journal first, removal;
        assert(prepare(store, first));
        Engine install(store, first);
        assert(install.continue_apply(true) == Result::pending_reboot);
        assert(install.rollback() == Result::complete);
        unsigned writes = mutation;
        assert(make_uninstall_plan(store, first, removal) == Result::invalid && mutation == writes);
        original_files();
    }
    printf("PASS restored upgrade uninstall: %u injected mutations, exact baseline restore, "
           "repeated rollback lineage, repair rollback, read-only history and foreign refusal\n",
           boundaries);
}
// Drive the real Win32 journal/adapter through a complete removal or rollback,
// then restart from the original public namespace with a changed payload.
static void reinstall(bool removed) {
    initial();
    Win32Store store;
    Journal first;
    assert(prepare(store, first));
    Engine install(store, first);
    assert(install.continue_apply(true) == Result::pending_reboot);
    Journal terminal;
    if (removed) {
        first.state = State::activated; // API verification is outside this adapter test.
        assert(store.persist(first) && uninstall_plan(first, terminal));
        assert(store.create_generation(terminal.generation));
        for (unsigned n = 0; n < terminal.count; ++n) {
            const Item retained = terminal.items[n];
            assert(store.capture(terminal, n));
            assert(same(terminal.items[n].before, retained.before));
            terminal.items[n] = retained;
        }
        assert(store.persist(terminal));
        Engine remove(store, terminal);
        assert(remove.continue_apply(false) == Result::complete);
        assert(terminal.state == State::removed);
    } else {
        assert(install.rollback() == Result::complete);
        terminal = first;
        assert(terminal.state == State::failed);
    }
    original_files();
    const auto private_before = fake_win32::files;
    // Foreign changes after acknowledged restoration must reject before any
    // new journal directory, backup, or public mutation.
    const auto public_path = fake_win32::canon("C:\\WINDOWS\\SYSTEM\\ddraw.dll");
    fake_win32::files[public_path].bytes.push_back(0x77);
    unsigned mutations = fake_win32::mutation;
    Journal next;
    assert(next_system_generation(Os::nt5, payloads, store, terminal, next) == Result::conflict);
    assert(fake_win32::mutation == mutations);
    fake_win32::files[public_path] = private_before.at(public_path);
    auto &bytes = fake_win32::resources[payloads[0].id];
    bytes.push_back(0x42);
    payloads[0].size = unsigned(bytes.size());
    native_alias::hash(bytes, payloads[0].sha);
    assert(next_system_generation(Os::nt5, payloads, store, terminal, next) == Result::complete);
    assert(next.generation == terminal.generation + 1 && !next.uninstall);
    for (unsigned n = 0; n < next.count; ++n) {
        assert(next.items[n].original_generation == next.generation);
        assert(next.items[n].original_index == n);
    }
    for (const auto &[path, before] : private_before)
        if (path.find("\\t0000000") != std::string::npos) {
            const auto &after = fake_win32::files.at(path);
            assert(after.bytes == before.bytes && after.attributes == before.attributes);
        }
    Engine again(store, next);
    assert(again.continue_apply(true) == Result::pending_reboot);
    assert(fake_win32::files.at(fake_win32::canon("C:\\WINDOWS\\SYSTEM\\dgpugl.dll")).bytes ==
           bytes);
    assert(again.rollback() == Result::complete);
    original_files();
}
static unsigned preparation_recovery(unsigned mode) {
    auto run = [mode](unsigned failure, bool recover) {
        initial();
        Win32Store store;
        Journal old, next;
        if (mode) {
            if (mode == 3) {
                assert(store.init("C:\\WINDOWS\\DreamGPU"));
                assert(shared_plan(Os::nt5, payloads, old));
                assert(capture_system(Os::nt5, store, old));
            } else {
                assert(prepare(store, old));
                Engine install(store, old);
                assert(install.continue_apply(true) == Result::pending_reboot);
                old.state = State::activated;
                assert(store.persist(old));
            }
        } else
            assert(store.init("C:\\WINDOWS\\DreamGPU"));
        auto prepare_next = [&](Win32Store &target) {
            next = {};
            if (mode == 3)
                return promote_staged_system(Os::nt5, payloads, target, old, next) ==
                       Result::complete;
            if (mode == 1)
                return next_system_generation(Os::nt5, payloads, target, old, next) ==
                       Result::complete;
            if (mode == 2)
                return uninstall_plan(old, next) &&
                       target.prepare_generation(next, nullptr, true) &&
                       target.publish_preparation(next);
            return system_plan(Os::nt5, payloads, next, true) &&
                   capture_system(Os::nt5, target, next);
        };
        fake_win32::mutation = 0;
        fake_win32::fail = failure;
        const bool result = prepare_next(store);
        const unsigned count = fake_win32::mutation;
        assert(result == !failure);
        fake_win32::fail = 0;
        if (recover) {
            Win32Store fresh;
            assert(fresh.init("C:\\WINDOWS\\DreamGPU"));
            if (!prepare_next(fresh)) {
                fprintf(stderr, "preparation retry failed mode=%u mutation=%u\n", mode, failure);
                abort();
            }
            Journal durable;
            assert(fresh.load(durable) && durable.generation == next.generation);
            if (mode)
                for (unsigned n = 0; n < old.count; ++n)
                    assert(fresh.classify(old, n, mode == 3) == Actual::after);
            else
                original_files();
        }
        return count;
    };
    const unsigned count = run(0, false);
    for (unsigned failure = 1; failure <= count; ++failure)
        run(failure, true);
    return count;
}
static void preparation_conflicts() {
    // Directory creation itself does not grant ownership.
    initial();
    fake_win32::files[fake_win32::canon("C:\\WINDOWS\\DreamGPU\\T00000001")].directory = true;
    Win32Store unowned;
    Journal j;
    assert(!prepare(unowned, j));
    original_files();
    for (bool alias : {false, true})
        for (bool foreign : {false, true}) {
            initial();
            Win32Store store;
            assert(store.init("C:\\WINDOWS\\DreamGPU"));
            assert(system_plan(Os::nt5, payloads, j, true));
            assert(store.prepare_generation(j));
            const char *path = alias ? "C:\\WINDOWS\\DreamGPU\\T00000001\\07.payload"
                                     : "C:\\WINDOWS\\DreamGPU\\T00000001\\10.before";
            auto &file = fake_win32::files[fake_win32::canon(path)];
            auto expected = native();
            file.bytes.assign(expected.begin(), expected.begin() + 17);
            if (foreign)
                file.bytes[0] ^= 1;
            Win32Store retry;
            assert(retry.init("C:\\WINDOWS\\DreamGPU"));
            assert(system_plan(Os::nt5, payloads, j, true));
            assert(capture_system(Os::nt5, retry, j) == !foreign);
            original_files();
            if (foreign)
                assert(fake_win32::files.at(fake_win32::canon(path)).bytes[0] == (expected[0] ^ 1));
        }
    for (unsigned tail : {1u, 7u, 8u, unsigned(sizeof(Journal)), unsigned(sizeof(Journal) + 63)})
        for (bool corrupt : {false, true}) {
            initial();
            Win32Store writer;
            assert(prepare(writer, j));
            auto &journal_file =
                fake_win32::files[fake_win32::canon("C:\\WINDOWS\\DreamGPU\\lifecycle.bin")];
            journal_file.bytes.resize(tail);
            if (corrupt)
                journal_file.bytes.back() ^= 1;
            const auto preserved = journal_file.bytes;
            Win32Store retry;
            assert(retry.init("C:\\WINDOWS\\DreamGPU"));
            assert(system_plan(Os::nt5, payloads, j, true));
            assert(capture_system(Os::nt5, retry, j) == !corrupt);
            if (corrupt)
                assert(journal_file.bytes == preserved);
            else {
                Journal loaded;
                assert(retry.load(loaded) && loaded.generation == 1);
            }
        }
    initial();
    Win32Store store;
    assert(store.init("C:\\WINDOWS\\DreamGPU"));
    assert(system_plan(Os::nt5, payloads, j, true) && store.prepare_generation(j));
    payloads[0].sha[0] = payloads[0].sha[0] == '0' ? '1' : '0';
    const unsigned before = fake_win32::mutation;
    assert(system_plan(Os::nt5, payloads, j, true));
    assert(!capture_system(Os::nt5, store, j));
    assert(fake_win32::mutation == before);
    original_files();
}
static void repair_plan_tests() {
    for (unsigned runtime = 0; runtime < 3; ++runtime) {
        initial();
        if (runtime == 2)
            fake_win32::files[fake_win32::canon("C:\\WINDOWS\\SYSTEM\\d3d9.dll")].bytes = native();
        Win32Store store;
        Journal first, next;
        assert(prepare(store, first));
        Engine install(store, first);
        assert(install.continue_apply(true) == Result::pending_reboot);
        first.state = State::activated;
        assert(store.persist(first));
        auto unchanged = assess_repair(store, first);
        assert(unchanged.result == Result::complete && !unchanged.changed && !unchanged.proofs);
        uint32_t proofs = 99;
        const unsigned before = fake_win32::mutation;
        assert(prepare_system_repair(Os::nt5, payloads, store, first, next, proofs) ==
               Result::complete);
        assert(!proofs && next.generation == first.generation && before == fake_win32::mutation);
        const std::string public_path =
            std::string("C:\\WINDOWS\\SYSTEM\\") + public_runtime[runtime];
        fake_win32::files[fake_win32::canon(public_path.c_str())].bytes = native();
        auto assessment = assess_repair(store, first);
        assert(assessment.result == Result::complete && assessment.changed);
        assert(assessment.proofs == (runtime == 0 ? 0x0cu : runtime == 1 ? 0x10u : 0x20u));
        assert(prepare_system_repair(Os::nt5, payloads, store, first, next, proofs) ==
               Result::complete);
        assert(next.generation == 2 && proofs == assessment.proofs);
        for (unsigned n = 0; n < next.count; ++n) {
            assert(same(next.items[n].original, first.items[n].original));
            assert(next.items[n].original_generation == first.items[n].original_generation);
            assert(next.items[n].original_index == first.items[n].original_index);
        }
        // This adapter gate checks file ownership; verification seeding is
        // independently exercised with the actual child/proof receipt below.
        assert(store.publish_preparation(next));
        Engine repair(store, next);
        assert(repair.continue_apply(true) == Result::pending_reboot);
        assert(store.classify(first, 10 + runtime, false) == Actual::after);
        assert(repair.rollback() == Result::complete);
        assert(fake_win32::files.at(fake_win32::canon(public_path.c_str())).bytes == native());
        for (unsigned n = 0; n < first.count; ++n)
            if (n != 10 + runtime)
                assert(store.classify(first, n, false) == Actual::after);
    }
    for (unsigned conflict = 0; conflict < 4; ++conflict) {
        initial();
        Win32Store store;
        Journal first, next;
        assert(prepare(store, first));
        Engine install(store, first);
        assert(install.continue_apply(true) == Result::pending_reboot);
        first.state = State::activated;
        assert(store.persist(first));
        if (conflict == 0)
            fake_win32::files[fake_win32::canon("C:\\WINDOWS\\SYSTEM\\ddraw.dll")].bytes = {'x'};
        if (conflict == 1)
            fake_win32::files.erase(fake_win32::canon("C:\\WINDOWS\\SYSTEM\\dgpugl.dll"));
        if (conflict == 2)
            fake_win32::files.erase(fake_win32::canon("C:\\WINDOWS\\SYSTEM\\d3d9.dll"));
        if (conflict == 3) {
            auto &repaired = fake_win32::files[fake_win32::canon("C:\\WINDOWS\\SYSTEM\\ddraw.dll")];
            repaired.bytes = native();
            repaired.attributes = FILE_ATTRIBUTE_READONLY;
            payloads[0].sha[0] = payloads[0].sha[0] == '0' ? '1' : '0';
        }
        const unsigned before = fake_win32::mutation;
        uint32_t proofs;
        assert(prepare_system_repair(Os::nt5, payloads, store, first, next, proofs) ==
               Result::conflict);
        assert(fake_win32::mutation == before);
    }
    puts(
        "PASS actual repair plan: only present known-original public runtimes admitted; unchanged "
        "no-op, per-route proof masks, baseline/immediate-before retained, unknown drift rejected");
}
int main() {
    uninstall_restored_tests();
    repair_plan_tests();
    const unsigned fresh_faults = preparation_recovery(0);
    const unsigned upgrade_faults = preparation_recovery(1);
    const unsigned removal_faults = preparation_recovery(2);
    const unsigned migration_faults = preparation_recovery(3);
    preparation_conflicts();
    printf("PASS preparation recovery: %u initial, %u upgrade, %u removal, %u legacy staging "
           "migration mutation failures; "
           "owned prefixes resume, foreign bytes/directories and changed intents rejected\n",
           fresh_faults, upgrade_faults, removal_faults, migration_faults);
    reinstall(false);
    reinstall(true);
    unsigned preparation, application, restoration;
    {
        initial();
        Win32Store store;
        Journal j;
        assert(!system_plan(Os::nt5, payloads, j, false));
        assert(prepare(store, j) && j.count == 18 && valid(j));
        preparation = fake_win32::mutation;
        original_files();
        assert(j.items[9].phase == Phase::borrowed && !j.items[9].desired.exists);
        assert(j.items[17].desired.value[0] == 1); // Real loader requires Flags=1.
        for (unsigned n : {7u, 8u}) {
            const auto &node = fake_win32::files.at(
                fake_win32::canon((std::string("C:\\WINDOWS\\DreamGPU\\T00000001\\0") +
                                   std::to_string(n) + ".payload")
                                      .c_str()));
            assert(node.bytes == native() && node.flushed);
        }
        fake_win32::mutation = 0;
        Engine engine(store, j);
        assert(engine.continue_apply(true) == Result::pending_reboot);
        application = fake_win32::mutation;
        assert(j.state != State::activated); // Bytes are not runtime/API verification.
        assert(fake_win32::files.at(fake_win32::canon("C:\\WINDOWS\\SYSTEM\\ddsys.dll")).bytes ==
               native());
        // Upgrade must retain ownership of unchanged aliases. It must derive
        // from the first generation's OS originals, not the installed switcher.
        Journal next;
        assert(system_plan(Os::nt5, payloads, next, true));
        next.generation = 2;
        assert(capture_system(Os::nt5, store, next, &j));
        assert(next.items[7].phase == Phase::prepared);
        assert(same(next.items[7].desired, j.items[7].desired));
        assert(next.items[7].original_generation == 1);
        fake_win32::mutation = 0;
        assert(engine.rollback() == Result::complete);
        restoration = fake_win32::mutation;
        original_files();
        assert(!fake_win32::files.count(fake_win32::canon("C:\\WINDOWS\\SYSTEM\\ddsys.dll")));
    }
    for (unsigned failure = 1; failure <= preparation; ++failure) {
        initial();
        fake_win32::fail = failure;
        Win32Store store;
        Journal j;
        assert(!prepare(store, j));
        original_files();
    }
    for (unsigned failure = 1; failure <= application; ++failure) {
        initial();
        Win32Store store;
        Journal j;
        assert(prepare(store, j));
        fake_win32::mutation = 0;
        fake_win32::fail = failure;
        Engine engine(store, j);
        assert(engine.continue_apply(true) != Result::complete);
        fake_win32::fail = 0;
        Journal recovered;
        assert(store.load(recovered));
        Engine recovery(store, recovered);
        assert(recovery.rollback() == Result::complete);
        original_files();
    }
    for (unsigned failure = 1; failure <= restoration; ++failure) {
        initial();
        Win32Store store;
        Journal j;
        assert(prepare(store, j));
        Engine engine(store, j);
        assert(engine.continue_apply(true) == Result::pending_reboot);
        fake_win32::mutation = 0;
        fake_win32::fail = failure;
        assert(engine.rollback() != Result::complete);
        fake_win32::fail = 0;
        Journal recovered;
        assert(store.load(recovered));
        Engine recovery(store, recovered);
        assert(recovery.rollback() == Result::complete);
        original_files(); // READONLY metadata survives interrupted temp restoration.
    }
    // An unrelated alias must be preserved, never replaced or removed.
    initial();
    fake_win32::files[fake_win32::canon("C:\\WINDOWS\\SYSTEM\\ddsys.dll")].bytes = {'x'};
    Win32Store conflict;
    Journal j;
    assert(!prepare(conflict, j));
    original_files();
    assert(fake_win32::files.at(fake_win32::canon("C:\\WINDOWS\\SYSTEM\\ddsys.dll")).bytes ==
           std::vector<BYTE>{'x'});
    // Foreign registry values/subkeys added after install block removal of the
    // originally absent parent key; Win98 must never recursively delete them.
    for (bool subkey : {false, true}) {
        initial();
        Win32Store store;
        Journal plan;
        assert(prepare(store, plan));
        Engine engine(store, plan);
        assert(engine.continue_apply(true) == Result::pending_reboot);
        if (subkey)
            fake_win32::keys[fake_win32::canon((std::string(nt_icd_key) + "\\Foreign").c_str())];
        else
            fake_win32::keys[fake_win32::canon(nt_icd_key)]["foreign"] = {REG_DWORD, {1, 0, 0, 0}};
        assert(engine.rollback() == Result::io_error);
        assert(fake_win32::keys.count(fake_win32::canon(nt_icd_key)));
    }
    printf("PASS actual system provider plan: 18 items; %u preparation and %u mutation failure "
           "points, %u restoration faults; immutable aliases, ownership upgrade, foreign-key "
           "preservation\n",
           preparation, application, restoration);
}
