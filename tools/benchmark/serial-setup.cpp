/* SPDX-License-Identifier: GPL-2.0-or-later
 * One-shot disposable NT guest setup. Start the installed legacy Serial driver;
 * record every result and verify that COM1 exists. No registry exports or UI.
 */
#define WIN32_LEAN_AND_MEAN
extern "C" {
#include <windows.h>
#include <winsvc.h>
void *memset(void *destination, int value, unsigned int count) {
    volatile BYTE *p = static_cast<volatile BYTE *>(destination);
    while (count--)
        *p++ = (BYTE)value;
    return destination;
}
static HANDLE log_file;
static void Log(const char *stage, DWORD value) {
    static const char hex[] = "0123456789abcdef";
    char number[] = " value=0x00000000\r\n";
    DWORD written;
    unsigned i;
    for (i = 0; i < 8; ++i)
        number[9 + i] = hex[(value >> (28 - i * 4)) & 15];
    WriteFile(log_file, stage, lstrlenA(stage), &written, NULL);
    WriteFile(log_file, number, sizeof(number) - 1, &written, NULL);
    FlushFileBuffers(log_file);
}
void WINAPI WinMainCRTStartup(void) {
    SC_HANDLE manager, service;
    SERVICE_STATUS status;
    BYTE configuration[2048];
    QUERY_SERVICE_CONFIGA *config = (QUERY_SERVICE_CONFIGA *)configuration;
    DWORD needed, began, error;
    HANDLE serial;
    STARTUPINFOA startup = {};
    PROCESS_INFORMATION process;
    char command[] = "C:\\DGPUBEN.EXE";
    UINT result = 1;
    log_file = CreateFileA("C:\\DGSERIAL.LOG", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (log_file == INVALID_HANDLE_VALUE)
        ExitProcess(2);
    manager = OpenSCManagerA(NULL, NULL, SC_MANAGER_CONNECT);
    if (!manager) {
        Log("FAIL OpenSCManager", GetLastError());
        goto done;
    }
    service = OpenServiceA(manager, "Serial",
                           SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS | SERVICE_CHANGE_CONFIG |
                               SERVICE_START);
    if (!service) {
        Log("FAIL OpenServiceSerial", GetLastError());
        CloseServiceHandle(manager);
        goto done;
    }
    if (!QueryServiceConfigA(service, config, sizeof(configuration), &needed)) {
        Log("FAIL QueryServiceConfig", GetLastError());
        goto close_service;
    }
    Log("Serial service type", config->dwServiceType);
    Log("Serial original start type", config->dwStartType);
    if (config->dwServiceType != SERVICE_KERNEL_DRIVER) {
        Log("FAIL unexpected service type", config->dwServiceType);
        goto close_service;
    }
    if (config->dwStartType != SERVICE_SYSTEM_START && config->dwStartType != SERVICE_BOOT_START) {
        if (!ChangeServiceConfigA(service, SERVICE_NO_CHANGE, SERVICE_SYSTEM_START,
                                  SERVICE_NO_CHANGE, NULL, NULL, NULL, NULL, NULL, NULL, NULL)) {
            Log("FAIL ChangeServiceConfig", GetLastError());
            goto close_service;
        }
        Log("Serial configured system start", SERVICE_SYSTEM_START);
    }
    if (!StartServiceA(service, 0, NULL)) {
        error = GetLastError();
        Log("StartService result", error);
        if (error != ERROR_SERVICE_ALREADY_RUNNING)
            goto close_service;
    }
    began = GetTickCount();
    do {
        if (!QueryServiceStatus(service, &status)) {
            Log("FAIL QueryServiceStatus", GetLastError());
            goto close_service;
        }
        if (status.dwCurrentState == SERVICE_RUNNING)
            break;
        if (status.dwCurrentState != SERVICE_START_PENDING) {
            Log("FAIL service state", status.dwCurrentState);
            Log("service exit", status.dwWin32ExitCode);
            goto close_service;
        }
        Sleep(50);
    } while (GetTickCount() - began < 5000);
    Log("Serial final state", status.dwCurrentState);
    serial =
        CreateFileA("\\\\.\\COM1", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (serial == INVALID_HANDLE_VALUE) {
        Log("FAIL OpenCOM1 after service start", GetLastError());
        goto close_service;
    }
    CloseHandle(serial);
    Log("PASS COM1 available", 0);
    startup.cb = sizeof(startup);
    if (!CreateProcessA(command, command, NULL, NULL, FALSE, 0, NULL, NULL, &startup, &process)) {
        Log("FAIL start runner", GetLastError());
        goto close_service;
    }
    Log("PASS runner started", process.dwProcessId);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    result = 0;
close_service:
    CloseServiceHandle(service);
    CloseServiceHandle(manager);
done:
    CloseHandle(log_file);
    ExitProcess(result);
}

} /* extern C */
