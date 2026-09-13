// SPDX-License-Identifier: GPL-2.0-or-later
// DreamGPU installer foundation. OS-compatible Win32 boundary; no activation
// success is reported until a real system provider implements its transaction.
// PCI enumeration follows the bounded CM_Get_Device_IDA approach already used
// by our tools/win9x/driver32.cpp; no third-party implementation copied.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include "policy.h"
#include "sha256.h"
#include "payload.h"
#include "lifecycle-runtime.h"
namespace {
class Handle {
    HANDLE h_;

  public:
    explicit Handle(HANDLE h) : h_(h) {}
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    ~Handle() {
        if (h_ != INVALID_HANDLE_VALUE && h_)
            CloseHandle(h_);
    }
    HANDLE get() const {
        return h_;
    }
};
class SetupLock {
    HANDLE handle_ = nullptr;
    bool owned_ = false;

  public:
    explicit SetupLock(setup::Os os) {
        handle_ = CreateMutexA(nullptr, TRUE,
                               os == setup::Os::nt5 ? "Global\\DreamGPU.Setup" : "DreamGPU.Setup");
        owned_ = handle_ && GetLastError() != ERROR_ALREADY_EXISTS;
    }
    SetupLock(const SetupLock &) = delete;
    SetupLock &operator=(const SetupLock &) = delete;
    ~SetupLock() {
        if (owned_)
            ReleaseMutex(handle_);
        if (handle_)
            CloseHandle(handle_);
    }
    bool acquired() const {
        return owned_;
    }
};
class Devices {
    HDEVINFO value_;

  public:
    Devices()
        : value_(
              SetupDiGetClassDevsA(nullptr, nullptr, nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES)) {}
    Devices(const Devices &) = delete;
    Devices &operator=(const Devices &) = delete;
    ~Devices() {
        if (value_ != INVALID_HANDLE_VALUE)
            SetupDiDestroyDeviceInfoList(value_);
    }
    bool exactly_one() const {
        if (value_ == INVALID_HANDLE_VALUE)
            return false;
        unsigned matches = 0;
        for (DWORD i = 0; i < 4096; i++) {
            SP_DEVINFO_DATA item = {};
            item.cbSize = sizeof(item);
            if (!SetupDiEnumDeviceInfo(value_, i, &item))
                return GetLastError() == ERROR_NO_MORE_ITEMS && matches == 1;
            char id[256] = {};
            if (CM_Get_Device_IDA(item.DevInst, id, sizeof(id) - 1, 0) == CR_SUCCESS &&
                setup::pci(id, sizeof(id)))
                ++matches;
        }
        return false;
    }
};
struct Blob {
    const BYTE *data;
    DWORD bytes;
};
bool resource(const Payload &p, Blob &out) {
    HRSRC r = FindResourceA(nullptr, MAKEINTRESOURCEA(p.id), RT_RCDATA);
    if (!r || SizeofResource(nullptr, r) != p.size)
        return false;
    HGLOBAL h = LoadResource(nullptr, r);
    if (!h)
        return false;
    out = {static_cast<const BYTE *>(LockResource(h)), p.size};
    return out.data != nullptr;
}
bool equal_hash(const BYTE *data, DWORD bytes, const char *expected) {
    setup::Sha256 hash;
    char actual[65];
    hash.update(data, bytes);
    hash.finish(actual);
    return !lstrcmpA(actual, expected);
}
bool all_payloads(unsigned os) {
    unsigned count = 0;
    for (const auto &p : payloads) {
        if (p.os != os)
            continue;
        Blob blob = {};
        if (!setup::safe_path(p.path) || !resource(p, blob) ||
            !equal_hash(blob.data, blob.bytes, p.sha))
            return false;
        ++count;
    }
    return count != 0;
}
struct Node {
    char path[MAX_PATH];
    bool directory;
};
class Store {
    char root_[MAX_PATH] = {}, final_[MAX_PATH] = {};
    Node nodes_[2048] = {};
    unsigned count_ = 0;
    bool created_ = false;
    bool add(const char *path, bool dir) {
        if (count_ >= 2048)
            return false;
        lstrcpyA(nodes_[count_].path, path);
        nodes_[count_++].directory = dir;
        return true;
    }
    bool safe_directory(const char *p) {
        DWORD a = GetFileAttributesA(p);
        return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) &&
               !(a & FILE_ATTRIBUTE_REPARSE_POINT);
    }

