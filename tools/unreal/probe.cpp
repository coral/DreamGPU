/* Fixed UT99 CityIntro Glide run. Logs and module ownership establish readiness;
 * presentation FPS is measured by the host trace, never invented here. */
#include "common.h"
#include <tlhelp32.h>
#include "focus.h"
#include "focus-input.h"
#ifdef DG_UT_D3D
#define PROBE_NAME "utd3d"
#define PROBE_LOG "C:\\DGUTD3.LOG"
#define ENGINE_LOG "C:\\DGUTD3E.LOG"
#define GAME_INI "DGD3D.INI"
#define RENDERER_READY "Direct3D"
#define PROVIDER_NAME "dgddr.dll"
#define PROVIDER_PATH "C:\\UT99\\System\\dgddr.dll"
#else
#define PROBE_NAME "utglide"
#define PROBE_LOG "C:\\DGUT.LOG"
#define ENGINE_LOG "C:\\DGUTENG.LOG"
#define GAME_INI "DGUT.INI"
#define RENDERER_READY "Glide info:"
#define PROVIDER_NAME "glide2x.dll"
#define PROVIDER_PATH "C:\\UT99\\System\\glide2x.dll"
#endif
static char EngineLog[65537];
static BYTE RawLog[65536];
static DWORD GamePid;
static BOOL Contains(const char *text, const char *word) {
    DWORD i, j;
    for (i = 0; text[i]; i++) {
        for (j = 0; word[j] && text[i + j]; j++) {
            char a = text[i + j], b = word[j];
            if (a >= 'A' && a <= 'Z')
                a += 32;
            if (b >= 'A' && b <= 'Z')
                b += 32;
            if (a != b)
                break;
        }
        if (!word[j])
            return TRUE;
    }
    return FALSE;
}
static DWORD ReadEngine(void) {
    OwnedHandle file{CreateFileA(ENGINE_LOG, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                                 OPEN_EXISTING, 0, NULL)};
    DWORD length, bytes = 0, i, n = 0;
    BOOL wide = FALSE;
    BYTE first[2];
    EngineLog[0] = 0;
    if (!file)
        return 0;
    length = GetFileSize(file.get(), NULL);
    if (length == INVALID_FILE_SIZE) {
        file.reset();
        return 0;
    }
    ReadFile(file.get(), first, 2, &bytes, NULL);
    wide = bytes == 2 && first[0] == 0xff && first[1] == 0xfe;
    SetFilePointer(file.get(), length > sizeof(RawLog) ? length - sizeof(RawLog) : 0, NULL,
                   FILE_BEGIN);
    bytes = 0;
    ReadFile(file.get(), RawLog, sizeof(RawLog), &bytes, NULL);
    file.reset();
    for (i = wide ? ((length > sizeof(RawLog)) ? 0 : 2) : 0; i < bytes; i += wide ? 2 : 1) {
        BYTE c = RawLog[i];
        if (wide && i + 1 >= bytes)
            break;
        EngineLog[n++] = (!wide || !RawLog[i + 1]) &&
                                 (c == '\r' || c == '\n' || c == '\t' || (c >= 32 && c < 127))
                             ? (char)c
                             : '?';
    }
    EngineLog[n] = 0;
    return n;
}

/* Retail UT's file writer caches 4096 bytes even for FILEWRITE_Unbuffered.
 * The original -log window receives GLogHook synchronously. Read only the
 * owned WLog edit control; no screenshots, console typing or injected code. */
/* Win9x SendMessageTimeout wParam is 16-bit; 65537 becomes one and
 * silently returns an empty edit buffer. Keep this cross-OS read below 32K. */
