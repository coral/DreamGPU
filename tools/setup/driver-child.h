// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "durable-record.h"
namespace setup::driver {
// NT process IDs are reusable, including across boots. A creation timestamp is
// captured while our new child is suspended and durably saved before it runs.
// Historical PID-only journals are recovered using an exact executable path;
// inability to inspect a live process never authorizes another SetupAPI writer.
struct ChildRecord {
    uint32_t magic = 0x43474444, version = 1, pid = 0, low = 0, high = 0;
    char executable[MAX_PATH]{}, sha[68]{}, installer_sha[68]{};
};
inline bool valid_child(const ChildRecord &r) {
    return r.magic == 0x43474444 && r.version == 1 && r.pid && (r.low || r.high) &&
           bounded(r.executable, MAX_PATH) && hash(r.sha) && hash(r.installer_sha);
}
struct ChildApi {
#ifndef DG_DRIVER_CHILD_TEST
    template <class Function> static bool resolve(Function &out, HMODULE module, const char *name) {
        FARPROC address = GetProcAddress(module, name);
        if (!address)
            return false;
        static_assert(sizeof(out) == sizeof(address));
        auto *to = reinterpret_cast<BYTE *>(&out);
        const auto *from = reinterpret_cast<const BYTE *>(&address);
        for (unsigned n = 0; n < sizeof(out); ++n)
            to[n] = from[n];
        return true;
    }
#endif
    static bool creation(HANDLE process, uint32_t &low, uint32_t &high) {
#ifdef DG_DRIVER_CHILD_TEST
        return driver_fake::child_creation(process, low, high);
#else
        using Times = BOOL(WINAPI *)(HANDLE, LPFILETIME, LPFILETIME, LPFILETIME, LPFILETIME);
        Times call = nullptr;
        if (!resolve(call, GetModuleHandleA("kernel32.dll"), "GetProcessTimes"))
            return false;
        FILETIME created{}, exited{}, kernel{}, user{};
        if (!call || !call(process, &created, &exited, &kernel, &user))
            return false;
        low = created.dwLowDateTime;
        high = created.dwHighDateTime;
        return low || high;
#endif
    }
    static bool executable(HANDLE process, const char *system, char out[MAX_PATH]) {
#ifdef DG_DRIVER_CHILD_TEST
        (void)system;
        return driver_fake::child_executable(process, out);
#else
        // Load only the OS's PSAPI; a current-directory DLL cannot supply this
        // legacy recovery identity. This keeps NT-only APIs out of PE4 imports.
        char library[MAX_PATH];
        if (lstrlenA(system) + 11 >= MAX_PATH)
            return false;
        lstrcpyA(library, system);
        lstrcatA(library, "\\psapi.dll");
        HMODULE module = LoadLibraryA(library);
        if (!module)
            return false;
        using Name = DWORD(WINAPI *)(HANDLE, HMODULE, LPSTR, DWORD);
        Name call = nullptr;
        DWORD count = resolve(call, module, "GetModuleFileNameExA")
                          ? call(process, nullptr, out, MAX_PATH)
                          : 0;
        bool result = count && count < MAX_PATH && out[count] == 0;
        FreeLibrary(module);
        return result;
#endif
    }
};
} // namespace setup::driver
