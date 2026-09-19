// SPDX-License-Identifier: GPL-2.0-or-later
// Observe only the launched installer's standard dialogs. Never click by
// position, accept an unknown dialog, or request an automatic reboot.
#pragma once
struct SetupDialog {
    DWORD pid = 0;
    HWND window = nullptr;
    unsigned count = 0, children = 0, messages = 0, buttons = 0;
    DWORD expected_exit = DWORD(-1);
    int button = 0;
    bool failed = false;
};
static bool SetupDialogMessage(const char *text, DWORD &code, int &button) {
    struct Message {
        const char *text;
        DWORD code;
        int button;
    };
    constexpr Message messages[] = {
        {"DreamGPU is installed. OpenGL, Glide 2 and Direct3D are ready for applications "
         "system-wide.",
         0, IDOK},
        {"DreamGPU needs to restart Windows to continue. Setup will resume "
         "automatically.\r\n\r\nRestart now?",
         11, IDNO},
        {"DreamGPU needs to restart Windows to continue. Setup installs the display "
         "driver first, then the graphics libraries; another restart may be needed "
         "after setup resumes automatically.\r\n\r\nRestart now?",
         11, IDNO},
        {"DreamGPU setup resumed after Windows started and needs another restart to "
         "finish. Setup installs the display driver first, then the graphics libraries; "
         "these steps can require separate restarts. The graphics update is not ready "
         "yet.\r\n\r\nRestart Windows now?",
         11, IDNO},
        {"The previous installation has been restored.", 12, IDOK},
        {"DreamGPU has been removed and the previous system drivers restored.", 13, IDOK},
        {"DreamGPU setup is already running.", 28, IDOK},
    };
    for (const auto &message : messages) {
        if (!lstrcmpA(text, message.text)) {
            code = message.code;
            button = message.button;
            return true;
        }
    }
    return false;
}
static BOOL CALLBACK SetupDialogChild(HWND window, LPARAM opaque) {
    auto &dialog = *reinterpret_cast<SetupDialog *>(opaque);
    DWORD owner = 0;
    char type[32]{};
    if (++dialog.children > 64 || !GetWindowThreadProcessId(window, &owner) ||
        owner != dialog.pid) {
        dialog.failed = true;
        return FALSE;
    }
    if (!GetClassNameA(window, type, sizeof(type))) {
        line("INSTALLER_UI_FAILURE child class");
        dialog.failed = true;
        return FALSE;
    }
    if (!lstrcmpiA(type, "Button") && IsWindowVisible(window)) {
        ++dialog.buttons;
        char detail[128]{};
        wsprintfA(detail, "INSTALLER_UI_BUTTON id=%d visible=%d enabled=%d", GetDlgCtrlID(window),
                  IsWindowVisible(window), IsWindowEnabled(window));
        line(detail);
    }
    if (lstrcmpiA(type, "Static"))
        return TRUE;
    char text[1024]{};
    DWORD_PTR copied = 0;
    if (!SendMessageTimeoutA(window, WM_GETTEXT, sizeof(text), reinterpret_cast<LPARAM>(text),
                             SMTO_ABORTIFHUNG | SMTO_BLOCK, 100, &copied) ||
        copied >= sizeof(text) - 1) {
        line("INSTALLER_UI_FAILURE child text");
        dialog.failed = true;
        return FALSE;
    }
    if (copied && SetupDialogMessage(text, dialog.expected_exit, dialog.button)) {
        ++dialog.messages;
        line("INSTALLER_DIALOG_TEXT");
        line(text);
    }
    return TRUE;
}
static BOOL CALLBACK SetupDialogWindow(HWND window, LPARAM opaque) {
    auto &dialog = *reinterpret_cast<SetupDialog *>(opaque);
    DWORD owner = 0;
    if (!GetWindowThreadProcessId(window, &owner) || owner != dialog.pid ||
        !IsWindowVisible(window))
        return TRUE;
    char type[32]{}, title[64]{};
    if (!GetClassNameA(window, type, sizeof(type)) || lstrcmpA(type, "#32770"))
        return TRUE;
    ++dialog.count;
    if (dialog.count != 1 || !GetWindowTextA(window, title, sizeof(title)) ||
        lstrcmpA(title, "DreamGPU setup")) {
        dialog.failed = true;
        return FALSE;
    }
    dialog.window = window;
    EnumChildWindows(window, SetupDialogChild, opaque);
    return !dialog.failed;
}
static bool WaitSetupDialog(HANDLE child, DWORD pid, DWORD timeout, DWORD &code) {
    const DWORD started = GetTickCount();
    while (GetTickCount() - started < timeout) {
        const DWORD status = WaitForSingleObject(child, 100);
        if (status == WAIT_OBJECT_0)
            return false; // Exited without displaying an observed result.
        if (status != WAIT_TIMEOUT)
            return false;
        SetupDialog dialog{};
        dialog.pid = pid;
        if (!EnumWindows(SetupDialogWindow, reinterpret_cast<LPARAM>(&dialog)) || dialog.failed) {
            line("INSTALLER_UI_FAILURE enumeration");
            return false;
        }
        if (!dialog.count)
            continue;
        if (dialog.messages != 1 || !dialog.window) {
            line("INSTALLER_UI_FAILURE message identity");
            return false;
        }
        int action = dialog.button;
        HWND button = GetDlgItem(dialog.window, action);
        // NT5 represents the sole MB_OK button as IDCANCEL, while MessageBox
        // still returns IDOK. Accept only the observed one-button OK form.
        if (!button && action == IDOK && dialog.buttons == 1) {
            action = IDCANCEL;
            button = GetDlgItem(dialog.window, action);
        }
        DWORD owner = 0;
        if (!button || !IsWindowVisible(button) || !IsWindowEnabled(button) ||
            !GetWindowThreadProcessId(button, &owner) || owner != pid) {
            line("INSTALLER_UI_FAILURE button identity");
            return false;
        }
        char type[32]{}, label[32]{};
        DWORD_PTR copied = 0;
        if (!GetClassNameA(button, type, sizeof(type)) || lstrcmpiA(type, "Button") ||
            GetDlgCtrlID(button) != action ||
            !SendMessageTimeoutA(button, WM_GETTEXT, sizeof(label), reinterpret_cast<LPARAM>(label),
                                 SMTO_ABORTIFHUNG | SMTO_BLOCK, 100, &copied) ||
            copied >= sizeof(label) - 1 ||
            (dialog.button == IDOK && lstrcmpA(label, "OK") && lstrcmpA(label, "&OK")) ||
            (dialog.button == IDNO && lstrcmpA(label, "No") && lstrcmpA(label, "&No"))) {
            line("INSTALLER_UI_FAILURE button class, ID or label");
            return false;
        }
        if (dialog.button == IDNO) {
            DWORD_PTR selected = 0;
            if (!SendMessageTimeoutA(dialog.window, DM_GETDEFID, 0, 0,
                                     SMTO_ABORTIFHUNG | SMTO_BLOCK, 100, &selected) ||
                HIWORD(selected) != DC_HASDEFID || LOWORD(selected) != IDNO)
                return false;
        }
        // Revalidate the dialog immediately before dispatch. A foreign or reused
        // HWND must never receive an installer action.
        owner = 0;
        if (!GetWindowThreadProcessId(dialog.window, &owner) || owner != pid ||
            !PostMessageA(dialog.window, WM_COMMAND, MAKEWPARAM(action, BN_CLICKED),
                          reinterpret_cast<LPARAM>(button)))
            return false;
        line(dialog.button == IDNO ? "INSTALLER_RESTART_DECLINED" : "INSTALLER_DIALOG_ACCEPTED");
        // Do not race the queued close by enumerating the same window again.
        const DWORD elapsed = GetTickCount() - started;
        if (elapsed >= timeout || WaitForSingleObject(child, timeout - elapsed) != WAIT_OBJECT_0)
            return false;
        return GetExitCodeProcess(child, &code) && code == dialog.expected_exit;
    }
    return false;
}
