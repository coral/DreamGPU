/* SPDX-License-Identifier: GPL-2.0-or-later
 * NT5 installer for the exact experimental PCI identity, using the OS newdev.
 * Run from the directory containing DREAMGPU.INF. No files or drivers are replaced
 * unless that adapter is present. Designed for disposable test guests.
 */
#define WIN32_LEAN_AND_MEAN
extern "C" {
#include <windows.h>
#include <setupapi.h>
#ifdef DG_UNATTENDED
#define MessageBoxA(window, text, title, flags) ((void)0)
#define INSTALL_LOG "C:\\DGDRV.LOG"
#else
#define INSTALL_LOG "C:\\DGSETUP.LOG"
#endif

typedef BOOL(WINAPI *UPDATE_DRIVER)(HWND, LPCSTR, LPCSTR, DWORD, PBOOL);

static void Log(HANDLE file, LPCSTR message) {
    DWORD written;
    if (file == INVALID_HANDLE_VALUE)
        return;
    WriteFile(file, message, lstrlenA(message), &written, NULL);
    WriteFile(file, "\r\n", 2, &written, NULL);
    FlushFileBuffers(file);
}

#ifdef DG_UNATTENDED
/* Windows 2000's newdev API can display this one signature warning despite
 * the unattended entry point. Match the installing process, exact caption,
 * exact DreamGPU adapter label and unique Yes button; never dismiss other UI. */
static volatile LONG DialogWorkerStop;
static DWORD InstallPid;
static HANDLE InstallLog;
typedef struct {
    BOOL device;
    BOOL xp;
    HWND yes;
    DWORD buttons;
} SignatureChildren;
static BOOL CALLBACK SignatureChild(HWND window, LPARAM opaque) {
    SignatureChildren *found = (SignatureChildren *)opaque;
    CHAR text[256], kind[32];
    if (!GetWindowTextA(window, text, sizeof(text)) || !GetClassNameA(window, kind, sizeof(kind)))
        return TRUE;
    if (!lstrcmpiA(kind, "Static") && !lstrcmpA(text, "DreamGPU"))
        found->device = TRUE;
    if (!lstrcmpiA(kind, "Button") &&
        ((!found->xp && (!lstrcmpA(text, "&Yes") || !lstrcmpA(text, "Yes"))) ||
         (found->xp &&
          (!lstrcmpA(text, "Continue Anyway") || !lstrcmpA(text, "&Continue Anyway"))))) {
        found->yes = window;
        found->buttons++;
    }
    return TRUE;
}
static BOOL CALLBACK SignatureDialog(HWND window, LPARAM ignored) {
    DWORD pid;
    CHAR title[128], kind[32];
    SignatureChildren found = {};
    DWORD_PTR result;
    (void)ignored;
    GetWindowThreadProcessId(window, &pid);
    if (pid != InstallPid || !IsWindowVisible(window) ||
        !GetWindowTextA(window, title, sizeof(title)) ||
        (lstrcmpA(title, "Digital Signature Not Found") &&
         lstrcmpA(title, "Hardware Installation")) ||
        !GetClassNameA(window, kind, sizeof(kind)) || lstrcmpA(kind, "#32770"))
        return TRUE;
    found.xp = !lstrcmpA(title, "Hardware Installation");
    EnumChildWindows(window, SignatureChild, (LPARAM)&found);
    if (found.device && found.buttons == 1 && IsWindowEnabled(found.yes)) {
        if (SendMessageTimeoutA(found.yes, BM_CLICK, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 500,
                                &result))
            Log(InstallLog,
                found.xp ? "ACKNOWLEDGED owned DreamGPU XP Hardware Installation warning"
                         : "ACKNOWLEDGED owned DreamGPU Digital Signature Not Found warning");
    }
    return TRUE;
}
static DWORD WINAPI SignatureWorker(LPVOID unused) {
    DWORD began = GetTickCount();
    (void)unused;
    while (!InterlockedCompareExchange(&DialogWorkerStop, 0, 0) &&
           GetTickCount() - began < 110000) {
        EnumWindows(SignatureDialog, 0);
        Sleep(100);
    }
    return 0;
}
#endif

static BOOL FindAdapter(CHAR *hardware_id, HANDLE log) {
    HDEVINFO devices = SetupDiGetClassDevsA(NULL, NULL, NULL, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    SP_DEVINFO_DATA info;
    DWORD index = 0, required, type;
    CHAR values[4096];
    const CHAR prefix[] = "PCI\\VEN_1234&DEV_1113";
    BOOL found = FALSE;
    if (devices == INVALID_HANDLE_VALUE)
        return FALSE;
    info.cbSize = sizeof(info);
    while (SetupDiEnumDeviceInfo(devices, index++, &info)) {
        CHAR *id;
        DWORD offset;
        if (!SetupDiGetDeviceRegistryPropertyA(devices, &info, SPDRP_HARDWAREID, &type,
                                               (BYTE *)values, sizeof(values) - 2, &required) ||
            type != REG_MULTI_SZ)
            continue;
        values[sizeof(values) - 2] = 0;
        values[sizeof(values) - 1] = 0;
        for (offset = 0; offset < sizeof(values) - 1 && values[offset];
             offset += lstrlenA(values + offset) + 1) {
            CHAR candidate[sizeof(prefix)];
            DWORD i;
            id = values + offset;
            Log(log, id);
            for (i = 0; i < sizeof(prefix) - 1 && id[i]; ++i)
                candidate[i] = id[i];
            candidate[i] = 0;
            if (i == sizeof(prefix) - 1 && !lstrcmpiA(candidate, prefix) &&
                (id[i] == '&' || id[i] == 0) && lstrlenA(id) < 256 && !found) {
                lstrcpyA(hardware_id, id);
                found = TRUE;
            }
        }
    }
    SetupDiDestroyDeviceInfoList(devices);
    return found;
}

void WINAPI WinMainCRTStartup(void) {
    CHAR path[MAX_PATH];
    CHAR message[200];
    CHAR hardware_id[256];
    HANDLE log = CreateFileA(INSTALL_LOG, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, NULL);
    HMODULE library;
    UPDATE_DRIVER update;
    BOOL reboot = FALSE;
    BOOL updated;
#ifdef DG_UNATTENDED
    HANDLE dialog_worker;
#endif
    DWORD error;
    DWORD length = GetModuleFileNameA(NULL, path, sizeof(path));
    if (!length || length >= sizeof(path))
        ExitProcess(1);
    while (length && path[length - 1] != '\\')
        --length;
    lstrcpyA(path + length, "DREAMGPU.INF");
    Log(log, "Present display-adapter hardware IDs (all devices enumerated):");
    if (!FindAdapter(hardware_id, log)) {
        Log(log, "No present PCI 1234:1113 device matched.");
        CloseHandle(log);
        MessageBoxA(NULL, "No DreamGPU is present. Device IDs are in C:\\DGSETUP.LOG.", "DreamGPU",
                    MB_ICONERROR);
        ExitProcess(4);
    }
    Log(log, "Installing matching hardware ID:");
    Log(log, hardware_id);
    Log(log, path);
    library = LoadLibraryA("newdev.dll");
    update = library ? (UPDATE_DRIVER)GetProcAddress(library, "UpdateDriverForPlugAndPlayDevicesA")
                     : NULL;
    if (!update) {
        MessageBoxA(NULL, "Windows newdev.dll driver installation API is unavailable.", "DreamGPU",
                    MB_ICONERROR);
        ExitProcess(2);
    }
    Log(log, "STAGE update matched DreamGPU display driver pair");
#ifdef DG_UNATTENDED
    InstallPid = GetCurrentProcessId();
    InstallLog = log;
    dialog_worker = CreateThread(NULL, 0, SignatureWorker, NULL, 0, NULL);
    if (!dialog_worker) {
        Log(log, "FAIL signature watcher creation");
        CloseHandle(log);
        ExitProcess(5);
    }
#endif
    updated = update(NULL, hardware_id, path, 1 /* INSTALLFLAG_FORCE */, &reboot);
    error = GetLastError();
#ifdef DG_UNATTENDED
    InterlockedExchange(&DialogWorkerStop, 1);
    if (WaitForSingleObject(dialog_worker, 2000) != WAIT_OBJECT_0) {
        Log(log, "FAIL signature watcher cleanup");
        ExitProcess(6);
    }
    CloseHandle(dialog_worker);
#endif
    SetLastError(error);
    if (!updated) {
        error = GetLastError();
        wsprintfA(message, "Driver installation failed (Windows error %lu).", error);
        Log(log, message);
        CloseHandle(log);
        MessageBoxA(NULL, message, "DreamGPU", MB_ICONERROR);
        ExitProcess(error ? error : 3);
    }
    Log(log, "Driver installed successfully.");
    Log(log, reboot ? "REBOOT_REQUIRED 1" : "REBOOT_REQUIRED 0");
    Log(log, "PASS automated ntupdate: matched DreamGPU display driver pair installed; activation "
             "requires controlled reboot");
    CloseHandle(log);
    MessageBoxA(NULL,
                reboot ? "Driver installed. Restart this test VM to activate it."
                       : "Driver installed.",
                "DreamGPU", MB_ICONINFORMATION);
    ExitProcess(0);
}

} /* extern C */
