/* SPDX-License-Identifier: GPL-2.0-or-later
 * Explicit disposable-fixture mode setter. Default: 1280x1024x32, so full
 * screen operations exceed the 4 MiB inline budget. /restore: 1024x768x32.
 * /8 and /16 select 1024x768 at those depths for palette/RGB565 checks.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show) {
    DEVMODEA mode;
    HANDLE file;
    HDC dc;
    DWORD wrote;
    LONG result;
    char text[160];
    int restore = command && lstrcmpiA(command, "/restore") == 0;
    int depth8 = command && lstrcmpiA(command, "/8") == 0;
    int depth16 = command && lstrcmpiA(command, "/16") == 0;
    int small = restore || depth8 || depth16;
    (void)instance;
    (void)previous;
    (void)show;
    ZeroMemory(&mode, sizeof(mode));
    mode.dmSize = sizeof(mode);
    mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
    if (command && *command && !small)
        return 2;
    mode.dmPelsWidth = small ? 1024 : 1280;
    mode.dmPelsHeight = small ? 768 : 1024;
    mode.dmBitsPerPel = depth8 ? 8 : (depth16 ? 16 : 32);
    result = ChangeDisplaySettingsA(&mode, CDS_UPDATEREGISTRY);
    dc = GetDC(NULL);
    wsprintfA(text, "ChangeDisplaySettings result=%ld; actual=%dx%dx%d\r\n", result,
              GetDeviceCaps(dc, HORZRES), GetDeviceCaps(dc, VERTRES), GetDeviceCaps(dc, BITSPIXEL));
    ReleaseDC(NULL, dc);
    file = CreateFileA("C:\\DG9MODE.LOG", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0,
                       NULL);
    if (file != INVALID_HANDLE_VALUE) {
        WriteFile(file, text, lstrlenA(text), &wrote, NULL);
        CloseHandle(file);
    }
    return result == DISP_CHANGE_SUCCESSFUL ? 0 : 1;
}
