/* SPDX-License-Identifier: GPL-2.0-or-later
 * Failure-only evidence for the controller's own child. Bounded module walk,
 * no arbitrary process scan, paths, memory reads or guest input injection.
 */
#ifndef DG_PROCESS_EVIDENCE_H
#define DG_PROCESS_EVIDENCE_H
#include <tlhelp32.h>
static void DgEvidenceText(char *buffer, DWORD capacity, DWORD *bytes, const char *text) {
    while (*text && *bytes < capacity)
        buffer[(*bytes)++] = *text++;
}
static void DgEvidenceHex(char *buffer, DWORD capacity, DWORD *bytes, DWORD value) {
    static const char digits[] = "0123456789abcdef";
    char text[11];
    unsigned i;
    text[0] = '0';
    text[1] = 'x';
    text[10] = 0;
    for (i = 0; i < 8; ++i)
        text[2 + i] = digits[(value >> (28 - i * 4)) & 15];
    DgEvidenceText(buffer, capacity, bytes, text);
}
static const char *DgEvidenceModule(const char *name) {
    static const char *names[] = {"hl.exe", "hw.dll", "sw.dll", "dgpugl.dll", "opengl32.dll"};
    unsigned i, j;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        for (j = 0; names[i][j]; ++j) {
            char c = name[j];
            if (c >= 'A' && c <= 'Z')
                c += 'a' - 'A';
            if (c != names[i][j])
                break;
        }
        if (!names[i][j] && !name[j])
            return names[i];
    }
    return NULL;
}
static void ProcessEvidence(DWORD pid, HANDLE process, char *buffer, DWORD capacity, DWORD *bytes) {
    HANDLE snapshot;
    MODULEENTRY32 entry;
    DWORD value, started, count = 0;
    BOOL more;
    if (!buffer || !bytes || *bytes > capacity || !pid)
        return;
    DgEvidenceText(buffer, capacity, bytes, "\r\nOWNED_PROCESS exit=");
    if (GetExitCodeProcess(process, &value))
        DgEvidenceHex(buffer, capacity, bytes, value);
    else {
        value = GetLastError();
        DgEvidenceText(buffer, capacity, bytes, "query-error:");
        DgEvidenceHex(buffer, capacity, bytes, value);
    }
    DgEvidenceText(buffer, capacity, bytes, "\r\n");
    started = GetTickCount();
    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    if (snapshot == INVALID_HANDLE_VALUE) {
        value = GetLastError();
        DgEvidenceText(buffer, capacity, bytes, "OWNED_MODULE snapshot-error=");
        DgEvidenceHex(buffer, capacity, bytes, value);
        DgEvidenceText(buffer, capacity, bytes, "\r\n");
        return;
    }
    memset(&entry, 0, sizeof(entry));
    entry.dwSize = sizeof(entry);
    more = Module32First(snapshot, &entry);
    while (more && count++ < 128 && (DWORD)(GetTickCount() - started) < 500 && *bytes < capacity) {
        const char *name;
        entry.szModule[sizeof(entry.szModule) - 1] = 0;
        name = DgEvidenceModule(entry.szModule);
        if (name) {
            DgEvidenceText(buffer, capacity, bytes, "OWNED_MODULE ");
            DgEvidenceText(buffer, capacity, bytes, name);
            DgEvidenceText(buffer, capacity, bytes, "\r\n");
        }
        more = Module32Next(snapshot, &entry);
    }
    if (more)
        DgEvidenceText(buffer, capacity, bytes, "OWNED_MODULE truncated\r\n");
    CloseHandle(snapshot);
}
#endif
