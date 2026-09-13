// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "driver-oem-test-win32.h"
#include "driver-unbound.h"
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
static bool exists_service, marked, malformed_config, marked_openable;
static DWORD service_state = SERVICE_RUNNING, service_start = SERVICE_SYSTEM_START;
static unsigned service_handles, manager_handles, deletes;
static std::string binary = "\\SystemRoot\\System32\\drivers\\dgpumini.sys";
static constexpr const char *ServiceKey = "system\\currentcontrolset\\services\\dgpumini";
static SC_HANDLE OpenSCManagerA(const char *, const char *, DWORD flags) {
    assert(flags == SC_MANAGER_CONNECT);
    ++manager_handles;
    return 1000;
}
static SC_HANDLE OpenServiceA(SC_HANDLE, const char *name, DWORD) {
    assert(!strcmp(name, "dgpumini"));
    if (!exists_service) {
        fake_win32::error = ERROR_SERVICE_DOES_NOT_EXIST;
        return 0;
    }
    if (marked && !marked_openable) {
        fake_win32::error = ERROR_SERVICE_MARKED_FOR_DELETE;
        return 0;
    }
    ++service_handles;
    return 1001;
}
static BOOL CloseServiceHandle(SC_HANDLE h) {
    if (h == 1000) {
        assert(manager_handles);
        --manager_handles;
    } else {
        assert(service_handles);
        --service_handles;
        if (marked && service_state == SERVICE_STOPPED && !service_handles) {
            exists_service = false;
            fake_win32::keys.erase(ServiceKey);
        }
    }
    return TRUE;
}
static BOOL QueryServiceConfigA(SC_HANDLE, QUERY_SERVICE_CONFIGA *out, DWORD cap, DWORD *required) {
    *required = 1024;
    assert(cap >= 1024);
    auto *cursor = reinterpret_cast<char *>(out + 1);
    auto put = [&cursor](const char *text) {
        char *p = cursor;
        strcpy(cursor, text);
        cursor += strlen(text) + 1;
        return p;
    };
    out->dwServiceType = SERVICE_KERNEL_DRIVER;
    out->dwStartType = service_start;
    out->dwErrorControl = SERVICE_ERROR_IGNORE;
    out->dwTagId = 7;
    out->lpBinaryPathName = put(binary.c_str());
    out->lpLoadOrderGroup = put("Video");
    out->lpServiceStartName = put("");
    out->lpDisplayName = put("dgpumini");
    out->lpDependencies = put("");
    if (malformed_config)
        out->lpBinaryPathName = reinterpret_cast<char *>(uintptr_t(1));
    return TRUE;
}
static BOOL QueryServiceStatus(SC_HANDLE, SERVICE_STATUS *out) {
    out->dwServiceType = SERVICE_KERNEL_DRIVER;
    out->dwCurrentState = service_state;
    return TRUE;
}
static BOOL DeleteService(SC_HANDLE) {
    if (fake_win32::fault())
        return FALSE;
    if (marked) {
        fake_win32::error = ERROR_SERVICE_MARKED_FOR_DELETE;
        return FALSE;
    }
    ++deletes;
    marked = true;
    if (marked_openable)
        service_start = SERVICE_DISABLED;
    return TRUE;
}
static HMODULE GetModuleHandleA(const char *) {
    return 4;
}
static FARPROC GetProcAddress(HMODULE, const char *name) {
    FARPROC result = nullptr;
    auto copy = [&result](auto f) {
        static_assert(sizeof(f) == sizeof(result));
        memcpy(&result, &f, sizeof(result));
    };
    if (!strcmp(name, "OpenSCManagerA"))
        copy(&OpenSCManagerA);
    if (!strcmp(name, "OpenServiceA"))
        copy(&OpenServiceA);
    if (!strcmp(name, "QueryServiceConfigA"))
        copy(&QueryServiceConfigA);
    if (!strcmp(name, "QueryServiceStatus"))
        copy(&QueryServiceStatus);
    if (!strcmp(name, "DeleteService"))
        copy(&DeleteService);
    if (!strcmp(name, "CloseServiceHandle"))
        copy(&CloseServiceHandle);
    return result;
}