static char WindowLog[32768];
static_assert(sizeof(WindowLog) <= 0xffff, "Win9x message capacity");
static HWND LogWindow;
static BOOL MapLoaded, EngineInitialized, RendererInitialized;
#ifdef DG_UT_D3D
static BOOL PrecacheComplete;
#endif
static BOOL CALLBACK ReadLogEdit(HWND window, LPARAM ignored) {
    char name[64];
    DWORD pid;
    DWORD_PTR bytes = 0;
    (void)ignored;
    GetWindowThreadProcessId(window, &pid);
    if (pid != GamePid || !GetClassNameA(window, name, sizeof(name)) ||
        !Contains(name, "WEditTerminal"))
        return TRUE;
    if (SendMessageTimeoutA(window, WM_GETTEXT, sizeof(WindowLog), (LPARAM)WindowLog,
                            SMTO_ABORTIFHUNG | SMTO_BLOCK, 100, &bytes) &&
        bytes) {
        WindowLog[sizeof(WindowLog) - 1] = 0;
        if (Contains(WindowLog, "Bringing Level CityIntro."))
            MapLoaded = TRUE;
        if (Contains(WindowLog, "Game engine initialized"))
            EngineInitialized = TRUE;
        if (Contains(WindowLog, RENDERER_READY))
            RendererInitialized = TRUE;
#ifdef DG_UT_D3D
        if (Contains(WindowLog, "D3D Driver: Preloaded "))
            PrecacheComplete = TRUE;
#endif
    }
    return TRUE;
}
static BOOL CALLBACK ReadLogWindow(HWND window, LPARAM ignored) {
    char name[128];
    DWORD pid;
    (void)ignored;
    GetWindowThreadProcessId(window, &pid);
    if (pid != GamePid || !GetClassNameA(window, name, sizeof(name)) || !Contains(name, "WLog"))
        return TRUE;
    LogWindow = window;
    EnumChildWindows(window, ReadLogEdit, 0);
    return TRUE;
}
#ifndef DG_UT_D3D
#include "present-status.h"
static char GlideLog[16385];
static BOOL LatestContextPresented(void) {
    OwnedHandle file{CreateFileA("C:\\UT99\\System\\OpenGLid.log", GENERIC_READ,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL)};
    DWORD bytes = 0, i;
    BOOL present = FALSE;
    if (!file)
        return FALSE;
    ReadFile(file.get(), GlideLog, sizeof(GlideLog) - 1, &bytes, NULL);
    file.reset();
    GlideLog[bytes] = 0;
    for (i = 0; i < bytes;) {
        DWORD end = i;
        char saved;
        while (GlideLog[end] && GlideLog[end] != '\n')
            end++;
        saved = GlideLog[end];
        GlideLog[end] = 0;
        if (Contains(GlideLog + i, "DG_GLIDE_OPEN_COMPLETE"))
            present = FALSE;
        if (Contains(GlideLog + i, "DG_FIRST_PRESENT_ACCEPTED"))
            present = TRUE;
        GlideLog[end] = saved;
        i = end + (saved ? 1 : 0);
    }
    return present;
}
#endif

static BOOL Providers(DWORD pid) {
    OwnedHandle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid)};
    MODULEENTRY32 module;
    BOOL found, gl = FALSE, glide = FALSE;
#ifdef DG_UT_D3D
    BOOL wine = FALSE;
#endif
    if (!snapshot) {
        return FALSE;
    }
    module.dwSize = sizeof(module);
    found = Module32First(snapshot.get(), &module);
    while (found) {
        if (Equal(module.szModule, "dgpugl.dll"))
            gl = Equal(module.szExePath, "C:\\UT99\\System\\dgpugl.dll");
        if (Equal(module.szModule, PROVIDER_NAME))
            glide = Equal(module.szExePath, PROVIDER_PATH);
#ifdef DG_UT_D3D
        if (Equal(module.szModule, "wined3d.dll"))
            wine = Equal(module.szExePath, "C:\\UT99\\System\\wined3d.dll");
#endif
        found = Module32Next(snapshot.get(), &module);
    }
    snapshot.reset();
#ifdef DG_UT_D3D
    return gl && glide && wine;
#else
    return gl && glide;
#endif
}

/* Foreground ownership matters: the stock engine can throttle background
 * viewports. Select only this process's actual viewport, never another app. */
