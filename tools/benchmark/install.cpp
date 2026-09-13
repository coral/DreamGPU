/* Fixed fixture package update through the guest filesystem. No shell/network. */
#define WIN32_LEAN_AND_MEAN
extern "C" {
#include <windows.h>
#include <tlhelp32.h>

static HANDLE Log;
static char Directory[MAX_PATH], Source[MAX_PATH];
static DWORD Length(const char *s) {
    DWORD n = 0;
    while (s[n])
        ++n;
    return n;
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
/* Win98 returns a path in PROCESSENTRY32.szExeFile; NT usually a basename.
 * This only selects candidates. The module path below still proves ownership. */
static BOOL RunnerName(const char *path) {
    const char *name = path;
    for (; *path; path++)
        if (*path == '\\' || *path == '/')
            name = path + 1;
    return Equal(name, "DGPUBEN.EXE");
}
static void Record(const char *stage, DWORD error) {
    static const char hex[] = "0123456789ABCDEF";
    char line[128];
    DWORD n = 0, i, written;
    while (*stage && n < 96)
        line[n++] = *stage++;
    line[n++] = ' ';
    line[n++] = '0';
    line[n++] = 'x';
    for (i = 0; i < 8; i++)
        line[n++] = hex[(error >> (28 - i * 4)) & 15];
    line[n++] = '\r';
    line[n++] = '\n';
    WriteFile(Log, line, n, &written, NULL);
    FlushFileBuffers(Log);
}
static BOOL StopRunner(void) {
    HANDLE list = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    PROCESSENTRY32 process;
    BOOL found;
    if (list == INVALID_HANDLE_VALUE)
        return FALSE;
    process.dwSize = sizeof(process);
    found = Process32First(list, &process);
    while (found) {
        if (RunnerName(process.szExeFile)) {
            HANDLE modules = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, process.th32ProcessID);
            MODULEENTRY32 module;
            module.dwSize = sizeof(module);
            BOOL owned = modules != INVALID_HANDLE_VALUE && Module32First(modules, &module) &&
                         (Equal(module.szExePath, "C:\\DGPUBEN.EXE") ||
                          Equal(module.szExePath, "E:\\DGPUBEN.EXE"));
            if (modules != INVALID_HANDLE_VALUE)
                CloseHandle(modules);
            if (!owned) {
                CloseHandle(list);
                SetLastError(ERROR_ACCESS_DENIED);
                return FALSE;
            }
            HANDLE target =
                OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, process.th32ProcessID);
            if (!target) {
                CloseHandle(list);
                return FALSE;
            }
            BOOL stopped =
                TerminateProcess(target, 0) && WaitForSingleObject(target, 5000) == WAIT_OBJECT_0;
            CloseHandle(target);
            if (!stopped) {
                CloseHandle(list);
                return FALSE;
            }
        }
        found = Process32Next(list, &process);
    }
    CloseHandle(list);
    return TRUE;
}
static BOOL Copy(const char *name, const char *target) {
    DWORD i, n = Length(Directory), bytes = Length(name);
    if (n + bytes >= MAX_PATH) {
        SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return FALSE;
    }
    for (i = 0; i < n; i++)
        Source[i] = Directory[i];
    for (i = 0; i <= bytes; i++)
        Source[n + i] = name[i];
    /* CopyFile inherits the CD-ROM source's read-only bit. Clear only that
     * bit on package-owned targets so the next package can replace them. */
    {
        DWORD attributes = GetFileAttributesA(target), error;
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_READONLY) &&
            !SetFileAttributesA(target, attributes & ~FILE_ATTRIBUTE_READONLY))
            return FALSE;
        if (!CopyFileA(Source, target, FALSE)) {
            error = GetLastError();
            if (attributes != INVALID_FILE_ATTRIBUTES)
                SetFileAttributesA(target, attributes);
            SetLastError(error);
            return FALSE;
        }
        attributes = GetFileAttributesA(target);
        return attributes != INVALID_FILE_ATTRIBUTES &&
               SetFileAttributesA(target, attributes & ~FILE_ATTRIBUTE_READONLY);
    }
}
/* Optional, fixed package members: omission preserves a previously installed
 * translator, while every supplied file must copy successfully. */
