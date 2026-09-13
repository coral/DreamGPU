// SPDX-License-Identifier: GPL-2.0-or-later
// Exercise the production action gateway and Win32Store with syscall seams.
#define DG_SETUP_ADAPTER_TEST
#define DG_SETUP_UNREADY_PROVIDER_TEST
#include "test-win32.h"
inline DWORD GetWindowsDirectoryA(char *out, DWORD n) {
    constexpr char path[] = "C:\\WINDOWS";
    if (n < sizeof(path))
        return sizeof(path);
    strcpy(out, path);
    return sizeof(path) - 1;
}
#include "lifecycle-runtime.h"
#include <array>
#include <cassert>
using namespace setup::lifecycle;
struct Payload {
    unsigned os, id, size;
    const char *path, *sha;
};
static constexpr std::array<Payload, 0> payloads{};
static constexpr char root[] = "C:\\WINDOWS\\DreamGPU";
static constexpr char destination[] = "C:\\WINDOWS\\SYSTEM\\glide2x.dll";
static constexpr char receipt[] = "C:\\WINDOWS\\DreamGPU\\RESULT.json";
static void initial(Win32Store &store, Journal &j) {
    using namespace fake_win32;
    reset();
    files[canon(root)].directory = true;
    constexpr char valid_receipt[] =
        "{\"schema\":1,\"state\":\"staged\",\"system_activated\":false,"
        "\"provider\":\"not_ready\"}\r\n";
    files[canon(receipt)].bytes.assign(valid_receipt, valid_receipt + sizeof(valid_receipt) - 1);
    files[canon(destination)].bytes = {'o', 'l', 'd'};
    files[canon(destination)].flushed = true;
    resources[100] = {'n', 'e', 'w'};
    j = {};
    j.count = 1;
    j.generation = 1;
    auto &i = j.items[0];
    strcpy(i.path, "glide2x.dll");
    i.resource = 100;
    i.desired.exists = 1;
    i.desired.size = 3;
    setup::Sha256 hash;
    hash.update(resources[100].data(), 3);
    hash.finish(i.desired.sha);
    assert(verified_owner(store));
    assert(store.create_generation(1));
    assert(store.capture(j, 0));
    assert(store.persist(j));
}
static void recovery(Win32Store &store, Journal &j, bool removal) {
    initial(store, j);
    Engine install(store, j);
    assert(install.continue_apply(true) == Result::pending_reboot);
    if (removal) {
        j.state = State::activated; // Recorded historical provider activation.
        Journal next;
        assert(uninstall_plan(j, next));
        assert(store.create_generation(next.generation));
        Item retained = next.items[0];
        assert(store.capture(next, 0));
        assert(same(next.items[0].before, retained.before));
        next.items[0] = retained;
        next.state = State::pending_reboot;
        j = next;
    } else {
        j.state = State::rolling_back;
    }
    assert(store.persist(j));
}
static Result resume(setup::Os os) {
    return act(os, Action::continue_install, payloads);
}
static void restored(Win32Store &store, bool removal) {
    using namespace fake_win32;
    assert(files[canon(destination)].bytes == std::vector<BYTE>({'o', 'l', 'd'}));
    Journal j;
    assert(store.load(j));
    assert(j.state == (removal ? State::removed : State::failed));
    assert(handles.empty() && key_handles.empty());
}
int main() {
    unsigned recovery_points = 0;
    for (auto os : {setup::Os::win98, setup::Os::nt5}) {
        assert(!setup::providers(os).ready());
        for (auto state : {State::staged, State::applying, State::pending_reboot}) {
            Win32Store store;
            Journal j;
            initial(store, j);
            j.state = state;
            assert(store.persist(j));
            fake_win32::mutation = 0;
            assert(resume(os) == Result::provider_not_ready);
            assert(act(os, Action::upgrade, payloads) == Result::provider_not_ready);
            assert(fake_win32::mutation == 0);
            assert(fake_win32::files[fake_win32::canon(destination)].bytes ==
                   std::vector<BYTE>({'o', 'l', 'd'}));
        }
        for (bool removal : {false, true}) {
            unsigned points;
            {
                Win32Store store;
                Journal j;
                recovery(store, j, removal);
                fake_win32::mutation = 0;
                assert(resume(os) == Result::complete);
                points = fake_win32::mutation;
                restored(store, removal);
            }
            // Each syscall mutation failure leaves a journal the real gateway
            // can recover without turning provider readiness on.
            for (unsigned fault = 1; fault <= points; ++fault) {
                Win32Store store;
                Journal j;
                recovery(store, j, removal);
                fake_win32::mutation = 0;
                fake_win32::fail = fault;
                assert(resume(os) != Result::complete);
                fake_win32::fail = 0;
                Journal recovered;
                assert(store.load(recovered));
                // A final flush failure may leave the terminal record readable.
                // Completed transactions need no continuation or replay.
                if (recovered.state != (removal ? State::removed : State::failed))
                    assert(resume(os) == Result::complete);
                restored(store, removal);
            }
            recovery_points += points;
        }
        // An unowned or corrupted receipt never authorizes recovery writes.
        Win32Store store;
        Journal j;
        recovery(store, j, false);
        fake_win32::files[fake_win32::canon(receipt)].bytes = {'x'};
        fake_win32::mutation = 0;
        assert(resume(os) == Result::invalid);
        assert(fake_win32::mutation == 0);
    }
    printf("PASS real lifecycle gateway: unready install blocked, rollback/removal recover across "
           "%u syscall failures\n",
           recovery_points);
}
