// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "progress.h"
#include "icons.h"
namespace setup::ui {
// USER32-only controls keep the PE4 installer loadable on Windows 98/2000/XP.
// All controls belong to the entry thread; installation runs on a worker so
// hashing, driver calls and verification waits cannot freeze the window.
inline HWND window = nullptr, status = nullptr, history = nullptr, elapsed = nullptr;
inline char last_operation[768]{};
inline DWORD started = 0;
constexpr UINT ProgressMessage = WM_APP + 1;
struct Update {
    const char *operation;
    const char *detail;
};
inline void append(const char *text) {
    // Stay below the legacy Win98 edit control's text limit.
    if (GetWindowTextLengthA(history) > 24000) {
        SendMessageA(history, EM_SETSEL, 0, 12000);
        SendMessageA(history, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(""));
    }
    const int end = GetWindowTextLengthA(history);
    SendMessageA(history, EM_SETSEL, end, end);
    SendMessageA(history, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(text));
    SendMessageA(history, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>("\r\n"));
    SendMessageA(history, EM_SCROLLCARET, 0, 0);
}
inline LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM first, LPARAM second) {
    if (message == ProgressMessage) {
        const auto &update = *reinterpret_cast<const Update *>(second);
        lstrcpynA(last_operation, update.operation, 256);
        if (update.detail[0]) {
            lstrcatA(last_operation, ": ");
            lstrcpynA(last_operation + lstrlenA(last_operation), update.detail, 510);
        }
        SetWindowTextA(status, last_operation);
        append(last_operation);
        return 0;
    }
    if (message == WM_TIMER) {
        char text[128];
        wsprintfA(text, "Working... %lu seconds elapsed. Please wait.",
                  (GetTickCount() - started) / 1000);
        SetWindowTextA(elapsed, text);
        return 0;
    }
    // Closing mid-transaction is not cancellation. Keep pumping until the
    // worker has finished and the existing result/restart dialog is dismissed.
    if (message == WM_CLOSE)
        return 0;
    return DefWindowProcA(hwnd, message, first, second);
}
inline void report(const char *operation, const char *detail) {
    const DWORD error = GetLastError();
    const Update update{operation, detail};
    // Synchronous delivery keeps worker-owned filenames alive until copied.
    SendMessageA(window, ProgressMessage, 0, reinterpret_cast<LPARAM>(&update));
    SetLastError(error);
}
inline bool open(const char *title) {
    if (!load_icons())
        return false;
    HINSTANCE instance = GetModuleHandleA(nullptr);
    WNDCLASSEXA type{};
    type.cbSize = sizeof(type);
    type.lpfnWndProc = procedure;
    type.hInstance = instance;
    type.hCursor = LoadCursorA(nullptr, IDC_ARROW);
    type.hIcon = large_icon;
    type.hIconSm = small_icon;
    type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    type.lpszClassName = "DreamGPUSetupProgress";
    if (!RegisterClassExA(&type))
        return false;
    RECT bounds{0, 0, 576, 350};
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    if (!AdjustWindowRect(&bounds, style, FALSE))
        return false;
    window = CreateWindowExA(0, type.lpszClassName, "DreamGPU setup", style, CW_USEDEFAULT,
                             CW_USEDEFAULT, bounds.right - bounds.left, bounds.bottom - bounds.top,
                             nullptr, nullptr, instance, nullptr);
    if (!window)
        return false;
    set_icons(window);
    auto control = [&](const char *kind, const char *text, DWORD flags, int y, int height) {
        return CreateWindowExA(0, kind, text, WS_CHILD | WS_VISIBLE | flags, 16, y, 544, height,
                               window, nullptr, instance, nullptr);
    };
    HWND heading = control("STATIC", title, SS_LEFT, 14, 24);
    status = control("STATIC", "Starting setup...", SS_LEFT | SS_NOPREFIX, 44, 48);
    history =
        control("EDIT", "",
                WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | ES_NOHIDESEL,
                100, 204);
    elapsed = control("STATIC", "Working... Please wait.", SS_LEFT, 320, 20);
    if (!heading || !status || !history || !elapsed) {
        DestroyWindow(window);
        window = nullptr;
        return false;
    }
    SendMessageA(history, EM_LIMITTEXT, 30000, 0);
    EnableMenuItem(GetSystemMenu(window, FALSE), SC_CLOSE, MF_BYCOMMAND | MF_GRAYED);
    started = GetTickCount();
    SetTimer(window, 1, 1000, nullptr);
    ShowWindow(window, SW_SHOWNORMAL);
    UpdateWindow(window);
    progress_observer = report;
    return true;
}
inline DWORD wait(HANDLE worker) {
    for (;;) {
        // QS_ALLINPUT also services synchronous progress messages from worker.
        if (MsgWaitForMultipleObjects(1, &worker, FALSE, INFINITE, QS_ALLINPUT) == WAIT_OBJECT_0)
            break;
        MSG message;
        while (PeekMessageA(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageA(&message);
        }
    }
    DWORD code = 32;
    GetExitCodeThread(worker, &code);
    CloseHandle(worker);
    progress_observer = nullptr;
    KillTimer(window, 1);
    return code;
}
inline void finish(unsigned code) {
    KillTimer(window, 1);
    const char *text = code == 11 || code == 16 ? "Restart required to continue setup."
                       : code >= 20             ? "Setup failed. See the error message for details."
                                                : "Setup finished.";
    SetWindowTextA(status, text);
    char result[128];
    wsprintfA(result, "%s (code %u)", text, code);
    append(result);
    SetWindowTextA(elapsed, "");
}
} // namespace setup::ui
