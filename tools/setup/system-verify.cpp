// SPDX-License-Identifier: GPL-2.0-or-later
// Fixed serial-fixture gate for the complete installer. The controller cannot
// provide executable paths or command arguments. Reads journals without repair.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "global-record.h"
#include "sha256.h"
#include "setup-request.h"
#ifndef DG_EXPECTED_INSTALLER_HASH
#error Exact installer artifact hash required
#endif
#ifndef DG_SYSTEM_PHASE
#error Fixed installer operation required
#endif
namespace {
constexpr unsigned Operation = DG_SYSTEM_PHASE;
static_assert(Operation < 7);
constexpr const char *Names[] = {"sysinstall", "sysresume", "sysupgrade", "sysrollback",
                                 "sysremove",  "sysrepair", "sysrecover"};
constexpr const char *Arguments[] = {"",           "/continue", "/upgrade", "/rollback",
                                     "/uninstall", "/repair",   "/recover"};
constexpr const char *Logs[] = {"C:\\DGINST.LOG", "C:\\DGCONT.LOG",   "C:\\DGUPGR.LOG",
                                "C:\\DGUNDO.LOG", "C:\\DGREMOVE.LOG", "C:\\DGREPAIR.LOG",
                                "C:\\DGRECOV.LOG"};
class Handle {
    HANDLE value_;

