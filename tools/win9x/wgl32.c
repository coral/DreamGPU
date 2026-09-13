/* SPDX-License-Identifier: GPL-2.0-or-later
 * Real shared WGL frontend, automatic Win98 desktop pixel/coherence oracle.
 * Reuses the common public lifecycle tests; replaces only delivery/UI wait.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static HANDLE Serial = INVALID_HANDLE_VALUE;
static BOOL WINAPI Mirror(HANDLE file, const void *data, DWORD bytes, DWORD *written,
                          OVERLAPPED *overlapped) {
    DWORD ignored;
    BOOL ok = WriteFile(file, data, bytes, written, overlapped);
    if (Serial != INVALID_HANDLE_VALUE)
        WriteFile(Serial, data, bytes, &ignored, NULL);
    return ok;
}
static void WINAPI Finish(UINT result) {
    DWORD written;
    const char *text = result ? "DONE FAIL Win98 public WGL\r\n" : "DONE PASS Win98 public WGL\r\n";
    if (!result) {
        HANDLE log = CreateFileA("C:\\DGWGL.LOG", GENERIC_WRITE, FILE_SHARE_READ, NULL,
                                 OPEN_EXISTING, 0, NULL);
        const char *passed =
            "PASS automated win98: native WGL pixels, clipping and CPU coherence\r\n";
        if (log != INVALID_HANDLE_VALUE) {
            SetFilePointer(log, 0, NULL, FILE_END);
            WriteFile(log, passed, lstrlenA(passed), &written, NULL);
            CloseHandle(log);
        }
    }
    if (Serial != INVALID_HANDLE_VALUE)
        WriteFile(Serial, text, lstrlenA(text), &written, NULL);
    ExitProcess(result);
}
static BOOL WINAPI Automatic(MSG *, HWND, UINT, UINT);
#define WriteFile Mirror
#define ExitProcess Finish
#define GetMessageA Automatic
#define WinMainCRTStartup CommonPublicMain
#include "../opengl/probe.c"
#undef WriteFile
#undef ExitProcess
#undef GetMessageA
static BOOL BulkReadback(void) {
    static BYTE texture[65536], actual[65538];
    GLuint name;
    DWORD x, y, i;
    if (!Check(pwglMakeCurrent(DCs[0], Contexts[0]), "FAIL bulk readback context"))
        return FALSE;
    for (y = 0; y < 64; ++y)
        for (x = 0; x < 256; ++x) {
            i = (y * 256 + x) * 4;
            texture[i] = (BYTE)x;
            texture[i + 1] = (BYTE)y;
            texture[i + 2] = (BYTE)(x ^ y);
            texture[i + 3] = 255;
        }
    pglGenTextures(1, &name);
    pglBindTexture(GL_TEXTURE_2D, name);
    pglPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    pglPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    pglPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
    pglPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    pglPixelStorei(GL_PACK_ALIGNMENT, 4);
    pglPixelStorei(GL_PACK_ROW_LENGTH, 0);
    pglPixelStorei(GL_PACK_SKIP_ROWS, 0);
    pglPixelStorei(GL_PACK_SKIP_PIXELS, 0);
    pglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, texture);
    actual[0] = 0xa5;
    actual[65537] = 0x5a;
    pglGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, actual + 1);
    if (!Check(pglGetError() == GL_NO_ERROR && actual[0] == 0xa5 && actual[65537] == 0x5a,
               "FAIL bulk texture readback error or output guard"))
        return FALSE;
    for (i = 0; i < sizeof(texture); ++i)
        if (!Check(actual[i + 1] == texture[i], "FAIL bulk texture readback exact pixels"))
            return FALSE;
    pglBindTexture(GL_TEXTURE_2D, 0);
    Log("PASS 64KiB texture readback,16384 exact RGBA pixels and unaligned output guards");
    return TRUE;
}
static BOOL WINAPI Automatic(MSG *message, HWND filter, UINT low, UINT high) {
    RECT r;
    HRGN clip, second;
    HBRUSH black, magenta;
    HWND occluder, replacement;
    HDC screen, cover;
    (void)message;
    (void)filter;
    (void)low;
    (void)high;
    if (!Check(GetPixel(DCs[0], 40, 40) == RGB(255, 0, 0) &&
                   GetPixel(DCs[0], 200, 40) == RGB(0, 0, 255),
               "FAIL GDI read sees native presented pixels"))
        return FALSE;
    Log("PASS primary READBACK/RETURN exact red/blue pixels");
    if (!BulkReadback())
        return FALSE;
    /* GetPixel has returned the primary to CPU ownership. An empty GDI clip
     * must still retire its native retained frame, with no desktop takeover. */
    clip = CreateRectRgn(0, 0, 0, 0);
    if (!Check(clip != NULL && SelectClipRgn(DCs[0], clip) != ERROR, "FAIL empty clip setup")) {
        if (clip)
            DeleteObject(clip);
        return FALSE;
    }
    if (!Draw(0)) {
        SelectClipRgn(DCs[0], NULL);
        DeleteObject(clip);
        return FALSE;
    }
    SelectClipRgn(DCs[0], NULL);
    DeleteObject(clip);
    if (!Check(GetPixel(DCs[0], 40, 40) == RGB(255, 0, 0),
               "FAIL invisible present changed CPU primary"))
        return FALSE;
    Log("PASS fully clipped retained present releases without desktop takeover");
    black = CreateSolidBrush(RGB(0, 0, 0));
    r.left = r.top = 0;
    r.right = 320;
    r.bottom = 240;
    if (!Check(FillRect(DCs[0], &r, black), "FAIL CPU primary fill"))
        goto done;
    clip = CreateRectRgn(10, 10, 40, 40);
    second = CreateRectRgn(180, 60, 210, 90);
    CombineRgn(clip, clip, second, RGN_OR);
    SelectClipRgn(DCs[0], clip);
    if (!Draw(0)) {
        SelectClipRgn(DCs[0], NULL);
        DeleteObject(clip);
        DeleteObject(second);
        goto done;
    }
    SelectClipRgn(DCs[0], NULL);
    DeleteObject(clip);
    DeleteObject(second);
    if (!Check(GetPixel(DCs[0], 20, 20) == RGB(255, 0, 0) &&
                   GetPixel(DCs[0], 190, 70) == RGB(0, 0, 255) &&
                   GetPixel(DCs[0], 100, 100) == RGB(0, 0, 0),
               "FAIL complex GDI clips or CPU/GPU coherence"))
        goto done;
    Log("PASS two disjoint GDI clips, untouched CPU primary outside clips");
    occluder = CreateWindowA("DreamGPUWGLProbe", "Occluder", WS_POPUP | WS_VISIBLE, 96, 96, 64, 64,
                             NULL, NULL, Instance, NULL);
    if (!Check(occluder != NULL, "FAIL occluder create"))
        goto done;
    UpdateWindow(occluder);
    cover = GetDC(occluder);
    magenta = CreateSolidBrush(RGB(255, 0, 255));
    r.left = r.top = 0;
    r.right = r.bottom = 64;
    FillRect(cover, &r, magenta);
    ReleaseDC(occluder, cover);
    DeleteObject(magenta);
    if (!Draw(0)) {
        DestroyWindow(occluder);
        goto done;
    }
    screen = GetDC(NULL);
    Check(GetPixel(screen, 112, 112) == RGB(255, 0, 255),
          "FAIL occluder pixels overwritten by GPU window");
    ReleaseDC(NULL, screen);
    DestroyWindow(occluder);
    if (Failed)
        goto done;
    Log("PASS overlapping window remains above native GPU output");
    if (!Check(pwglMakeCurrent(DCs[1], Contexts[1]), "FAIL destruction context"))
        goto done;
    DestroyWindow(Windows[1]);
    Windows[1] = NULL;
    replacement = CreateWindowA("DreamGPUWGLProbe", "Replacement", WS_POPUP | WS_VISIBLE, 416, 64,
                                320, 240, NULL, NULL, Instance, NULL);
    Check(!pwglSwapBuffers(DCs[1]), "FAIL dead window binding revived");
    DestroyWindow(replacement);
    if (Failed)
        goto done;
    Log("PASS destroyed window binding cannot present into replacement");
    if (!Check(pwglMakeCurrent(DCs[0], Contexts[0]), "FAIL resize current"))
        goto done;
    SetWindowPos(Windows[0], NULL, 0, 0, 280, 200, SWP_NOMOVE | SWP_NOZORDER);
    if (!Check(!pwglSwapBuffers(DCs[0]) && pwglMakeCurrent(DCs[0], Contexts[0]),
               "FAIL resize rebinding"))
        goto done;
    pglClearColor(0, 1, 0, 1);
    pglClear(GL_COLOR_BUFFER_BIT);
    if (!Check(pwglSwapBuffers(DCs[0]) && GetPixel(DCs[0], 100, 100) == RGB(0, 255, 0),
               "FAIL resized GPU primary"))
        goto done;
    Log("PASS resized drawable rebind and exact native primary pixels");
