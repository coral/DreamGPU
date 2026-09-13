/* Fixed existing UT/Glide evidence dump. Does not launch or modify the game. */
#include "common.h"
static BYTE Raw[16384];
static char Converted[16385];
static void Evidence(const char *path, DWORD limit) {
    OwnedHandle file;
    DWORD size, bytes = 0, i, n = 0;
    BYTE bom[2];
    BOOL wide;
    Text("FILE_BEGIN ");
    Text(path);
    Text("\r\n");
    file.reset(CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_EXISTING, 0, NULL));
    if (!file) {
        Record("FILE_UNAVAILABLE", GetLastError());
        return;
    }
    size = GetFileSize(file.get(), NULL);
    if (size == INVALID_FILE_SIZE) {
        Record("FILE_SIZE", GetLastError());
        file.reset();
        return;
    }
    ReadFile(file.get(), bom, 2, &bytes, NULL);
    wide = bytes == 2 && bom[0] == 255 && bom[1] == 254;
    SetFilePointer(file.get(), size > limit ? size - limit : 0, NULL, FILE_BEGIN);
    bytes = 0;
    ReadFile(file.get(), Raw, limit, &bytes, NULL);
    file.reset();
    for (i = wide ? (size > limit ? 0 : 2) : 0; i < bytes; i += wide ? 2 : 1) {
        BYTE c = Raw[i];
        if (wide && i + 1 >= bytes)
            break;
        Converted[n++] =
            (!wide || !Raw[i + 1]) && (c == '\r' || c == '\n' || c == '\t' || (c >= 32 && c < 127))
                ? (char)c
                : '?';
    }
    Converted[n] = 0;
    /* Keep diagnostic FAIL lines distinct from the helper's protocol result. */
    for (i = 0; i < n;) {
        DWORD end = i;
        char saved;
        while (end < n && Converted[end] != '\n')
            end++;
        saved = Converted[end];
        Converted[end] = 0;
        Text("| ");
        Text(Converted + i);
        Text("\n");
        Converted[end] = saved;
        i = end + (saved ? 1 : 0);
    }
    Text("\r\nFILE_END\r\n");
}
static UINT Run(void) {
    OwnedHandle log{CreateFileA("C:\\DGUTLOG.LOG", GENERIC_WRITE, FILE_SHARE_READ, NULL,
                                CREATE_ALWAYS, 0, NULL)};
    Log = log.get();
    if (Log == INVALID_HANDLE_VALUE)
        ExitProcess(1);
    Evidence("C:\\DGUTENG.LOG", 8192);
    Evidence("C:\\UT99\\System\\OpenGLid.log", 12288);
    Evidence("C:\\UT99\\System\\OpenGLid.err", 8192);
    Evidence("C:\\UT99\\System\\OpenGLid.ini", 4096);
    Evidence("C:\\SIERRA\\Half-Life\\OpenGLid.log", 12288);
    Evidence("C:\\SIERRA\\Half-Life\\OpenGLid.err", 8192);
    Text("PASS automated utlogs: bounded existing fixture logs; no game execution\r\n");
    return 0;
}

extern "C" void WINAPI WinMainCRTStartup(void) {
    ExitProcess(Run());
}
