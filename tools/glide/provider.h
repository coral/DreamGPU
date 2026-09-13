/* SPDX-License-Identifier: GPL-2.0-or-later
 * Normal system-loader preflight and exact loaded-module identity checks. */
#pragma once
#include "../common/provider-clean.h"

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
    /* Never preload a provider. Normal LoadLibrary must resolve the installed
     * SystemDirectory module from the authenticated clean helper directory. */
    if (GetModuleHandleA("glide2x.dll") || GetModuleHandleA("dgpugl.dll"))
        return false;
    return provider_clean::launch(Record);
}
