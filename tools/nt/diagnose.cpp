/* SPDX-License-Identifier: GPL-2.0-or-later
 * NT5 bring-up diagnostics. Writes C:\DGDIAG.LOG and requests the baseline
 * 1024x768x32 mode only when that exact mode is enumerated by the active driver.
 */
#define WIN32_LEAN_AND_MEAN
extern "C" {
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>

static HANDLE log_file;
static void Log(LPCSTR line) {
    DWORD written;
    WriteFile(log_file, line, lstrlenA(line), &written, NULL);
    WriteFile(log_file, "\r\n", 2, &written, NULL);
}

void WINAPI WinMainCRTStartup(void) {
    HDEVINFO devices;
    SP_DEVINFO_DATA info;
    DISPLAY_DEVICEA display;
    DEVMODEA mode;
    HDC dc;
    HKEY key;
    DWORD index, size, type, status, problem;
    CONFIGRET result;
    CHAR line[512];
    static CHAR ids[4096];
    const CHAR prefix[] = "PCI\\VEN_1234&DEV_1113";
    BOOL desired = FALSE, active_service = FALSE, active_provider = FALSE;
    log_file = CreateFileA("C:\\DGDIAG.LOG", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (log_file == INVALID_HANDLE_VALUE)
        ExitProcess(1);
    dc = GetDC(NULL);
    wsprintfA(line, "Screen: %ld x %ld, %ld bpp, raster caps %08lx", GetDeviceCaps(dc, HORZRES),
              GetDeviceCaps(dc, VERTRES), GetDeviceCaps(dc, BITSPIXEL),
              GetDeviceCaps(dc, RASTERCAPS));
    Log(line);
    ReleaseDC(NULL, dc);
    for (index = 0;; ++index) {
        ZeroMemory(&display, sizeof(display));
        display.cb = sizeof(display);
        if (!EnumDisplayDevicesA(NULL, index, &display, 0))
            break;
        Log(display.DeviceName);
        Log(display.DeviceString);
        Log(display.DeviceID);
        Log(display.DeviceKey);
        wsprintfA(line, "Display flags: %08lx", display.StateFlags);
        Log(line);
    }
    devices = SetupDiGetClassDevsA(NULL, NULL, NULL, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    info.cbSize = sizeof(info);
    for (index = 0; SetupDiEnumDeviceInfo(devices, index, &info); ++index) {
        if (!SetupDiGetDeviceRegistryPropertyA(devices, &info, SPDRP_HARDWAREID, &type, (BYTE *)ids,
                                               sizeof(ids), &size))
            continue;
        lstrcpynA(line, ids, sizeof(prefix));
        if (lstrcmpiA(line, prefix) || (ids[sizeof(prefix) - 1] && ids[sizeof(prefix) - 1] != '&'))
            continue;
        Log("DreamGPU device:");
        Log(ids);
        status = problem = 0;
        result = CM_Get_DevNode_Status(&status, &problem, info.DevInst, 0);
        wsprintfA(line, "CM result=%lu status=%08lx problem=%lu", result, status, problem);
        Log(line);
        if (SetupDiGetDeviceRegistryPropertyA(devices, &info, SPDRP_SERVICE, &type, (BYTE *)ids,
                                              sizeof(ids), &size)) {
            ids[sizeof(ids) - 1] = 0;
            Log("Service:");
            Log(ids);
            active_service = result == CR_SUCCESS && !problem && (status & DN_STARTED) &&
                             lstrcmpiA(ids, "dgpumini") == 0;
        }
        if (SetupDiGetDeviceRegistryPropertyA(devices, &info, SPDRP_DRIVER, &type, (BYTE *)ids,
                                              sizeof(ids), &size)) {
            Log("Driver class key:");
            Log(ids);
        }
    }
    SetupDiDestroyDeviceInfoList(devices);
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Services\\dgpumini", 0,
                      KEY_READ, &key) == ERROR_SUCCESS) {
        HKEY device_key;
        /* Display .SoftwareSettings lives under the video miniport's Device0,
         * not the PnP class key. Verify the provider for the started service. */
        if (RegOpenKeyExA(key, "Device0", 0, KEY_READ, &device_key) == ERROR_SUCCESS) {
            size = sizeof(ids);
            if (RegQueryValueExA(device_key, "InstalledDisplayDrivers", NULL, &type, (BYTE *)ids,
                                 &size) == ERROR_SUCCESS) {
                ids[sizeof(ids) - 1] = 0;
                Log("InstalledDisplayDrivers:");
                Log(ids);
                active_provider =
                    (type == REG_MULTI_SZ || type == REG_SZ) && lstrcmpiA(ids, "dgpudisp") == 0;
            }
            RegCloseKey(device_key);
        }
        static const LPCSTR names[] = {"DreamGPUEntryStatus",
                                       "DreamGPUInitStage",
                                       "DreamGPURange0Size",
                                       "DreamGPURange1Size",
                                       "DreamGPUAssignedInterruptLevel",
                                       "DreamGPUAssignedInterruptVector",
                                       "DreamGPUInterruptLevel",
                                       "DreamGPUInterruptVector",
                                       "DreamGPUInterruptMode",
                                       "DreamGPUTimeoutCount",
                                       "DreamGPUInterruptCount",
                                       "DreamGPUDpcCount",
                                       "DreamGPUWaitStatus",
                                       "DreamGPULastDeviceStatus"};
        for (index = 0; index < sizeof(names) / sizeof(names[0]); ++index) {
            size = sizeof(ids);
            if (RegQueryValueExA(key, names[index], NULL, &type, (BYTE *)ids, &size) ==
                ERROR_SUCCESS) {
                wsprintfA(line, "%s: %08lx (%lu)", names[index], *(DWORD *)ids, *(DWORD *)ids);
                Log(line);
            } else {
                wsprintfA(line, "%s: absent", names[index]);
                Log(line);
            }
        }
        RegCloseKey(key);
    }
    Log("Available display modes:");
    for (index = 0;; ++index) {
        ZeroMemory(&mode, sizeof(mode));
        mode.dmSize = sizeof(mode);
        if (!EnumDisplaySettingsA(NULL, index, &mode))
            break;
        wsprintfA(line, "%lu x %lu x %lu @ %lu", mode.dmPelsWidth, mode.dmPelsHeight,
                  mode.dmBitsPerPel, mode.dmDisplayFrequency);
        Log(line);
        if (mode.dmPelsWidth == 1024 && mode.dmPelsHeight == 768 && mode.dmBitsPerPel == 32)
            desired = TRUE;
    }
#ifndef DG_UNATTENDED
    Log(active_service && active_provider ? "DreamGPU driver is active"
                                          : "DreamGPU driver is not active");
    if (desired) {
        ZeroMemory(&mode, sizeof(mode));
        mode.dmSize = sizeof(mode);
        mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
        mode.dmPelsWidth = 1024;
        mode.dmPelsHeight = 768;
        mode.dmBitsPerPel = 32;
        wsprintfA(line, "Set baseline mode result: %ld",
                  ChangeDisplaySettingsA(&mode, CDS_UPDATEREGISTRY));
        Log(line);
    } else
        Log("1024x768x32 is not advertised: did not attempt an unsupported mode.");
#else
    (void)desired;
    Log(active_service && active_provider
            ? "PASS automated ntdiag: started DreamGPU device uses dgpumini and dgpudisp"
            : "FAIL automated ntdiag: renamed DreamGPU driver is not active");
#endif
    CloseHandle(log_file);
#ifndef DG_UNATTENDED
    MessageBoxA(NULL, "Diagnostics written to C:\\DGDIAG.LOG", "DreamGPU", MB_OK);
#else
    ExitProcess(active_service && active_provider ? 0 : 1);
#endif
    ExitProcess(0);
}

} /* extern C */
