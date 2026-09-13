// SPDX-License-Identifier: GPL-2.0-or-later
// Syscall seam for production driver adapter; no transaction policy duplicated.
#pragma once
#ifndef DG_SETUP_ADAPTER_TEST
#define DG_SETUP_ADAPTER_TEST
#endif
#define DG_DRIVER_CHILD_TEST
#include "test-win32.h"
#include <functional>
#include <cassert>
#include "driver-lifecycle.h"
using BOOL = int;
using ULONG = uint32_t;
using HDEVINFO = intptr_t;
constexpr BOOL FALSE = 0;
constexpr DWORD ERROR_PATH_NOT_FOUND = 3, ERROR_NO_MORE_ITEMS = 259, ERROR_INVALID_PARAMETER = 87,
                CR_SUCCESS = 0, DN_STARTED = 8, SPDIT_COMPATDRIVER = 2, DI_ENUMSINGLEINF = 1,
                DI_QUIETINSTALL = 2, DI_NOFILECOPY = 4, DIF_INSTALLDEVICE = 2, DICS_FLAG_GLOBAL = 1,
                DIREG_DRV = 2, DIGCF_PRESENT = 2, DIGCF_ALLCLASSES = 4, SPDRP_SERVICE = 4,
                SPDRP_DRIVER = 9, CREATE_SUSPENDED = 4, WAIT_OBJECT_0 = 0, WAIT_TIMEOUT = 258,
                SYNCHRONIZE = 0x100000, PROCESS_QUERY_INFORMATION = 0x400, PROCESS_VM_READ = 0x10;
struct SP_DEVINFO_DATA {
    DWORD cbSize = 0, DevInst = 0;
};
struct SP_DRVINFO_DATA_A {
    DWORD cbSize = 0, DriverType = 0, Reserved = 0;
    char Description[256]{}, MfgName[256]{}, ProviderName[256]{};
};
using UINT = unsigned;
using UINT_PTR = uintptr_t;
inline void SetLastError(DWORD error) {
    fake_win32::error = error;
}
#ifndef CALLBACK
#define CALLBACK
#endif
using PSP_FILE_CALLBACK_A = UINT (*)(void *, UINT, UINT_PTR, UINT_PTR);
constexpr UINT SPFILENOTIFY_NEEDMEDIA = 14, SPFILENOTIFY_COPYERROR = 13,
               SPFILENOTIFY_DELETEERROR = 5, SPFILENOTIFY_RENAMEERROR = 9,
               SPFILENOTIFY_STARTQUEUE = 1, SPFILENOTIFY_STARTSUBQUEUE = 3,
               SPFILENOTIFY_STARTCOPY = 11, SPFILENOTIFY_TARGETEXISTS = 0x20000,
               SPFILENOTIFY_TARGETNEWER = 0x40000, SPFILENOTIFY_LANGMISMATCH = 0x10000,
               SPFILENOTIFY_STARTDELETE = 4, SPFILENOTIFY_STARTRENAME = 8, FILEOP_ABORT = 0,
               FILEOP_DOIT = 1, FILEOP_NEWPATH = 4, SRCLIST_TEMPORARY = 1, SRCLIST_NOBROWSE = 2;
constexpr DWORD ERROR_INVALID_DATA = 13, ERROR_CANCELLED = 1223,
                ERROR_KEY_DOES_NOT_EXIST = 0xe0000204;