static BOOL OptionalFile(const char *name, const char *prefix) {
    char target[MAX_PATH];
    DWORD i, n = Length(prefix), d = Length(Directory), count = Length(name);
    if (d + count >= MAX_PATH || n + count >= MAX_PATH)
        return FALSE;
    for (i = 0; i < n; i++)
        target[i] = prefix[i];
    for (i = 0; i < d; i++)
        Source[i] = Directory[i];
    for (i = 0; i <= count; i++) {
        Source[d + i] = name[i];
        target[n + i] = name[i];
    }
    if (GetFileAttributesA(Source) == INVALID_FILE_ATTRIBUTES) {
        DWORD error = GetLastError();
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
    }
    return Copy(name, target);
}
/* Check every mandatory member while the old control runner is still alive.
 * Opening and reading the DOS header detects missing/unreadable/wrong media
 * before stopping control; later copy failures remain explicit. */
static BOOL RequiredFile(const char *name) {
    DWORD i, n = Length(Directory), count = Length(name), read = 0, error = 0;
    BYTE header[2];
    HANDLE file;
    if (n + count >= MAX_PATH) {
        SetLastError(ERROR_FILENAME_EXCED_RANGE);
        return FALSE;
    }
    for (i = 0; i < n; i++)
        Source[i] = Directory[i];
    for (i = 0; i <= count; i++)
        Source[n + i] = name[i];
    file = CreateFileA(Source, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;
    if (!ReadFile(file, header, sizeof(header), &read, NULL))
        error = GetLastError();
    else if (read != sizeof(header) || header[0] != 'M' || header[1] != 'Z')
        error = ERROR_BAD_EXE_FORMAT;
    CloseHandle(file);
    if (error) {
        SetLastError(error);
        return FALSE;
    }
    return TRUE;
}
void WINAPI WinMainCRTStartup(void) {
    DWORD length, i, error;
    STARTUPINFOA startup = {};
    PROCESS_INFORMATION process = {};
    char command[] = "C:\\DGPUBEN.EXE";
    Log = CreateFileA("C:\\DGSETUP.LOG", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0,
                      NULL);
    if (Log == INVALID_HANDLE_VALUE)
        ExitProcess(1);
    length = GetModuleFileNameA(NULL, Directory, sizeof(Directory));
    if (!length || length >= sizeof(Directory)) {
        Record("MODULE", GetLastError());
        ExitProcess(1);
    }
    for (i = length; i && Directory[i - 1] != '\\'; i--) {
    }
    Directory[i] = 0;
    if (!RequiredFile("dgpugl.dll") || !RequiredFile("DGWGL.EXE") || !RequiredFile("DGPUBEN.EXE")) {
        Record("PREFLIGHT_MEDIA", GetLastError());
        ExitProcess(1);
    }
    if (!StopRunner()) {
        error = GetLastError();
        Record("STOP_RUNNER", error);
        ExitProcess(1);
    }
    if (!Copy("dgpugl.dll", "C:\\SIERRA\\Half-Life\\dgpugl.dll") ||
        !Copy("DGWGL.EXE", "C:\\SIERRA\\Half-Life\\DGWGL.EXE") ||
        !Copy("DGPUBEN.EXE", "C:\\DGPUBEN.EXE")) {
        error = GetLastError();
        Record("COPY", error);
        ExitProcess(1);
    }
    {
        static const char *files[] = {"opengl32.dll", "wined3d.dll", "wined8.dll",  "wined9.dll",
                                      "winedd.dll",   "DGHLDBG.EXE", "DGDUAL.EXE",  "DGLOOP.EXE",
                                      "DGWIN.EXE",    "DGMODE.EXE",  "DGWGL9.EXE",  "DG9AUDIT.EXE",
                                      "DGD3D6.EXE",   "DGD3D7.EXE",  "DGD3D8.EXE",  "DGD3D9.EXE",
                                      "glide2x.dll",  "DGGLIDE.EXE", "DGUTSET.EXE", "DGUT.EXE",
                                      "DGUTLOG.EXE",  "DGUTD3.EXE",  "DGUTDS.EXE"};
        for (i = 0; i < sizeof(files) / sizeof(files[0]); i++)
            if (!OptionalFile(files[i], "C:\\SIERRA\\Half-Life\\")) {
                Record(files[i], GetLastError());
                ExitProcess(1);
            }
    }
    if (!OptionalFile("DGPUPROB.EXE", "C:\\")) {
        Record("DGPUPROB.EXE", GetLastError());
        ExitProcess(1);
    }
    startup.cb = sizeof(startup);
    if (!CreateProcessA(command, command, NULL, NULL, FALSE, 0, NULL, "C:\\", &startup, &process)) {
        error = GetLastError();
        Record("START_RUNNER", error);
        ExitProcess(1);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    Record("INSTALLED", 0);
    CloseHandle(Log);
    ExitProcess(0);
}

} /* extern C */
