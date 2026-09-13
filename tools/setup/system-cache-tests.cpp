// SPDX-License-Identifier: GPL-2.0-or-later
#define DG_SETUP_ADAPTER_TEST
#include "win32-lifecycle.h"
#include "plan.h"
#include <cassert>
using namespace setup;
using namespace setup::lifecycle;
constexpr char Owner[] = "C:\\WINDOWS\\DreamGPU";
constexpr char Cached[] = "C:\\WINDOWS\\SYSBCKUP\\ddraw.dll";
constexpr char Public[] = "C:\\WINDOWS\\SYSTEM\\ddraw.dll";
static void put(const std::string &path, const std::string &bytes) {
    fake_win32::files[fake_win32::canon(path.c_str())].bytes.assign(bytes.begin(), bytes.end());
}
static std::string bytes(const char *path) {
    const auto &b = fake_win32::files.at(fake_win32::canon(path)).bytes;
    return {b.begin(), b.end()};
}
static void initial(Win32Store &store, Journal &j) {
    fake_win32::reset();
    fake_win32::files[fake_win32::canon(Owner)].directory = true;
    fake_win32::files["c:\\windows\\sysbckup"].directory = true;
    assert(store.init(Owner));
    j = {};
    j.generation = 1;
    j.count = 3;
    for (unsigned n = 0; n < 3; ++n) {
        put(std::string("C:\\WINDOWS\\SYSTEM\\") + public_runtime[n], "native");
        const std::string next = "provider";
        fake_win32::resources[100 + n] = {next.begin(), next.end()};
        auto &i = j.items[n];
        strcpy(i.path, public_runtime[n]);
        i.resource = 100 + n;
        i.desired.exists = 1;
        i.desired.size = next.size();
        Sha256 h;
        h.update(reinterpret_cast<const BYTE *>(next.data()), next.size());
        h.finish(i.desired.sha);
    }
}
static void restore(bool uninstall) {
    Win32Store store;
    Journal j;
    initial(store, j);
    put(Cached, "native");
    assert(store.complete_system_plan(Os::win98, j));
    assert(j.count == 4 && !strcmp(j.items[0].path, "SYSBCKUP\\ddraw.dll") &&
           !strcmp(j.items[1].path, "ddraw.dll"));
    assert(store.complete_system_plan(Os::win98, j) && j.count == 4);
    assert(store.prepare_generation(j) && store.publish_preparation(j));
    Engine apply(store, j);
    assert(apply.continue_apply(true) == Result::pending_reboot);
    assert(bytes(Cached) == "provider" && bytes(Public) == "provider");
    if (uninstall) {
        j.state = State::activated;
        Journal remove;
        assert(store.persist(j) && uninstall_plan(j, remove));
        assert(store.prepare_generation(remove, nullptr, true) &&
               store.publish_preparation(remove));
        Engine undo(store, remove);
        assert(undo.continue_apply(false) == Result::complete);
    } else
        assert(apply.rollback() == Result::complete);
    assert(bytes(Cached) == "native" && bytes(Public) == "native");
    assert(!fake_win32::files.count("c:\\windows\\sysbckup\\d3d8.dll"));
    assert(!fake_win32::files.count("c:\\windows\\system\\sysbckup\\ddraw.dll"));
}
static void preflight() {
    Win32Store store;
    Journal j;
    initial(store, j);
    assert(store.complete_system_plan(Os::win98, j) && j.count == 3);
    put(Cached, "higher-version historical Wine");
    unsigned writes = fake_win32::mutation;
    assert(!store.complete_system_plan(Os::win98, j));
    assert(fake_win32::mutation == writes && bytes(Public) == "native");
    assert(store.complete_system_plan(Os::nt5, j) && j.count == 3);
    Item path;
    strcpy(path.path, "SYSBCKUP\\..\\ddraw.dll");
    char out[MAX_PATH];
    assert(!store.public_path(path, out));
    strcpy(path.path, "SYSBCKUP\\kernel32.dll");
    assert(!store.public_path(path, out));
}
static void repair(bool cache, bool public_file) {
    Win32Store store;
    Journal first;
    initial(store, first);
    const Journal input = first;
    put(Cached, "native");
    assert(store.complete_system_plan(Os::win98, first));
    assert(store.prepare_generation(first) && store.publish_preparation(first));
    Engine install(store, first);
    assert(install.continue_apply(true) == Result::pending_reboot);
    first.state = State::activated;
    assert(store.persist(first));
    if (cache)
        put(Cached, "native");
    if (public_file)
        put(Public, "native");
    auto assessment = assess_repair(store, first);
    assert(assessment.result == Result::complete && assessment.proofs == 0xcu);
    assert(assessment.changed == (unsigned(cache) | (unsigned(public_file) << 1)));
    Journal next = input;
    next.generation = 2;
    assert(store.complete_system_plan(Os::win98, next, &first, true));
    assert(store.prepare_generation(next, &first, false, true) && store.publish_preparation(next));
    Engine fix(store, next);
    assert(fix.continue_apply(true) == Result::pending_reboot);
    assert(bytes(Cached) == "provider" && bytes(Public) == "provider");
    next.state = State::activated;
    assert(store.persist(next));
    put(Cached, "foreign bytes");
    assert(assess_repair(store, next).result == Result::conflict);
    put(Cached, "provider");
    Journal remove;
    assert(uninstall_plan(next, remove));
    assert(store.prepare_generation(remove, nullptr, true) && store.publish_preparation(remove));
    Engine undo(store, remove);
    assert(undo.continue_apply(false) == Result::complete);
    assert(bytes(Cached) == "native" && bytes(Public) == "native");
}
int main() {
    preflight();
    restore(false);
    restore(true);
    repair(true, false);
    repair(false, true);
    repair(true, true);
    puts("PASS actual Win98 system-cache plan: incoherent baseline rejected "
         "before writes, "
         "exact cache/public rollback and uninstall, no absent-cache creation or "
         "NT changes");
}
