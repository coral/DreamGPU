/* SPDX-License-Identifier: GPL-2.0-or-later
 * Fixed Win98 display installation and cold-activation audit. The OS installs
 * one checked INF for one present PCI identity; no registry paths are rewritten.
 * Win98 exports the SetupAPI functions used here. Its V1 driver-info structure
 * must be requested explicitly; the later V2 layout is not its contract.
 */
#define WIN32_LEAN_AND_MEAN
#define USE_SP_DRVINFO_DATA_V1 1
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include "dg-escape.h"
#include "../setup/driver-node.h"

static_assert(sizeof(SP_DRVINFO_DATA_A) == 780, "Win98 driver-info V1 ABI");
static_assert(sizeof(SP_DEVINFO_DATA) == 28, "Win98 device-info ABI");
static HANDLE Log = INVALID_HANDLE_VALUE;
static void Line(const char *text) {
    DWORD written;
    WriteFile(Log, text, lstrlenA(text), &written, nullptr);
    WriteFile(Log, "\r\n", 2, &written, nullptr);
    FlushFileBuffers(Log);
}
static bool Fail(const char *stage) {
    char text[160];
    wsprintfA(text, "FAIL %s error=%lu", stage, GetLastError());
    Line(text);
    return false;
}
class Devices final {
  public:
    HDEVINFO value =
        SetupDiGetClassDevsA(nullptr, nullptr, nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    Devices() = default;
    Devices(const Devices &) = delete;
    ~Devices() {
        if (value != INVALID_HANDLE_VALUE)
            SetupDiDestroyDeviceInfoList(value);
    }
};
class Key final {
  public:
    HKEY value = nullptr;
    Key() = default;
    Key(const Key &) = delete;
    ~Key() {
        if (value && value != INVALID_HANDLE_VALUE)
            RegCloseKey(value);
    }
};
class DeviceFile final {
  public:
    HANDLE value = CreateFileA("\\\\.\\DREAMGPU", 0, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    DeviceFile() = default;
    DeviceFile(const DeviceFile &) = delete;
    ~DeviceFile() {
        if (value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
};
static bool MatchId(const char *id) {
    constexpr char wanted[] = "PCI\\VEN_1234&DEV_1113";
    char prefix[sizeof(wanted)]{};
    unsigned i = 0;
    for (; i < sizeof(wanted) - 1 && id[i]; i++)
        prefix[i] = id[i];
    return i == sizeof(wanted) - 1 && !lstrcmpiA(prefix, wanted) &&
           (!id[i] || id[i] == '&' || id[i] == '\\');
}
static bool FindAdapter(Devices &devices, SP_DEVINFO_DATA &selected) {
    unsigned matches = 0;
    SP_DEVINFO_DATA current{};
    current.cbSize = sizeof(current);
    if (devices.value == INVALID_HANDLE_VALUE)
        return Fail("enumerate devices");
    for (DWORD i = 0; i < 512; i++) {
        if (!SetupDiEnumDeviceInfo(devices.value, i, &current)) {
            if (GetLastError() != ERROR_NO_MORE_ITEMS)
                return Fail("device enumeration");
            if (matches != 1) {
                SetLastError(ERROR_INVALID_DATA);
                return Fail("expected one PCI1234:1113 adapter");
            }
            return true;
        }
        // Win98's HardwareID property is REG_SZ and its RequiredSize is not
        // reliable. The present CM devnode ID is the actual enumeration ABI.
        char id[MAX_DEVICE_ID_LEN]{};
        CONFIGRET result = CM_Get_Device_IDA(current.DevInst, id, sizeof(id), 0);
        if (result != CR_SUCCESS)
            continue;
        unsigned end = 0;
        while (end < sizeof(id) && id[end])
            end++;
        if (end == sizeof(id)) {
            SetLastError(ERROR_INVALID_DATA);
            return Fail("unterminated device ID");
        }
        if (MatchId(id)) {
            selected = current;
            matches++;
            Line(id);
        }
    }
    SetLastError(ERROR_MORE_DATA);
    return Fail("device enumeration bound");
}
static bool Value(HKEY key, const char *name, const char *expected) {
    char text[256]{};
    DWORD type = 0, bytes = sizeof(text);
    LONG error =
        RegQueryValueExA(key, name, nullptr, &type, reinterpret_cast<BYTE *>(text), &bytes);
    if (error || type != REG_SZ || !bytes || bytes > sizeof(text) || text[bytes - 1] ||
        lstrcmpiA(text, expected)) {
        SetLastError(error ? error : ERROR_INVALID_DATA);
        return Fail(name);
    }
    Line(name);
    Line(text);
    return true;
}
static bool DriverPair(Devices &devices, SP_DEVINFO_DATA &device) {
    Key driver, defaults;
    driver.value =
        SetupDiOpenDevRegKey(devices.value, &device, DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_READ);
    if (driver.value == INVALID_HANDLE_VALUE)
        return Fail("driver registry key");
    LONG error = RegOpenKeyExA(driver.value, "DEFAULT", 0, KEY_READ, &defaults.value);
    if (error) {
        SetLastError(error);
        return Fail("driver DEFAULT key");
    }
    return Value(defaults.value, "drv", "dgpumini.drv") &&
           Value(defaults.value, "minivdd", "dgpumini.vxd");
}
#ifndef DG_DIAGNOSTIC
static bool Install(Devices &devices, SP_DEVINFO_DATA &device) {
    char inf[MAX_PATH]{};
    DWORD length = GetModuleFileNameA(nullptr, inf, sizeof(inf));
    if (!length || length >= sizeof(inf))
        return Fail("installer path");
    while (length && inf[length - 1] != '\\')
        length--;
    if (!length || length + sizeof("DG9X.INF") > sizeof(inf))
        return Fail("INF path bound");
    lstrcpyA(inf + length, "DG9X.INF");
    Line(inf);
    if (GetFileAttributesA(inf) == INVALID_FILE_ATTRIBUTES)
        return Fail("fixed INF missing");
    setup::driver::Node expected{};
    lstrcpyA(expected.inf, inf);
    lstrcpyA(expected.description, "DreamGPU");
    lstrcpyA(expected.provider, "DreamGPU");
    lstrcpyA(expected.section, "Dg");
    setup::driver::DriverList list(devices.value, device);
    if (!list.select(expected))
        return Fail("unique exact INF selection");
    Line("STAGE DIF_INSTALLDEVICE exact DreamGPU INF; quiet installation");
    if (!list.bind())
        return Fail("install selected device");
    SP_DEVINSTALL_PARAMS_A parameters{};
    parameters.cbSize = sizeof(parameters);
    if (!DriverPair(devices, device))
        return false;
    if (!SetupDiGetDeviceInstallParamsA(devices.value, &device, &parameters))
        return Fail("read restart status");
    char status[100];
    wsprintfA(status, "INSTALL_FLAGS %08lx; cold reboot required before activation audit",
              parameters.Flags);
    Line(status);
    Line("PASS automated win9xinstall: exact PCI adapter installed with fixed DreamGPU driver "
         "pair; reboot required");
    return true;
}
#else
static bool Diagnose(Devices &devices, SP_DEVINFO_DATA &device) {
    ULONG status = 0, problem = 0;
    CONFIGRET result = CM_Get_DevNode_Status(&status, &problem, device.DevInst, 0);
    char text[128];
    wsprintfA(text, "CM result=%lu status=%08lx problem=%lu", result, status, problem);
    Line(text);
    if (result != CR_SUCCESS || problem || !(status & DN_STARTED)) {
        SetLastError(ERROR_INVALID_DATA);
        return Fail("started device status");
    }
    if (!DriverPair(devices, device))
        return false;
    DeviceFile file;
    if (file.value == INVALID_HANDLE_VALUE)
        return Fail("loaded DREAMGPU VxD");
    DG_ESCAPE_REQUEST request{};
    DG_ESCAPE_REPLY reply{};
    DWORD returned = 0;
    request.Version = DG_ESCAPE_VERSION;
    request.Operation = DG_ESCAPE_OPEN;
    if (!DeviceIoControl(file.value, DG_ESCAPE, &request, sizeof(request), &reply, sizeof(reply),
                         &returned, nullptr) ||
        returned != sizeof(reply) || reply.Version != DG_ESCAPE_VERSION ||
        reply.Status != DG_ESCAPE_OK || !reply.Client)
        return Fail("active VxD channel OPEN");
    request.Operation = DG_ESCAPE_CLOSE;
    request.Client = reply.Client;
    if (!DeviceIoControl(file.value, DG_ESCAPE, &request, sizeof(request), &reply, sizeof(reply),
                         &returned, nullptr) ||
        returned != sizeof(reply) || reply.Version != DG_ESCAPE_VERSION ||
        reply.Status != DG_ESCAPE_OK || reply.Client != request.Client)
        return Fail("active VxD channel CLOSE");
    Line("PASS automated win9xdiag: started DreamGPU driver pair and loaded DREAMGPU VxD channel "
         "verified");
    return true;
}
#endif
extern "C" void WINAPI WinMainCRTStartup(void) {
    Log = CreateFileA(
#ifdef DG_DIAGNOSTIC
        "C:\\DG9AUDIT.LOG",
#else
        "C:\\DG9INST.LOG",
#endif
        GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (Log == INVALID_HANDLE_VALUE)
        ExitProcess(2);
    bool okay = false;
    if (!(GetVersion() & 0x80000000UL)) {
        SetLastError(ERROR_OLD_WIN_VERSION);
        Fail("requires Windows9x");
    } else {
        Devices devices;
        SP_DEVINFO_DATA device{};
        device.cbSize = sizeof(device);
        if (FindAdapter(devices, device)) {
#ifdef DG_DIAGNOSTIC
            okay = Diagnose(devices, device);
#else
            okay = Install(devices, device);
#endif
        }
    }
    CloseHandle(Log);
    ExitProcess(okay ? 0 : 1);
}
