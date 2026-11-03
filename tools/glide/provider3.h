/* SPDX-License-Identifier: GPL-2.0-or-later
 * Glide 3 system-loader preflight: the Glide 2 policy plus glide3x.dll. */
#pragma once
#include "provider.h"

inline bool CleanSystemGlide3Launch() {
    /* No Glide provider may be preloaded, and the shared neighbour list
     * predates Glide 3, so an application-local glide3x.dll is rejected here.
     * CleanSystemLaunch has already required cwd == application directory. */
    if (GetModuleHandleA("glide3x.dll") || !CleanSystemLaunch())
        return false;
    char path[MAX_PATH];
    DWORD length = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (!length || length >= MAX_PATH)
        return false;
    while (length && path[length - 1] != '\\')
        --length;
    if (!length)
        return false;
    path[length] = 0;
    if (!provider_clean::append(path, "glide3x.dll"))
        return false;
    SetLastError(ERROR_SUCCESS);
    if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) {
        Record(path);
        return false;
    }
    const DWORD error = GetLastError();
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}
