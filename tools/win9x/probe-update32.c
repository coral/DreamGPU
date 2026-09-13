/* SPDX-License-Identifier: GPL-2.0-or-later
 * Disposable fixture diagnostic updater. The existing fixed E:\DGDRV.EXE
 * command runs this from a readonly test ISO; only the installed source-built
 * loader and local-setup probes are updated. No driver, runner, system DLL or registry writes.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
void WINAPI WinMainCRTStartup(void) {
    HANDLE log;
    DWORD written;
    BOOL ok;
    const char *text;
    char media[MAX_PATH], root[4];
    log = CreateFileA("C:\\DGDRV.LOG", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                      FILE_ATTRIBUTE_NORMAL, NULL);
    if (log == INVALID_HANDLE_VALUE)
        ExitProcess(2);
    ok = GetModuleFileNameA(NULL, media, sizeof(media)) > 0 &&
         (media[0] == 'D' || media[0] == 'E') && lstrcmpiA(media + 1, ":\\DGDRV.EXE") == 0;
    root[0] = ok ? media[0] : 0;
    root[1] = ':';
    root[2] = '\\';
    root[3] = 0;
    if (ok)
        ok = GetDriveTypeA(root) == DRIVE_CDROM;
    if (ok) {
        lstrcpyA(media + 3, "DGLOAD.EXE");
        ok = GetFileAttributesA("C:\\SIERRA\\Half-Life\\DGLOAD.EXE") != INVALID_FILE_ATTRIBUTES &&
             CopyFileA(media, "C:\\SIERRA\\Half-Life\\DGLOAD.EXE", FALSE);
    }
    if (ok) {
        lstrcpyA(media + 3, "DGSET9.EXE");
        ok = GetFileAttributesA("C:\\SIERRA\\Half-Life\\DGSET9.EXE") != INVALID_FILE_ATTRIBUTES &&
             CopyFileA(media, "C:\\SIERRA\\Half-Life\\DGSET9.EXE", FALSE);
    }
    text = ok ? "PASS automated update98: diagnostic DGLOAD and DGSET9 updated from readonly "
                "media; no driver changes\r\n"
              : "FAIL diagnostic DGLOAD update\r\n";
    WriteFile(log, text, lstrlenA(text), &written, NULL);
    FlushFileBuffers(log);
    CloseHandle(log);
    ExitProcess(ok ? 0 : 1);
}
