// SPDX-License-Identifier: GPL-2.0-or-later
#define DG_SETUP_ADAPTER_TEST
#include "driver-scm-test-win32.h"
#define main BoundLegacyMain
#include "driver-tests.cpp"
#undef main
#include "driver-actions.h"
#include "driver-runtime.h"
static Journal disk_journal(const std::string &where) {
    const auto &bytes =
        fake_win32::files.at(fake_win32::canon((where + "\\driver.bin").c_str())).bytes;
    assert(bytes.size() >= sizeof(Journal) + 64);
    Journal j;
    memcpy(&j, bytes.data() + bytes.size() - sizeof(Journal) - 64, sizeof(j));
    return j;
}
static void simulator() {
    driver_fake::execute = [](const std::string &exe) {
        if (exe.find("DGDBIND.EXE") == std::string::npos)
            return;
        bool rollback = exe.find("driver-backup") != std::string::npos;
        auto where = exe.substr(0, exe.find(rollback ? "\\driver-backup" : "\\drivers"));
        auto j = disk_journal(where);
        for (unsigned n = 0; n < j.count; ++n) {
            const auto &f = j.files[n];
            if (!f.replaced)
                continue;
            auto source = rollback ? where + f.backup : j.desired.inf;
            if (!rollback)
                source =
                    source.substr(0, source.find_last_of('\\') + 1) + (strrchr(f.path, '\\') + 1);
            fake_win32::files[fake_win32::canon(f.path)] =
                fake_win32::files.at(fake_win32::canon(source.c_str()));
        }
        if (rollback) {
            driver_fake::set_node(j.original);
            devices[0].inf = strrchr(j.original.inf, '\\') + 1;
        } else {
            auto installed = j.desired;
            std::string inf = std::string("C:\\WINDOWS\\INF\\oem") +
                              std::to_string(driver_fake::binds + driver_fake::launches) + ".inf";
            strcpy(installed.inf, inf.c_str());
            fake_win32::files[fake_win32::canon(installed.inf)] =
                fake_win32::files.at(fake_win32::canon(j.desired.inf));
            driver_fake::set_node(installed);
            devices[0].bound = true;
            devices[0].inf = strrchr(installed.inf, '\\') + 1;
            exists_service = true;
            fake_win32::keys[ServiceKey] = {};
            // SetupAPI discovers prior published packages, but preserves the
            // explicit single-INF candidate records used by DriverList.
            driver_fake::compatible.push_back(installed);
        }
    };
}
static void next_payload(unsigned id, const char *value) {
    file("C:\\dreamgpu.exe", value);
    char next_root[MAX_PATH];
    assert(generation_root(root, id, next_root));
    for (auto &p : payloads) {
        std::string bytes = std::string(value) + p.path;
        fake_win32::resources[p.id] = {bytes.begin(), bytes.end()};
        strings.push_back(digest(fake_win32::resources[p.id]));
        p.sha = strings.back().c_str();
        p.size = unsigned(bytes.size());
    }
    Node desired = driver_fake::current;
    const auto &p = payloads[0];
    std::string inf = std::string(next_root) + "\\drivers\\nt5\\dreamgpu.inf";
    strcpy(desired.inf, inf.c_str());
    strcpy(desired.inf_sha, p.sha);
    driver_fake::compatible.push_back(desired);
}
static void nt_flow(bool remove_after_rollback) {
    setup_case(Os::nt5, false);
    auto absent = Node{};
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
    simulator();
    assert(act_at(Os::nt5, 0, root, payloads) == Result::pending_reboot);
    assert(act_at(Os::nt5, 1, root, payloads) == Result::verified);
    GenerationInfo initial;
    assert(query_generation_at(Os::nt5, root, initial) == GenerationRead::present &&
           initial.id == 1);
    const auto original = fake_win32::files.at("c:\\windows\\system\\dgpudisp.dll").bytes;
    next_payload(2, "second installer");
    assert(act_at(Os::nt5, 3, root, payloads) == Result::pending_reboot);
    assert(act_at(Os::nt5, 1, root, payloads) == Result::verified);
    GenerationInfo upgraded;
    assert(query_generation_at(Os::nt5, root, upgraded) == GenerationRead::present &&
           upgraded.id == 2 && upgraded.parent == 1);
    assert(verify_generation_at(Os::nt5, root, upgraded, false));
    file("C:\\WINDOWS\\DreamGPU\\R00000003\\setup.exe", "corrected reverse executor");
    const auto recovery_bytes =
        fake_win32::files.at("c:\\windows\\dreamgpu\\r00000003\\setup.exe").bytes;
    auto recovery_sha = digest(recovery_bytes);
    ReverseExecutor recovery{"C:\\WINDOWS\\DreamGPU\\R00000003\\setup.exe", recovery_sha.c_str()};
    assert(act_at(Os::nt5, 1, root, payloads, &recovery) == Result::invalid); // no forward takeover
    assert(act_at(Os::nt5, 2, root, payloads) == Result::pending_reboot);
    const auto run_key = fake_win32::canon("Software\\Microsoft\\Windows\\CurrentVersion\\Run");
    const auto old_startup = fake_win32::keys.at(run_key).at("dreamgpu.driver");
    fake_win32::keys.at(run_key)["dreamgpu.driver"] = {REG_SZ,
                                                       {'f', 'o', 'r', 'e', 'i', 'g', 'n', 0}};
    assert(!retire_reverse_resume(Os::nt5));
    assert(fake_win32::keys.at(run_key).at("dreamgpu.driver").bytes ==
           std::vector<BYTE>({'f', 'o', 'r', 'e', 'i', 'g', 'n', 0}));
    fake_win32::keys.at(run_key)["dreamgpu.driver"] = old_startup;
    const auto audit_path = "c:\\windows\\dreamgpu\\d00000002\\tools\\nt5\\dgaudit.exe";
    const auto audit = fake_win32::files.at(audit_path).bytes;
    file(audit_path, "foreign helper");
    assert(!reverse_inputs_ready(Os::nt5));
    fake_win32::files.at(audit_path).bytes = audit;
    assert(reverse_inputs_ready(Os::nt5));
    assert(retire_reverse_resume(Os::nt5));
    const auto immutable =
        fake_win32::files.at("c:\\windows\\dreamgpu\\d00000002\\setup.exe").bytes;
    file(recovery.path, "foreign executor");
    assert(act_at(Os::nt5, 1, root, payloads, &recovery) == Result::invalid);
    fake_win32::files.at(fake_win32::canon(recovery.path)).bytes = recovery_bytes;
    assert(act_at(Os::nt5, 1, root, payloads, &recovery) == Result::restored);
    assert(fake_win32::files.at("c:\\windows\\dreamgpu\\d00000002\\setup.exe").bytes == immutable);
    assert(fake_win32::files.at("c:\\windows\\system\\dgpudisp.dll").bytes == original);
    if (remove_after_rollback) {
        auto removed = act_at(Os::nt5, 4, root, payloads);
        for (unsigned n = 0; n < 5 && removed == Result::pending_reboot; ++n) {
            if (marked) {
                exists_service = marked = false;
                fake_win32::keys.erase(ServiceKey);
            }
            removed = act_at(Os::nt5, 1, root, payloads);
        }
        assert(removed == Result::restored && !exists_service && removes == 1 && deletes == 1);
        assert(!fake_win32::files.contains("c:\\windows\\system\\dgpudisp.dll"));
        GenerationInfo baseline;
        assert(query_generation_at(Os::nt5, root, baseline) == GenerationRead::present &&
               baseline.id == 2 && baseline.baseline == 1 &&
               verify_generation_at(Os::nt5, root, baseline, true));
        return;
    }
    driver_fake::compatible.erase(
        std::remove_if(
            driver_fake::compatible.begin(), driver_fake::compatible.end(),
            [](const Node &n) { return GetFileAttributesA(n.inf) == INVALID_FILE_ATTRIBUTES; }),
        driver_fake::compatible.end());
    next_payload(3, "third installer");
    assert(act_at(Os::nt5, 3, root, payloads) == Result::pending_reboot);
    assert(act_at(Os::nt5, 1, root, payloads) == Result::verified);
    auto removed = act_at(Os::nt5, 4, root, payloads);
    for (unsigned n = 0; n < 5 && removed == Result::pending_reboot; ++n) {
        if (marked) {
            exists_service = marked = false;
            fake_win32::keys.erase(ServiceKey);
        }
        removed = act_at(Os::nt5, 1, root, payloads);
    }
    assert(removed == Result::restored);
    assert(!fake_win32::files.contains("c:\\windows\\system\\dgpudisp.dll"));
    assert(!exists_service && removes == 1 && deletes == 1);
    GenerationInfo baseline;
    assert(query_generation_at(Os::nt5, root, baseline) == GenerationRead::present &&
           baseline.id == 3 && baseline.baseline == 1);
    assert(verify_generation_at(Os::nt5, root, baseline, true));
    puts("PASS actual NT driver generations: unbound baseline, upgrade OEM publication, prior "
         "rollback, owned package cleanup, full service/unbound uninstall");
}

int main() {
    nt_flow(false);
    nt_flow(true);
}