/* Optional runner-owned phase events delimit measurement without polling files. */
static void SignalMeasurement(const char *name) {
    OwnedHandle event{OpenEventA(EVENT_MODIFY_STATE, FALSE, name)};
    if (event) {
        SetEvent(event.get());
        event.reset();
    }
}
static HWND GameViewport;
static DWORD ViewportCount;
static BOOL CALLBACK FindViewport(HWND window, LPARAM ignored) {
    DWORD pid;
    char name[128];
    (void)ignored;
    GetWindowThreadProcessId(window, &pid);
    if (pid == GamePid && IsWindowVisible(window) && GetClassNameA(window, name, sizeof(name)) &&
        Contains(name, "WWindowsViewportWindow")) {
        GameViewport = window;
        ViewportCount++;
    }
    return TRUE;
}
static BOOL RecordForeground(const char *stage) {
    HWND window = GetForegroundWindow();
    DWORD pid = 0;
    char text[256];
    GetWindowThreadProcessId(window, &pid);
    Record(stage, pid);
    /* Only an executable basename, never arbitrary window text or user paths.
     * This distinguishes our supervisor from a shell/modal focus blocker. */
    if (pid && pid != GamePid) {
        OwnedHandle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};
        PROCESSENTRY32 entry;
        DWORD count = 0;
        BOOL found;
        entry.dwSize = sizeof(entry);
        found = snapshot && Process32First(snapshot.get(), &entry);
        while (found && count++ < 4096) {
            if (entry.th32ProcessID == pid) {
                const char *name = entry.szExeFile, *p = name;
                entry.szExeFile[sizeof(entry.szExeFile) - 1] = 0;
                while (*p) {
                    if (*p == '\\' || *p == '/')
                        name = p + 1;
                    p++;
                }
                Text("FOREGROUND_EXECUTABLE ");
                Text(name);
                Text("\r\n");
                break;
            }
            found = Process32Next(snapshot.get(), &entry);
        }
        if (snapshot)
            snapshot.reset();
    }
    if (pid == GamePid) {
        if (GetClassNameA(window, text, sizeof(text))) {
            Text("FOREGROUND_CLASS ");
            Text(text);
            Text("\r\n");
        }
        if (GetWindowTextA(window, text, sizeof(text))) {
            Text("FOREGROUND_TITLE ");
            Text(text);
            Text("\r\n");
        }
    }
    return pid == GamePid && window == GameViewport;
}
static BOOL FocusViewport(void) {
    GameViewport = NULL;
    ViewportCount = 0;
    EnumWindows(FindViewport, 0);
    Record("OWNED_VIEWPORT_COUNT", ViewportCount);
    if (ViewportCount != 1)
        return FALSE;
    RecordForeground("FOREGROUND_BEFORE_FOCUS_PID");
    if (!ActivateOwnedViewport(GameViewport, GamePid)) {
        RecordForeground("FOREGROUND_ACTIVATION_FAILED_PID");
        return FALSE;
    }
    /* Activate first: hiding the active log before the viewport has accepted
     * activation can give foreground ownership to an unrelated application. */
    if (LogWindow)
        ShowWindowAsync(LogWindow, SW_HIDE);
    return RecordForeground("FOREGROUND_MEASURE_PID");
}

static BOOL CALLBACK CloseGameWindow(HWND window, LPARAM ignored) {
    DWORD pid;
    (void)ignored;
    GetWindowThreadProcessId(window, &pid);
    if (pid == GamePid)
        PostMessageA(window, WM_CLOSE, 0, 0);
    return TRUE;
}
/* Detect owned startup errors without interacting with the desktop. The retail
 * engine buffers its log until shutdown, so a modal error otherwise wastes the
 * entire readiness deadline before its exact cause becomes readable. */
