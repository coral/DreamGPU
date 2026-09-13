/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef DREAMGPU_UT_SYSTEM_PROVIDER_H
#define DREAMGPU_UT_SYSTEM_PROVIDER_H

/* Normal games resolve providers from the OS directory. Never copy providers
 * from media or another application; a foreign neighbour fails before launch. */
static const char *const UtProviderNames[] = {
    "opengl32.dll", "dgpugl.dll", "jrgopengl.dll", "glide2x.dll", "wined3d.dll", "winedd.dll",
    "wined8.dll",   "wined9.dll", "ddraw.dll",     "d3d8.dll",    "d3d9.dll",    "dgddr.dll"};
static BOOL UtProviderName(const char *name) {
    for (const char *provider : UtProviderNames)
        if (Equal(name, provider))
            return TRUE;
    return FALSE;
}
static BOOL UtCleanDirectory(const char *directory) {
    for (const char *name : UtProviderNames) {
        char path[MAX_PATH];
        if (!Join(path, directory, name))
            return FALSE;
        if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES ||
            GetLastError() != ERROR_FILE_NOT_FOUND)
            return FALSE;
    }
    return TRUE;
}
struct UtSystemProviders {
    char directory[MAX_PATH]{};
    unsigned seen{};
    BOOL valid{TRUE};
    BOOL initialize() {
        const UINT n = GetSystemDirectoryA(directory, MAX_PATH);
        return n && n < MAX_PATH;
    }
    void observe(const char *name, const char *path) {
        if (!UtProviderName(name))
            return;
        char expected[MAX_PATH];
        if (Equal(name, "dgddr.dll") || Equal(name, "jrgopengl.dll") ||
            !Join(expected, directory, name) || !Equal(path, expected)) {
            valid = FALSE;
            return;
        }
        const unsigned bit = Equal(name, "dgpugl.dll")    ? 1u
                             : Equal(name, "ddraw.dll")   ? 2u
                             : Equal(name, "wined3d.dll") ? 4u
                             : Equal(name, "glide2x.dll") ? 8u
                                                          : 0u;
        if (bit && (seen & bit))
            valid = FALSE;
        seen |= bit;
    }
    BOOL complete(BOOL d3d) const {
        const unsigned required = d3d ? 7u : 9u;
        return valid && (seen & required) == required;
    }
};
#endif
