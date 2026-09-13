/* A serial command is not desktop input and conveys no NT5 foreground right.
 * Receive one real input transaction in our own temporary window, then give
 * that right to the already-verified game viewport. No foreign window is
 * clicked, no foreground policy is changed, and input queues remain separate.
 * https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-setforegroundwindow
 * https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-sendinput
 */
#define DG_FOCUS_INPUT_TAG 0x44474649UL
static DWORD FocusInputDown, FocusInputUp;
static LRESULT CALLBACK FocusInputWindow(HWND window, UINT message, WPARAM wp, LPARAM lp) {
    if ((DWORD_PTR)GetMessageExtraInfo() == DG_FOCUS_INPUT_TAG) {
        if (message == WM_LBUTTONDOWN)
            FocusInputDown++;
        if (message == WM_LBUTTONUP)
            FocusInputUp++;
    }
    return DefWindowProcA(window, message, wp, lp);
}

static HWND AcquireOwnedInput(POINT *savedCursor) {
    static const char name[] = "DreamGPUForegroundInput";
    WNDCLASSA cls = {};
    HWND window = NULL;
    POINT point = {48, 48};
    INPUT input[3] = {};
    DWORD began, pid = 0;
    UINT inserted;
    int width, height;
    MSG message;
    /* Never turn a held real button/modifier into a synthetic drag or chord. */
    const int keys[] = {VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_SHIFT, VK_CONTROL, VK_MENU};
    unsigned i;
    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
        if (GetAsyncKeyState(keys[i]) & 0x8000) {
            Record("FOCUS_INPUT_BUSY", keys[i]);
            return NULL;
        }
    if (!GetCursorPos(savedCursor))
        return NULL;
    width = GetSystemMetrics(SM_CXSCREEN);
    height = GetSystemMetrics(SM_CYSCREEN);
    if (width <= 64 || height <= 64)
        return NULL;
    cls.lpfnWndProc = FocusInputWindow;
    cls.hInstance = GetModuleHandleA(NULL);
    cls.lpszClassName = name;
    if (!RegisterClassA(&cls)) {
        Record("FOCUS_INPUT_CLASS_FAILED", GetLastError());
        return NULL;
    }
    window = CreateWindowExA(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, name, "DreamGPU benchmark input",
                             WS_POPUP, 32, 32, 32, 32, NULL, NULL, cls.hInstance, NULL);
    if (!window)
        goto fail;
    ShowWindow(window, SW_SHOWNOACTIVATE);
    /* Geometric hit testing, not screen pixels: only our exact window may
     * receive the transaction. Concurrent interactive activity aborts a run. */
    GetWindowThreadProcessId(window, &pid);
    if (pid != GetCurrentProcessId() || WindowFromPoint(point) != window) {
        Record("FOCUS_INPUT_TARGET_NOT_OWNED", pid);
        goto fail;
    }
    FocusInputDown = FocusInputUp = 0;
    for (i = 0; i < 3; i++) {
        input[i].type = INPUT_MOUSE;
        input[i].mi.dwExtraInfo = DG_FOCUS_INPUT_TAG;
    }
    input[0].mi.dx = (point.x * 65535 + (width - 1) / 2) / (width - 1);
    input[0].mi.dy = (point.y * 65535 + (height - 1) / 2) / (height - 1);
    input[0].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    input[1].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    input[2].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    inserted = SendInput(3, input, sizeof(INPUT));
    Record("FOCUS_INPUT_INSERTED", inserted);
    if (inserted != 3) {
        if (inserted == 2)
            Record("FOCUS_INPUT_RELEASE", SendInput(1, &input[2], sizeof(INPUT)));
        goto fail;
    }
    began = GetTickCount();
    while (GetTickCount() - began < 1000) {
        for (i = 0; i < 32 && PeekMessageA(&message, NULL, 0, 0, PM_REMOVE); i++) {
            TranslateMessage(&message);
            DispatchMessageA(&message);
        }
        if (FocusInputDown == 1 && FocusInputUp == 1)
            break;
        {
            DWORD elapsed = GetTickCount() - began;
            if (elapsed >= 1000)
                break;
            if (MsgWaitForMultipleObjects(0, NULL, FALSE, 1000 - elapsed, QS_ALLINPUT) ==
                WAIT_FAILED)
                break;
        }
    }
    Record("FOCUS_INPUT_DOWN_ACK", FocusInputDown);
    Record("FOCUS_INPUT_UP_ACK", FocusInputUp);
    if (FocusInputDown != 1 || FocusInputUp != 1 || GetForegroundWindow() != window)
        goto fail;
    Record("FOCUS_INPUT_FOREGROUND_PID", GetCurrentProcessId());
    return window;
fail:
    if (window)
        DestroyWindow(window);
    UnregisterClassA(name, cls.hInstance);
    SetCursorPos(savedCursor->x, savedCursor->y);
    return NULL;
}

static void ReleaseOwnedInput(HWND window, const POINT *savedCursor) {
    DestroyWindow(window);
    UnregisterClassA("DreamGPUForegroundInput", GetModuleHandleA(NULL));
    SetCursorPos(savedCursor->x, savedCursor->y);
}

/* The temporary input window and saved cursor form one owned transaction. */
class OwnedInputWindow final {
    HWND window_{};
    POINT saved_{};

  public:
    OwnedInputWindow() noexcept = default;
    OwnedInputWindow(const OwnedInputWindow &) = delete;
    OwnedInputWindow &operator=(const OwnedInputWindow &) = delete;
    ~OwnedInputWindow() noexcept {
        reset();
    }
    [[nodiscard]] bool acquire() noexcept {
        if (window_)
            return false;
        window_ = AcquireOwnedInput(&saved_);
        return window_ != HWND{};
    }
    void reset() noexcept {
        if (!window_)
            return;
        HWND old = window_;
        window_ = HWND{};
        DWORD error = GetLastError();
        ReleaseOwnedInput(old, &saved_);
        SetLastError(error);
    }
};
