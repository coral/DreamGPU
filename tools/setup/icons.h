// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "resources.h"
namespace setup::ui {
inline HICON large_icon = nullptr, small_icon = nullptr;
inline bool load_icons() {
    if (large_icon && small_icon)
        return true;
    OSVERSIONINFOA version{};
    version.dwOSVersionInfoSize = sizeof(version);
    const bool xp = GetVersionExA(&version) && version.dwPlatformId == VER_PLATFORM_WIN32_NT &&
                    (version.dwMajorVersion > 5 ||
                     (version.dwMajorVersion == 5 && version.dwMinorVersion >= 1));
    HINSTANCE instance = GetModuleHandleA(nullptr);
    const auto resource = MAKEINTRESOURCEA(xp ? IDI_DREAMGPU_XP : IDI_DREAMGPU_LEGACY);
    // Keep these two handles until process exit. Do not use LR_SHARED: its
    // resource-name cache can return the large icon for a small-icon request.
    large_icon =
        static_cast<HICON>(LoadImageA(instance, resource, IMAGE_ICON, GetSystemMetrics(SM_CXICON),
                                      GetSystemMetrics(SM_CYICON), 0));
    if (!large_icon)
        return false;
    small_icon =
        static_cast<HICON>(LoadImageA(instance, resource, IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                      GetSystemMetrics(SM_CYSMICON), 0));
    if (!small_icon) {
        DestroyIcon(large_icon);
        large_icon = nullptr;
        return false;
    }
    return true;
}
inline void set_icons(HWND target) {
    SendMessageA(target, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(large_icon));
    SendMessageA(target, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small_icon));
}
inline LRESULT CALLBACK dialog_icons(int code, WPARAM first, LPARAM second) {
    if (code == HCBT_ACTIVATE) {
        HWND dialog = reinterpret_cast<HWND>(first);
        char type[16]{};
        if (GetClassNameA(dialog, type, sizeof(type)) && !lstrcmpA(type, "#32770"))
            set_icons(dialog);
    }
    return CallNextHookEx(nullptr, code, first, second);
}
inline int message_box(HWND owner, const char *text, UINT flags) {
    // Standard result/restart dialogs have their own window class. Brand their
    // title bars and Alt+Tab icons too; scope the hook to this UI thread/call.
    HHOOK hook = load_icons()
                     ? SetWindowsHookExA(WH_CBT, dialog_icons, nullptr, GetCurrentThreadId())
                     : nullptr;
    const int result = MessageBoxA(owner, text, "DreamGPU setup", flags);
    if (hook)
        UnhookWindowsHookEx(hook);
    return result;
}
} // namespace setup::ui
