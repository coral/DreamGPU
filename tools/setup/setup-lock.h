// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#ifndef DG_SETUP_LOCK_TEST
#include <windows.h>
#endif
#include "policy.h"
namespace setup {
// Named-object existence is not ownership. The automatic startup executor and
// explicit continuation may overlap, or a readiness waiter may retain a handle
// after releasing ownership. Only the kernel wait grants access to journals.
// API contract: https://learn.microsoft.com/en-us/windows/win32/sync/using-mutex-objects
enum class LockScope { transaction, startup };
class SetupLock {
    HANDLE handle_ = nullptr;
    bool owned_ = false;

  public:
    explicit SetupLock(Os os, DWORD timeout = 90000, LockScope scope = LockScope::transaction) {
        if (os != Os::win98 && os != Os::nt5)
            return;
        const char *name =
            scope == LockScope::startup
                ? (os == Os::nt5 ? "Global\\DreamGPU.Setup.Startup" : "DreamGPU.Setup.Startup")
                : (os == Os::nt5 ? "Global\\DreamGPU.Setup" : "DreamGPU.Setup");
        handle_ = CreateMutexA(nullptr, FALSE, name);
        if (!handle_)
            return;
        const DWORD result = WaitForSingleObject(handle_, timeout > 90000 ? 90000 : timeout);
        owned_ = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
    }
    SetupLock(const SetupLock &) = delete;
    SetupLock &operator=(const SetupLock &) = delete;
    ~SetupLock() {
        if (owned_)
            ReleaseMutex(handle_);
        if (handle_)
            CloseHandle(handle_);
    }
    bool acquired() const {
        return owned_;
    }
};
} // namespace setup
