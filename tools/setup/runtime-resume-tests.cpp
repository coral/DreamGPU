// SPDX-License-Identifier: GPL-2.0-or-later
#define DG_SETUP_ADAPTER_TEST
#include "test-win32.h"
#include <cassert>
static DWORD GetModuleFileNameA(void *, char *out, DWORD capacity) {
    constexpr char self[] = "C:\\setup.exe";
    assert(capacity >= sizeof(self));
    strcpy(out, self);
    return sizeof(self) - 1;
}
#include "runtime-resume.h"
#include "startup-policy.h"
using namespace setup;
using namespace setup::lifecycle;
constexpr char Owner[] = "C:\\WINDOWS\\DreamGPU";
constexpr char Run[] = "Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr char Once[] = "Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce";
static void initial(Win32Store &store) {
    fake_win32::reset();
    fake_win32::files[fake_win32::canon(Owner)].directory = true;
    fake_win32::files["c:\\setup.exe"].bytes = {'e', 'x', 'e'};
    fake_win32::keys[fake_win32::canon(Run)];
    fake_win32::keys[fake_win32::canon(Once)];
    assert(store.init(Owner) && store.create_generation(1));
}
static auto &value(const char *key, const char *name) {
    return fake_win32::keys[fake_win32::canon(key)][fake_win32::canon(name)];
}
static void pending_startup(Os os) {
    Win32Store store;
    initial(store);
    const char *key = Run;
    const char *other = Once;
    value(key, "unrelated") = {REG_SZ, {'u', 0}};
    value(other, "DreamGPU.Runtime") = {REG_SZ, {'f', 0}};
    RuntimeResume resume(store, 1, os);
    assert(resume.prepare() && resume.arm());
    const auto command = value(key, "DreamGPU.Runtime").bytes;
    assert(std::string(command.begin(), command.end() - 1) ==
           "\"C:\\WINDOWS\\DreamGPU\\T00000001\\setup.exe\" /continue /startup");
    // Model three boots with persistent Run retaining exactly the same value.
    for (unsigned boot = 0; boot < 3; ++boot) {

        RuntimeResume child(store, 1, os);
        assert(child.prepare());
        fake_win32::mutation = 0;
        assert(child.arm());
        // Neither OS recreates a startup command from its own callback.
        assert(fake_win32::mutation == 1u);
        RuntimeResume observer(store, 1, os);
        auto writes = fake_win32::mutation;
        assert(observer.armed() && fake_win32::mutation == writes);
        assert(value(key, "DreamGPU.Runtime").bytes == command);
        assert(value(other, "DreamGPU.Runtime").bytes == std::vector<BYTE>({'f', 0}));
    }
    value(key, "DreamGPU.Runtime") = {REG_SZ, {'x', 0}};
    assert(!resume.arm() && !resume.finish());
    value(key, "DreamGPU.Runtime") = {REG_SZ, command};
    assert(resume.finish() && resume.finish());
    assert(!fake_win32::keys[fake_win32::canon(key)].count("dreamgpu.runtime"));
    assert(value(key, "unrelated").bytes == std::vector<BYTE>({'u', 0}));
}
static void prior_and_legacy() {
    Win32Store store;
    initial(store);
    value(Run, "DreamGPU.Runtime") = {REG_DWORD, {7, 0, 9, 0}};
    RuntimeResume resume(store, 1, Os::win98);
    assert(resume.prepare() && resume.arm() && resume.finish());
    assert(value(Run, "DreamGPU.Runtime").type == REG_DWORD);
    assert(value(Run, "DreamGPU.Runtime").bytes == std::vector<BYTE>({7, 0, 9, 0}));
    initial(store);
    // Build the actual historical V1 receipt, preserving its RunOnce baseline.
    struct Legacy {
        uint32_t magic = 0x52474744, version = 1, generation = 1;
        Image before{}, installer{};
    } record;
    value(Once, "DreamGPU.Runtime") = {REG_SZ, {'o', 0}};
    record.before = string_value("o");
    assert(store.inspect_file("C:\\setup.exe", record.installer));
    char path[MAX_PATH], program[MAX_PATH];
    assert(store.private_path(path, "\\T00000001\\RESUME.JRN") &&
           store.private_path(program, "\\T00000001\\setup.exe") &&
           store.retain_program("C:\\setup.exe", program, record.installer));
    DurableRecord<Legacy> disk(path);
    Legacy previous;
    bool exists;
    const auto valid = [](const Legacy &) { return true; };
    assert(disk.load(previous, exists, valid) && !exists && disk.save(record, valid));
    RuntimeResume legacy(store, 1, Os::nt5);
    assert(legacy.prepare());
    const auto files = fake_win32::files;
    const auto once = value(Once, "DreamGPU.Runtime").bytes;
    assert(!legacy.arm()); // New code never recreates a consumed RunOnce callback.
    RuntimeResume incompatible(store, 1, Os::win98);
    assert(!incompatible.prepare());
    assert(value(Once, "DreamGPU.Runtime").bytes == once);
    assert(!fake_win32::keys[fake_win32::canon(Run)].count("dreamgpu.runtime"));
    for (const auto &[name, file] : files)
        assert(fake_win32::files.at(name).bytes == file.bytes);
    assert(legacy.finish());
    record.version = 3;
    assert(disk.save(record, valid));
    RuntimeResume mixed(store, 1, Os::nt5);
    assert(!mixed.prepare() && !mixed.armed());
}
static void historical_silent(Os os) {
    Win32Store store;
    initial(store);
    struct Legacy {
        uint32_t magic = 0x52474744, version, generation = 1;
        Image before{}, installer{};
    } record{0x52474744, os == Os::win98 ? 2u : 3u};
    record.before = string_value("previous");
    assert(store.inspect_file("C:\\setup.exe", record.installer));
    char path[MAX_PATH], program[MAX_PATH];
    assert(store.private_path(path, "\\T00000001\\RESUME.JRN") &&
           store.private_path(program, "\\T00000001\\setup.exe") &&
           store.retain_program("C:\\setup.exe", program, record.installer));
    DurableRecord<Legacy> disk(path);
    Legacy loaded{};
    bool exists;
    const auto valid = [](const Legacy &) { return true; };
    assert(disk.load(loaded, exists, valid) && !exists && disk.save(record, valid));
    const std::string command = std::string("\"") + program + "\" /continue /silent";
    std::vector<BYTE> bytes(command.begin(), command.end());
    bytes.push_back(0);
    value(Run, "DreamGPU.Runtime") = {REG_SZ, bytes};
    RuntimeResume resume(store, 1, os);
    assert(resume.armed() && resume.prepare() && resume.arm());
    assert(value(Run, "DreamGPU.Runtime").bytes == bytes);
    RuntimeResume incompatible(store, 1, os == Os::win98 ? Os::nt5 : Os::win98);
    assert(!incompatible.prepare() && !incompatible.armed());
    assert(resume.finish());
    assert(value(Run, "DreamGPU.Runtime").bytes ==
           std::vector<BYTE>({'p', 'r', 'e', 'v', 'i', 'o', 'u', 's', 0}));
}
static void startup_notifications() {
    // The driver reboot can be followed by provider staging and a second reboot.
    for (unsigned pending : {16u, 11u})
        assert(notify_result(false, true, pending));
    for (unsigned failure : {20u, 22u, 23u, 24u, 26u, 27u, 29u, 31u, 32u})
        assert(notify_result(false, true, failure));
    assert(notify_result(false, true, 0));
    for (unsigned quiet : {10u, 12u, 13u, 14u, 15u, 17u, 28u})
        assert(!notify_result(false, true, quiet));
    for (unsigned result = 0; result <= 32; ++result) {
        assert(!notify_result(true, true, result));
        assert(!notify_result(true, false, result));
        assert(notify_result(false, false, result));
    }
    assert(valid_startup(true, false, 0));
    assert(!valid_startup(true, true, 0));
    for (int action = -1; action <= 8; ++action) {
        assert(valid_startup(false, false, action));
        if (action != 0)
            assert(!valid_startup(true, false, action));
    }
}
int main() {
    pending_startup(Os::win98);
    pending_startup(Os::nt5);
    prior_and_legacy();
    historical_silent(Os::win98);
    historical_silent(Os::nt5);
    startup_notifications();
    puts("PASS actual startup adapter: Win98 persistent next-boot registration, NT persistent Run, "
         "startup notification policy, historical silent ownership, prior/foreign values and "
         "legacy receipt refusal");
}
