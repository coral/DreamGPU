/* SPDX-License-Identifier: GPL-2.0-or-later
 * Exercise the Win98 VxD handle lifecycle. Driver-owned OS identities are
 * observed separately in the diagnostic mini-VDD's QEMU debug-console log.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../guest/win9x/dg-identity.h"
#include "../../guest/win9x/dg-memory.h"

static HANDLE log_file;
static HANDLE serial_file = INVALID_HANDLE_VALUE;
static char text[512];
static int failures;

static void line(const char *value) {
    DWORD written;
    WriteFile(log_file, value, lstrlenA(value), &written, NULL);
    WriteFile(log_file, "\r\n", 2, &written, NULL);
    FlushFileBuffers(log_file);
    if (serial_file != INVALID_HANDLE_VALUE) {
        WriteFile(serial_file, value, lstrlenA(value), &written, NULL);
        WriteFile(serial_file, "\r\n", 2, &written, NULL);
    }
}
static HANDLE open_device(void) {
    SECURITY_ATTRIBUTES security;
    HANDLE result;
    ZeroMemory(&security, sizeof(security));
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;
    /* Open the already loaded mini-VDD by its DDB name. No dynamic loading
     * or FILE_FLAG_DELETE_ON_CLOSE/unload request is permitted. */
    SetLastError(0);
    result = CreateFileA("\\\\.\\DREAMGPU", 0, 0, &security, OPEN_EXISTING, 0, NULL);
    wsprintfA(text, "OPEN pid=%08lX handle=%08lX error=%lu", GetCurrentProcessId(), (DWORD)result,
              GetLastError());
    line(text);
    return result;
}
static BOOL mark(HANDLE device, DWORD marker) {
    DWORD bytes = 0;
    BOOL result;
    SetLastError(0);
    result = DeviceIoControl(device, DG_IDENTITY_BASE | marker, NULL, 0, NULL, 0, &bytes, NULL);
    wsprintfA(text, "REQUEST pid=%08lX handle=%08lX marker=%04lX ok=%d error=%lu bytes=%lu",
              GetCurrentProcessId(), (DWORD)device, marker, result, GetLastError(), bytes);
    line(text);
    return result;
}
static void close_device(HANDLE handle, const char *label) {
    BOOL result;
    SetLastError(0);
    result = CloseHandle(handle);
    wsprintfA(text, "CLOSE %s pid=%08lX handle=%08lX ok=%d error=%lu", label, GetCurrentProcessId(),
              (DWORD)handle, result, GetLastError());
    line(text);
}
static void memory_case(HANDLE device, const char *name, void *input, DWORD input_bytes,
                        void *output, DWORD output_bytes, BOOL expected) {
    DWORD bytes = 0;
    BOOL ok = DeviceIoControl(device, DG9_MEMORY_COPY, input, input_bytes, output, output_bytes,
                              &bytes, NULL);
    DWORD error = ok ? 0 : GetLastError();
    BOOL pass = ok == expected && (!ok || (bytes == input_bytes && !memcmp(input, output, bytes)));
    wsprintfA(text, "MEMORY case=%s pass=%d ok=%d error=%lu bytes=%lu", name, pass, ok, error,
              bytes);
    line(text);
    if (!pass)
        failures++;
}
static void memory_tests(HANDLE device) {
    BYTE *input, *output;
    DWORD i, old;
    input = VirtualAlloc(NULL, 20 * 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    output = VirtualAlloc(NULL, 20 * 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!input || !output) {
        failures++;
        line("MEMORY allocation failed");
        return;
    }
    for (i = 0; i < 20 * 4096; ++i)
        input[i] = (BYTE)(i * 17 + 3);
    memory_case(device, "unaligned-18-pages", input + 4095, DG9_MEMORY_MAX_BYTES, output + 4095,
                DG9_MEMORY_MAX_BYTES, TRUE);
    memory_case(device, "same-buffer", input, 65536, input, 65536, TRUE);
    memory_case(device, "null-input", NULL, 4, output, 4, FALSE);
    memory_case(device, "null-output", input, 4, NULL, 4, FALSE);
    memory_case(device, "zero-length", input, 0, output, 0, FALSE);
    memory_case(device, "oversized", input, DG9_MEMORY_MAX_BYTES + 1, output,
                DG9_MEMORY_MAX_BYTES + 1, FALSE);
    memory_case(device, "mismatched-length", input, 4, output, 3, FALSE);
    memory_case(device, "kernel-input", (void *)0xc0000000UL, 4, output, 4, FALSE);
    memory_case(device, "overflow-output", input, 4, (void *)0xfffffffeUL, 4, FALSE);
    VirtualProtect(input, 4096, PAGE_READONLY, &old);
    memory_case(device, "readonly-input", input, 4096, output, 4096, TRUE);
    memory_case(device, "readonly-output", output, 4096, input, 4096, FALSE);
    VirtualProtect(input, 4096, PAGE_NOACCESS, &old);
    memory_case(device, "noaccess-input", input, 4096, output, 4096, FALSE);
    VirtualProtect(input, 4096, PAGE_READWRITE, &old);
    VirtualFree(input + 4096, 4096, MEM_DECOMMIT);
    memory_case(device, "cross-uncommitted", input + 4095, 2, output, 2, FALSE);
    VirtualFree(input, 0, MEM_RELEASE);
    VirtualFree(output, 0, MEM_RELEASE);
}

static void event_name(char *buffer, const char *type, DWORD role) {
    wsprintfA(buffer, "DGID_%s_%lu", type, role);
}

static int child(DWORD role, HANDLE inherited) {
    char name[48];
    HANDLE own, ready, finish;
    BOOL inherited_ok;
    inherited_ok = mark(inherited, 0x200 + role);
    wsprintfA(text, "INHERITED role=%lu request_ok=%d", role, inherited_ok);
    line(text);
    own = open_device();
    if (own == INVALID_HANDLE_VALUE || !mark(own, 0x210 + role))
        return 1;
    event_name(name, "READY", role);
    ready = OpenEventA(EVENT_MODIFY_STATE, FALSE, name);
    event_name(name, "FINISH", role);
    finish = OpenEventA(SYNCHRONIZE, FALSE, name);
    if (!ready || !finish)
        return 1;
    SetEvent(ready);
    if (WaitForSingleObject(finish, 30000) != WAIT_OBJECT_0)
        return 1;
    mark(own, 0x220 + role);
    close_device(own, "child-own");
    /* Keep the inherited handle open: the OS must provide exit cleanup. */
    CloseHandle(ready);
    CloseHandle(finish);
    line("NORMAL_EXIT leaves inherited handle to OS");
    return 0;
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show) {
    DWORD role = 0, inherited = 0, i, exit_code;
    char log_path[32], name[48], executable[MAX_PATH], child_command[384];
    HANDLE first, second, duplicate = INVALID_HANDLE_VALUE;
    HANDLE ready[3], finish[3];
    PROCESS_INFORMATION process[3];
    STARTUPINFOA startup;
    (void)instance;
    (void)previous;
    (void)show;
    if (*command &&
        (sscanf(command, "/child %lu %lx", &role, &inherited) != 2 || role < 1 || role > 3))
        return 2;
    /* The host gets completion and errors directly, without screen scraping
     * or opening the running disk. Children retain separate on-disk logs. */
    if (!role) {
        DCB state;
        COMMTIMEOUTS timeouts;
        serial_file = CreateFileA("COM1", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (serial_file != INVALID_HANDLE_VALUE) {
            ZeroMemory(&state, sizeof(state));
            state.DCBlength = sizeof(state);
            if (GetCommState(serial_file, &state)) {
                state.BaudRate = CBR_115200;
                state.ByteSize = 8;
                state.Parity = NOPARITY;
                state.StopBits = ONESTOPBIT;
                state.fOutxCtsFlow = state.fOutxDsrFlow = FALSE;
                state.fOutX = state.fInX = FALSE;
                SetCommState(serial_file, &state);
            }
            ZeroMemory(&timeouts, sizeof(timeouts));
            timeouts.WriteTotalTimeoutConstant = 250;
            SetCommTimeouts(serial_file, &timeouts);
        }
    }
    wsprintfA(log_path, "C:\\DGID%lu.LOG", role);
    log_file = CreateFileA(log_path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    if (log_file == INVALID_HANDLE_VALUE)
        return 1;
    wsprintfA(text, "DG identity probe v1 pid=%08lX role=%lu", GetCurrentProcessId(), role);
    line(text);
    if (role) {
        int result = child(role, (HANDLE)inherited);
        CloseHandle(log_file);
        return result;
    }
    first = open_device();
    if (first == INVALID_HANDLE_VALUE || !mark(first, 0x101)) {
        line("RESULT harness_failures=1 open-failed");
        CloseHandle(log_file);
        return 1;
    }
    second = open_device();
    if (second != INVALID_HANDLE_VALUE) {
        mark(second, 0x102);
        close_device(second, "second-open");
    }
    SetLastError(0);
    i = DuplicateHandle(GetCurrentProcess(), first, GetCurrentProcess(), &duplicate, 0, TRUE,
                        DUPLICATE_SAME_ACCESS);
    wsprintfA(text, "DUPLICATE same-process ok=%lu result=%08lX error=%lu", i, (DWORD)duplicate,
              GetLastError());
    line(text);
    if (i) {
        mark(duplicate, 0x110);
        close_device(duplicate, "duplicate");
        mark(first, 0x111);
    }
    GetModuleFileNameA(NULL, executable, sizeof(executable));
    ZeroMemory(process, sizeof(process));
    for (i = 0; i < 3; ++i) {
        event_name(name, "READY", i + 1);
        ready[i] = CreateEventA(NULL, TRUE, FALSE, name);
        event_name(name, "FINISH", i + 1);
        finish[i] = CreateEventA(NULL, TRUE, FALSE, name);
        ZeroMemory(&startup, sizeof(startup));
        startup.cb = sizeof(startup);
        wsprintfA(child_command, "\"%s\" /child %lu %08lX", executable, i + 1, (DWORD)first);
        SetLastError(0);
        if (!CreateProcessA(NULL, child_command, NULL, NULL, i != 2, 0, NULL, NULL, &startup,
                            &process[i])) {
            wsprintfA(text, "CREATE role=%lu error=%lu", i + 1, GetLastError());
            line(text);
            failures++;
            continue;
        }
        wsprintfA(text, "CREATE role=%lu pid=%08lX inherited=%d", i + 1, process[i].dwProcessId,
                  i != 2);
        line(text);
        exit_code = WaitForSingleObject(ready[i], 10000);
        wsprintfA(text, "READY role=%lu wait=%lu", i + 1, exit_code);
        line(text);
        if (exit_code != WAIT_OBJECT_0)
            failures++;
    }
    mark(first, 0x120);
    for (i = 0; i < 3; ++i) {
        if (!process[i].hProcess)
            continue;
        if (i == 1) {
            line("FORCE_EXIT role=2");
            TerminateProcess(process[i].hProcess, 77);
        } else
            SetEvent(finish[i]);
        exit_code = WaitForSingleObject(process[i].hProcess, 10000);
        if (exit_code != WAIT_OBJECT_0) {
            TerminateProcess(process[i].hProcess, 78);
            failures++;
        }
        GetExitCodeProcess(process[i].hProcess, &exit_code);
        wsprintfA(text, "EXIT role=%lu code=%lu", i + 1, exit_code);
        line(text);
        CloseHandle(process[i].hThread);
        CloseHandle(process[i].hProcess);
        CloseHandle(ready[i]);
        CloseHandle(finish[i]);
    }
    mark(first, 0x130);
    memory_tests(first);
    close_device(first, "first-open");
    for (i = 0; i < 12; ++i) {
        first = open_device();
        if (first == INVALID_HANDLE_VALUE || !mark(first, 0x300 + i)) {
            failures++;
            break;
        }
        close_device(first, "reuse");
    }
    wsprintfA(text,
              "RESULT harness_failures=%d; ownership interpretation requires paired VxD trace",
              failures);
    line(text);
    CloseHandle(log_file);
    return failures != 0;
}