  public:
    explicit Handle(HANDLE value) : value_(value) {}
    ~Handle() {
        if (value_ && value_ != INVALID_HANDLE_VALUE)
            CloseHandle(value_);
    }
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    HANDLE get() const {
        return value_;
    }
    bool valid() const {
        return value_ && value_ != INVALID_HANDLE_VALUE;
    }
};
HANDLE Log = INVALID_HANDLE_VALUE;
const char *Stage = "initialize";
bool line(const char *text) {
    DWORD wrote = 0;
    DWORD bytes = lstrlenA(text);
    return WriteFile(Log, text, bytes, &wrote, nullptr) && wrote == bytes &&
           WriteFile(Log, "\r\n", 2, &wrote, nullptr) && wrote == 2 && FlushFileBuffers(Log);
}
bool installer_hash() {
    Handle file(CreateFileA("C:\\dreamgpu.exe", GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, 0, nullptr));
    if (!file.valid())
        return false;
    DWORD high = 0, size = GetFileSize(file.get(), &high), total = 0;
    if (high || size > 128u * 1024 * 1024)
        return false;
    setup::Sha256 hash;
    BYTE buffer[4096];
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(file.get(), buffer, sizeof(buffer), &got, nullptr) || got > size - total)
            return false;
        if (!got)
            break;
        hash.update(buffer, got);
        total += got;
    }
    char actual[65];
    hash.finish(actual);
    return total == size && line("INSTALLER_SHA256") && line(actual) &&
           !lstrcmpA(actual, DG_EXPECTED_INSTALLER_HASH);
}
bool wait_existing_setup(DWORD timeout) {
    OSVERSIONINFOA version{};
    version.dwOSVersionInfoSize = sizeof(version);
    if (!GetVersionExA(&version))
        return false;
    const char *name =
        version.dwPlatformId == VER_PLATFORM_WIN32_NT ? "Global\\DreamGPU.Setup" : "DreamGPU.Setup";
    Handle mutex(CreateMutexA(nullptr, FALSE, name));
    if (!mutex.valid())
        return false;
    DWORD wait = WaitForSingleObject(mutex.get(), timeout);
    return (wait == WAIT_OBJECT_0 || wait == WAIT_ABANDONED) && ReleaseMutex(mutex.get());
}
#ifdef DG_SYSTEM_UI
#include "ui-verify.h"
#endif
bool execute(DWORD &code, DWORD timeout) {
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    char command[96];
#ifdef DG_SYSTEM_UI
    wsprintfA(command, "\"C:\\dreamgpu.exe\" %s", Arguments[Operation]);
#else
    wsprintfA(command, "\"C:\\dreamgpu.exe\" %s /silent", Arguments[Operation]);
#endif
    if (!line(command) || !CreateProcessA("C:\\dreamgpu.exe", command, nullptr, nullptr, FALSE, 0,
                                          nullptr, "C:\\", &startup, &process))
        return false;
    Handle child(process.hProcess), thread(process.hThread);
#ifdef DG_SYSTEM_UI
    return WaitSetupDialog(child.get(), process.dwProcessId, timeout, code);
#else
    if (WaitForSingleObject(child.get(), timeout) != WAIT_OBJECT_0) {
        char message[96];
        wsprintfA(message, "INSTALLER_WAIT_FAILED pid=%lu; installer retains transaction ownership",
                  process.dwProcessId);
        line(message);
        return false;
    }
    return GetExitCodeProcess(child.get(), &code) != FALSE;
#endif
}
struct Requests {
    uint32_t now() {
        return GetTickCount();
    }
    bool wait(uint32_t timeout) {
        Stage = "wait for setup mutex";
        return wait_existing_setup(timeout);
    }
    bool launch(uint32_t timeout, uint32_t &code) {
        Stage = "execute installer";
        DWORD result = DWORD(-1);
        const bool okay = execute(result, timeout);
        code = result;
        return okay;
    }
    bool busy(unsigned attempt, uint32_t elapsed) {
        char message[120];
        wsprintfA(message,
                  "INSTALLER_BUSY exit=28 attempt=%u elapsed_ms=%lu; no transaction ownership",
                  attempt, DWORD(elapsed));
        return line(message);
    }
    void pause(uint32_t milliseconds) {
        Sleep(milliseconds);
    }
};
bool journal(setup::global::Record &out) {
    char path[MAX_PATH];
    DWORD n = GetWindowsDirectoryA(path, MAX_PATH);
    if (!n || n >= MAX_PATH - 24)
        return false;
    lstrcatA(path, "\\DreamGPU\\GLOBAL.JRN");
    Handle file(
        CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
    if (!file.valid())
        return false;
    DWORD high = 0, size = GetFileSize(file.get(), &high);
    constexpr DWORD step = sizeof(out) + 64;
    if (high || size < step || size > 4u * 1024 * 1024 || size % step)
        return false;
    for (DWORD offset = 0; offset < size; offset += step) {
        DWORD got;
        char stored[65]{}, actual[65];
        if (!ReadFile(file.get(), &out, sizeof(out), &got, nullptr) || got != sizeof(out) ||
            !ReadFile(file.get(), stored, 64, &got, nullptr) || got != 64 ||
            !setup::global::valid_record(out))
            return false;
        setup::Sha256 hash;
        hash.update(reinterpret_cast<const BYTE *>(&out), sizeof(out));
        hash.finish(actual);
        if (lstrcmpA(stored, actual))
            return false;
    }
    return true;
}
void evidence(const char *path) {
    Handle file(CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_EXISTING, 0, nullptr));
    if (!file.valid())
        return;
    DWORD high = 0, size = GetFileSize(file.get(), &high);
    if (high || size > 4096) {
        line("EVIDENCE_OMITTED_SIZE_LIMIT");
        return;
    }
    char bytes[4097]{};
    DWORD got;
    if (ReadFile(file.get(), bytes, size, &got, nullptr) && got == size) {
        line(path);
        line(bytes);
    }
}
bool run() {
    Stage = "installer identity";
    if (!installer_hash())
        return false;
    Requests requests;
    uint32_t code = uint32_t(-1);
    if (!setup::request_setup(requests, code))
        return false;
    char receipt[320];
    wsprintfA(receipt, "INSTALLER_EXIT %lu", code);
    if (!line(receipt))
        return false;
    setup::global::Record record{};
    Stage = "read global journal";
    if (!journal(record))
        return false;
    Stage = "validate installer outcome";
    using namespace setup::global;
    const bool terminal = setup::global::terminal(record.flow.phase);
    wsprintfA(receipt,
              "{\"schema\":1,\"operation\":\"%s\",\"installer_exit\":%lu,\"intent\":%u,\"phase\":%"
              "u,\"epoch\":%u,\"provider_generation\":%u,\"terminal\":%s}",
              Names[Operation], code, unsigned(record.flow.intent), unsigned(record.flow.phase),
              record.flow.epoch, record.flow.provider_generation, terminal ? "true" : "false");
    if (!line(receipt))
        return false;
    if (code == 11)
        return !terminal;
    if (code == 0 && record.flow.phase == Phase::activated && forward(record.flow.intent)) {
        constexpr const char *proofs[] = {"C:\\DGSYSGL.LOG", "C:\\DGSYSGR.LOG", "C:\\DGSYS6.LOG",
                                          "C:\\DGSYS7.LOG",  "C:\\DGSYS8.LOG",  "C:\\DGSYS9.LOG"};
        for (const char *path : proofs)
            evidence(path);
        return Operation < 3 || Operation == 5;
    }
    return record.flow.phase == Phase::restored &&
           ((code == 12 && record.flow.intent == Intent::rollback &&
             (Operation == 1 || Operation == 3 || Operation == 6)) ||
            (code == 13 && record.flow.intent == Intent::uninstall &&
             (Operation == 1 || Operation == 4 || Operation == 6)));
}
} // namespace
extern "C" void WINAPI WinMainCRTStartup() {
#ifdef DG_SYSTEM_UI
    const char *log_path = "C:\\DGSETUI.LOG";
#else
    const char *log_path = Logs[Operation];
#endif
    Handle log(
        CreateFileA(log_path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, 0, nullptr));
    if (!log.valid())
        ExitProcess(2);
    Log = log.get();
    bool okay = run();
    DWORD error = GetLastError();
    char result[160];
    if (!okay) {
        wsprintfA(result, "GATE_STAGE %s WIN32_ERROR %lu", Stage, error);
        line(result);
    }
    wsprintfA(result,
              "%s automated %s: complete installer operation receipt (pending is not activated)",
              okay ? "PASS" : "FAIL", Names[Operation]);
    if (!line(result))
        okay = false;
    ExitProcess(okay ? 0 : 1);
}
