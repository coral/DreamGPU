// SPDX-License-Identifier: GPL-2.0-or-later
// Shared normal-loader preflight. The installer authenticates helper bytes;
// this code independently rejects app/cwd DLL search-path contamination.
#pragma once
namespace provider_clean {
using Trace = void (*)(const char *);
inline bool append(char *path, const char *name) {
    const auto used = static_cast<unsigned>(lstrlenA(path));
    const auto count = static_cast<unsigned>(lstrlenA(name));
    if (used >= MAX_PATH || count >= MAX_PATH - used)
        return false;
    for (unsigned i = 0; i <= count; ++i)
        path[used + i] = name[i];
    return true;
}
inline bool directory(const char *base, Trace trace = nullptr) {
    constexpr const char *names[] = {"opengl32.dll", "dgpugl.dll", "dgpuicd.dll", "glide2x.dll",
                                     "ddraw.dll",    "d3d8.dll",   "d3d9.dll",    "wined3d.dll",
                                     "winedd.dll",   "wined8.dll", "wined9.dll",  "ddsys.dll",
                                     "msd3d8.dll",   "msd3d9.dll", "dgddr.dll"};
    for (const auto *name : names) {
        char path[MAX_PATH]{};
        if (!append(path, base))
            return false;
        const auto length = lstrlenA(path);
        if ((!length || path[length - 1] != '\\') && !append(path, "\\"))
            return false;
        if (!append(path, name))
            return false;
        SetLastError(ERROR_SUCCESS);
        if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) {
            if (trace)
                trace(path);
            return false;
        }
        const DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND) {
            if (trace)
                trace(path);
            SetLastError(error);
            return false;
        }
    }
    return true;
}
inline bool launch(Trace trace = nullptr) {
    char cwd[MAX_PATH], app[MAX_PATH];
    DWORD a = GetCurrentDirectoryA(sizeof(cwd), cwd);
    DWORD b = GetModuleFileNameA(nullptr, app, sizeof(app));
    if (!a || a >= sizeof(cwd) || !b || b >= sizeof(app))
        return false;
    while (b && app[b - 1] != '\\')
        --b;
    if (!b)
        return false;
    app[b] = 0;
    // Keep a drive root's trailing slash; normalize ordinary directory endings.
    while (a > 3 && cwd[a - 1] == '\\')
        cwd[--a] = 0;
    while (b > 3 && app[b - 1] == '\\')
        app[--b] = 0;
    if (lstrcmpiA(cwd, app)) {
        if (trace) {
            trace(cwd);
            trace(app);
        }
        return false;
    }
    return directory(app, trace);
}
} // namespace provider_clean
