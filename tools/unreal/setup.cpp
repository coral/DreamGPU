/* Fixed original UT99 media installation; no shell, networking or menu automation. */
#include "common.h"
#include "system-provider.h"
static DWORD Started, Files, Bytes, Skipped;
static char MediaRoot[4], MediaGame[MAX_PATH];
static BOOL Deadline(void) {
    if (GetTickCount() - Started < 480000)
        return TRUE;
    SetLastError(ERROR_TIMEOUT);
    return FALSE;
}
static void Directory(const char *path) {
    if (!CreateDirectoryA(path, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        Die("FAIL DIRECTORY");
}
static BOOL SameSize(const WIN32_FIND_DATAA *source, const char *target) {
    WIN32_FILE_ATTRIBUTE_DATA data;
    return GetFileAttributesExA(target, GetFileExInfoStandard, &data) &&
           !(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
           source->nFileSizeHigh == data.nFileSizeHigh && source->nFileSizeLow == data.nFileSizeLow;
}
static void MediaDirectory(const char *name) {
    char sourceDir[MAX_PATH], targetDir[MAX_PATH], pattern[MAX_PATH], source[MAX_PATH],
        target[MAX_PATH];
    WIN32_FIND_DATAA file;
    FindHandle find;
    DWORD error;
    if (!Join(sourceDir, MediaGame, name) || !Join(targetDir, "C:\\UT99", name) ||
        !Join(pattern, sourceDir, "*"))
        Die("FAIL PATH");
    Directory(targetDir);
    find.reset(FindFirstFileA(pattern, &file));
    if (!find)
        Die(name);
    do {
        if ((file.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || UtProviderName(file.cFileName))
            continue;
        if (!Deadline()) {
            find.reset();
            Die("FAIL COPY_TIMEOUT");
        }
        if (!Join(source, sourceDir, file.cFileName) || !Join(target, targetDir, file.cFileName)) {
            find.reset();
            Die("FAIL PATH");
        }
        if (SameSize(&file, target))
            ++Skipped;
        else {
            if (!Copy(source, target)) {
                error = GetLastError();
                Record(file.cFileName, error);
                find.reset();
                SetLastError(error);
                Die("FAIL COPY");
            }
            ++Files;
            Bytes += file.nFileSizeLow;
        }
        if ((Files + Skipped) % 16 == 0) {
            Record(name, Files + Skipped);
            Record("COPY_BYTES", Bytes);
        }
    } while (FindNextFileA(find.get(), &file));
    error = GetLastError();
    find.reset();
    if (error != ERROR_NO_MORE_FILES) {
        SetLastError(error);
        Die("FAIL ENUMERATE");
    }
}
static void UccEvidence(void) {
    static BYTE raw[8192];
    static char text[8193];
    DWORD size, bytes = 0, i, n = 0;
    BOOL wide = FALSE;
    BYTE bom[2];
    OwnedHandle file{CreateFileA("C:\\DGUCC.LOG", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 NULL, OPEN_EXISTING, 0, NULL)};
    if (!file)
        return;
    size = GetFileSize(file.get(), NULL);
    if (size == INVALID_FILE_SIZE) {
        file.reset();
        return;
    }
    ReadFile(file.get(), bom, 2, &bytes, NULL);
    wide = bytes == 2 && bom[0] == 255 && bom[1] == 254;
    SetFilePointer(file.get(), size > sizeof(raw) ? size - sizeof(raw) : 0, NULL, FILE_BEGIN);
    bytes = 0;
    ReadFile(file.get(), raw, sizeof(raw), &bytes, NULL);
    file.reset();
    for (i = wide ? (size > sizeof(raw) ? 0 : 2) : 0; i < bytes; i += wide ? 2 : 1) {
        BYTE c = raw[i];
        if (wide && i + 1 >= bytes)
            break;
        text[n++] =
            (!wide || !raw[i + 1]) && (c == '\r' || c == '\n' || c == '\t' || (c >= 32 && c < 127))
                ? (char)c
                : '?';
    }
    text[n] = 0;
    Text("UCC_LOG_BEGIN\r\n");
    Text(text);
    Text("\r\nUCC_LOG_END\r\n");
}
static BOOL Decompress(const char *name) {
    STARTUPINFOA startup = {};
    PROCESS_INFORMATION process = {};
    DWORD result, exitCode, i, n;
    char mapName[MAX_PATH], map[MAX_PATH], systemMap[MAX_PATH], source[MAX_PATH], command[640];
    const char *prefix = "\"C:\\UT99\\System\\UCC.exe\" decompress \"";
    const char *suffix = "\" ABSLOG=C:\\DGUCC.LOG";
    n = Length(name);
    if (n < 7 || n >= MAX_PATH || !Equal(name + n - 7, ".unr.uz")) {
        SetLastError(ERROR_INVALID_NAME);
        return FALSE;
    }
    for (i = 0; i < n - 3; i++) {
        mapName[i] = name[i];
    }
    mapName[n - 3] = 0;
    if (!Join(map, "C:\\UT99\\Maps", mapName) || !Join(systemMap, "C:\\UT99\\System", mapName) ||
        !Join(source, "C:\\UT99\\Maps", name)) {
        SetLastError(ERROR_INVALID_NAME);
        return FALSE;
    }
    n = 0;
    for (i = 0; prefix[i]; i++)
        command[n++] = prefix[i];
    for (i = 0; source[i]; i++)
        command[n++] = source[i];
    for (i = 0; suffix[i]; i++) {
        command[n++] = suffix[i];
    }
    command[n] = 0;
    /* UCC writes the decompressed basename in its current directory. A previous
     * completed setup may already have it; verify its real Unreal package tag. */
    OwnedHandle file{CreateFileA(map, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL)};
    DWORD tag = 0, read = 0;
    if (file) {
        ReadFile(file.get(), &tag, 4, &read, NULL);
        file.reset();
        if (read == 4 && tag == 0x9e2a83c1)
            return TRUE;
    }
    if (!Deadline()) {
        return FALSE;
    }
    DeleteFileA("C:\\DGUCC.LOG");
    startup.cb = sizeof(startup);
    Record("UCC_START", 0);
    if (!CreateProcessA("C:\\UT99\\System\\UCC.exe", command, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                        NULL, "C:\\UT99\\Maps", &startup, &process))
        return FALSE;
    OwnedHandle child{process.hProcess};
    OwnedHandle thread{process.hThread};
    thread.reset();
    {
        DWORD elapsed = GetTickCount() - Started, left = elapsed < 480000 ? 480000 - elapsed : 0;
        result = WaitForSingleObject(child.get(), left < 120000 ? left : 120000);
    }
    if (result != WAIT_OBJECT_0) {
        DWORD error = result == WAIT_FAILED ? GetLastError() : ERROR_TIMEOUT;
        TerminateProcess(child.get(), 1);
        WaitForSingleObject(child.get(), 3000);
        child.reset();
        SetLastError(error);
        return FALSE;
    }
    if (!GetExitCodeProcess(child.get(), &exitCode))
        return FALSE;
    child.reset();
    Record("UCC_EXIT", exitCode);
    if (exitCode) {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    file.reset(CreateFileA(map, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL));
    if (!file) {
        if (!Copy(systemMap, map))
            return FALSE;
        file.reset(CreateFileA(map, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL));
        if (!file)
            return FALSE;
    }
    tag = read = 0;
    ReadFile(file.get(), &tag, 4, &read, NULL);
    file.reset();
    if (read != 4 || tag != 0x9e2a83c1) {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    return TRUE;
}
static BOOL AllMaps(void) {
    WIN32_FIND_DATAA map;
    DWORD error;
    FindHandle find{FindFirstFileA("C:\\UT99\\Maps\\*.unr.uz", &map)};
    if (!find)
        return FALSE;
    do {
        if (map.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        if (!Decompress(map.cFileName)) {
            error = GetLastError();
            Record(map.cFileName, error);
            find.reset();
            SetLastError(error);
            return FALSE;
        }
    } while (FindNextFileA(find.get(), &map));
    error = GetLastError();
    find.reset();
    if (error != ERROR_NO_MORE_FILES) {
        SetLastError(error);
        return FALSE;
    }
    return TRUE;
}
static UINT Run(void) {
    static const char *directories[] = {"System", "Textures", "Sounds", "Music", "Maps", "Help"};
    DWORD i;
    char config[MAX_PATH];
    OwnedHandle log{CreateFileA("C:\\DGUTSET.LOG", GENERIC_WRITE, FILE_SHARE_READ, NULL,
                                CREATE_ALWAYS, 0, NULL)};
    Log = log.get();
    if (Log == INVALID_HANDLE_VALUE) {
        ExitProcess(1);
    }
    Started = GetTickCount();
    Record("UT_SETUP_BEGIN", 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    if (!PackageMedia(MediaRoot, "UT99\\DGUT.INI") || !Join(MediaGame, MediaRoot, "UT99") ||
        !Join(config, MediaGame, "DGUT.INI"))
        Die("FAIL PACKAGE_MEDIA");
    Text("PACKAGE_DRIVE ");
    Text(MediaRoot);
    Text("\r\n");
    Directory("C:\\UT99");
    Directory("C:\\UT99\\Save");
    Directory("C:\\UT99\\Cache");
    for (i = 0; i < sizeof(directories) / sizeof(directories[0]); i++)
        MediaDirectory(directories[i]);
    /* Only original game resources and renderer preferences are installed.
     * Public runtimes belong to the system installer. */
    if (!UtCleanDirectory("C:\\UT99\\System"))
        Die("FAIL APP_LOCAL_PROVIDER");
    if (!Copy(config, "C:\\UT99\\System\\DGUT.INI") ||
        !Copy("C:\\UT99\\System\\DefUser.ini", "C:\\UT99\\System\\DGUSER.INI"))
        Die("FAIL CONFIG");
    if (!AllMaps()) {
        DWORD error = GetLastError();
        UccEvidence();
        SetLastError(error);
        Die("FAIL DECOMPRESS");
    }
    Record("COPIED_FILES", Files);
    Record("SKIPPED_SAME_SIZE", Skipped);
    Record("COPY_BYTES", Bytes);
    Record("ELAPSED_MS", GetTickCount() - Started);
    Text("PASS automated utsetup: original CityIntro, normal system Glide and "
         "guest-fullscreen640x480 config\r\n");
    return 0;
}

extern "C" void WINAPI WinMainCRTStartup(void) {
    ExitProcess(Run());
}
