// SPDX-License-Identifier: GPL-2.0-or-later
// Fixed independent-fixture acceptance helper; never enables provider flags.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "win32-lifecycle.h"
#include "lifecycle-runtime.h"
#ifndef DG_EXPECTED_INSTALLER_HASH
#error Expected installer identity must be supplied by its validated build receipt
#endif
namespace {
HANDLE log_file = INVALID_HANDLE_VALUE;
void line(const char *text) {
    DWORD n;
    if (log_file != INVALID_HANDLE_VALUE) {
        WriteFile(log_file, text, lstrlenA(text), &n, nullptr);
        WriteFile(log_file, "\r\n", 2, &n, nullptr);
        FlushFileBuffers(log_file);
    }
}
bool fail(const char *what) {
    char out[256];
    wsprintfA(out, "FAIL setupcheck %s error=%lu", what, GetLastError());
    line(out);
    return false;
}
class File {
    HANDLE h_ = INVALID_HANDLE_VALUE;

  public:
    explicit File(HANDLE h) : h_(h) {}
    File(const File &) = delete;
    File &operator=(const File &) = delete;
    ~File() {
        if (h_ != INVALID_HANDLE_VALUE && h_)
            CloseHandle(h_);
    }
    HANDLE get() const {
        return h_;
    }
};
using setup::lifecycle::Image;
bool hash_file(const char *path, Image &image) {
    image = {};
    DWORD attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES)
        return GetLastError() == ERROR_FILE_NOT_FOUND;
    if (attrs & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
        return false;
    File file(CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
    if (file.get() == INVALID_HANDLE_VALUE)
        return false;
    DWORD high = 0, size = GetFileSize(file.get(), &high);
    if (high || size > 64 * 1024 * 1024)
        return false;
    BYTE buffer[4096];
    DWORD total = 0, got;
    setup::Sha256 h;
    do {
        if (!ReadFile(file.get(), buffer, sizeof(buffer), &got, nullptr) || got > size - total)
            return false;
        h.update(buffer, got);
        total += got;
    } while (got);
    if (total != size)
        return false;
    image.exists = 1;
    image.size = size;
    h.finish(image.sha);
    return true;
}
constexpr const char *files[] = {"dgpugl.dll",   "glide2x.dll", "wined3d.dll", "winedd.dll",
                                 "wined8.dll",   "wined9.dll",  "dgpuicd.dll", "opengl32.dll",
                                 "ddraw.dll",    "d3d8.dll",    "d3d9.dll",    "dgpudisp.dll",
                                 "dgpumini.drv", "dgpumini.vxd"};
Image before[sizeof(files) / sizeof(files[0])];
Image registry_before[4];
constexpr const char *values[] = {"Dll", "Version", "DriverVersion", "Flags"};
setup::Os operating_system = setup::Os::unsupported;
bool snapshot(bool capture, const char *phase) {
    char system[MAX_PATH];
    DWORD n = GetSystemDirectoryA(system, sizeof(system));
    if (!n || n > MAX_PATH - 32)
        return fail("system directory");
    for (unsigned i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        char path[MAX_PATH];
        lstrcpyA(path, system);
        lstrcatA(path, "\\");
        lstrcatA(path, files[i]);
        Image current;
        if (!hash_file(path, current))
            return fail("global file read");
        if (capture)
            before[i] = current;
        else if (!setup::lifecycle::same(current, before[i]))
            return fail("global file changed");
        char record[256];
        wsprintfA(
            record,
            "{\"event\":\"%s\",\"file\":\"%s\",\"exists\":%lu,\"size\":%lu,\"sha256\":\"%s\"}",
            phase, files[i], current.exists, current.size, current.sha);
        line(record);
    }
    const bool win98 = operating_system == setup::Os::win98;
    for (unsigned i = 0; i < (win98 ? 1u : 4u); i++) {
        Image current = {};
        HKEY key = nullptr;
        LONG error =
            RegOpenKeyExA(HKEY_LOCAL_MACHINE, win98 ? setup::win98_icd_key : setup::nt_icd_key, 0,
                          KEY_QUERY_VALUE, &key);
        if (error != ERROR_SUCCESS && error != ERROR_FILE_NOT_FOUND)
            return fail("registry key read");
        if (error == ERROR_SUCCESS) {
            DWORD type = 0, bytes = sizeof(current.value);
            error = RegQueryValueExA(key, win98 ? "DGPUICD" : values[i], nullptr, &type,
                                     current.value, &bytes);
            RegCloseKey(key);
            if (error != ERROR_SUCCESS && error != ERROR_FILE_NOT_FOUND)
                return fail("registry value read");
            if (error == ERROR_SUCCESS) {
                current.exists = 1;
                current.type = type;
                current.size = bytes;
                setup::Sha256 h;
                h.update(current.value, bytes);
                h.finish(current.sha);
            } else
                current = {};
        }
        if (capture)
            registry_before[i] = current;
        else if (!setup::lifecycle::same(current, registry_before[i]))
            return fail("ICD registry changed");
    }
    return true;
}
bool run(const char *args, DWORD expected, DWORD timeout) {
    char command[160] = "C:\\dreamgpu.exe ";
    lstrcatA(command, args);
    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
    File input(CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                           OPEN_EXISTING, 0, nullptr));
    if (input.get() == INVALID_HANDLE_VALUE)
        return fail("child input");
    STARTUPINFOA startup = {};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = input.get();
    startup.hStdOutput = log_file;
    startup.hStdError = log_file;
    PROCESS_INFORMATION process = {};
    line(args);
    if (!CreateProcessA("C:\\dreamgpu.exe", command, nullptr, nullptr, TRUE, 0, nullptr, "C:\\",
                        &startup, &process))
        return fail("installer launch");
    File thread(process.hThread), child(process.hProcess);
    if (WaitForSingleObject(child.get(), timeout) != WAIT_OBJECT_0) {
        TerminateProcess(child.get(), 99);
        WaitForSingleObject(child.get(), 5000);
        return fail("installer timeout");
    }
    DWORD code;
    if (!GetExitCodeProcess(child.get(), &code))
        return fail("installer exit");
    char record[96];
    wsprintfA(record, "{\"event\":\"installer_exit\",\"expected\":%lu,\"actual\":%lu}", expected,
              code);
    line(record);
    return code == expected || fail("unexpected installer exit");
}
bool absent_owned() {
    char root[MAX_PATH];
    DWORD n = GetWindowsDirectoryA(root, MAX_PATH);
    if (!n || n > MAX_PATH - 20)
        return false;
    constexpr const char *names[] = {"\\DreamGPU", "\\DGSETUP.NEW"};
    for (const char *name : names) {
        char path[MAX_PATH];
        lstrcpyA(path, root);
        lstrcatA(path, name);
        if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES ||
            GetLastError() != ERROR_FILE_NOT_FOUND)
            return false;
    }
    return true;
}
bool journal(bool rolled_back) {
    setup::lifecycle::Win32Store store;
    if (!setup::lifecycle::verified_owner(store))
        return fail("private receipt");
    static setup::lifecycle::Journal j;
    if (!store.load(j))
        return fail("journal identity");
    if (j.count != 6 || j.generation != 1 ||
        j.state !=
            (rolled_back ? setup::lifecycle::State::failed : setup::lifecycle::State::staged))
        return fail("journal state");
    for (unsigned i = 0; i < j.count; i++) {
        const auto &item = j.items[i];
        if (item.kind != setup::lifecycle::Kind::file ||
            lstrcmpA(item.path, setup::shared_runtime[i]) ||
            !setup::lifecycle::same(item.before, before[i]) ||
            !setup::lifecycle::same(item.original, before[i]) ||
            store.classify(j, i, true) != setup::lifecycle::Actual::after)
            return fail("journal ownership");
        if (rolled_back && item.phase != setup::lifecycle::Phase::restored &&
            item.phase != setup::lifecycle::Phase::borrowed)
            return fail("rollback phase");
        char record[256];
        wsprintfA(record,
                  "{\"event\":\"journal_item\",\"path\":\"%s\",\"phase\":%lu,\"original_sha256\":"
                  "\"%s\",\"desired_sha256\":\"%s\"}",
                  item.path, DWORD(item.phase), item.original.sha, item.desired.sha);
        line(record);
    }
    return true;
}
bool verify() {
    OSVERSIONINFOA os = {};
    os.dwOSVersionInfoSize = sizeof(os);
    if (!GetVersionExA(&os))
        return fail("Windows version");
    operating_system = setup::select_os(os.dwPlatformId, os.dwMajorVersion, os.dwMinorVersion);
    if (operating_system == setup::Os::unsupported)
        return fail("Windows98/2000/XP fixture required");
    char version[160];
    wsprintfA(version,
              "{\"event\":\"os\",\"platform\":%lu,\"major\":%lu,\"minor\":%lu,"
              "\"global_files\":14,\"icd_values\":%u}",
              os.dwPlatformId, os.dwMajorVersion, os.dwMinorVersion,
              operating_system == setup::Os::win98 ? 1u : 4u);
    line(version);
    if (!absent_owned())
        return fail("fixture already has installer state");
    Image installer;
    if (!hash_file("C:\\dreamgpu.exe", installer) || !installer.exists ||
        lstrcmpA(installer.sha, DG_EXPECTED_INSTALLER_HASH))
        return fail("installer hash");
    line("{\"event\":\"installer_identity\",\"sha256\":\"" DG_EXPECTED_INSTALLER_HASH "\"}");
    if (!snapshot(true, "before") || !run("/silent", 30, 20000) || !absent_owned() ||
        !snapshot(false, "after_default"))
        return false;
    if (!run("/stage /silent", 10, 60000) || !journal(false) || !snapshot(false, "after_stage"))
        return false;
    if (!run("/rollback /silent", 12, 20000) || !journal(true) ||
        !snapshot(false, "after_rollback"))
        return false;
    line("PASS automated setupcheck: exact installer; default no private state; six staged "
         "ownership records; "
         "rollback; fourteen global hashes and OS-specific ICD values unchanged; system activation "
         "never claimed");
    return true;
}
} // namespace
extern "C" void WINAPI WinMainCRTStartup() {
    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
    log_file = CreateFileA("C:\\DGSETTST.LOG", GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log_file == INVALID_HANDLE_VALUE)
        ExitProcess(2);
    bool okay = verify();
    FlushFileBuffers(log_file);
    CloseHandle(log_file);
    ExitProcess(okay ? 0 : 1);
}
