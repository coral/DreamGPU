// SPDX-License-Identifier: GPL-2.0-or-later
// Execute the actual dialog adapter with hostile window/lifetime responses.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#define wsprintfA(buffer, ...) std::snprintf(buffer, sizeof(buffer), __VA_ARGS__)
using DWORD = uint32_t;
using DWORD_PTR = uintptr_t;
using LPARAM = intptr_t;
using WPARAM = uintptr_t;
using HWND = void *;
using HANDLE = uintptr_t;
using BOOL = int;
#define CALLBACK
constexpr BOOL TRUE = 1, FALSE = 0;
constexpr unsigned IDOK = 1, IDCANCEL = 2, IDYES = 6, IDNO = 7, WM_GETTEXT = 13, WM_COMMAND = 273;
constexpr unsigned SMTO_ABORTIFHUNG = 2, SMTO_BLOCK = 1, DM_GETDEFID = 1024;
constexpr unsigned DC_HASDEFID = 0x534b, BN_CLICKED = 0;
constexpr DWORD WAIT_OBJECT_0 = 0, WAIT_TIMEOUT = 258, WAIT_FAILED = DWORD(-1);
#define HIWORD(x) ((uint32_t(x) >> 16) & 0xffff)
#define LOWORD(x) (uint32_t(x) & 0xffff)
#define MAKEWPARAM(lo, hi) (uintptr_t(lo) | (uintptr_t(hi) << 16))
static const HWND Dialog = reinterpret_cast<HWND>(1), Text = reinterpret_cast<HWND>(2),
                  Button = reinterpret_cast<HWND>(3);
static const char *message;
static DWORD ticks, exit_code;
static unsigned posts, dialog_lookups, children;
static bool foreign, reused, duplicate, hung, wrong_default, post_failure, wait_failure;
static bool legacy_ok, wrong_label, extra_button;
static int lstrcmpA(const char *a, const char *b) {
    return std::strcmp(a, b);
}
static int lstrcmpiA(const char *a, const char *b) {
    return std::strcmp(a, b);
}
static bool line(const char *) {
    return true;
}
static DWORD GetTickCount() {
    return ticks;
}
static DWORD WaitForSingleObject(HANDLE, DWORD timeout) {
    ticks += timeout;
    return wait_failure ? WAIT_FAILED : posts ? WAIT_OBJECT_0 : WAIT_TIMEOUT;
}
static BOOL GetExitCodeProcess(HANDLE, DWORD *code) {
    *code = exit_code;
    return TRUE;
}
static DWORD GetWindowThreadProcessId(HWND window, DWORD *owner) {
    if (window == Dialog)
        ++dialog_lookups;
    *owner = foreign || (reused && window == Dialog && dialog_lookups > 1) ? 99 : 42;
    return 1;
}
static BOOL IsWindowVisible(HWND) {
    return TRUE;
}
static BOOL IsWindowEnabled(HWND) {
    return TRUE;
}
static int GetClassNameA(HWND window, char *out, int count) {
    return std::snprintf(out, size_t(count), "%s",
                         window == Dialog   ? "#32770"
                         : window == Button ? "Button"
                                            : "Static");
}
static int GetWindowTextA(HWND, char *out, int count) {
    return std::snprintf(out, size_t(count), "%s", "DreamGPU setup");
}
static BOOL EnumChildWindows(HWND, BOOL (*callback)(HWND, LPARAM), LPARAM opaque) {
    for (unsigned n = 0; n < children; ++n)
        if (!callback(Text, opaque))
            return FALSE;
    if (!callback(Button, opaque))
        return FALSE;
    return !extra_button || callback(Button, opaque);
}
static BOOL EnumWindows(BOOL (*callback)(HWND, LPARAM), LPARAM opaque) {
    if (!callback(Dialog, opaque))
        return FALSE;
    return !duplicate || callback(Dialog, opaque);
}
static int GetDlgCtrlID(HWND) {
    return exit_code == 11 ? IDNO : legacy_ok ? IDCANCEL : IDOK;
}
static HWND GetDlgItem(HWND, int id) {
    return id == GetDlgCtrlID(Button) ? Button : nullptr;
}
static BOOL SendMessageTimeoutA(HWND window, unsigned kind, WPARAM size, LPARAM out, unsigned,
                                unsigned, DWORD_PTR *result) {
    if (hung)
        return FALSE;
    if (kind == WM_GETTEXT) {
        *result = uintptr_t(std::snprintf(reinterpret_cast<char *>(out), size, "%s",
                                          window == Button ? (wrong_label       ? "Cancel"
                                                              : exit_code == 11 ? "&No"
                                                                                : "OK")
                                                           : message));
    } else {
        assert(kind == DM_GETDEFID);
        *result = (DC_HASDEFID << 16) | (wrong_default ? IDYES : IDNO);
    }
    return TRUE;
}
static BOOL PostMessageA(HWND window, unsigned kind, WPARAM button, LPARAM child) {
    assert(window == Dialog && kind == WM_COMMAND && child == LPARAM(Button));
    assert(LOWORD(button) == unsigned(GetDlgCtrlID(Button)));
    assert(!foreign && !reused);
    if (post_failure)
        return FALSE;
    ++posts;
    return TRUE;
}
#include "../../../tools/setup/ui-verify.h"
static void reset() {
    ticks = posts = dialog_lookups = exit_code = 0;
    children = 1;
    legacy_ok = wrong_label = extra_button = false;
    foreign = reused = duplicate = hung = wrong_default = post_failure = wait_failure = false;
    message = "DreamGPU is installed. OpenGL, Glide 2 and Direct3D are ready for applications "
              "system-wide.";
}
static bool run() {
    DWORD code = DWORD(-1);
    return WaitSetupDialog(7, 42, 1000, code);
}
int main() {
    reset();
    assert(run() && posts == 1);
    reset();
    message = "DreamGPU needs to restart Windows to continue. Setup will resume "
              "automatically.\r\n\r\nRestart now?";
    exit_code = 11;
    assert(run() && posts == 1);
    reset();
    foreign = true;
    assert(!run() && !posts);
    reset();
    reused = true;
    assert(!run() && !posts);
    reset();
    duplicate = true;
    assert(!run() && !posts);
    reset();
    hung = true;
    assert(!run() && !posts);
    reset();
    message = "Unknown dialog";
    assert(!run() && !posts);
    reset();
    children = 65;
    assert(!run() && !posts);
    reset();
    message = "DreamGPU needs to restart Windows to continue. Setup will resume "
              "automatically.\r\n\r\nRestart now?";
    wrong_default = true;
    exit_code = 11;
    assert(!run() && !posts);
    reset();
    post_failure = true;
    assert(!run() && !posts);
    reset();
    wait_failure = true;
    assert(!run() && !posts);
    reset();
    exit_code = 12;
    assert(!run() && posts == 1);
    reset();
    legacy_ok = true;
    assert(run() && posts == 1);
    reset();
    legacy_ok = wrong_label = true;
    assert(!run() && !posts);
    reset();
    legacy_ok = extra_button = true;
    assert(!run() && !posts);
    reset();
    wrong_label = true;
    assert(!run() && !posts);
    std::puts("installer dialog adapter: 16 lifetime/ownership/outcome cases PASS");
}
