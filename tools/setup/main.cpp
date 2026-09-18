// SPDX-License-Identifier: GPL-2.0-or-later
// DreamGPU installer foundation. OS-compatible Win32 boundary; no activation
// success is reported until a real system provider implements its transaction.
// PCI enumeration follows the bounded CM_Get_Device_IDA approach already used
// by our tools/win9x/driver32.cpp; no third-party implementation copied.
#define WIN32_LEAN_AND_MEAN
#define USE_SP_DRVINFO_DATA_V1 1
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include "policy.h"
#include "sha256.h"
#include "payload.h"
#include "system-runtime.h"
#include "driver-runtime.h"
#include "global-runtime.h"
#include "stage-store.h"
#include "setup-lock.h"
#include "progress-window.h"
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
    bool exactly_one(bool allow_absent = false) const {
        if (value_ == INVALID_HANDLE_VALUE)
            return false;
        unsigned matches = 0;
        for (DWORD i = 0; i < 4096; i++) {
            SP_DEVINFO_DATA item = {};
            item.cbSize = sizeof(item);
            if (!SetupDiEnumDeviceInfo(value_, i, &item))
                return GetLastError() == ERROR_NO_MORE_ITEMS &&
                       (matches == 1 || (allow_absent && matches == 0));
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
        setup::progress("Checking installer file", p.path);
        Blob blob = {};
        if (!setup::safe_path(p.path) || !resource(p, blob) ||
            !equal_hash(blob.data, blob.bytes, p.sha))
            return false;
        ++count;
    }
    return count != 0;
}
setup::staging::Store storage;
void receipt(const char *text) {
    DWORD bytes;
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out && out != INVALID_HANDLE_VALUE)
        WriteFile(out, text, lstrlenA(text), &bytes, nullptr);
    OutputDebugStringA(text);
}
unsigned stage(unsigned os, bool cancel = false) {
    setup::progress(cancel ? "Removing unfinished setup files" : "Preparing installation files");
    setup::lifecycle::Win32Store inspector;
    setup::lifecycle::Image executing;
    char self[MAX_PATH];
    setup::progress("Locating running installer");
    DWORD length = GetModuleFileNameA(nullptr, self, sizeof(self));
    if (!length || length >= sizeof(self)) {
        setup::failure("Could not locate running installer", "", GetLastError());
        return 24;
    }
    setup::progress("Reading running installer", self);
    if (!inspector.inspect_file(self, executing, true) || !executing.exists)
        return 24;
    setup::progress("Locating Windows installation directory");
    if (!storage.reset(static_cast<setup::Os>(os), executing.sha))
        return 24;
    static constexpr char pending[] =
        "{\"schema\":1,\"state\":\"staging\",\"system_activated\":false,"
        "\"ownership\":\"fresh_directory\"}\r\n";
    static constexpr char complete[] =
        "{\"schema\":1,\"state\":\"staged\",\"system_activated\":false,"
        "\"provider\":\"not_ready\"}\r\n";
    setup::progress("Preparing file catalog", "PREPARE.json");
    if (!storage.add("PREPARE.json", reinterpret_cast<const BYTE *>(pending), sizeof(pending) - 1))
        return 24;
    for (const auto &p : payloads) {
        if (p.os != os)
            continue;
        setup::progress("Preparing file catalog", p.path);
        Blob blob{};
        if (!resource(p, blob) || !storage.add(p.path, blob.data, blob.bytes, p.sha))
            return 24;
    }
    setup::progress("Preparing file catalog", "RESULT.json");
    if (!storage.add("RESULT.json", reinterpret_cast<const BYTE *>(complete), sizeof(complete) - 1))
        return 24;
    if (!storage.begin(cancel))
        return 24;
    if (cancel)
        return storage.cancel() ? 17 : 26;
    // A flushed binding owns partial files across process death. Preserve that
    // recovery state on I/O failure; never claim rollback of an uncertain write.
    if (!storage.copy() || !storage.commit() || !storage.finish())
        return 26;
    setup::progress("Preparing system installation");
    return setup::lifecycle::ensure_prepared(static_cast<setup::Os>(os), payloads) ? 10 : 27;
}
unsigned run(bool stage_only, int action) {
    setup::progress("Checking Windows version");
    OSVERSIONINFOA version = {};
    version.dwOSVersionInfoSize = sizeof(version);
    if (!GetVersionExA(&version))
        return 20;
    setup::Os os =
        setup::select_os(version.dwPlatformId, version.dwMajorVersion, version.dwMinorVersion);
    if (os == setup::Os::unsupported)
        return 20;
    // Wait for ownership before observing mutable device/journal state. The
    // startup executor may finish a phase while this continuation is waiting.
    setup::progress("Waiting for any other DreamGPU setup to finish");
    setup::SetupLock lock(os, 90000);
    if (!lock.acquired())
        return 28;
    const auto global_presence = setup::global::presence();
    // A recorded removal can temporarily leave no present PCI devnode.
    // Component journals still verify the exact original device identity and
    // ownership before recovery; new installation always requires one adapter.
    const bool recovery =
        action == 0 || action == 1 || action == 2 || action == 5 || action == 6 || action == 8 ||
        (action < 0 && !stage_only && global_presence == setup::global::Presence::present);
    setup::progress("Checking DreamGPU adapter");
    Devices devices;
    if (!devices.exactly_one(recovery))
        return 21;
    if (!all_payloads(static_cast<unsigned>(os)))
        return 22;
    // Double-clicking a new installer replaces abandoned extraction state,
    // including receipts written by a different build. Explicit continuation
    // and rollback retain their exact-identity recovery semantics.
    if (action < 0 && !setup::staging::Store::discard_pending())
        return 26;
    setup::progress("Checking saved installation state");
    const auto staging_presence = setup::staging::pending();
    if (staging_presence == setup::staging::Presence::error)
        return 29;
    if (staging_presence == setup::staging::Presence::present) {
        if (global_presence != setup::global::Presence::absent)
            return 29;
        const bool cancel = action == 1 || action == 2 ||
                            setup::staging::cancelling() == setup::staging::Presence::present;
        if (!cancel && action != -1 && action != 0)
            return 29;
        if (!cancel && !stage_only && !setup::providers(os).ready())
            return 30;
        const unsigned staged = stage(static_cast<unsigned>(os), cancel);
        if (staged != 10 || stage_only)
            return staged;
    }
    auto global_action = [&](setup::global::Request request) -> unsigned {
        setup::progress("Preparing system installation");
        if (global_presence == setup::global::Presence::absent &&
            request == setup::global::Request::start) {
            if (!setup::providers(os).ready())
                return 30;
            // Resume an owned staging commit interrupted before its initial
            // component journal. Preparation verifies its durable before-images;
            // never adopt an arbitrary directory or discard a corrupt journal.
            if (!setup::lifecycle::ensure_prepared(os, payloads))
                return 27;
        }
        setup::global::Intent intent = setup::global::Intent::install;
        const auto result = setup::global::act(os, request, payloads, intent);
        using setup::lifecycle::Result;
        if (result == Result::provider_not_ready)
            return 30;
        if (result == Result::pending_reboot)
            return 11;
        if (result == Result::conflict)
            return 29;
        if (result != Result::complete)
            return 26;
        return intent == setup::global::Intent::rollback    ? 12
               : intent == setup::global::Intent::uninstall ? 13
                                                            : 0;
    };
    if (global_presence == setup::global::Presence::error)
        return 29;
    if (global_presence == setup::global::Presence::present && !stage_only) {
        auto request = setup::global::Request::resume;
        if (action < 0)
            request = setup::global::Request::start;
        if (action == 1 || action == 6)
            request = setup::global::Request::rollback;
        if (action == 2)
            request = setup::global::Request::uninstall;
        if (action == 3)
            request = setup::global::Request::upgrade;
        if (action == 7)
            request = setup::global::Request::repair;
        if (action == 8)
            request = setup::global::Request::recover;
        return global_action(request);
    }
    if (action == 7 || action == 8)
        return 29; // Repair requires an owned complete-system installation.
    if (action >= 4) {
        auto result = setup::driver::act(os, action - 4, payloads);
        using setup::driver::Result;
        if (result == Result::verified)
            return 14;
        if (result == Result::restored)
            return 15;
        if (result == Result::pending_reboot)
            return 16;
        return result == Result::conflict ? 29 : result == Result::invalid ? 31 : 26;
    }
    if (action >= 0) {
        if (action == 0) {
            setup::lifecycle::Win32Store owner;
            static setup::lifecycle::Journal journal;
            if (setup::lifecycle::verified_owner(owner)) {
                if (owner.load(journal)) {
                    if (setup::lifecycle::is_system_journal(journal))
                        return global_action(setup::global::Request::start);
                } else {
                    return global_action(setup::global::Request::start);
                }
            }
        }
        auto result = setup::lifecycle::system_act(
            os, static_cast<setup::lifecycle::Action>(action), payloads);
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
    if (stage_only)
        return stage(static_cast<unsigned>(os));
    if (!setup::providers(os).ready())
        return 30;
    char root[MAX_PATH];
    if (!setup::lifecycle::owner_path(root))
        return 24;
    DWORD attributes = GetFileAttributesA(root);
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        if (GetLastError() != ERROR_FILE_NOT_FOUND)
            return 24;
        unsigned result = stage(static_cast<unsigned>(os));
        if (result != 10)
            return result;
    }
    return global_action(setup::global::Request::start);
}
struct Work {
    bool staging;
    int action;
};
DWORD WINAPI install_worker(void *opaque) {
    const auto &work = *static_cast<Work *>(opaque);
    return run(work.staging, work.action);
}
unsigned execute(bool staging, int action, bool valid, bool silent) {
    if (silent)
        return valid ? run(staging, action) : 23;
    const char *title = staging                      ? "Preparing DreamGPU files..."
                        : action == 1 || action == 6 ? "Restoring the previous installation..."
                        : action == 2                ? "Removing DreamGPU..."
                        : action == 7                ? "Repairing DreamGPU..."
                        : action == 8                ? "Recovering DreamGPU setup..."
                        : action == 0 || action == 5 ? "Continuing DreamGPU setup..."
                        : action == 3                ? "Upgrading DreamGPU..."
                                                     : "Installing DreamGPU...";
    if (!setup::ui::open(title))
        return 32;
    if (!valid)
        return 23;
    Work work{staging, action};
    HANDLE worker = CreateThread(nullptr, 0, install_worker, &work, 0, nullptr);
    if (!worker)
        return 32;
    return setup::ui::wait(worker);
}
template <class Function> bool api(Function &function, HMODULE module, const char *name) {
    FARPROC address = GetProcAddress(module, name);
    if (!address)
        return false;
    static_assert(sizeof(address) == sizeof(function));
    CopyMemory(&function, &address, sizeof(function));
    return true;
}
bool restart_windows() {
    OSVERSIONINFOA version{};
    version.dwOSVersionInfoSize = sizeof(version);
    if (!GetVersionExA(&version))
        return false;
    if (version.dwPlatformId == VER_PLATFORM_WIN32_NT) {
        // Resolve NT security APIs only on NT. The unified executable remains
        // loadable on Win98 without adding NT-only loader requirements.
        HMODULE advapi = GetModuleHandleA("advapi32.dll");
        decltype(&OpenProcessToken) open = nullptr;
        decltype(&LookupPrivilegeValueA) lookup = nullptr;
        decltype(&AdjustTokenPrivileges) adjust = nullptr;
        if (!advapi || !api(open, advapi, "OpenProcessToken") ||
            !api(lookup, advapi, "LookupPrivilegeValueA") ||
            !api(adjust, advapi, "AdjustTokenPrivileges"))
            return false;
        HANDLE raw = nullptr;
        if (!open(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &raw))
            return false;
        Handle token(raw);
        TOKEN_PRIVILEGES privileges{};
        privileges.PrivilegeCount = 1;
        privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        if (!lookup(nullptr, "SeShutdownPrivilege", &privileges.Privileges[0].Luid))
            return false;
        SetLastError(ERROR_SUCCESS);
        if (!adjust(token.get(), FALSE, &privileges, 0, nullptr, nullptr) ||
            GetLastError() != ERROR_SUCCESS)
            return false;
    }
    return ExitWindowsEx(EWX_REBOOT, 0) != FALSE;
}
void show_result(unsigned code) {
    if (code == 11 || code == 16) {
        const int choice = setup::ui::message_box(
            setup::ui::window,
            "DreamGPU needs to restart Windows to continue. Setup will resume "
            "automatically.\r\n\r\nRestart now?",
            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2);
        if (choice == IDYES && !restart_windows())
            setup::ui::message_box(
                setup::ui::window,
                "Windows could not restart automatically. Restart Windows to continue setup.",
                MB_OK | MB_ICONWARNING);
        return;
    }
    const char *message =
        "DreamGPU could not finish setup. The saved installation state can be resumed by running "
        "setup again. Use /rollback to restore the previous installation.";
    UINT icon = MB_ICONWARNING;
    switch (code) {
        case 0:
            message = "DreamGPU is installed. OpenGL, Glide 2 and Direct3D are ready for "
                      "applications system-wide.";
            icon = MB_ICONINFORMATION;
            break;
        case 10:
            message = "DreamGPU files have been staged. The GPU has not been activated.";
            icon = MB_ICONINFORMATION;
            break;
        case 12:
            message = "The previous installation has been restored.";
            icon = MB_ICONINFORMATION;
            break;
        case 13:
            message = "DreamGPU has been removed and the previous system drivers restored.";
            icon = MB_ICONINFORMATION;
            break;
        case 14:
            message = "The display driver was verified. This diagnostic does not activate the "
                      "complete GPU system.";
            icon = MB_ICONINFORMATION;
            break;
        case 15:
            message = "The previous display driver was restored.";
            icon = MB_ICONINFORMATION;
            break;
        case 17:
            message = "Unfinished DreamGPU setup files have been removed.";
            icon = MB_ICONINFORMATION;
            break;
        case 20:
            message = "This installer supports 32-bit Windows 98, Windows 2000 and Windows XP.";
            break;
        case 21:
            message = "Setup could not identify a single DreamGPU adapter. Check this virtual "
                      "machine's GPU configuration.";
            break;
        case 22:
            message = "The installer payload failed its integrity check. Obtain an intact DreamGPU "
                      "installer.";
            break;
        case 23:
            message = "The setup command is invalid. Run dreamgpu.exe to install, or use "
                      "/continue, /upgrade, /repair, /rollback, /uninstall or /recover.";
            break;
        case 24:
            message = "Setup could not prepare the installation. The operation and path below "
                      "identify where it stopped.";
            break;
        case 27:
            message = "The files were staged, but setup could not prepare the system installation. "
                      "The saved state has been retained. Run setup again to continue.";
            break;
        case 28:
            message = "DreamGPU setup is already running.";
            break;
        case 29:
            message = "Setup stopped because a file, driver or pending system operation conflicts "
                      "with its saved state. Resolve the conflict before continuing.";
            break;
        case 30:
            message = "This development build does not yet enable complete system installation.";
            break;
        case 31:
            message = "Setup could not capture the existing display driver configuration.";
            break;
        case 32:
            message = "Setup could not start its installation window or worker. "
                      "No installation was started. Close other applications and try again.";
            break;
    }
    char detail[1600];
    if (code >= 20 && code != 28) {
        // Include a stable setup error code and the actual last reported action.
        // GetLastError here could be stale after transaction cleanup.
        wsprintfA(detail, "%s\r\n\r\nSetup error code: %u\r\nLast operation: ", message, code);
        lstrcatA(detail,
                 setup::ui::last_operation[0] ? setup::ui::last_operation : "Starting setup");
        if (setup::failure_error) {
            char error[64];
            wsprintfA(error, "\r\nWindows error code: %u", setup::failure_error);
            lstrcatA(detail, error);
        }
        message = detail;
    }
    setup::ui::message_box(setup::ui::window, message, MB_OK | icon);
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
        char arg[24];
        unsigned n = 0;
        while (*command && *command != ' ' && *command != '\t') {
            if (n < 23)
                arg[n++] = *command;
            else
                valid = false;
            ++command;
        }
        arg[n] = 0;
        if (!lstrcmpiA(arg, "/stage"))
            staging = true;
        else if (!lstrcmpiA(arg, "/driver-install") || !lstrcmpiA(arg, "/driver-resume") ||
                 !lstrcmpiA(arg, "/driver-restore")) {
            if (action >= 0)
                valid = false;
            action = !lstrcmpiA(arg, "/driver-install")  ? 4
                     : !lstrcmpiA(arg, "/driver-resume") ? 5
                                                         : 6;
        } else if (!lstrcmpiA(arg, "/continue") || !lstrcmpiA(arg, "/rollback") ||
                   !lstrcmpiA(arg, "/uninstall") || !lstrcmpiA(arg, "/upgrade") ||
                   !lstrcmpiA(arg, "/repair") || !lstrcmpiA(arg, "/recover")) {
            if (action >= 0)
                valid = false;
            action = !lstrcmpiA(arg, "/continue")    ? 0
                     : !lstrcmpiA(arg, "/rollback")  ? 1
                     : !lstrcmpiA(arg, "/uninstall") ? 2
                     : !lstrcmpiA(arg, "/repair")    ? 7
                     : !lstrcmpiA(arg, "/recover")   ? 8
                                                     : 3;
        } else if (!lstrcmpiA(arg, "/silent"))
            silent = true;
        else
            valid = false;
    }
    unsigned code = execute(staging, action, valid && !(staging && action >= 0), silent);
    const char *result = "{\"schema\":1,\"exit_code\":23,\"status\":\"invalid_arguments\",\"system_"
                         "activated\":false}\r\n";
    switch (code) {
        case 0:
            result = "{\"schema\":1,\"exit_code\":0,\"status\":\"activated\",\"system_activated\":"
                     "true}\r\n";
            break;
        case 14:
            result = "{\"schema\":1,\"exit_code\":14,\"status\":\"driver_verified\",\"driver_"
                     "verified\":true,\"system_activated\":false}\r\n";
            break;
        case 15:
            result = "{\"schema\":1,\"exit_code\":15,\"status\":\"driver_restored\",\"system_"
                     "activated\":false}\r\n";
            break;
        case 16:
            result = "{\"schema\":1,\"exit_code\":16,\"status\":\"driver_pending_reboot\",\"system_"
                     "activated\":false}\r\n";
            break;
        case 31:
            result = "{\"schema\":1,\"exit_code\":31,\"status\":\"driver_capture_invalid\","
                     "\"system_activated\":false}\r\n";
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
        case 17:
            result = "{\"schema\":1,\"exit_code\":17,\"status\":\"stage_cancelled\",\"system_"
                     "activated\":false}\r\n";
            break;
        case 26:
            result = "{\"schema\":1,\"exit_code\":26,\"status\":\"operation_incomplete\",\"system_"
                     "activated\":false}\r\n";
            break;
        case 30:
            result = "{\"schema\":1,\"exit_code\":30,\"status\":\"system_provider_not_ready\","
                     "\"system_activated\":false}\r\n";
            break;
        case 32:
            result = "{\"schema\":1,\"exit_code\":32,\"status\":\"ui_start_failed\","
                     "\"system_activated\":false}\r\n";
            break;
    }
    receipt(result);
    if (!silent) {
        if (setup::ui::window)
            setup::ui::finish(code);
        show_result(code);
        if (setup::ui::window)
            DestroyWindow(setup::ui::window);
    }
    ExitProcess(code);
}
