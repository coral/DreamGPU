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
    const char *key = os == Os::win98 ? Run : Once;
    const char *other = os == Os::win98 ? Once : Run;
    value(key, "unrelated") = {REG_SZ, {'u', 0}};
    value(other, "DreamGPU.Runtime") = {REG_SZ, {'f', 0}};
    RuntimeResume resume(store, 1, os);
    assert(resume.prepare() && resume.arm());
    const auto command = value(key, "DreamGPU.Runtime").bytes;
    // Model three boots with the real registration policy. RunOnce consumes
    // the value before calling the child; persistent Run keeps the same value.
    for (unsigned boot = 0; boot < 3; ++boot) {
        if (os == Os::nt5)
            fake_win32::keys[fake_win32::canon(key)].erase("dreamgpu.runtime");
        RuntimeResume child(store, 1, os);
        assert(child.prepare());
        fake_win32::mutation = 0;
        assert(child.arm());
        // Win98 performs only a flush, never RegSetValueEx from its own Run
        // callback. NT performs one Set and one flush after consumption.
        assert(fake_win32::mutation == (os == Os::win98 ? 1u : 2u));
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
    // V1 has the exact legacy Win98 layout as well as the NT layout. It must
    // never be reinterpreted as a captured baseline from a different key.
    RuntimeResume legacy(store, 1, Os::nt5);
    assert(legacy.prepare() && legacy.arm());
    const auto files = fake_win32::files;
    const auto once = value(Once, "DreamGPU.Runtime").bytes;
    RuntimeResume incompatible(store, 1, Os::win98);
    assert(!incompatible.prepare());
    assert(value(Once, "DreamGPU.Runtime").bytes == once);
    assert(!fake_win32::keys[fake_win32::canon(Run)].count("dreamgpu.runtime"));
    for (const auto &[name, file] : files)
        assert(fake_win32::files.at(name).bytes == file.bytes);
    assert(legacy.finish());
}
int main() {
    pending_startup(Os::win98);
    pending_startup(Os::nt5);
    prior_and_legacy();
    puts("PASS actual startup adapter: Win98 persistent next-boot registration, NT RunOnce, "
         "prior/foreign values and legacy receipt refusal");
}
