/* SPDX-License-Identifier: GPL-2.0-or-later
 * Normal system-loader preflight and exact loaded-module identity checks. */
#pragma once

static bool SystemModule(HMODULE module, const char *name) {
    char expected[MAX_PATH], actual[MAX_PATH];
    UINT count = GetSystemDirectoryA(expected, MAX_PATH);
    DWORD length = module ? GetModuleFileNameA(module, actual, MAX_PATH) : 0;
    if (!count || count >= MAX_PATH || !length || length >= MAX_PATH ||
        count + 1 + (UINT)lstrlenA(name) >= MAX_PATH)
        return false;
    expected[count++] = '\\';
    lstrcpyA(expected + count, name);
    Record(actual);
    return lstrcmpiA(expected, actual) == 0;
}

static bool CleanSystemLaunch() {
    char directory[MAX_PATH], executable[MAX_PATH];
    DWORD count = GetCurrentDirectoryA(MAX_PATH, directory);
    DWORD length = GetModuleFileNameA(NULL, executable, MAX_PATH);
    if (!count || count >= MAX_PATH || !length || length >= MAX_PATH ||
        lstrcmpiA(directory, "C:\\") || lstrcmpiA(executable, "C:\\DGSYSGR.EXE"))
        return false;
    /* Do not preload either provider. A neighboring DLL would win normal
     * Windows loader search before the system directory. */
    if (GetModuleHandleA("glide2x.dll") || GetModuleHandleA("dgpugl.dll"))
        return false;
    for (const char *name : {"C:\\glide2x.dll", "C:\\dgpugl.dll"}) {
        if (GetFileAttributesA(name) != INVALID_FILE_ATTRIBUTES)
            return false;
        DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
            return false;
    }
    return true;
}
