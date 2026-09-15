/* SPDX-License-Identifier: GPL-2.0-or-later
 * Retail Half-Life launcher/fullscreen reactivation diagnostic. Runs only a new
 * owned process, preserving normal launcher initialization. No memory patches,
 * menu-coordinate automation or termination of another process.
 * Stage as E:\DGDRV.EXE for the existing serial ntupdate diagnostic command.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

namespace {
HANDLE Log = INVALID_HANDLE_VALUE;
DWORD GamePid;
HWND Launcher;
HWND Engine;
DWORD Began;

void Text(const char *text) {
    DWORD written;
    WriteFile(Log, text, static_cast<DWORD>(lstrlenA(text)), &written, nullptr);
    FlushFileBuffers(Log);
}

void Value(const char *name, DWORD value) {
    char line[128];
    wsprintfA(line, "%s=%lu\r\n", name, value);
    Text(line);
}

DWORD Pid(HWND window) {
    DWORD result = 0;
    GetWindowThreadProcessId(window, &result);
    return result;
}

void Window(HWND window) {
    char title[160]{};
    char klass[100]{};
    char line[512];
    RECT rect{};
    GetWindowTextA(window, title, sizeof(title));
    GetClassNameA(window, klass, sizeof(klass));
    GetWindowRect(window, &rect);
    wsprintfA(line,
              "WINDOW hwnd=%08lx parent=%08lx owner=%08lx id=%lx visible=%d "
              "enabled=%d iconic=%d rect=%ld,%ld,%ld,%ld class=%s title=%s\r\n",
              reinterpret_cast<DWORD>(window), reinterpret_cast<DWORD>(GetParent(window)),
              reinterpret_cast<DWORD>(GetWindow(window, GW_OWNER)), GetDlgCtrlID(window),
              IsWindowVisible(window), IsWindowEnabled(window), IsIconic(window), rect.left,
              rect.top, rect.right, rect.bottom, klass, title);
    Text(line);
}

BOOL CALLBACK Child(HWND window, LPARAM) {
    if (Pid(window) == GamePid)
        Window(window);
    return TRUE;
}

BOOL CALLBACK Top(HWND window, LPARAM) {
    if (Pid(window) != GamePid)
        return TRUE;
    char klass[100]{};
    GetClassNameA(window, klass, sizeof(klass));
    if (!lstrcmpiA(klass, "Half-Life"))
        Engine = window;
    if (GetDlgItem(window, 0x3fb) || GetDlgItem(window, 0x48f))
        Launcher = window;
    Window(window);
    EnumChildWindows(window, Child, 0);
    return TRUE;
}

void Phase(const char *name) {
    Text("PHASE ");
    Text(name);
    Text("\r\n");
    Value("elapsed_ms", GetTickCount() - Began);
    Value("foreground", reinterpret_cast<DWORD>(GetForegroundWindow()));
    Value("foreground_pid", Pid(GetForegroundWindow()));
    DEVMODEA mode{};
    mode.dmSize = sizeof(mode);
    if (EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &mode)) {
        Value("width", mode.dmPelsWidth);
        Value("height", mode.dmPelsHeight);
        Value("bpp", mode.dmBitsPerPel);
    }
    EnumWindows(Top, 0);
}

void Stroke(BYTE key, bool release) {
    keybd_event(key, static_cast<BYTE>(MapVirtualKeyA(key, 0)), release ? KEYEVENTF_KEYUP : 0, 0);
}

void Key(BYTE key) {
    Stroke(key, false);
    Sleep(35);
    Stroke(key, true);
    Sleep(35);
}

bool Type(const char *text) {
    for (; *text; ++text) {
        if (GetForegroundWindow() != Engine || Pid(Engine) != GamePid)
            return false;
        const SHORT key = VkKeyScanA(*text);
        if (key == -1 || (HIBYTE(key) & ~1) != 0)
            return false;
        if (HIBYTE(key) & 1)
            Stroke(VK_SHIFT, false);
        Key(LOBYTE(key));
        if (HIBYTE(key) & 1)
            Stroke(VK_SHIFT, true);
    }
    return true;
}

#ifdef DG_FOCUS_CONTINUE
bool ExpectedPid() {
    HANDLE input = CreateFileA("E:\\DGFOCUS.PID", GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (input == INVALID_HANDLE_VALUE)
        return false;
    DWORD expected = 0, got = 0;
    const bool okay = GetFileSize(input, nullptr) == sizeof(expected) &&
                      ReadFile(input, &expected, sizeof(expected), &got, nullptr) &&
                      got == sizeof(expected) && expected != 0 && expected == GamePid;
    CloseHandle(input);
    return okay;
}
#endif

bool NotifyControl() {
    if (!Launcher || Pid(Launcher) != GamePid)
        return false;
    // These retail IDs share a handler, but use a real live control HWND.
    // A menu-form WM_COMMAND with lParam=0 does not exercise the same route.
    const int ids[] = {0x48f, 0x3fb};
    for (int id : ids) {
        HWND button = GetDlgItem(Launcher, id);
        if (!button || Pid(button) != GamePid || !IsWindowVisible(button) ||
            !IsWindowEnabled(button))
            continue;
        Window(button);
        Value("notify_control", static_cast<DWORD>(id));
        Value("notify_hwnd", reinterpret_cast<DWORD>(button));
        return PostMessageA(Launcher, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED),
                            reinterpret_cast<LPARAM>(button)) != FALSE;
    }
    Text("FAIL no visible enabled Console/Resume control\r\n");
    return false;
}

BOOL CALLBACK FindEngine(HWND window, LPARAM) {
    char klass[100]{};
    if (Pid(window) == GamePid && GetClassNameA(window, klass, sizeof(klass)) &&
        !lstrcmpiA(klass, "Half-Life"))
        Engine = window;
    return TRUE;
}

bool EngineActive(DWORD timeout) {
    const DWORD start = GetTickCount();
    do {
        if (Engine && IsWindowVisible(Engine) && !IsIconic(Engine) &&
            GetForegroundWindow() == Engine)
            return true;
        Sleep(100);
        // Engine HWND may be created or replaced after the control notification.
        EnumWindows(FindEngine, 0);
    } while (GetTickCount() - start < timeout);
    return false;
}

void AltTab() {
    Stroke(VK_MENU, false);
    Sleep(80);
    Key(VK_TAB);
    Sleep(80);
    Stroke(VK_MENU, true);
    Sleep(1500);
}

DWORD Run() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 11;
    PROCESSENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    unsigned existing = 0;
    if (Process32First(snapshot, &entry)) {
        do {
            if (!lstrcmpiA(entry.szExeFile, "hl.exe")) {
                ++existing;
                GamePid = entry.th32ProcessID;
            }
        } while (Process32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
#ifdef DG_FOCUS_CONTINUE
    // Explicit diagnostic continuation: preserve the already-owned normal
    // launcher/console process rather than replaying game startup after a
    // collector correction. This target must only be run at its console.
    if (existing != 1 || !ExpectedPid())
        return 12;
    HANDLE modules = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GamePid);
    if (modules == INVALID_HANDLE_VALUE)
        return 13;
    MODULEENTRY32 module{};
    module.dwSize = sizeof(module);
    const bool owned = Module32First(modules, &module) &&
                       !lstrcmpiA(module.szExePath, "C:\\SIERRA\\Half-Life\\hl.exe");
    CloseHandle(modules);
    if (!owned)
        return 14;
    Value("continued_game_pid", GamePid);
#ifdef DG_FOCUS_RETURN
    Phase("return_existing_game");
    if (!Launcher || Pid(Launcher) != GamePid)
        return 15;
    Value("restore_posted", PostMessageA(Launcher, WM_SYSCOMMAND, SC_RESTORE, 0));
    Sleep(1500);
    Phase("launcher_restored");
    if (EngineActive(100)) {
        Value("engine_resumed", 1);
        Phase("return_final");
        return 0;
    }
    if (!NotifyControl())
        return 7;
    Value("engine_resumed", EngineActive(10000));
    Phase("return_final");
    return EngineActive(100) ? 0 : 8;
#else
    Phase("continue_console");
    if (!EngineActive(500))
        return 15;
#endif
#else
    if (existing) {
        Text("FAIL existing Half-Life process; no process launched or altered\r\n");
        return 12;
    }
    char command[] = "\"C:\\SIERRA\\Half-Life\\hl.exe\" -console -gl -nosound -dev";
    STARTUPINFOA startup{};
    PROCESS_INFORMATION game{};
    startup.cb = sizeof(startup);
    if (!CreateProcessA("C:\\SIERRA\\Half-Life\\hl.exe", command, nullptr, nullptr, FALSE, 0,
                        nullptr, "C:\\SIERRA\\Half-Life", &startup, &game)) {
        Value("create_error", GetLastError());
        return 1;
    }
    GamePid = game.dwProcessId;
    CloseHandle(game.hThread);
    CloseHandle(game.hProcess);
    Value("game_pid", GamePid);
    Sleep(7000);
    Phase("normal_launcher");
    if (!NotifyControl() || !EngineActive(10000)) {
        Phase("console_entry_failed");
        return 2;
    }
#endif
    Phase("console_active");
    if (!Type("map c1a0") || GetForegroundWindow() != Engine || Pid(Engine) != GamePid)
        return 3;
    Key(VK_RETURN);
    Sleep(17000);
    Phase("map_before_away");
    if (!EngineActive(500))
        return 4;
    AltTab();
    Phase("away");
    const bool away = Pid(GetForegroundWindow()) != GamePid;
    Value("focus_away", away);
    if (!away)
        return 5;
    AltTab();
    Phase("back");
    const bool back = Pid(GetForegroundWindow()) == GamePid;
    Value("focus_back", back);
    if (!back)
        return 6;
    if (!EngineActive(500)) {
        if (!NotifyControl())
            return 7;
        Value("engine_resumed", EngineActive(10000));
    } else {
        Value("engine_resumed", 1);
    }
    Phase("final");
    // Deliberately leave the owned game alive for diagnosis after exporting log.
    return EngineActive(100) ? 0 : 8;
}
} // namespace

extern "C" void WINAPI WinMainCRTStartup() {
    Began = GetTickCount();
    Log = CreateFileA("C:\\DGDRV.LOG", GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                      FILE_ATTRIBUTE_NORMAL, nullptr);
    if (Log == INVALID_HANDLE_VALUE)
        ExitProcess(10);
    const DWORD result = Run();
    Value("diagnostic_exit", result);
    Text("DIAGNOSTIC_COMPLETE\r\n");
    if (!result)
        Text("PASS automated ntupdate: activation diagnostic completed; "
             "graphics acceptance requires composed frame evidence\r\n");
    CloseHandle(Log);
    ExitProcess(result);
}
