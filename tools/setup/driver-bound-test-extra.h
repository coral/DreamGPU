// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
// Compile seams for the legacy bound-driver suite. First-install paths have
// separate actual syscall suites; reaching one here is an explicit test error.
constexpr DWORD CREATE_ALWAYS = 5, ERROR_NO_MORE_FILES = 18, DIF_REMOVE = 5,
                DI_REMOVEDEVICE_GLOBAL = 1;
struct SP_CLASSINSTALL_HEADER {
    DWORD cbSize{}, InstallFunction{};
};
struct SP_REMOVEDEVICE_PARAMS {
    SP_CLASSINSTALL_HEADER ClassInstallHeader{};
    DWORD Scope{}, HwProfile{};
};
struct WIN32_FIND_DATAA {
    DWORD dwFileAttributes{};
    char cFileName[260]{};
};
inline HANDLE FindFirstFileA(const char *, WIN32_FIND_DATAA *) {
    assert(false && "unbound capture outside bound suite");
    return INVALID_HANDLE_VALUE;
}
inline BOOL FindNextFileA(HANDLE, WIN32_FIND_DATAA *) {
    assert(false);
    return FALSE;
}
inline BOOL FindClose(HANDLE) {
    return TRUE;
}
inline BOOL SetupDiSetClassInstallParamsA(HDEVINFO, SP_DEVINFO_DATA *, SP_CLASSINSTALL_HEADER *,
                                          DWORD) {
    assert(false && "unbound removal outside bound suite");
    return FALSE;
}
using SC_HANDLE = uintptr_t;
using HMODULE = uintptr_t;
using FARPROC = void (*)();
#define WINAPI
constexpr DWORD SERVICE_KERNEL_DRIVER = 1, SERVICE_SYSTEM_START = 1, SERVICE_DEMAND_START = 3,
                SERVICE_DISABLED = 4, SERVICE_ERROR_IGNORE = 0;
constexpr DWORD SC_MANAGER_CONNECT = 1, SERVICE_QUERY_CONFIG = 1, SERVICE_QUERY_STATUS = 4,
                DELETE = 0x10000;
constexpr DWORD SERVICE_STOPPED = 1, SERVICE_RUNNING = 4, ERROR_SERVICE_DOES_NOT_EXIST = 1060,
                ERROR_SERVICE_MARKED_FOR_DELETE = 1072;
struct QUERY_SERVICE_CONFIGA {
    DWORD dwServiceType{}, dwStartType{}, dwErrorControl{};
    char *lpBinaryPathName{};
    char *lpLoadOrderGroup{};
    DWORD dwTagId{};
    char *lpDependencies{};
    char *lpServiceStartName{};
    char *lpDisplayName{};
};
struct SERVICE_STATUS {
    DWORD dwServiceType{}, dwCurrentState{};
};
inline HMODULE GetModuleHandleA(const char *) {
    return 0; // This bound-only fixture intentionally has no NT service adapter.
}
inline FARPROC GetProcAddress(HMODULE, const char *) {
    return nullptr;
}