static BOOL CALLBACK ErrorWindow(HWND window, LPARAM result) {
    DWORD pid;
    char title[256];
    GetWindowThreadProcessId(window, &pid);
    if (pid != GamePid || !IsWindowVisible(window))
        return TRUE;
    GetWindowTextA(window, title, sizeof(title));
    if (Contains(title, "Critical Error") || Contains(title, "General protection fault")) {
        Text("OWNED_ERROR_WINDOW ");
        Text(title);
        Text("\r\n");
        *(BOOL *)result = TRUE;
    }
    return TRUE;
}
static BOOL Cleanup(HANDLE process) {
    DWORD result = WaitForSingleObject(process, 0);
    if (result == WAIT_OBJECT_0)
        return TRUE;
    EnumWindows(CloseGameWindow, 0);
    result = WaitForSingleObject(process, 2000);
    if (result == WAIT_OBJECT_0) {
        Record("CLEANUP_GRACEFUL", 0);
        return TRUE;
    }
    if (!TerminateProcess(process, 0))
        return FALSE;
    result = WaitForSingleObject(process, 3000);
    Record("CLEANUP_OWNED_TERMINATE", result);
    return result == WAIT_OBJECT_0;
}
static void Evidence(void) {
    DWORD bytes = ReadEngine();
    Text("ENGINE_LOG_BEGIN\r\n");
    if (bytes > 40000)
        Text(EngineLog + bytes - 40000);
    else
        Text(EngineLog);
    Text("\r\nENGINE_LOG_END\r\n");
#ifndef DG_UT_D3D
    Text("OWNED_ENGINE_WINDOW_LOG_BEGIN\r\n");
    Text(WindowLog);
    Text("\r\nOWNED_ENGINE_WINDOW_LOG_END\r\n");
#endif
#ifdef DG_UT_D3D
    {
        OwnedHandle file{CreateFileA("C:\\DGDDRAW.LOG", GENERIC_READ,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0,
                                     NULL)};
        DWORD length = 0;
        Text("DDRAW_FAILURE_LOG_BEGIN\r\n");
        if (file) {
            ReadFile(file.get(), EngineLog, 16384, &length, NULL);
            file.reset();
            EngineLog[length] = 0;
            Text(EngineLog);
        }
        Text("\r\nDDRAW_FAILURE_LOG_END\r\n");
        file.reset(CreateFileA("C:\\DGWCOPY.LOG", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               NULL, OPEN_EXISTING, 0, NULL));
        length = 0;
        Text("WINE_COPY_LOG_BEGIN\r\n");
        if (file) {
            ReadFile(file.get(), EngineLog, 24576, &length, NULL);
            file.reset();
            EngineLog[length] = 0;
            Text(EngineLog);
        }
        Text("\r\nWINE_COPY_LOG_END\r\n");
    }
#endif
}
static UINT Run(void) {
    STARTUPINFOA startup = {};
    PROCESS_INFORMATION process = {};
    DWORD started, readyAt = 0, result, code = 0;
    BOOL ready = FALSE, passed = FALSE, clean;
    OwnedInputWindow input;
    char command[] = "\"C:\\UT99\\System\\UnrealTournament.exe\" CityIntro.unr INI=" GAME_INI
                     " USERINI=DGUSER.INI ABSLOG=" ENGINE_LOG " -nosound -noframecap"
                     " -log";
    OwnedHandle log{
        CreateFileA(PROBE_LOG, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL)};
    Log = log.get();
    if (Log == INVALID_HANDLE_VALUE) {
        ExitProcess(1);
    }
    Text("UT_BEGIN " PROBE_NAME "\r\n");
    Record("HELPER_PID", GetCurrentProcessId());
    Record("HELPER_THREAD", GetCurrentThreadId());
#ifdef DG_UT_D3D
    DeleteFileA("C:\\DGDDRAW.LOG");
    if (!DeleteFileA("C:\\DGWCOPY.LOG") && GetLastError() != ERROR_FILE_NOT_FOUND)
        Die("FAIL STALE_WINE_COPY_LOG");
#endif
#ifndef DG_UT_D3D
    if (!DeleteFileA("C:\\UT99\\System\\OpenGLid.log") && GetLastError() != ERROR_FILE_NOT_FOUND)
        Die("FAIL STALE_GLIDE_LOG");
    if (!DeleteFileA("C:\\UT99\\System\\OpenGLid.err") && GetLastError() != ERROR_FILE_NOT_FOUND)
        Die("FAIL STALE_GLIDE_ERROR");
#endif
    if (!DeleteFileA(ENGINE_LOG) && GetLastError() != ERROR_FILE_NOT_FOUND)
        Die("FAIL STALE_ENGINE_LOG");
    /* Retail crash recovery uses this fixture-local marker. The preceding
     * probe owns and finishes its process before another fixed launch; remove
     * the stale marker so a handled failure cannot send later runs to a wizard. */
    if (DeleteFileA("C:\\UT99\\System\\Running.ini"))
        Record("REMOVED_STALE_RUNNING_MARKER", 1);
    else if (GetLastError() != ERROR_FILE_NOT_FOUND)
        Die("FAIL RUNNING_MARKER");
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    startup.cb = sizeof(startup);
    started = GetTickCount();
    /* Acquire before launching: fullscreen Glide captures mouse input once
     * initialized. The process starts from our real foreground window and
     * receives an exact PID grant while that ownership is still valid. */
    if (!input.acquire())
        Die("FAIL OWNED_INPUT_HANDOFF");
    if (!CreateProcessA("C:\\UT99\\System\\UnrealTournament.exe", command, NULL, NULL, FALSE,
                        CREATE_SUSPENDED, NULL, "C:\\UT99\\System", &startup, &process)) {
        DWORD error = GetLastError();
        input.reset();
        SetLastError(error);
        Die("FAIL CREATE_GAME");
    }
    OwnedHandle child{process.hProcess};
    OwnedHandle thread{process.hThread};
    GamePid = process.dwProcessId;
    Record("GAME_PID", GamePid);
    GrantOwnedForeground(GamePid);
    if (ResumeThread(thread.get()) == (DWORD)-1) {
        DWORD error = GetLastError();
        TerminateProcess(child.get(), 1);
        WaitForSingleObject(child.get(), 3000);
        thread.reset();
        child.reset();
        input.reset();
        SetLastError(error);
        Die("FAIL RESUME_GAME");
    }
    thread.reset();
    while (GetTickCount() - started < 50000) {
        if (WaitForSingleObject(child.get(), 250) == WAIT_OBJECT_0) {
            GetExitCodeProcess(child.get(), &code);
            Record("FAIL EARLY_EXIT", code);
            break;
        }
        {
            MSG message;
            unsigned count;
            for (count = 0; count < 32 && PeekMessageA(&message, NULL, 0, 0, PM_REMOVE); count++) {
                TranslateMessage(&message);
                DispatchMessageA(&message);
            }
        }
        {
            BOOL failed = FALSE;
            EnumWindows(ErrorWindow, (LPARAM)&failed);
            if (failed) {
                Record("FAIL ENGINE_ERROR_WINDOW", 0);
                break;
            }
        }
        ReadEngine();
        if (Contains(EngineLog, "Critical:") || Contains(EngineLog, "Critical error")) {
            Record("FAIL ENGINE_CRITICAL", 0);
            break;
        }
        EnumWindows(ReadLogWindow, 0);
        if (Contains(EngineLog, "Bringing Level CityIntro."))
            MapLoaded = TRUE;
        if (Contains(EngineLog, "Game engine initialized"))
            EngineInitialized = TRUE;
        if (Contains(EngineLog, RENDERER_READY))
            RendererInitialized = TRUE;
#ifdef DG_UT_D3D
        if (Contains(EngineLog, "D3D Driver: Preloaded "))
            PrecacheComplete = TRUE;
#endif
#ifndef DG_UT_D3D
        if (GlidePresentFailed()) {
            Text("GLIDE_PRESENT_ERROR_BEGIN\r\n");
            Text(GlidePresentError);
            Text("\r\nGLIDE_PRESENT_ERROR_END\r\n");
            Record("FAIL GLIDE_PRESENT", 0);
            break;
        }
        if (MapLoaded && EngineInitialized && LatestContextPresented()) {
#else
        if (MapLoaded && EngineInitialized && RendererInitialized && PrecacheComplete) {
#endif
            if (!Providers(GamePid)) {
                Record("FAIL PROVIDER_PATH", 0);
                break;
            }
            Record("CITYINTRO_LOADED", MapLoaded);
#ifdef DG_UT_D3D
            Record("D3D_PRECACHE_COMPLETE", PrecacheComplete);
#endif
#ifndef DG_UT_D3D
            Record("LATEST_CONTEXT_PRESENTED", 1);
#endif
            RecordForeground("FOREGROUND_BEFORE_LOG_HIDE_PID");
            if (!FocusViewport()) {
                Record("FAIL GAME_NOT_FOREGROUND", 0);
                break;
            }
            input.reset();
            ready = TRUE;
            readyAt = GetTickCount();
            Record("ENGINE_READY_MS", readyAt - started);
            SignalMeasurement("DreamGPUGameMeasuring");
            break;
        }
    }
    if (ready) {
        result = WaitForSingleObject(child.get(), 10000);
        if (result == WAIT_TIMEOUT) {
            ReadEngine();
            passed = !Contains(EngineLog, "Critical:") && !Contains(EngineLog, "Critical error");
        } else {
            GetExitCodeProcess(child.get(), &code);
            Record("FAIL MEASUREMENT_EXIT", code);
        }
        Record("MEASURED_MS", GetTickCount() - readyAt);
        SignalMeasurement("DreamGPUGameMeasured");
        {
            OwnedHandle capture{OpenEventA(SYNCHRONIZE, FALSE, "DreamGPUGameCaptureDone")};
            if (capture) {
                DWORD captured = WaitForSingleObject(capture.get(), 5000);
                capture.reset();
                Record("CAPTURE_ACK", captured);
                if (captured != WAIT_OBJECT_0)
                    passed = FALSE;
            }
        }
    } else {
        Record("READINESS_MAP", MapLoaded);
        Record("READINESS_ENGINE", EngineInitialized);
        Record("READINESS_RENDERER", RendererInitialized);
#ifdef DG_UT_D3D
        Record("READINESS_PRECACHE", PrecacheComplete);
#endif
        Record("FAIL ENGINE_NOT_READY", GetTickCount() - started);
    }
    clean = Cleanup(child.get());
    if (!clean)
        Record("FAIL CLEANUP", GetLastError());
    input.reset();
    child.reset();
    Evidence();
    if (passed && clean)
        Text("PASS automated " PROBE_NAME
             ": CityIntro engine-ready, app-local GPU providers,10-second game run, owned process "
             "cleanup\r\n");
    else
        Text("FAIL automated " PROBE_NAME "\r\n");
    return passed && clean ? 0 : 1;
}

extern "C" void WINAPI WinMainCRTStartup(void) {
    ExitProcess(Run());
}
