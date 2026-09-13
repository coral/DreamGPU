/* SPDX-License-Identifier: GPL-2.0-or-later
 * Fixed read-only inspection of retained Half-Life console headers. Does not
 * launch a game or claim a module snapshot for an already terminated process.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "renderer-evidence.h"
extern "C" void *memset(void *destination, int value, unsigned int bytes) {
    volatile unsigned char *p = (volatile unsigned char *)destination;
    for (unsigned int i = 0; i < bytes; ++i)
        p[i] = (unsigned char)value;
    return destination;
}
static char scratch[32768], output[4096];
extern "C" void __stdcall WinMainCRTStartup() {
    static const char *logs[] = {"C:\\SIERRA\\Half-Life\\qconsole.log",
                                 "C:\\SIERRA\\Half-Life\\valve\\qconsole.log"};
    DWORD used = 0;
    BOOL found = FALSE;
    DgRenderText(
        output, sizeof(output), &used,
        "CONSOLE_ONLY retained logs; no process/module identity or GPU execution proof\r\n");
    for (const char *path : logs) {
        DgRenderText(output, sizeof(output), &used, path);
        DgRenderText(output, sizeof(output), &used, "\r\n");
        if (DgRendererLog(path, scratch, sizeof(scratch), output, sizeof(output), &used))
            found = TRUE;
    }
    DgRenderText(output, sizeof(output), &used,
                 found ? "PASS automated hlevidence: retained DreamGPU console strings only\r\n"
                       : "FAIL automated hlevidence: missing or non-DreamGPU console strings\r\n");
    HANDLE file = CreateFileA("C:\\DGHLEVID.LOG", GENERIC_WRITE, FILE_SHARE_READ, NULL,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD written = 0;
    BOOL ok = file != INVALID_HANDLE_VALUE && WriteFile(file, output, used, &written, NULL) &&
              written == used && FlushFileBuffers(file);
    if (file != INVALID_HANDLE_VALUE && !CloseHandle(file))
        ok = FALSE;
    ExitProcess(ok && found ? 0 : 1);
}
