// SPDX-License-Identifier: GPL-2.0-or-later
// Shared normal-loader checks for the exact D3D6/7/8/9 pixel probes.
#pragma once
#include "../common/provider-clean.h"

namespace system_loader {
inline bool append(char *path, const char *name) {
    const auto used = static_cast<unsigned>(lstrlenA(path));
    const auto count = static_cast<unsigned>(lstrlenA(name));
    if (used >= MAX_PATH || count >= MAX_PATH - used)
        return false;
    for (unsigned i = 0; i <= count; ++i)
        path[used + i] = name[i];
    return true;
}
inline bool module(HMODULE value, const char *name) {
    char actual[MAX_PATH], expected[MAX_PATH];
    const DWORD a = value ? GetModuleFileNameA(value, actual, sizeof(actual)) : 0;
    const DWORD b = GetSystemDirectoryA(expected, sizeof(expected));
    if (!a || a >= sizeof(actual) || !b || b >= sizeof(expected) || !append(expected, "\\") ||
        !append(expected, name))
        return false;
    return !lstrcmpiA(actual, expected);
}
using Trace = void (*)(const char *);
inline bool clean_directory(const char *base, Trace trace = nullptr) {
    return provider_clean::directory(base, trace);
}
inline bool clean(Trace trace = nullptr) {
    return provider_clean::launch(trace);
}
inline HMODULE load(const char *name, Trace trace = nullptr) {
    if (!clean(trace))
        return nullptr;
    HMODULE result = LoadLibraryA(name);
    if (result && !module(result, name)) {
        if (trace) {
            char actual[MAX_PATH];
            const DWORD length = GetModuleFileNameA(result, actual, sizeof(actual));
            if (length && length < sizeof(actual))
                trace(actual);
        }
        FreeLibrary(result);
        return nullptr;
    }
    return result;
}
// Module presence alone cannot prove that the factory returned Wine's object.
// Verify its actual COM vtable mapping as well as all GPU dependency locations.
inline bool object(const void *vtable, const char *provider) {
    MEMORY_BASIC_INFORMATION memory{};
    return VirtualQuery(vtable, &memory, sizeof(memory)) == sizeof(memory) &&
           module(static_cast<HMODULE>(memory.AllocationBase), provider) &&
           module(GetModuleHandleA("wined3d.dll"), "wined3d.dll") &&
           module(GetModuleHandleA("dgpugl.dll"), "dgpugl.dll");
}
} // namespace system_loader
