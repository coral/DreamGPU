// SPDX-License-Identifier: GPL-2.0-or-later
#define DG_SETUP_LOCK_TEST
#include <cassert>
#include <cstdint>
#include <string>
#include <cstdio>
using DWORD = uint32_t;
using HANDLE = void *;
constexpr bool FALSE = false;
constexpr DWORD WAIT_OBJECT_0 = 0, WAIT_ABANDONED = 0x80, WAIT_TIMEOUT = 258,
                WAIT_FAILED = 0xffffffff;
struct Kernel {
    unsigned handles = 0, waits = 0, releases = 0, closes = 0;
    bool create_error = false, wait_error = false, held = false, abandoned = false,
         release_during_wait = false;
    DWORD last_error = 183, timeout = 0, durable_phase = 0;
    std::string name;
} kernel;
static HANDLE CreateMutexA(void *, bool initial_owner, const char *name) {
    assert(!initial_owner);
    kernel.name = name;
    if (kernel.create_error)
        return nullptr;
    ++kernel.handles;
    return &kernel;
}
static DWORD WaitForSingleObject(HANDLE handle, DWORD timeout) {
    assert(handle == &kernel && kernel.handles);
    ++kernel.waits;
    kernel.timeout = timeout;
    if (kernel.wait_error)
        return WAIT_FAILED;
    if (kernel.held && kernel.release_during_wait && timeout) {
        kernel.durable_phase = 2;
        kernel.held = false;
    }
    if (kernel.held)
        return WAIT_TIMEOUT;
    kernel.held = true;
    return kernel.abandoned ? WAIT_ABANDONED : WAIT_OBJECT_0;
}
static bool ReleaseMutex(HANDLE handle) {
    assert(handle == &kernel && kernel.held);
    kernel.held = false;
    ++kernel.releases;
    return true;
}
static bool CloseHandle(HANDLE handle) {
    assert(handle == &kernel && kernel.handles);
    --kernel.handles;
    ++kernel.closes;
    return true;
}
#include "setup-lock.h"
int main() {
    for (auto os : {setup::Os::win98, setup::Os::nt5}) {
        kernel = {};
        // An observer keeps an existing, unowned named object alive. A stale
        // ERROR_ALREADY_EXISTS value cannot make ownership unavailable.
        kernel.handles = 1;
        {
            setup::SetupLock lock(os);
            assert(lock.acquired() && kernel.held && kernel.last_error == 183);
            assert(kernel.timeout == 90000);
        }
        assert(kernel.handles == 1 && kernel.releases == 1 && kernel.closes == 1);
        assert(kernel.name == (os == setup::Os::nt5 ? "Global\\DreamGPU.Setup" : "DreamGPU.Setup"));
    }
    kernel = {};
    kernel.held = true;
    kernel.release_during_wait = true;
    {
        setup::SetupLock lock(setup::Os::win98);
        assert(lock.acquired() && kernel.durable_phase == 2);
    }
    assert(kernel.releases == 1 && !kernel.handles);
    for (unsigned failure = 0; failure < 3; ++failure) {
        kernel = {};
        kernel.create_error = failure == 0;
        kernel.wait_error = failure == 1;
        kernel.held = failure == 2;
        {
            setup::SetupLock lock(setup::Os::nt5, 0);
            assert(!lock.acquired());
        }
        assert(!kernel.handles && !kernel.releases);
        assert(kernel.closes == unsigned(failure != 0));
    }
    kernel = {};
    kernel.abandoned = true;
    {
        setup::SetupLock lock(setup::Os::nt5, 0xffffffff);
        assert(lock.acquired() && kernel.timeout == 90000);
    }
    assert(kernel.releases == 1 && !kernel.handles);
    kernel = {};
    {
        setup::SetupLock lock(setup::Os::unsupported);
        assert(!lock.acquired());
    }
    assert(!kernel.waits && !kernel.handles);
    for (auto os : {setup::Os::win98, setup::Os::nt5}) {
        kernel = {};
        {
            setup::SetupLock startup(os, 0, setup::LockScope::startup);
            assert(startup.acquired() && kernel.timeout == 0);
            assert(kernel.name == (os == setup::Os::nt5 ? "Global\\DreamGPU.Setup.Startup"
                                                        : "DreamGPU.Setup.Startup"));
            // A second process cannot wait for the first dialog to close and
            // then repeat the pending-reboot prompt. The mock models two owners.
            kernel.release_during_wait = true;
            {
                setup::SetupLock duplicate(os, 0, setup::LockScope::startup);
                assert(!duplicate.acquired() && kernel.timeout == 0);
            }
            assert(kernel.held && kernel.releases == 0 && kernel.handles == 1);
        }
        assert(!kernel.held && kernel.handles == 0 && kernel.releases == 1);
        kernel = {};
        kernel.abandoned = true;
        {
            setup::SetupLock recovered(os, 0, setup::LockScope::startup);
            assert(recovered.acquired());
        }
        assert(!kernel.handles && kernel.releases == 1);
    }
    puts("PASS actual setup mutex: retained unowned handle, bounded startup handoff, "
         "timeout/failure/abandonment, exact OS namespace and balanced ownership");
}