struct SOURCE_MEDIA_A {
    const char *SourceFile;
};
struct SP_DEVINSTALL_PARAMS_A {
    DWORD cbSize = 0, Flags = 0;
    char DriverPath[MAX_PATH]{};
    PSP_FILE_CALLBACK_A InstallMsgHandler = nullptr;
    void *InstallMsgHandlerContext = nullptr;
};
struct SP_DRVINFO_DETAIL_DATA_A {
    DWORD cbSize = 0;
    char InfFileName[MAX_PATH]{}, SectionName[256]{}, HardwareID[256]{};
};
struct STARTUPINFOA {
    DWORD cb = 0;
};
struct PROCESS_INFORMATION {
    HANDLE hProcess = 0, hThread = 0;
    DWORD dwProcessId = 0, dwThreadId = 0;
};
inline int lstrcmpiA(const char *a, const char *b) {
    return fake_win32::canon(a).compare(fake_win32::canon(b));
}
inline int driver_printf(char *out, const char *format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[1024];
    int n = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    assert(n >= 0 && n < int(sizeof(buffer)));
    memcpy(out, buffer, size_t(n) + 1);
    return n;
}
#define wsprintfA driver_printf
namespace driver_fake {
inline setup::driver::Node current;
inline std::vector<setup::driver::Node> compatible;
inline SP_DEVINSTALL_PARAMS_A params;
inline std::function<void(const std::string &)> execute;
inline std::string executable;
inline unsigned selected = 0, binds = 0, launches = 0, resumes = 0, destroys = 0;
inline uint32_t created_low = 1, created_high = 1;
inline bool times_available = true, path_available = true, leave_running = false;
inline bool child_creation(HANDLE, uint32_t &low, uint32_t &high) {
    low = created_low;
    high = created_high;
    return times_available;
}
inline bool child_executable(HANDLE, char out[MAX_PATH]) {
    if (!path_available || executable.size() >= MAX_PATH)
        return false;
    strcpy(out, executable.c_str());
    return true;
}
inline bool live = false, started = true, malformed_detail = false, ambiguous = false;
inline bool present = true, driver_key = true;
inline DWORD property_error = 0;
inline void reg(const char *name, const char *value) {
    auto *p = reinterpret_cast<const BYTE *>(value);
    fake_win32::keys["driver"][fake_win32::canon(name)] = {REG_SZ, {p, p + strlen(value) + 1}};
}
inline void set_node(const setup::driver::Node &node) {
    current = node;
    driver_key = node.inf[0] != 0;
    if (!driver_key) {
        fake_win32::keys.erase("driver");
        return;
    }
    reg("binding", "{display}\\0000");
    const char *name = strrchr(node.inf, '\\');
    assert(name);
    reg("InfPath", name + 1);
    reg("InfSection", node.section);
    reg("DriverDesc", node.description);
    reg("ProviderName", node.provider);
}
} // namespace driver_fake
inline DWORD GetWindowsDirectoryA(char *out, DWORD n) {
    constexpr char p[] = "C:\\WINDOWS";
    if (n < sizeof(p))
        return sizeof(p);
    strcpy(out, p);
    return sizeof(p) - 1;
}
inline DWORD GetModuleFileNameA(void *, char *out, DWORD n) {
    constexpr char p[] = "C:\\dreamgpu.exe";
    if (n < sizeof(p))
        return n;
    strcpy(out, p);
    return sizeof(p) - 1;
}
inline HDEVINFO SetupDiGetClassDevsA(void *, void *, void *, DWORD) {
    return 2;
}
inline BOOL SetupDiDestroyDeviceInfoList(HDEVINFO) {
    return TRUE;
}
inline BOOL SetupDiEnumDeviceInfo(HDEVINFO, DWORD n, SP_DEVINFO_DATA *d) {
    if (n || !driver_fake::present) {
        fake_win32::error = ERROR_NO_MORE_ITEMS;
        return FALSE;
    }
    d->DevInst = 1;
    return TRUE;
}
inline DWORD CM_Get_Device_IDA(DWORD, char *out, DWORD size, DWORD) {
    if (strlen(driver_fake::current.device) >= size)
        return 1;
    strcpy(out, driver_fake::current.device);
    return 0;
}
inline DWORD CM_Get_DevNode_Status(ULONG *status, ULONG *problem, DWORD, DWORD) {
    *status = driver_fake::started ? DN_STARTED : 0;
    *problem = 0;
    return CR_SUCCESS;
}
inline HKEY SetupDiOpenDevRegKey(HDEVINFO, SP_DEVINFO_DATA *, DWORD, DWORD, DWORD, DWORD) {
    if (!driver_fake::driver_key) {
        SetLastError(ERROR_KEY_DOES_NOT_EXIST);
        return INVALID_HANDLE_VALUE;
    }
    auto h = fake_win32::next++;
    fake_win32::key_handles[h] = "driver";
    return h;
}
inline BOOL SetupDiGetDeviceRegistryPropertyA(HDEVINFO, SP_DEVINFO_DATA *, DWORD property,
                                              DWORD *type, BYTE *out, DWORD capacity,
                                              DWORD *bytes) {
    if (driver_fake::property_error) {
        SetLastError(driver_fake::property_error);
        return FALSE;
    }
    const char *name = property == SPDRP_DRIVER ? "binding" : "service";
    if (!fake_win32::keys["driver"].count(name)) {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }
    auto &v = fake_win32::keys["driver"][name];
    if (v.bytes.size() > capacity)
        return FALSE;
    *bytes = DWORD(v.bytes.size());
    *type = v.type;
    memcpy(out, v.bytes.data(), v.bytes.size());
    return TRUE;
}
inline BOOL SetupDiGetDeviceInstallParamsA(HDEVINFO, SP_DEVINFO_DATA *, SP_DEVINSTALL_PARAMS_A *p) {
    *p = driver_fake::params;
    return TRUE;
}
inline BOOL SetupDiSetDeviceInstallParamsA(HDEVINFO, SP_DEVINFO_DATA *, SP_DEVINSTALL_PARAMS_A *p) {
    driver_fake::params = *p;
    return TRUE;
}
inline BOOL SetupDiBuildDriverInfoList(HDEVINFO, SP_DEVINFO_DATA *, DWORD) {
    return TRUE;
}
inline BOOL SetupDiDestroyDriverInfoList(HDEVINFO, SP_DEVINFO_DATA *, DWORD) {
    driver_fake::destroys++;
    return TRUE;
}
inline std::vector<setup::driver::Node> driver_matches() {
    std::vector<setup::driver::Node> result;
    for (auto &node : driver_fake::compatible)
        if (!lstrcmpiA(node.inf, driver_fake::params.DriverPath))
            result.push_back(node);
    if (driver_fake::ambiguous && !result.empty())
        result.push_back(result[0]);
    return result;
}
inline BOOL SetupDiEnumDriverInfoA(HDEVINFO, SP_DEVINFO_DATA *, DWORD, DWORD n,
                                   SP_DRVINFO_DATA_A *out) {
    auto entries = driver_matches();
    if (n >= entries.size()) {
        fake_win32::error = ERROR_NO_MORE_ITEMS;
        return FALSE;
    }
    out->Reserved = n;
    strcpy(out->Description, entries[n].description);
    strcpy(out->ProviderName, entries[n].provider);
    return TRUE;
}
inline BOOL SetupDiGetDriverInfoDetailA(HDEVINFO, SP_DEVINFO_DATA *, SP_DRVINFO_DATA_A *item,
                                        SP_DRVINFO_DETAIL_DATA_A *out, DWORD, DWORD *required) {
    auto entries = driver_matches();
    auto &node = entries[item->Reserved];
    strcpy(out->InfFileName, node.inf);
    strcpy(out->SectionName, node.section);
    *required = driver_fake::malformed_detail ? 5000 : sizeof(*out);
    return TRUE;
}
inline BOOL SetupDiSetSelectedDriverA(HDEVINFO, SP_DEVINFO_DATA *, SP_DRVINFO_DATA_A *d) {
    driver_fake::selected = d->Reserved;
    return TRUE;
}
inline std::string temporary_source;
inline BOOL SetupSetSourceListA(DWORD flags, const char **sources, UINT count) {
    assert(flags == (SRCLIST_TEMPORARY | SRCLIST_NOBROWSE) && count == 1);
    if (fake_win32::fault())
        return FALSE;
    temporary_source = sources[0];
    return TRUE;
}
inline BOOL SetupCancelTemporarySourceList() {
    temporary_source.clear();
    return TRUE;
}
inline BOOL SetupDiCallClassInstaller(DWORD, HDEVINFO, SP_DEVINFO_DATA *) {
    if (fake_win32::fault())
        return FALSE;
    driver_fake::binds++;
    driver_fake::set_node(driver_matches()[driver_fake::selected]);
    return TRUE;
}
inline BOOL CreateProcessA(const char *exe, char *, void *, void *, BOOL, DWORD flags, void *,
                           const char *, STARTUPINFOA *, PROCESS_INFORMATION *pi) {
    assert(flags == CREATE_SUSPENDED);
    if (fake_win32::fault())
        return FALSE;
    driver_fake::executable = exe;
    driver_fake::live = true;
    driver_fake::launches++;
    *pi = {9000, 9001, 9000, 9001};
    return TRUE;
}
inline DWORD ResumeThread(HANDLE) {
    if (fake_win32::fault())
        return DWORD(-1);
    driver_fake::resumes++;
    if (driver_fake::execute)
        driver_fake::execute(driver_fake::executable);
    driver_fake::live = driver_fake::leave_running;
    return 1;
}
inline BOOL TerminateProcess(HANDLE, DWORD) {
    driver_fake::live = false;
    return TRUE;
}
inline DWORD WaitForSingleObject(HANDLE, DWORD) {
    return driver_fake::live ? WAIT_TIMEOUT : WAIT_OBJECT_0;
}
inline BOOL GetExitCodeProcess(HANDLE, DWORD *code) {
    *code = 0;
    return TRUE;
}
inline HANDLE OpenProcess(DWORD, BOOL, DWORD) {
    if (driver_fake::live)
        return 9000;
    fake_win32::error = ERROR_INVALID_PARAMETER;
    return 0;
}

#ifndef DG_DRIVER_OEM_EXTENDED
#include "driver-bound-test-extra.h"
#endif