done:
    DeleteObject(black);
    return FALSE;
}
int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show) {
    DCB state;
    COMMTIMEOUTS timeouts;
    (void)instance;
    (void)previous;
    (void)command;
    (void)show;
    Serial = CreateFileA("COM1", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (Serial != INVALID_HANDLE_VALUE) {
        ZeroMemory(&state, sizeof(state));
        state.DCBlength = sizeof(state);
        if (GetCommState(Serial, &state)) {
            state.BaudRate = CBR_115200;
            state.ByteSize = 8;
            state.Parity = NOPARITY;
            state.StopBits = ONESTOPBIT;
            state.fOutxCtsFlow = state.fOutxDsrFlow = state.fOutX = state.fInX = FALSE;
            SetCommState(Serial, &state);
        }
        ZeroMemory(&timeouts, sizeof(timeouts));
        timeouts.WriteTotalTimeoutConstant = 250;
        SetCommTimeouts(Serial, &timeouts);
    }
    {
        HDC dc = GetDC(NULL);
        char line[96];
        DWORD written;
        wsprintfA(line, "DGWGL9 primary=%dx%dx%d\r\n", GetDeviceCaps(dc, HORZRES),
                  GetDeviceCaps(dc, VERTRES), GetDeviceCaps(dc, BITSPIXEL));
        ReleaseDC(NULL, dc);
        if (Serial != INVALID_HANDLE_VALUE)
            WriteFile(Serial, line, lstrlenA(line), &written, NULL);
    }
    SetCursor(LoadCursorA(NULL, IDC_ARROW));
    SetCursor(NULL); /* Native hidden cursor is still a coherent cursor plane. */
    CommonPublicMain();
    return 0;
}
