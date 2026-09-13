/* SPDX-License-Identifier: GPL-2.0-or-later */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#ifdef __cplusplus
#include "handles.hpp"
#endif
static HANDLE Log;
static DWORD Length(const char *s) {
    DWORD n = 0;
    while (s[n])
        ++n;
    return n;
}
static void Text(const char *s) {
    DWORD written;
    WriteFile(Log, s, Length(s), &written, NULL);
    FlushFileBuffers(Log);
}
static void Record(const char *stage, DWORD value) {
    static const char hex[] = "0123456789ABCDEF";
    char line[160];
    DWORD i, n = 0;
    while (*stage && n < 140)
        line[n++] = *stage++;
    line[n++] = ' ';
    line[n++] = '0';
    line[n++] = 'x';
    for (i = 0; i < 8; i++)
        line[n++] = hex[(value >> (28 - 4 * i)) & 15];
    line[n++] = '\r';
    line[n++] = '\n';
    line[n] = 0;
    Text(line);
}
static BOOL Equal(const char *a, const char *b) {
    while (*a && *b) {
        char x = *a++, y = *b++;
        if (x >= 'A' && x <= 'Z')
            x += 32;
        if (y >= 'A' && y <= 'Z')
            y += 32;
        if (x != y)
            return FALSE;
    }
    return *a == *b;
}
static BOOL Join(char *out, const char *dir, const char *name) {
    DWORD a = Length(dir), b = Length(name), i;
    if (a + b + 2 > MAX_PATH)
        return FALSE;
    for (i = 0; i < a; i++) {
        out[i] = dir[i];
    }
    out[a++] = '\\';
    for (i = 0; i <= b; i++) {
        out[a + i] = name[i];
    }
    return TRUE;
}
static BOOL Writable(const char *path) {
    DWORD a = GetFileAttributesA(path);
    if (a == INVALID_FILE_ATTRIBUTES)
        return TRUE;
    return SetFileAttributesA(path, a & ~FILE_ATTRIBUTE_READONLY);
}
static BOOL Copy(const char *source, const char *target) {
    if (!Writable(target))
        return FALSE;
    if (!CopyFileA(source, target, FALSE))
        return FALSE;
    return Writable(target);
}
/* The NT fixture has two CD drives (package E:); Win98 has one (D:).
 * Discover only optical media carrying our fixed marker, never a user path.
 * Ambiguity is an error so two installed packages cannot silently mix. */
static BOOL PackageMedia(char root[4], const char *marker) {
    char candidate[4] = "D:\\", path[MAX_PATH];
    DWORD matches = 0, attributes;
    for (; candidate[0] <= 'Z'; candidate[0]++) {
        if (GetDriveTypeA(candidate) != DRIVE_CDROM)
            continue;
        candidate[2] = 0;
        if (!Join(path, candidate, marker))
            return FALSE;
        candidate[2] = '\\';
        attributes = GetFileAttributesA(path);
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY))
            continue;
        root[0] = candidate[0];
        root[1] = ':';
        root[2] = 0;
        root[3] = 0;
        matches++;
    }
    if (matches != 1) {
        SetLastError(matches ? ERROR_DUP_NAME : ERROR_FILE_NOT_FOUND);
        return FALSE;
    }
    return TRUE;
}
static void Die(const char *stage) {
    DWORD error = GetLastError();
    Record(stage, error);
    CloseHandle(Log);
    ExitProcess(1);
}