  public:
    bool begin() {
        if (created_)
            return false;
        unsigned n = GetWindowsDirectoryA(root_, MAX_PATH);
        if (!n || n > MAX_PATH - 24 || !safe_directory(root_))
            return false;
        lstrcpyA(final_, root_);
        lstrcatA(final_, "\\DreamGPU");
        if (GetFileAttributesA(final_) != INVALID_FILE_ATTRIBUTES ||
            GetLastError() != ERROR_FILE_NOT_FOUND)
            return false;
        lstrcatA(root_, "\\DGSETUP.NEW");
        // CREATE_NEW-style root acquisition: never adopt someone else's tree.
        if (!CreateDirectoryA(root_, nullptr))
            return false;
        created_ = true;
        return true;
    }
    bool write(const char *relative, const BYTE *data, DWORD bytes,
               const char *expected = nullptr) {
        if (!created_ || !setup::safe_path(relative))
            return false;
        char path[MAX_PATH];
        if (lstrlenA(root_) + lstrlenA(relative) + 2 >= MAX_PATH)
            return false;
        lstrcpyA(path, root_);
        lstrcatA(path, "\\");
        unsigned start = lstrlenA(path);
        lstrcatA(path, relative);
        for (unsigned i = start; path[i]; i++) {
            if (path[i] != '/')
                continue;
            path[i] = 0;
            if (!safe_directory(path)) {
                if (count_ >= 2048 || !CreateDirectoryA(path, nullptr))
                    return false;
                add(path, true);
            }
            path[i] = '\\';
        }
        if (count_ >= 2048)
            return false;
        Handle file(CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                FILE_ATTRIBUTE_NORMAL, nullptr));
        if (file.get() == INVALID_HANDLE_VALUE)
            return false;
        add(path, false);
        DWORD written = 0;
        if (!WriteFile(file.get(), data, bytes, &written, nullptr) || written != bytes ||
            !FlushFileBuffers(file.get()))
            return false;
        if (SetFilePointer(file.get(), 0, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER)
            return false;
        setup::Sha256 hash;
        BYTE buffer[4096];
        DWORD got = 0, total = 0;
        do {
            if (!ReadFile(file.get(), buffer, sizeof(buffer), &got, nullptr))
                return false;
            if (got > bytes - total)
                return false;
            hash.update(buffer, got);
            total += got;
        } while (got);
        if (total != bytes)
            return false;
        if (expected) {
            char actual[65];
            hash.finish(actual);
            if (lstrcmpA(actual, expected))
                return false;
        }
        return true;
    }
    bool commit() {
        return created_ && MoveFileA(root_, final_);
    }
    bool rollback() {
        if (!created_)
            return true;
        // Keep failed entries in the ownership ledger; callers receive incomplete
        // rollback instead of an invented restored-state result.
        bool ok = true;
        for (unsigned i = count_; i; i--) {
            Node &n = nodes_[i - 1];
            if (!n.path[0])
                continue;
            if (n.directory ? RemoveDirectoryA(n.path) : DeleteFileA(n.path))
                n.path[0] = 0;
            else
                ok = false;
        }
        if (ok && RemoveDirectoryA(root_)) {
            created_ = false;
            count_ = 0;
            return true;
        }
        return false;
    }
};
Store storage;
void receipt(const char *text) {
    DWORD bytes;
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out && out != INVALID_HANDLE_VALUE)
        WriteFile(out, text, lstrlenA(text), &bytes, nullptr);
    OutputDebugStringA(text);
}
unsigned stage(unsigned os) {
    setup::Transaction transaction(storage);
    if (!transaction.begin())
        return 24;
    const char pending[] = "{\"schema\":1,\"state\":\"staging\",\"system_activated\":false,"
                           "\"ownership\":\"fresh_directory\"}\r\n";
    bool ok =
        storage.write("PREPARE.json", reinterpret_cast<const BYTE *>(pending), sizeof(pending) - 1);
    for (const auto &p : payloads) {
        if (!ok || p.os != os)
            continue;
        Blob b = {};
        ok = resource(p, b) && storage.write(p.path, b.data, b.bytes, p.sha);
    }
    const char complete[] = "{\"schema\":1,\"state\":\"staged\",\"system_activated\":false,"
                            "\"provider\":\"not_ready\"}\r\n";
    ok = ok && storage.write("RESULT.json", reinterpret_cast<const BYTE *>(complete),
                             sizeof(complete) - 1);
    if (ok && transaction.commit())
        return setup::lifecycle::prepare_shared(static_cast<setup::Os>(os), payloads) ? 10 : 27;
    return transaction.rollback() ? 25 : 26;
}
unsigned run(bool stage_only, int action) {
    OSVERSIONINFOA version = {};
    version.dwOSVersionInfoSize = sizeof(version);
    if (!GetVersionExA(&version))
        return 20;
    setup::Os os =
        setup::select_os(version.dwPlatformId, version.dwMajorVersion, version.dwMinorVersion);
    if (os == setup::Os::unsupported)
        return 20;
    Devices devices;
    if (!devices.exactly_one())
        return 21;
    if (!all_payloads(static_cast<unsigned>(os)))
        return 22;
    // Descriptors are deliberately false until system ICD/D3D implementations
    // are integrated and independently verified. Staging is never installation.
    SetupLock lock(os);
    if (!lock.acquired())
        return 28;
    if (action >= 0) {
        auto result =
            setup::lifecycle::act(os, static_cast<setup::lifecycle::Action>(action), payloads);
        using setup::lifecycle::Result;
        if (result == Result::provider_not_ready)
            return 30;
        if (result == Result::pending_reboot)
            return 11;
        if (result == Result::complete)
            return action == 1 ? 12 : action == 2 ? 13 : 0;
        if (result == Result::conflict)
            return 29;
        return 26;
    }
    if (!stage_only)
        return 30;
    return stage(static_cast<unsigned>(os));
}
} // namespace
extern "C" void WINAPI WinMainCRTStartup() {
    char *command = GetCommandLineA();
    if (*command == '"') {
        ++command;
        while (*command && *command != '"')
            ++command;
        if (*command)
            ++command;
    } else
        while (*command && *command != ' ' && *command != '\t')
            ++command;
    bool staging = false, silent = false, valid = true;
    int action = -1;
    while (*command) {
        while (*command == ' ' || *command == '\t')
            ++command;
        if (!*command)
            break;
        char arg[16];
        unsigned n = 0;
        while (*command && *command != ' ' && *command != '\t') {
            if (n < 15)
                arg[n++] = *command;
            else
                valid = false;
            ++command;
        }
        arg[n] = 0;
        if (!lstrcmpiA(arg, "/stage"))
            staging = true;
        else if (!lstrcmpiA(arg, "/continue") || !lstrcmpiA(arg, "/rollback") ||
                 !lstrcmpiA(arg, "/uninstall") || !lstrcmpiA(arg, "/upgrade")) {
            if (action >= 0)
                valid = false;
            action = !lstrcmpiA(arg, "/continue")    ? 0
                     : !lstrcmpiA(arg, "/rollback")  ? 1
                     : !lstrcmpiA(arg, "/uninstall") ? 2
                                                     : 3;
        } else if (!lstrcmpiA(arg, "/silent"))
            silent = true;
        else
            valid = false;
    }
    unsigned code = valid && !(staging && action >= 0) ? run(staging, action) : 23;
    const char *result = "{\"schema\":1,\"exit_code\":23,\"status\":\"invalid_arguments\",\"system_"
                         "activated\":false}\r\n";
    switch (code) {
        case 0:
            result = "{\"schema\":1,\"exit_code\":0,\"status\":\"activated\",\"system_activated\":"
                     "true}\r\n";
            break;
        case 11:
            result = "{\"schema\":1,\"exit_code\":11,\"status\":\"pending_reboot\",\"system_"
                     "activated\":false}\r\n";
            break;
        case 12:
            result = "{\"schema\":1,\"exit_code\":12,\"status\":\"rolled_back\",\"system_"
                     "activated\":false}\r\n";
            break;
        case 13:
            result = "{\"schema\":1,\"exit_code\":13,\"status\":\"removed\",\"system_activated\":"
                     "false}\r\n";
            break;
        case 27:
            result = "{\"schema\":1,\"exit_code\":27,\"status\":\"staged_journal_failed\",\"system_"
                     "activated\":false}\r\n";
            break;
        case 28:
            result = "{\"schema\":1,\"exit_code\":28,\"status\":\"installer_busy\",\"system_"
                     "activated\":false}\r\n";
            break;
        case 29:
            result = "{\"schema\":1,\"exit_code\":29,\"status\":\"ownership_conflict\",\"system_"
                     "activated\":false}\r\n";
            break;

        case 10:
            result = "{\"schema\":1,\"exit_code\":10,\"status\":\"staged_only\",\"system_"
                     "activated\":false}\r\n";
            break;
        case 20:
            result = "{\"schema\":1,\"exit_code\":20,\"status\":\"unsupported_os\",\"system_"
                     "activated\":false}\r\n";
            break;
        case 21:
            result = "{\"schema\":1,\"exit_code\":21,\"status\":\"device_preflight_failed\","
                     "\"system_activated\":false}\r\n";
            break;
        case 22:
            result = "{\"schema\":1,\"exit_code\":22,\"status\":\"payload_invalid\",\"system_"
                     "activated\":false}\r\n";
            break;
        case 24:
            result = "{\"schema\":1,\"exit_code\":24,\"status\":\"destination_unavailable\","
                     "\"system_activated\":false}\r\n";
            break;
        case 25:
            result = "{\"schema\":1,\"exit_code\":25,\"status\":\"stage_failed_rolled_back\","
                     "\"system_activated\":false}\r\n";
            break;
        case 26:
            result = "{\"schema\":1,\"exit_code\":26,\"status\":\"rollback_incomplete\",\"system_"
                     "activated\":false}\r\n";
            break;
        case 30:
            result = "{\"schema\":1,\"exit_code\":30,\"status\":\"system_provider_not_ready\","
                     "\"system_activated\":false}\r\n";
            break;
    }
    receipt(result);
    if (!silent)
        MessageBoxA(nullptr, result, "DreamGPU setup",
                    MB_OK | (code == 10 ? MB_ICONINFORMATION : MB_ICONERROR));
    ExitProcess(code);
}
