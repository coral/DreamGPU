// SPDX-License-Identifier: GPL-2.0-or-later
#define DG_SETUP_ADAPTER_TEST
#include "test-win32.h"
inline DWORD GetModuleFileNameA(void *, char *out, DWORD size) {
    constexpr char path[] = "C:\\WINDOWS\\DreamGPU\\setup.exe";
    if (size < sizeof(path))
        return size;
    strcpy(out, path);
    return sizeof(path) - 1;
}
#include "runtime-resume.h"
#include "system-engine.h"
#include <cassert>
using namespace setup;
using namespace setup::lifecycle;
struct Store : Win32Store {
    bool pass = true;
    unsigned verified = 0;
    bool verify_activation(const Journal &) {
        ++verified;
        return pass;
    }
};
struct Boot {
    Store &store;
    bool ready = false;
    Result cancellation = Result::complete;
    unsigned calls = 0, cancelled = 0;
    Result transition(bool undo) {
        Journal durable;
        assert(store.load(durable));
        assert(durable.state == (undo ? State::rolling_back : State::applying));
        if (undo)
            assert(cancelled);
        ++calls;
        return ready ? Result::complete : Result::pending_reboot;
    }
    Result cancel_apply() {
        ++cancelled;
        return cancellation;
    }
};
static void init(Store &store, Journal &j) {
    using namespace fake_win32;
    reset();
    files[canon("C:\\WINDOWS\\DreamGPU")].directory = true;
    files[canon("C:\\WINDOWS\\DreamGPU\\setup.exe")].bytes = {'p', 'e'};
    files[canon("C:\\WINDOWS\\SYSTEM\\ddraw.dll")].bytes = {'o', 'l', 'd'};
    keys[canon("Software\\Microsoft\\Windows\\CurrentVersion\\Run")];
    resources[100] = {'n', 'e', 'w'};
    j = {};
    j.generation = 1;
    j.count = 1;
    j.items[0].resource = 100;
    strcpy(j.items[0].path, "ddraw.dll");
    j.items[0].desired.exists = 1;
    j.items[0].desired.size = 3;
    native_alias::hash(resources[100], j.items[0].desired.sha);
    assert(store.init("C:\\WINDOWS\\DreamGPU") && store.create_generation(1) &&
           store.capture(j, 0) && store.persist(j));
    mutation = 0;
}
int main() {
    unsigned points;
    {
        Store store;
        Journal j;
        init(store, j);
        Boot boot{store};
        RuntimeResume resume(store, 1, Os::nt5);
        SystemEngine engine(store, j, boot, resume);
        assert(engine.apply(false) == Result::provider_not_ready && boot.calls == 0 &&
               fake_win32::mutation == 0);
        assert(engine.apply(true) == Result::pending_reboot && j.state == State::pending_reboot);
        points = fake_win32::mutation;
        fake_win32::files[fake_win32::canon("C:\\WINDOWS\\SYSTEM\\ddraw.dll")].bytes = {'n', 'e',
                                                                                        'w'};
        boot.ready = true;
        assert(engine.apply(true) == Result::complete && j.state == State::activated &&
               store.verified == 1);
        assert(engine.apply(true) == Result::complete && store.verified == 1);
        boot.cancellation = Result::conflict;
        unsigned calls = boot.calls;
        assert(engine.rollback() == Result::conflict && boot.calls == calls &&
               j.state == State::rolling_back);
        boot.cancellation = Result::complete;
        boot.ready = false;
        assert(engine.rollback() == Result::pending_reboot && j.state == State::rolling_back);
        fake_win32::files[fake_win32::canon("C:\\WINDOWS\\SYSTEM\\ddraw.dll")].bytes = {'o', 'l',
                                                                                        'd'};
        boot.ready = true;
        assert(engine.rollback() == Result::complete && j.state == State::failed);
    }
    for (unsigned failure = 1; failure <= points; ++failure) {
        Store store;
        Journal j;
        init(store, j);
        Boot boot{store};
        RuntimeResume resume(store, 1, Os::nt5);
        fake_win32::fail = failure;
        SystemEngine engine(store, j, boot, resume);
        assert(engine.apply(true) == Result::io_error);
        assert(fake_win32::files.at(fake_win32::canon("C:\\WINDOWS\\SYSTEM\\ddraw.dll")).bytes ==
               std::vector<BYTE>({'o', 'l', 'd'}));
    }
    {
        Store store;
        Journal j;
        init(store, j);
        Boot boot{store};
        RuntimeResume resume(store, 1, Os::nt5);
        SystemEngine engine(store, j, boot, resume);
        assert(engine.apply(true) == Result::pending_reboot);
        fake_win32::files[fake_win32::canon("C:\\WINDOWS\\SYSTEM\\ddraw.dll")].bytes = {'n', 'e',
                                                                                        'w'};
        boot.ready = true;
        store.pass = false;
        assert(engine.apply(true) == Result::io_error && j.state != State::activated);
    }
    printf("PASS actual SystemEngine ordering: %u mutation boundaries; "
           "persist/arm before queue, "
           "cancel before restore, no repeated passed API gate, failed API is "
           "not a reboot promise\n",
           points);
}
