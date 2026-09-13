/* SPDX-License-Identifier: GPL-2.0-or-later
 * Fixed optical package replacement. No path or command comes from serial.
 * Spawn suspended so INSTALLING is sent before the child can stop this runner. */
static BOOL InstallMember(char drive, const char *name) {
    char path[MAX_PATH], *end;
    BYTE magic[2];
    DWORD read = 0;
    HANDLE file;
    path[0] = drive;
    path[1] = ':';
    path[2] = '\\';
    end = Append(path + 3, name);
    (void)end;
    file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;
    BOOL valid = ReadFile(file, magic, sizeof(magic), &read, NULL) && read == 2 &&
                 magic[0] == 'M' && magic[1] == 'Z';
    CloseHandle(file);
    return valid;
}
static const char *InstallPackage(const char *id) {
    char root[4] = "D:\\", selected = 0, command[] = "D:\\DGSETUP.EXE";
    DWORD matches = 0;
    UINT previous = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    STARTUPINFOA startup;
    PROCESS_INFORMATION process;
    memset(&startup, 0, sizeof(startup));
    memset(&process, 0, sizeof(process));
    for (; root[0] <= 'Z'; root[0]++) {
        if (GetDriveTypeA(root) != DRIVE_CDROM)
            continue;
        if (InstallMember(root[0], "DGSETUP.EXE") && InstallMember(root[0], "DGPUBEN.EXE") &&
            InstallMember(root[0], "DGWGL.EXE") && InstallMember(root[0], "dgpugl.dll")) {
            selected = root[0];
            matches++;
        }
    }
    SetErrorMode(previous);
    if (matches != 1)
        return matches ? "install-ambiguous-media" : "install-media-unavailable";
    command[0] = selected;
    startup.cb = sizeof(startup);
    if (!CreateProcessA(command, command, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, "C:\\",
                        &startup, &process))
        return "install-launch-failed";
    if (!Reply("INSTALLING", id, NULL, FALSE)) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 3000);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return "install-ack-failed";
    }
    if (ResumeThread(process.hThread) == (DWORD)-1) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 3000);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return "install-resume-failed";
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return NULL;
}
