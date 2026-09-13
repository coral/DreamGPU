/* SPDX-License-Identifier: GPL-2.0-or-later
 * Fixed, process-owned retail HL startup debugger. Bounded exception evidence,
 * never attach to an existing process or accept a path/command from the peer.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static HANDLE Log;
static void Text(const char *s) {
    DWORD n;
    WriteFile(Log, s, lstrlenA(s), &n, nullptr);
    FlushFileBuffers(Log);
}
static void Value(const char *name, DWORD n) {
    char s[160];
    wsprintfA(s, "%s %08lx\r\n", name, n);
    Text(s);
}
static bool Read(HANDLE p, const void *address, void *out, SIZE_T size) {
    SIZE_T n = 0;
    return ReadProcessMemory(p, address, out, size, &n) && n == size;
}
static void Image(HANDLE process, const LOAD_DLL_DEBUG_INFO &info) {
    Value("DLL_BASE", reinterpret_cast<DWORD>(info.lpBaseOfDll));
    DWORD pointer = 0;
    char name[260]{};
    WORD wide[260]{};
    if (info.lpImageName && Read(process, info.lpImageName, &pointer, 4) && pointer) {
        if (info.fUnicode) {
            if (Read(process, reinterpret_cast<void *>(pointer), wide, sizeof(wide))) {
                for (unsigned i = 0; i < 259 && wide[i]; i++)
                    name[i] = wide[i] < 128 ? char(wide[i]) : '?';
            }
        } else
            Read(process, reinterpret_cast<void *>(pointer), name, sizeof(name) - 1);
        name[259] = 0;
        const char *base = name;
        for (const char *p = name; *p; p++)
            if (*p == '\\' || *p == '/')
                base = p + 1;
        Text("DLL_NAME ");
        Text(base);
        Text("\r\n");
    }
    if (info.hFile)
        CloseHandle(info.hFile);
}
extern "C" void WINAPI WinMainCRTStartup(void) {
    Log = CreateFileA("C:\\DGHLDBG.LOG", GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                      FILE_ATTRIBUTE_NORMAL, nullptr);
    if (Log == INVALID_HANDLE_VALUE)
        ExitProcess(2);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);
    char command[] = "\"C:\\SIERRA\\Half-Life\\hl.exe\" -windowed -toconsole -nosound -dev -gl "
                     "-gldrv dgpugl.dll -condebug +timedemo jrgperf";
    STARTUPINFOA start{};
    start.cb = sizeof(start);
    PROCESS_INFORMATION child{};
    if (!CreateProcessA("C:\\SIERRA\\Half-Life\\hl.exe", command, nullptr, nullptr, FALSE,
                        DEBUG_ONLY_THIS_PROCESS, nullptr, "C:\\SIERRA\\Half-Life", &start,
                        &child)) {
        Value("FAIL CREATE", GetLastError());
        CloseHandle(Log);
        ExitProcess(1);
    }
    struct Thread {
        DWORD id;
        HANDLE handle;
    };
    Thread threads[128]{};
    unsigned count = 0;
    DWORD began = GetTickCount();
    bool ended = false, captured = false, terminating = false;
    for (unsigned events = 0; events < 2048 && GetTickCount() - began < 35000; events++) {
        DEBUG_EVENT event{};
        if (!WaitForDebugEvent(&event, 1000)) {
            if (GetLastError() != ERROR_SEM_TIMEOUT) {
                Value("WAIT_ERROR", GetLastError());
                break;
            }
            continue;
        }
        DWORD status = DBG_CONTINUE;
        if (event.dwProcessId != child.dwProcessId) {
            Text("FAIL foreign debug event\r\n");
            break;
        }
        switch (event.dwDebugEventCode) {
            case CREATE_PROCESS_DEBUG_EVENT:
                Value("IMAGE_BASE",
                      reinterpret_cast<DWORD>(event.u.CreateProcessInfo.lpBaseOfImage));
                threads[count++] = {event.dwThreadId, event.u.CreateProcessInfo.hThread};
                if (event.u.CreateProcessInfo.hFile)
                    CloseHandle(event.u.CreateProcessInfo.hFile);
                break;
            case CREATE_THREAD_DEBUG_EVENT:
                if (count < 128)
                    threads[count++] = {event.dwThreadId, event.u.CreateThread.hThread};
                break;
            case EXIT_THREAD_DEBUG_EVENT:
                for (unsigned i = 0; i < count; i++)
                    if (threads[i].id == event.dwThreadId)
                        threads[i].handle = nullptr;
                break;
            case LOAD_DLL_DEBUG_EVENT:
                Image(child.hProcess, event.u.LoadDll);
                break;
            case EXCEPTION_DEBUG_EVENT: {
                auto &exception = event.u.Exception;
                DWORD code = exception.ExceptionRecord.ExceptionCode;
                if (code == EXCEPTION_BREAKPOINT)
                    break;
                Value("EXCEPTION", code);
                Value("FIRST_CHANCE", exception.dwFirstChance);
                Value("ADDRESS",
                      reinterpret_cast<DWORD>(exception.ExceptionRecord.ExceptionAddress));
                for (unsigned i = 0; i < count; i++)
                    if (threads[i].id == event.dwThreadId && threads[i].handle) {
                        CONTEXT context{};
                        context.ContextFlags = CONTEXT_FULL;
                        if (GetThreadContext(threads[i].handle, &context)) {
                            Value("EIP", context.Eip);
                            Value("ESP", context.Esp);
                            Value("EBP", context.Ebp);
                            Value("EAX", context.Eax);
                            Value("ECX", context.Ecx);
                            Value("EDX", context.Edx);
                            DWORD stack[32]{};
                            if (Read(child.hProcess, reinterpret_cast<void *>(context.Esp), stack,
                                     sizeof(stack)))
                                for (unsigned j = 0; j < 32; j++)
                                    Value("STACK", stack[j]);
                        }
                    }
                if (!exception.dwFirstChance || !exception.ExceptionRecord.ExceptionAddress) {
                    captured = true;
                    TerminateProcess(child.hProcess, 125);
                    terminating = true;
                } else
                    status = DBG_EXCEPTION_NOT_HANDLED;
                break;
            }
            case EXIT_PROCESS_DEBUG_EVENT:
                Value("EXIT", event.u.ExitProcess.dwExitCode);
                ended = true;
                break;
        }
        if (!ContinueDebugEvent(event.dwProcessId, event.dwThreadId, status)) {
            Value("CONTINUE_ERROR", GetLastError());
            break;
        }
        if (ended)
            break;
        if (GetTickCount() - began > 30000 && !terminating) {
            TerminateProcess(child.hProcess, 125);
            terminating = true;
        }
    }
    if (!ended)
        TerminateProcess(child.hProcess, 125);
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
    Text(captured
             ? "PASS automated hldebug: owned startup exception captured; no performance claim\r\n"
             : "FAIL no owned startup exception captured\r\n");
    CloseHandle(Log);
    ExitProcess(captured ? 0 : 1);
}
