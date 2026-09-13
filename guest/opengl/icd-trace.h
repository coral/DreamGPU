/* SPDX-License-Identifier: GPL-2.0-or-later
 * Best-effort diagnostic loader trace, bounded to 64 records per process.
 * No GL draw calls enter this path. A concurrent writer never waits/retries.
 */
#ifndef DREAMGPU_ICD_TRACE_H
#define DREAMGPU_ICD_TRACE_H
static volatile LONG IcdTraceRecords;
static void IcdTrace(const char *event, DWORD first, DWORD second) {
    DWORD saved = GetLastError();
    if (InterlockedCompareExchange(&IcdTraceRecords, 64, 64) >= 64)
        return;
    LONG record = InterlockedIncrement(&IcdTraceRecords);
    if (record > 64)
        return;
    char text[128];
    unsigned used = 0;
    while (*event && used < 100)
        text[used++] = *event++;
    const DWORD values[] = {first, second};
    for (DWORD value : values) {
        text[used++] = ' ';
        for (int shift = 28; shift >= 0; shift -= 4)
            text[used++] = "0123456789abcdef"[(value >> shift) & 15];
    }
    text[used++] = '\r';
    text[used++] = '\n';
    HANDLE file =
        CreateFileA("C:\\DGICD.LOG", GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                    record == 1 ? CREATE_ALWAYS : OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        /* No shared write access: competing diagnostic writers fail immediately.
         * Preserve the API caller's LastError regardless of diagnostic failure. */
        if (SetFilePointer(file, 0, nullptr, FILE_END) != INVALID_SET_FILE_POINTER) {
            DWORD written;
            WriteFile(file, text, used, &written, nullptr);
        }
        CloseHandle(file);
    }
    SetLastError(saved);
}
#endif
