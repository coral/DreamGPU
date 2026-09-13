// SPDX-License-Identifier: GPL-2.0-or-later
#define DG_SETUP_ADAPTER_TEST
#include "driver-scm-test-win32.h"
#define main BoundLegacyMain
#include "driver-tests.cpp"
#undef main
#include "driver-generation.h"

static void first_install(bool interrupted, bool lost_remove = false, bool stale_metadata = false,
                          bool never_bound = false) {
    setup_case(Os::nt5, false);
    Node absent{};
    strcpy(absent.device, driver_fake::current.device);
    devices = {{absent.device, "", false, true}};
    driver_fake::set_node(absent);
    exists_service = marked = malformed_config = false;
    marked_openable = true;
    removes = deletes = 0;
    pending_remove = lost_response = false;
    service_state = SERVICE_RUNNING;
    service_start = SERVICE_DEMAND_START;
    binary = "system32\\DRIVERS\\dgpumini.sys";
    Win32Store store;
    Journal j{};
    assert(store.init(Os::nt5, root));
    GenerationInfo absent_info;
    assert(query_generation_at(Os::nt5, root, absent_info) == GenerationRead::absent);
    assert(store.capture(j, payloads));
    assert(j.original_binding == Binding::unbound);
    if (never_bound) {
        Engine abort(store, j);
        assert(abort.rollback() == Result::restored);
        GenerationInfo current;
        auto writes = fake_win32::mutation;
        assert(query_generation_at(Os::nt5, root, current) == GenerationRead::present);
        assert(verify_generation_at(Os::nt5, root, current, true));
        assert(fake_win32::mutation == writes && removes == 0 && deletes == 0);
        return;
    }
    const auto oem_baseline = fake_win32::files["c:\\windows\\dreamgpu\\unbound.bin"];
    const auto service_baseline = fake_win32::files["c:\\windows\\dreamgpu\\service.bin"];
    execution(j);
    auto execute = driver_fake::execute;
    driver_fake::execute = [&, execute](const std::string &exe) {
        execute(exe);
        if (exe.find("DGDBIND.EXE") == std::string::npos)
            return;
        auto installed = j.desired;
        strcpy(installed.inf, "C:\\WINDOWS\\INF\\OEM1.INF");
        fake_win32::files[fake_win32::canon(installed.inf)] =
            fake_win32::files[fake_win32::canon(j.desired.inf)];
        file("C:\\WINDOWS\\INF\\OEM1.PNF", "published pnf");
        devices[0].bound = true;
        devices[0].inf = "oem1.inf";
        driver_fake::set_node(installed);
        exists_service = true;
        fake_win32::keys[ServiceKey] = {};
    };
    Engine engine(store, j);
    assert(engine.install() == Result::pending_reboot);
    if (interrupted) {
        // Model a process loss after SetupAPI publication but before observing
        // and persisting ownership of the newly published OEM package/service.
        fake_win32::files["c:\\windows\\dreamgpu\\unbound.bin"] = oem_baseline;
        fake_win32::files["c:\\windows\\dreamgpu\\service.bin"] = service_baseline;
        j.phase = Phase::binding;
        assert(store.persist(j));
    }
    Win32Store boot;
    Journal cold{};
    assert(boot.init(Os::nt5, root));
    assert(boot.load(cold));
    Engine after_boot(boot, cold);
    auto resumed = after_boot.resume();
    if (interrupted) {
        assert(resumed == Result::pending_reboot);
        resumed = after_boot.resume();
    }
    assert(resumed == Result::verified);
    GenerationInfo installed_info;
    auto writes = fake_win32::mutation;
    assert(query_generation_at(Os::nt5, root, installed_info) == GenerationRead::present);
    assert(installed_info.id == 1 && installed_info.baseline == 1 && installed_info.parent == 0);
    assert(verify_generation_at(Os::nt5, root, installed_info, false));
    assert(fake_win32::mutation == writes);
    auto wrong_info = installed_info;
    ++wrong_info.id;
    assert(!verify_generation_at(Os::nt5, root, wrong_info, false));
    lost_response = lost_remove;
    pending_remove = stale_metadata;
    auto rollback = after_boot.rollback();
    if (lost_remove) {
        assert(rollback == Result::io_error && removes == 1 && deletes == 0);
        Win32Store recovered;
        Journal recovery{};
        assert(recovered.init(Os::nt5, root) && recovered.load(recovery));
        Engine retry(recovered, recovery);
        assert(retry.resume() == Result::pending_reboot);
    } else {
        assert(rollback == Result::pending_reboot);
    }
    assert(removes == 1 && deletes == 1 && marked);
    exists_service = false;
    pending_remove = false;
    devices[0].bound = false; // Reenumeration after the kernel unloads at cold boot.
    fake_win32::keys.erase(ServiceKey);
    driver_fake::started = false;
    Win32Store restored;
    Journal final{};
    assert(restored.init(Os::nt5, root));
    assert(restored.load(final));
    Engine after_remove(restored, final);
    assert(after_remove.resume() == Result::restored);
    assert(removes == 1 && deletes == 1);
    writes = fake_win32::mutation;
    GenerationInfo restored_info;
    assert(query_generation_at(Os::nt5, root, restored_info) == GenerationRead::present);
    assert(restored_info.phase == Phase::restored);
    assert(verify_generation_at(Os::nt5, root, restored_info, true));
    assert(!verify_generation_at(Os::nt5, root, installed_info, false));
    assert(fake_win32::mutation == writes);
    auto &journal_bytes = fake_win32::files["c:\\windows\\dreamgpu\\driver.bin"].bytes;
    auto saved_journal = journal_bytes;
    journal_bytes.push_back(0x7f);
    assert(query_generation_at(Os::nt5, root, restored_info) == GenerationRead::present);
    assert(journal_bytes.size() ==
           saved_journal.size() + 1); // Read-only never trims a torn append.
    journal_bytes = saved_journal;
    journal_bytes[0] ^= 1;
    assert(query_generation_at(Os::nt5, root, restored_info) == GenerationRead::error);
    assert(fake_win32::mutation == writes);
    journal_bytes = saved_journal;
    assert(!fake_win32::files.count("c:\\windows\\inf\\oem1.inf"));
    assert(!fake_win32::files.count("c:\\windows\\inf\\oem1.pnf"));
    assert(!fake_win32::files.count("c:\\windows\\system\\dgpudisp.dll"));
    assert(!fake_win32::files.count("c:\\windows\\system\\drivers\\dgpumini.sys"));
    assert(digest(fake_win32::files["c:\\windows\\system\\vga.dll"].bytes) ==
           digest(std::vector<BYTE>{'m', 'i', 'c', 'r', 'o', 's', 'o', 'f', 't', ' ', 'd', 'i', 's',
                                    'p', 'l', 'a', 'y'}));
}
int main() {
    first_install(false);
    first_install(true);
    first_install(false, true);
    first_install(false, false, true);
    first_install(false, false, false, true);
    puts("PASS actual first-install adapter: unbound install, interrupted publication recovery, "
         "cold verification, OEM/service removal, cold unbound restoration");
}
