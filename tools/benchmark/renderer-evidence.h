/* SPDX-License-Identifier: GPL-2.0-or-later
 * Read-only evidence after the host acknowledges the timedemo summary. This
 * proves the owned game's loader route and its own GL strings, not GPU timing.
 */
#ifndef DG_RENDERER_EVIDENCE_H
#define DG_RENDERER_EVIDENCE_H
#include <tlhelp32.h>
static inline BOOL DgRenderEqual(const char *a, const char *b) {
    for (;;) {
        char x = *a++, y = *b++;
        if (x >= 'A' && x <= 'Z')
            x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z')
            y += 'a' - 'A';
        if (x != y)
            return FALSE;
        if (!x)
            return TRUE;
    }
}
static inline BOOL DgRenderText(char *out, DWORD cap, DWORD *used, const char *text) {
    while (*text) {
        if (*used >= cap)
            return FALSE;
        out[(*used)++] = *text++;
    }
    return TRUE;
}
static inline BOOL DgRenderTerminated(const char *text, DWORD cap) {
    for (DWORD i = 0; i < cap; ++i)
        if (!text[i])
            return TRUE;
    return FALSE;
}
static inline BOOL DgRenderPath(char *out, DWORD cap, const char *dir, const char *name) {
    DWORD used = 0;
    return DgRenderText(out, cap, &used, dir) && DgRenderText(out, cap, &used, "\\") &&
           DgRenderText(out, cap, &used, name) && used < cap && (out[used] = 0, TRUE);
}
/* Parse only complete lines in the bounded fresh log prefix, never a previous
 * log or a trailing console fragment. Do not substitute our own GL strings. */
static inline BOOL DgRendererConsole(const char *data, DWORD size, char *out, DWORD cap,
                                     DWORD *used, BOOL *observed = nullptr) {
    if (observed)
        *observed = FALSE;
    static const char *labels[] = {"GL_VENDOR: ", "GL_RENDERER: ", "GL_VERSION: "};
    char values[3][192] = {};
    unsigned mask = 0;
    for (DWORD start = 0; start < size;) {
        DWORD end = start;
        while (end < size && data[end] != '\n')
            ++end;
        if (end == size)
            break;
        DWORD last = end;
        if (last > start && data[last - 1] == '\r')
            --last;
        for (unsigned k = 0; k < 3; ++k) {
            DWORD n = 0;
            while (labels[k][n] && start + n < last && labels[k][n] == data[start + n])
                ++n;
            if (labels[k][n])
                continue;
            if (observed)
                *observed = TRUE;
            DWORD length = last - start - n;
            if ((mask & (1u << k)) || !length || length >= sizeof(values[k]))
                return FALSE;
            for (DWORD j = 0; j < length; ++j) {
                unsigned char c = (unsigned char)data[start + n + j];
                if (c < 32 || c > 126)
                    return FALSE;
                values[k][j] = (char)c;
            }
            values[k][length] = 0;
            mask |= 1u << k;
        }
        start = end + 1;
    }
    for (unsigned k = 0; k < 3; ++k)
        if ((mask & (1u << k)) &&
            (!DgRenderText(out, cap, used, "ENGINE_") || !DgRenderText(out, cap, used, labels[k]) ||
             !DgRenderText(out, cap, used, values[k]) || !DgRenderText(out, cap, used, "\r\n")))
            return FALSE;
    return mask == 7 && DgRenderEqual(values[0], "DreamGPU") &&
           DgRenderEqual(values[1], "DreamGPU (native host OpenGL)");
}
static inline BOOL DgRendererLog(const char *path, char *scratch, DWORD scratch_bytes, char *out,
                                 DWORD cap, DWORD *used, BOOL *observed = nullptr) {
    if (observed)
        *observed = FALSE;
    HANDLE file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;
    DWORD read = 0;
    BOOL ok = ReadFile(file, scratch, scratch_bytes, &read, NULL);
    BOOL closed = CloseHandle(file);
    return ok && closed && DgRendererConsole(scratch, read, out, cap, used, observed);
}
static inline BOOL DgRendererModules(DWORD pid, HANDLE process, const char *game, char *out,
                                     DWORD cap, DWORD *used) {
    static const char *names[] = {"hl.exe", "hw.dll", "opengl32.dll", "dgpuicd.dll", "dgpugl.dll"};
    char system[MAX_PATH], expected[MAX_PATH];
    DWORD n = GetSystemDirectoryA(system, sizeof(system)), exit_code = 0;
    if (!n || n >= sizeof(system) || !GetExitCodeProcess(process, &exit_code) ||
        exit_code != STILL_ACTIVE)
        return FALSE;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    if (snapshot == INVALID_HANDLE_VALUE)
        return FALSE;
    MODULEENTRY32 entry = {};
    entry.dwSize = sizeof(entry);
    DWORD start = GetTickCount(), count = 0;
    unsigned mask = 0;
    BOOL ok = TRUE, more = Module32First(snapshot, &entry);
    while (more) {
        if (++count > 128 || (DWORD)(GetTickCount() - start) >= 500 ||
            !DgRenderTerminated(entry.szModule, sizeof(entry.szModule)) ||
            !DgRenderTerminated(entry.szExePath, sizeof(entry.szExePath))) {
            ok = FALSE;
            break;
        }
        if (DgRenderEqual(entry.szModule, "sw.dll") ||
            DgRenderEqual(entry.szModule, "jrgopengl.dll")) {
            ok = FALSE;
            break;
        }
        for (unsigned k = 0; k < 5; ++k) {
            if (!DgRenderEqual(entry.szModule, names[k]))
                continue;
            if (!DgRenderText(out, cap, used, "RENDERER_MODULE ") ||
                !DgRenderText(out, cap, used, entry.szExePath) ||
                !DgRenderText(out, cap, used, "\r\n"))
                ok = FALSE;
            if ((mask & (1u << k)) ||
                !DgRenderPath(expected, sizeof(expected), k < 2 ? game : system, names[k]) ||
                !DgRenderEqual(entry.szExePath, expected))
                ok = FALSE;
            mask |= 1u << k;
        }
        if (!ok)
            break;
        more = Module32Next(snapshot, &entry);
    }
    if (more || GetLastError() != ERROR_NO_MORE_FILES)
        ok = FALSE;
    if (!CloseHandle(snapshot))
        ok = FALSE;
    return ok && mask == 31 && GetExitCodeProcess(process, &exit_code) && exit_code == STILL_ACTIVE;
}
#endif
