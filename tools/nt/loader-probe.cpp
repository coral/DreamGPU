// SPDX-License-Identifier: GPL-2.0-or-later
// Isolated NT5 loader feasibility probe. Publishes only DGPTST.DLL, backed by
// a frozen marker image, while a demand-start service owns the section handle.
// No Microsoft DLL, permanent object, KnownDLL registry list or graphics setting
// is changed. The owned service/section is removed at the end of the probe.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
namespace {
constexpr char ServiceName[] = "DreamGPULoaderProbe";
constexpr char ReadyName[] = "Global\\DreamGPU.LoaderProbe.Ready";
constexpr char StopName[] = "Global\\DreamGPU.LoaderProbe.Stop";
constexpr char MarkerPath[] = "C:\\DGPTST.BIN";
constexpr DWORD Marker = 0x44504755;
class Handle {
  public:
    HANDLE value = nullptr;
    Handle() = default;
    Handle(const Handle &) = delete;
    ~Handle() {
        if (value && value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
};
class ServiceHandle {
  public:
    SC_HANDLE value = nullptr;
    ServiceHandle() = default;
    ServiceHandle(const ServiceHandle &) = delete;
    ~ServiceHandle() {
        if (value)
            CloseServiceHandle(value);
    }
};
class Log {
    Handle file_;

  public:
    explicit Log(const char *path) {
        file_.value = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    bool valid() const {
        return file_.value != INVALID_HANDLE_VALUE;
    }
    void line(const char *text) {
        DWORD written;
        if (valid()) {
            WriteFile(file_.value, text, lstrlenA(text), &written, nullptr);
            WriteFile(file_.value, "\r\n", 2, &written, nullptr);
            FlushFileBuffers(file_.value);
        }
    }
    void number(const char *name, DWORD value) {
        char text[96];
        unsigned n = 0;
        while (*name && n < 85)
            text[n++] = *name++;
        text[n++] = ' ';
        for (int shift = 28; shift >= 0; shift -= 4)
            text[n++] = "0123456789abcdef"[(value >> shift) & 15];
        text[n] = 0;
        line(text);
    }
    void include(const char *path) {
        Handle input;
        input.value = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (input.value == INVALID_HANDLE_VALUE)
            return;
        char bytes[1025];
        for (unsigned n = 0; n < 16; ++n) {
            DWORD count = 0;
            if (!ReadFile(input.value, bytes, sizeof(bytes) - 1, &count, nullptr) || !count)
                break;
            bytes[count] = 0;
            line(bytes);
        }
    }
};
template <class T> T entry(const char *name) {
    FARPROC raw = GetProcAddress(GetModuleHandleA("ntdll.dll"), name);
    T result;
    static_assert(sizeof(result) == sizeof(raw));
    CopyMemory(&result, &raw, sizeof(result));
    return result;
}
UNICODE_STRING unicode(wchar_t *value) {
    const auto size = static_cast<USHORT>(lstrlenW(value) * sizeof(wchar_t));
    return {size, static_cast<USHORT>(size + sizeof(wchar_t)), value};
}
OBJECT_ATTRIBUTES attributes(UNICODE_STRING *name, HANDLE root = nullptr,
                             void *security = nullptr) {
    OBJECT_ATTRIBUTES value{};
    value.Length = sizeof(value);
    value.RootDirectory = root;
    value.ObjectName = name;
    value.Attributes = 0x40; // OBJ_CASE_INSENSITIVE, never OPENIF/PERMANENT.
    value.SecurityDescriptor = security;
    return value;
}
HANDLE stop_event = nullptr;
SERVICE_STATUS_HANDLE status_handle = nullptr;
SERVICE_STATUS status = {
    SERVICE_WIN32_OWN_PROCESS, SERVICE_START_PENDING, SERVICE_ACCEPT_STOP, NO_ERROR, 0, 0, 0};
void state(DWORD value, DWORD error = 0) {
    status.dwCurrentState = value;
    status.dwWin32ExitCode = error;
    if (status_handle)
        SetServiceStatus(status_handle, &status);
}
void WINAPI control(DWORD code) {
    if (code == SERVICE_CONTROL_STOP && stop_event)
        SetEvent(stop_event);
}
bool publish(Handle &section, Log &log) {
    using OpenDirectory = NTSTATUS(NTAPI *)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES);
    using CreateSection = NTSTATUS(NTAPI *)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES,
                                            PLARGE_INTEGER, ULONG, ULONG, HANDLE);
    const auto open_directory = entry<OpenDirectory>("NtOpenDirectoryObject");
    const auto create_section = entry<CreateSection>("NtCreateSection");
    if (!open_directory || !create_section)
        return false;
    wchar_t directory_name[] = L"\\KnownDlls";
    auto directory_string = unicode(directory_name);
    auto directory_attributes = attributes(&directory_string);
    Handle directory;
    NTSTATUS result = open_directory(&directory.value, 7, &directory_attributes);
    log.number("OPEN_DIRECTORY", static_cast<DWORD>(result));
    if (result < 0)
        return false;
    Handle file;
    file.value = CreateFileA(MarkerPath, GENERIC_READ | GENERIC_EXECUTE, FILE_SHARE_READ, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file.value == INVALID_HANDLE_VALUE) {
        log.number("OPEN_IMAGE", GetLastError());
        return false;
    }
    // Only mapping/query access is granted to ordinary loaders. The marker's
    // image pages use normal image copy-on-write semantics, not a writable file.
    SID_IDENTIFIER_AUTHORITY world_authority = SECURITY_WORLD_SID_AUTHORITY;
    PSID world = nullptr;
    if (!AllocateAndInitializeSid(&world_authority, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0,
                                  &world))
        return false;
    alignas(ACL) BYTE acl_bytes[128]{};
    auto *acl = reinterpret_cast<ACL *>(acl_bytes);
    SECURITY_DESCRIPTOR security{};
    bool valid = InitializeAcl(acl, sizeof(acl_bytes), ACL_REVISION) &&
                 AddAccessAllowedAce(acl, ACL_REVISION,
                                     SECTION_QUERY | SECTION_MAP_READ | SECTION_MAP_WRITE |
                                         SECTION_MAP_EXECUTE,
                                     world) &&
                 InitializeSecurityDescriptor(&security, SECURITY_DESCRIPTOR_REVISION) &&
                 SetSecurityDescriptorDacl(&security, TRUE, acl, FALSE);
    FreeSid(world);
    if (!valid)
        return false;
    wchar_t name[] = L"DGPTST.DLL";
    auto text = unicode(name);
    auto object = attributes(&text, directory.value, &security);
    result = create_section(&section.value, SECTION_QUERY | SECTION_MAP_READ | SECTION_MAP_EXECUTE,
                            &object, nullptr, PAGE_READONLY, SEC_IMAGE, file.value);
    log.number("CREATE_MARKER_SECTION", static_cast<DWORD>(result));
    return result >= 0;
}
void WINAPI service_main(DWORD, LPSTR *) {
    Log log("C:\\DGLOADS.LOG");
    Handle stop, ready;
    stop.value = OpenEventA(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, StopName);
    ready.value = OpenEventA(EVENT_MODIFY_STATE, FALSE, ReadyName);
    stop_event = stop.value;
    status_handle = RegisterServiceCtrlHandlerA(ServiceName, control);
    if (!status_handle)
        return;
    state(SERVICE_RUNNING);
    {
        Handle section;
        const bool okay = log.valid() && stop.value && ready.value && publish(section, log);
        log.line(okay ? "SECTION_READY" : "SECTION_FAILED");
        if (ready.value)
            SetEvent(ready.value);
        if (okay)
            WaitForSingleObject(stop.value, 45000);
    }
    stop_event = nullptr;
    state(SERVICE_STOPPED);
}
bool absent(const char *path) {
    SetLastError(0);
    return GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES &&
           GetLastError() == ERROR_FILE_NOT_FOUND;
}
bool dynamic(const char *path, Log &log, DWORD expected = Marker) {
    HMODULE module = LoadLibraryA(path);
    log.line(path);
    if (!module) {
        log.number("LOAD_ERROR", GetLastError());
        return false;
    }
    FARPROC raw = GetProcAddress(module, "DreamGpuLoaderMarker");
    using MarkerFunction = DWORD (*)();
    MarkerFunction function;
    CopyMemory(&function, &raw, sizeof(function));
    const DWORD marker = function ? function() : 0;
    bool okay = function && marker == expected;
    log.number("OBSERVED_MARKER", marker);
    char actual[MAX_PATH];
    DWORD size = GetModuleFileNameA(module, actual, sizeof(actual));
    if (size && size < sizeof(actual))
        log.line(actual);
    else
        okay = false;
    FreeLibrary(module);
    log.number("DYNAMIC_MARKER", okay);
    return okay;
}
bool implicit(Log &log) {
    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    char command[] = "C:\\DGIMPLC.EXE";
    if (!CreateProcessA(command, command, nullptr, nullptr, FALSE, 0, nullptr, "C:\\", &startup,
                        &child)) {
        log.number("IMPLICIT_CREATE", GetLastError());
        return false;
    }
    Handle process, thread;
    process.value = child.hProcess;
    thread.value = child.hThread;
    DWORD waited = WaitForSingleObject(process.value, 5000), code = 0xffffffff;
    if (waited != WAIT_OBJECT_0) {
        TerminateProcess(process.value, 1);
        WaitForSingleObject(process.value, 1000);
    } else
        GetExitCodeProcess(process.value, &code);
    log.number("IMPLICIT_EXIT", code);
    return waited == WAIT_OBJECT_0 && code == 0;
}
DWORD run() {
    Log log("C:\\DGLOAD.LOG");
    if (!log.valid())
        return 2;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    OSVERSIONINFOA version{};
    version.dwOSVersionInfoSize = sizeof(version);
    if (!GetVersionExA(&version) || version.dwPlatformId != VER_PLATFORM_WIN32_NT ||
        version.dwMajorVersion != 5 || version.dwMinorVersion > 1)
        return 3;
    char system[MAX_PATH];
    DWORD length = GetSystemDirectoryA(system, sizeof(system));
    if (!length || length > MAX_PATH - 14)
        return 3;
    lstrcatA(system, "\\DGPTST.DLL");
    if (!absent("C:\\DGPTST.DLL") || GetFileAttributesA(system) == INVALID_FILE_ATTRIBUTES)
        return 4;
    // A separate native marker file models an existing system API without
    // touching any Microsoft runtime. It must load normally before publication.
    if (!dynamic(system, log, 0x4e415449) || GetModuleHandleA("DGPTST.DLL"))
        return 4;
    Handle ready, stop;
    log.line("CREATE_READY_EVENT");
    ready.value = CreateEventA(nullptr, TRUE, FALSE, ReadyName);
    if (!ready.value || GetLastError() == ERROR_ALREADY_EXISTS)
        return 5;
    log.line("CREATE_STOP_EVENT");
    stop.value = CreateEventA(nullptr, TRUE, FALSE, StopName);
    if (!stop.value || GetLastError() == ERROR_ALREADY_EXISTS)
        return 5;
    ServiceHandle manager, service;
    log.line("OPEN_SERVICE_MANAGER");
    manager.value =
        OpenSCManagerA(nullptr, nullptr, SC_MANAGER_CONNECT | SC_MANAGER_CREATE_SERVICE);
    if (!manager.value) {
        log.number("SCM_OPEN", GetLastError());
        return 6;
    }
    log.line("CREATE_SERVICE");
    service.value =
        CreateServiceA(manager.value, ServiceName, ServiceName,
                       SERVICE_START | SERVICE_QUERY_STATUS | SERVICE_STOP | DELETE,
                       SERVICE_WIN32_OWN_PROCESS, SERVICE_DEMAND_START, SERVICE_ERROR_IGNORE,
                       "\"C:\\DGLOAD.EXE\" /service", nullptr, nullptr, nullptr, nullptr, nullptr);
    if (!service.value) {
        log.number("SERVICE_CREATE", GetLastError());
        return 6;
    }
    bool tests = false;
    log.line("START_SERVICE");
    const bool started = StartServiceA(service.value, 0, nullptr);
    log.number("SERVICE_START_RESULT", started ? 0 : GetLastError());
    if (started && WaitForSingleObject(ready.value, 10000) == WAIT_OBJECT_0) {
        bool a = dynamic("DGPTST.DLL", log);
        bool b = dynamic(system, log);
        bool c = implicit(log);
        tests = a && b && c;
    } else
        log.number("SERVICE_START_OR_WAIT", GetLastError());
    log.line("STOP_SERVICE");
    SetEvent(stop.value);
    SERVICE_STATUS service_status{};
    bool stopped = false;
    for (unsigned i = 0; i < 100; ++i) {
        if (!QueryServiceStatus(service.value, &service_status))
            break;
        if (service_status.dwCurrentState == SERVICE_STOPPED) {
            stopped = true;
            break;
        }
        Sleep(50);
    }
    bool removed = stopped && DeleteService(service.value);
    log.include("C:\\DGLOADS.LOG");
    log.number("SERVICE_REMOVED", removed);
    const bool restored = removed && dynamic(system, log, 0x4e415449);
    log.number("ORIGINAL_PROVIDER_RESTORED", restored);
    log.line(tests && restored
                 ? "PASS automated ntloader: implicit bare_and_absolute_dynamic_marker=3 "
                   "owned_service_removed=1"
                 : "FAIL automated ntloader: native loader route or cleanup incomplete");
    return tests && restored ? 0 : 1;
}
} // namespace
extern "C" void WINAPI WinMainCRTStartup() {
    if (lstrcmpA(GetCommandLineA(), "\"C:\\DGLOAD.EXE\" /service") == 0 ||
        lstrcmpA(GetCommandLineA(), "C:\\DGLOAD.EXE /service") == 0) {
        {
            Log log("C:\\DGLOADS.LOG");
            log.line("SERVICE_DISPATCH");
        }
        SERVICE_TABLE_ENTRYA table[] = {{const_cast<char *>(ServiceName), service_main},
                                        {nullptr, nullptr}};
        ExitProcess(StartServiceCtrlDispatcherA(table) ? 0 : 1);
    }
    ExitProcess(run());
}
