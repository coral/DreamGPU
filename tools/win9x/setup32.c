/* SPDX-License-Identifier: GPL-2.0-or-later
 * One-time Win98 local-desktop configuration for the disposable DreamGPU image.
 * Select Windows Logon instead of the inherited Microsoft Network provider;
 * no password is read, written or removed. Keep the previous provider value.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static const char *Configure(void) {
    HKEY key, backup;
    DWORD bytes = 256, type = 0, disposition;
    char provider[256];
    LONG status;
    if (!(GetVersion() & 0x80000000UL))
        return "FAIL setup98 requires Windows9x\r\n";
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "Network\\Logon", 0, KEY_QUERY_VALUE | KEY_SET_VALUE,
                      &key))
        return "FAIL local logon key missing\r\n";
    status = RegQueryValueExA(key, "PrimaryProvider", NULL, &type, (BYTE *)provider, &bytes);
    if (status || type != REG_SZ || !bytes || bytes > sizeof(provider) || provider[bytes - 1] ||
        (provider[0] && lstrcmpiA(provider, "Microsoft Network"))) {
        RegCloseKey(key);
        return "FAIL unrecognized primary logon provider\r\n";
    }
    if (provider[0]) {
        status = RegCreateKeyExA(HKEY_LOCAL_MACHINE, "Software\\DreamGPU\\Win98Setup", 0, NULL, 0,
                                 KEY_SET_VALUE, NULL, &backup, &disposition);
        if (status) {
            RegCloseKey(key);
            return "FAIL provider backup key\r\n";
        }
        status =
            RegSetValueExA(backup, "PreviousPrimaryProvider", 0, REG_SZ, (BYTE *)provider, bytes);
        if (!status)
            status = RegFlushKey(backup);
        RegCloseKey(backup);
        if (status) {
            RegCloseKey(key);
            return "FAIL provider backup write\r\n";
        }
    }
    status = RegSetValueExA(key, "PrimaryProvider", 0, REG_SZ, (const BYTE *)"", 1);
    if (status) {
        RegCloseKey(key);
        return "FAIL local logon write\r\n";
    }
    bytes = sizeof(provider);
    type = 0;
    status = RegQueryValueExA(key, "PrimaryProvider", NULL, &type, (BYTE *)provider, &bytes);
    if (!status)
        status = RegFlushKey(key);
    RegCloseKey(key);
    if (status || type != REG_SZ || bytes != 1 || provider[0])
        return "FAIL local logon readback\r\n";
    return "PASS automated setup98: local Windows logon selected; prior provider preserved; reboot "
           "required\r\n";
}
int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show) {
    HANDLE log;
    DWORD bytes;
    const char *result;
    (void)instance;
    (void)previous;
    (void)command;
    (void)show;
    log = CreateFileA("C:\\DGSET9.LOG", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                      FILE_ATTRIBUTE_NORMAL, NULL);
    if (log == INVALID_HANDLE_VALUE)
        return 2;
    result = Configure();
    WriteFile(log, result, lstrlenA(result), &bytes, NULL);
    FlushFileBuffers(log);
    CloseHandle(log);
    return result[0] == 'P' ? 0 : 1;
}
