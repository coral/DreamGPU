// SPDX-License-Identifier: GPL-2.0-or-later
#define main ExistingGlobalTests
#include "global-runtime-tests.cpp"
#undef main
constexpr char NtRun[] = "Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce";
static Result nt(Request request) {
    Intent intent{};
    auto result = act(Os::nt5, request, 0, intent);
    assert(fake_win32::handles.empty() && fake_win32::key_handles.empty());
    return result;
}
static void setup_recovery() {
    initial();
    fake_win32::keys[fake_win32::canon(NtRun)] = fake_win32::keys[fake_win32::canon(RunKey)];
    assert(nt(Request::start) == Result::complete);
    NativeComponents::reverse_pause = true;
    assert(nt(Request::rollback) == Result::pending_reboot);
    assert(global_record().flow.phase == Phase::restore_driver);
    executing = "C:\\corrected.exe";
    put(executing, "corrected-executor");
    NativeComponents::reverse_pause = false;
    fake_win32::mutation = 0;
}
static void restored() {
    auto r = global_record();
    assert(r.flow.phase == Phase::restored && lifecycle::same(r.installer, image("installer-one")));
    assert(fake_win32::files
               .at(fake_win32::canon((std::string(Owner) + "\\G00000002\\setup.exe").c_str()))
               .bytes ==
           std::vector<BYTE>({'i', 'n', 's', 't', 'a', 'l', 'l', 'e', 'r', '-', 'o', 'n', 'e'}));
    assert(fake_win32::keys.at(fake_win32::canon(NtRun)).at("dreamgpu.setup").bytes ==
           std::vector<BYTE>(Prior, Prior + sizeof(Prior)));
    assert(!fake_win32::keys.at(fake_win32::canon(NtRun)).contains("dreamgpu.recovery"));
}
int main() {
    setup_recovery();
    assert(nt(Request::resume) == Result::conflict);
    assert(nt(Request::recover) == Result::complete);
    auto total = fake_win32::mutation;
    restored();
    const auto completed_mutations = fake_win32::mutation;
    assert(nt(Request::resume) == Result::complete);
    assert(fake_win32::mutation ==
           completed_mutations + 1); // Only the existing RegFlushKey, no re-arm writes.
    for (unsigned n = 1; n <= total; ++n) {
        setup_recovery();
        fake_win32::fail = n;
        auto result = nt(Request::recover);
        fake_win32::fail = 0;
        if (result != Result::complete)
            assert(nt(Request::recover) == Result::complete);
        restored();
    }
    setup_recovery();
    put(target(public_runtime[0]), "foreign-runtime");
    assert(nt(Request::recover) == Result::conflict && fake_win32::mutation == 0);
    setup_recovery();
    fake_win32::files[fake_win32::canon((std::string(Owner) + "\\R00000002").c_str())].directory =
        true;
    assert(nt(Request::recover) == Result::conflict);
    setup_recovery();
    put(std::string(Owner) + "\\G00000002\\setup.exe", "foreign-old-executor");
    assert(nt(Request::recover) == Result::conflict);
    setup_recovery();
    NativeComponents::reverse_pause = true;
    assert(nt(Request::recover) == Result::pending_reboot);
    put(std::string(Owner) + "\\R00000002\\setup.exe", "foreign-new-executor");
    assert(nt(Request::resume) == Result::conflict);
    setup_recovery();
    NativeComponents::reverse_pause = true;
    assert(nt(Request::recover) == Result::pending_reboot);
    char journal[MAX_PATH];
    assert(journal_path(journal));
    DurableRecord<Record> disk(journal);
    Record altered;
    bool exists;
    assert(disk.load(altered, exists, valid_record) && exists);
    altered.origin_provider_state = lifecycle::State::activated;
    assert(disk.save(altered, valid_record));
    assert(nt(Request::resume) == Result::conflict);
    printf("PASS explicit reverse executor recovery: %u durable mutation interruptions, exact "
           "origin/program/provider guards\n",
           total);
}
