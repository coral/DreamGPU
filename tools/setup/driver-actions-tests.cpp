// SPDX-License-Identifier: GPL-2.0-or-later
#define DG_SETUP_ADAPTER_TEST
#define main BoundLegacyMain
#include "driver-tests.cpp"
#undef main
#include "driver-actions.h"
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
        if (rollback)
            driver_fake::set_node(j.original);
        else {
            auto installed = j.desired;
            std::string inf = std::string("C:\\WINDOWS\\INF\\installed") +
                              std::to_string(driver_fake::binds + driver_fake::launches) + ".inf";
            strcpy(installed.inf, inf.c_str());
            fake_win32::files[fake_win32::canon(installed.inf)] =
                fake_win32::files.at(fake_win32::canon(j.desired.inf));
            driver_fake::set_node(installed);
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
    std::string inf = std::string(next_root) + "\\drivers\\win98\\dg9x.inf";
    strcpy(desired.inf, inf.c_str());
    strcpy(desired.inf_sha, p.sha);
    driver_fake::compatible.push_back(desired);
}
static unsigned upgrade_failure(unsigned failure) {
    setup_case(Os::win98, true);
    simulator();
    assert(act_at(Os::win98, 0, root, payloads) == Result::pending_reboot);
    assert(act_at(Os::win98, 1, root, payloads) == Result::verified);
    next_payload(2, "upgrade failure injection");
    fake_win32::fail = failure;
    fake_win32::mutation = 0;
    auto result = act_at(Os::win98, 3, root, payloads);
    unsigned calls = fake_win32::mutation;
    fake_win32::fail = 0;
    GenerationInfo current;
    assert(query_generation_at(Os::win98, root, current) == GenerationRead::present);
    if (current.id == 1) {
        assert(verify_generation_at(Os::win98, root, current, false));
        result = act_at(Os::win98, 3, root, payloads);
    } else
        result = act_at(Os::win98, 1, root, payloads);
    if (result == Result::pending_reboot)
        result = act_at(Os::win98, 1, root, payloads);
    if (result != Result::verified) {
        fprintf(stderr, "failed driver upgrade recovery mutation=%u result=%u\n", failure,
                unsigned(result));
        for (const auto &[name, file] : fake_win32::files)
            if (name.ends_with("driver-status.log"))
                fprintf(stderr, "%s: %.*s\n", name.c_str(), int(file.bytes.size()),
                        file.bytes.data());
    }
    assert(result == Result::verified);
    return calls;
}
static void abandon_unpublished() {
    setup_case(Os::win98, true);
    simulator();
    assert(act_at(Os::win98, 0, root, payloads) == Result::pending_reboot);
    assert(act_at(Os::win98, 1, root, payloads) == Result::verified);
    next_payload(2, "unpublished corrupt resource");
    fake_win32::resources[payloads[0].id][0] ^= 1;
    assert(act_at(Os::win98, 3, root, payloads) == Result::io_error);
    GenerationInfo before;
    assert(query_generation_at(Os::win98, root, before) == GenerationRead::present &&
           before.id == 1);
    assert(verify_generation_at(Os::win98, root, before, false));
    next_payload(3, "replacement request");
    assert(act_at(Os::win98, 3, root, payloads) == Result::pending_reboot);
    assert(act_at(Os::win98, 1, root, payloads) == Result::verified);
    GenerationInfo current;
    assert(query_generation_at(Os::win98, root, current) == GenerationRead::present &&
           current.id == 3 && current.parent == 1 && current.baseline == 1);
    Lineage l;
    bool exists;
    assert(load_lineage(root, Os::win98, l, exists, true) && exists &&
           l.generations[1].state == GenerationState::abandoned);
    auto result = act_at(Os::win98, 4, root, payloads);
    for (unsigned n = 0; n < 4 && result == Result::pending_reboot; ++n)
        result = act_at(Os::win98, 1, root, payloads);
    assert(result == Result::restored); // Never attempts to restore abandoned id2.
}
int main() {
    abandon_unpublished();
    const unsigned failures = upgrade_failure(0);
    for (unsigned n = 1; n <= failures; ++n)
        upgrade_failure(n);
    setup_case(Os::win98, true);
    simulator();
    assert(act_at(Os::win98, 0, root, payloads) == Result::pending_reboot);
    assert(act_at(Os::win98, 1, root, payloads) == Result::verified);
    GenerationInfo initial;
    assert(query_generation_at(Os::win98, root, initial) == GenerationRead::present &&
           initial.id == 1);
    const auto original = fake_win32::files.at("c:\\windows\\system\\dgpumini.drv").bytes;
    next_payload(2, "second installer");
    assert(act_at(Os::win98, 3, root, payloads) == Result::pending_reboot);
    assert(act_at(Os::win98, 1, root, payloads) == Result::verified);
    GenerationInfo upgraded;
    assert(query_generation_at(Os::win98, root, upgraded) == GenerationRead::present &&
           upgraded.id == 2 && upgraded.parent == 1);
    assert(verify_generation_at(Os::win98, root, upgraded, false));
    assert(act_at(Os::win98, 2, root, payloads) == Result::pending_reboot);
    assert(act_at(Os::win98, 1, root, payloads) == Result::restored);
    assert(fake_win32::files.at("c:\\windows\\system\\dgpumini.drv").bytes == original);
    next_payload(3, "third installer");
    assert(act_at(Os::win98, 3, root, payloads) == Result::pending_reboot);
    assert(act_at(Os::win98, 1, root, payloads) == Result::verified);
    auto removed = act_at(Os::win98, 4, root, payloads);
    for (unsigned n = 0; n < 5 && removed == Result::pending_reboot; ++n)
        removed = act_at(Os::win98, 1, root, payloads);
    assert(removed == Result::restored);
    const auto &bytes = fake_win32::files.at("c:\\windows\\system\\dgpumini.drv").bytes;
    assert(std::string(bytes.begin(), bytes.end()) == "old display");
    GenerationInfo baseline;
    assert(query_generation_at(Os::win98, root, baseline) == GenerationRead::present &&
           baseline.id == 3 && baseline.baseline == 1);
    assert(verify_generation_at(Os::win98, root, baseline, true));
    next_payload(4, "third installer"); // Same installer after full uninstall, new cycle.
    assert(act_at(Os::win98, 3, root, payloads) == Result::pending_reboot);
    assert(act_at(Os::win98, 1, root, payloads) == Result::verified);
    GenerationInfo cycle;
    assert(query_generation_at(Os::win98, root, cycle) == GenerationRead::present &&
           cycle.id == 4 && cycle.parent == 3 && cycle.baseline == 4);
    printf("Upgrade mutation recovery: %u failure points\n", failures);
    puts("PASS actual driver generations: changed payload, cold continuation, prior rollback, "
         "multi-generation baseline uninstall, fresh cycle");
}
