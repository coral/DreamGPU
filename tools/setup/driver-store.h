// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "driver-node.h"
#include "driver-unbound.h"
#include "driver-service.h"
#include "sha256.h"
#include "driver-child.h"
namespace setup::driver {
// Concrete OS adapter. The policy engine never receives an HKEY/HDEVINFO or
// mutates files directly. The original Microsoft VGA files are verified in place;
// only files owned by an existing DreamGPU binding are copied into rollback media.
class Win32Store final {
    struct Handle {
        HANDLE h = INVALID_HANDLE_VALUE;
        explicit Handle(HANDLE value) : h(value) {}
        Handle(const Handle &) = delete;
        ~Handle() {
            if (h != INVALID_HANDLE_VALUE && h)
                CloseHandle(h);
        }
    };
    struct Key {
        HKEY h = 0;
        Key() = default;
        Key(const Key &) = delete;
        ~Key() {
            if (h && h != INVALID_HANDLE_VALUE)
                RegCloseKey(h);
        }
    };
    HDEVINFO devices_ = INVALID_HANDLE_VALUE;
    SP_DEVINFO_DATA device_{};
    bool device_present_ = false;
    char root_[MAX_PATH]{}, windows_[MAX_PATH]{}, system_[MAX_PATH]{};
    char installer_[MAX_PATH]{}, command_[MAX_PATH + 64]{};
    char resume_installer_[MAX_PATH]{}, resume_sha_[68]{};
    Os os_ = Os::unsupported;
    UnboundStore unbound_, package_;
    bool generation_ = false, package_ready_ = false, managed_startup_ = false;
    ServiceStore service_;
    bool unbound_ready_ = false;
    bool unbound_helpers(const Journal &j) {
        if (j.original_binding != Binding::unbound)
            return true;
        if (unbound_ready_)
            return true;
        unbound_ready_ = unbound_.init(root_, windows_, system_, j.original.device) &&
                         service_.init(root_, system_, j.original.device);
        return unbound_ready_;
    }

    bool package_helpers(const Journal &j) {
        if (!generation_ || os_ != Os::nt5 || j.original_binding != Binding::dreamgpu)
            return true;
        if (!package_ready_)
            package_ready_ = package_.init(root_, windows_, system_, j.original.device, true);
        return package_ready_;
    }
    bool tracks_package(const Journal &j) const {
        return generation_ && os_ == Os::nt5 && j.original_binding == Binding::dreamgpu;
    }

    const char *stage_ = "init";
    static const char *run_key(const Journal &j) {
        return (j.os == Os::win98 && j.version == 3) || (j.os == Os::nt5 && j.version == 4)
                   ? "Software\\Microsoft\\Windows\\CurrentVersion\\Run"
                   : "Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce";
    }
    static constexpr const char *RunName = "DreamGPU.Driver";
    static bool absent(const char *p) {
        return GetFileAttributesA(p) == INVALID_FILE_ATTRIBUTES &&
               GetLastError() == ERROR_FILE_NOT_FOUND;
    }
    static bool append(char *out, const char *base, const char *suffix) {
        if (lstrlenA(base) + lstrlenA(suffix) >= MAX_PATH)
            return false;
        if (out != base)
            lstrcpyA(out, base);
        lstrcatA(out, suffix);
        return true;
    }
    bool path(char *out, const char *suffix) const {
        return append(out, root_, suffix);
    }
    static bool string(HKEY key, const char *name, char *out, DWORD capacity) {
        DWORD bytes = capacity, type = 0;
        return RegQueryValueExA(key, name, nullptr, &type, reinterpret_cast<BYTE *>(out), &bytes) ==
                   ERROR_SUCCESS &&
               type == REG_SZ && bytes > 1 && bytes <= capacity && !out[bytes - 1] &&
               DWORD(lstrlenA(out)) + 1 == bytes;
    }
    static bool image(const char *p, File &out) {
        out.exists = 0;
        out.size = 0;
        out.sha[0] = 0;
        DWORD attr = GetFileAttributesA(p);
        if (attr == INVALID_FILE_ATTRIBUTES)
            return GetLastError() == ERROR_FILE_NOT_FOUND;
        if (attr & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
            return false;
        Handle f(CreateFileA(p, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
        if (f.h == INVALID_HANDLE_VALUE)
            return false;
        DWORD high = 0, size = GetFileSize(f.h, &high);
        if (high || size > 64u * 1024 * 1024)
            return false;
        Sha256 hash;
        BYTE buffer[4096];
        DWORD total = 0, bytes = 0;
        do {
            if (!ReadFile(f.h, buffer, sizeof(buffer), &bytes, nullptr) || bytes > size - total)
                return false;
            hash.update(buffer, bytes);
            total += bytes;
        } while (bytes);
        if (total != size)
            return false;
        out.exists = 1;
        out.size = size;
        hash.finish(out.sha);
        return true;
    }
    static bool match(const char *p, const File &expected) {
        File found;
        return image(p, found) && found.exists == expected.exists &&
               (!found.exists ||
                (found.size == expected.size && !lstrcmpA(found.sha, expected.sha)));
    }
    static bool hash_match(const char *p, const char *sha) {
        File found;
        return image(p, found) && found.exists && !lstrcmpA(found.sha, sha);
    }
    static bool flush(const char *p, DWORD expected_attributes) {
        DWORD attrs = GetFileAttributesA(p);
        if (attrs == INVALID_FILE_ATTRIBUTES || expected_attributes == INVALID_FILE_ATTRIBUTES ||
            (attrs & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
            return false;
        // CopyFile preserves READONLY. Open the owned private copy for flushing
        // with that bit temporarily clear, then restore the source attributes
        // while the write handle is still open and flush the completed file.
        if ((attrs & FILE_ATTRIBUTE_READONLY) &&
            !SetFileAttributesA(p, attrs & ~FILE_ATTRIBUTE_READONLY))
            return false;
        Handle f(
            CreateFileA(p, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr));
        bool restored = SetFileAttributesA(p, expected_attributes) != FALSE;
        return restored && f.h != INVALID_HANDLE_VALUE && FlushFileBuffers(f.h) &&
               GetFileAttributesA(p) == expected_attributes;
    }
    static bool copy(const char *from, const char *to, const char *sha) {
        if (!hash_match(from, sha))
            return false;
        DWORD source_attributes = GetFileAttributesA(from), attributes = GetFileAttributesA(to);
        if (attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes &
             (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_READONLY)))
            return false;
        if (attributes == INVALID_FILE_ATTRIBUTES && GetLastError() != ERROR_FILE_NOT_FOUND)
            return false;
        {
            Handle input(CreateFileA(from, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0,
                                     nullptr));
            Handle output(
                CreateFileA(to, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                            attributes == INVALID_FILE_ATTRIBUTES ? CREATE_NEW : OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr));
            if (input.h == INVALID_HANDLE_VALUE || output.h == INVALID_HANDLE_VALUE)
                return false;
            DWORD high = 0, size = GetFileSize(input.h, &high);
            if (high || size > 64u * 1024 * 1024)
                return false;
            DWORD existing = GetFileSize(output.h, &high), offset = 0;
            if (high || existing > size)
                return false;
            BYTE source[4096], target[4096];
            while (offset < existing) {
                DWORD count = existing - offset, got = 0;
                if (count > sizeof(source))
                    count = sizeof(source);
                if (!ReadFile(input.h, source, count, &got, nullptr) || got != count ||
                    !ReadFile(output.h, target, count, &got, nullptr) || got != count)
                    return false;
                for (DWORD n = 0; n < count; ++n)
                    if (source[n] != target[n])
                        return false;
                offset += count;
            }
            while (offset < size) {
                DWORD count = size - offset, got = 0, wrote = 0;
                if (count > sizeof(source))
                    count = sizeof(source);
                if (!ReadFile(input.h, source, count, &got, nullptr) || got != count ||
                    !WriteFile(output.h, source, count, &wrote, nullptr) || wrote != count)
                    return false;
                offset += count;
            }
            if (!FlushFileBuffers(output.h))
                return false;
        }
        return hash_match(from, sha) && hash_match(to, sha) && flush(to, source_attributes);
    }
    static bool ensure_copy(const char *from, const char *to, const char *sha) {
        return hash_match(to, sha) ? flush(to, GetFileAttributesA(from)) : copy(from, to, sha);
    }
    static bool component(const char *s) {
        if (!bounded(s, 256) || !safe_path(s))
            return false;
        for (; *s; ++s)
            if (*s == '/')
                return false;
        return true;
    }
    bool missing_property(DWORD property) {
        BYTE bytes[512];
        DWORD size = 0, type = 0;
        if (SetupDiGetDeviceRegistryPropertyA(devices_, &device_, property, &type, bytes,
                                              sizeof(bytes), &size))
            return false;
        const DWORD error = GetLastError();
        return error == ERROR_INVALID_DATA || error == ERROR_FILE_NOT_FOUND;
    }
    bool node(Node &out) {
        out = {};
        if (!device_present_)
            return false;
        if (CM_Get_Device_IDA(device_.DevInst, out.device, sizeof(out.device), 0) != CR_SUCCESS ||
            !bounded(out.device, sizeof(out.device)) || !pci(out.device, sizeof(out.device)))
            return false;
        if (os_ == Os::nt5 && missing_property(SPDRP_DRIVER)) {
            // An unconfigured PCI device has no previous driver node to select.
            // Require both binding and service absence; do not invent a VGA INF.
            if (!missing_property(SPDRP_SERVICE))
                return false;
            Key absent_key;
            absent_key.h =
                SetupDiOpenDevRegKey(devices_, &device_, DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_READ);
            if (absent_key.h != INVALID_HANDLE_VALUE)
                return false;
            const DWORD error = GetLastError();
            return error == ERROR_KEY_DOES_NOT_EXIST || error == ERROR_FILE_NOT_FOUND;
        }
        Key key;
        key.h = SetupDiOpenDevRegKey(devices_, &device_, DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_READ);
        if (key.h == INVALID_HANDLE_VALUE)
            return false;
        char name[256]{};
        stage_ = "original INF/section/provider/description registry fields";
        if (!string(key.h, "InfPath", name, sizeof(name)) || !component(name) ||
            !string(key.h, "InfSection", out.section, sizeof(out.section)) ||
            !string(key.h, "ProviderName", out.provider, sizeof(out.provider)) ||
            !string(key.h, "DriverDesc", out.description, sizeof(out.description)) ||
            !append(out.inf, windows_, "\\INF\\") || lstrlenA(out.inf) + lstrlenA(name) >= MAX_PATH)
            return false;
        lstrcatA(out.inf, name);
        File file;
        stage_ = "original installed INF hash";
        if (!image(out.inf, file) || !file.exists)
            return false;
        lstrcpyA(out.inf_sha, file.sha);
        return true;
    }
    static bool same_node(const Node &a, const Node &b) {
        return !lstrcmpiA(a.device, b.device) && !lstrcmpA(a.section, b.section) &&
               !lstrcmpA(a.provider, b.provider) && !lstrcmpA(a.description, b.description) &&
               !lstrcmpA(a.inf_sha, b.inf_sha);
    }
    bool current_kind(bool &owned) {
        // These are the two reconstructable classes supported by this first
        // lifecycle: existing DreamGPU, and the OS's VGA fallback. Any other
        // display package fails before the capture directory is created.
        Key key, defaults;
        key.h = SetupDiOpenDevRegKey(devices_, &device_, DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_READ);
        if (key.h == INVALID_HANDLE_VALUE)
            return false;
        char name[256]{};
        if (os_ == Os::win98) {
            if (RegOpenKeyExA(key.h, "DEFAULT", 0, KEY_READ, &defaults.h) ||
                !string(defaults.h, "drv", name, sizeof(name)))
                return false;
            owned = !lstrcmpiA(name, "dgpumini.drv");
            if (!owned)
                return !lstrcmpiA(name, "vga.drv");
            return string(defaults.h, "minivdd", name, sizeof(name)) &&
                   !lstrcmpiA(name, "dgpumini.vxd");
        }
        DWORD type = 0, bytes = 0;
        if (!SetupDiGetDeviceRegistryPropertyA(devices_, &device_, SPDRP_SERVICE, &type,
                                               reinterpret_cast<BYTE *>(name), sizeof(name),
                                               &bytes) ||
            type != REG_SZ || !bytes || bytes > sizeof(name) || name[bytes - 1])
            return false;
        owned = !lstrcmpiA(name, "dgpumini");
        return owned || !lstrcmpiA(name, "Vga") || !lstrcmpiA(name, "VgaSave");
    }
    enum class ChildState { gone, running, error };
    int child_kind(const char *exe) const {
        const char *suffixes[] = {
            "\\driver-backup\\DGDBIND.EXE",
            os_ == Os::win98 ? "\\drivers\\win98\\DGDBIND.EXE" : "\\drivers\\nt5\\DGDBIND.EXE",
            os_ == Os::win98 ? "\\tools\\common\\DG9AUDIT.EXE" : "\\tools\\nt5\\DGAUDIT.EXE"};
        for (unsigned n = 0; n < 3; ++n) {
            char allowed[MAX_PATH];
            if (path(allowed, suffixes[n]) && !lstrcmpiA(exe, allowed))
                return int(n);
        }
        return -1;
    }
    bool child_path(const Journal &j, const char *exe, const char *sha) const {
        const int kind = child_kind(exe);
        return kind >= 0 && !lstrcmpA(sha, kind == 2 ? j.audit_sha : j.helper_sha) &&
               hash_match(exe, sha);
    }
    bool capture_child(const char *exe, HANDLE process, DWORD pid, const Journal &j) {
        if (os_ != Os::nt5)
            return true;
        char filename[MAX_PATH];
        ChildRecord record, previous;
        record.pid = pid;
        File executable;
        if (!path(filename, "\\driver-child.bin") || !image(exe, executable) ||
            !executable.exists || !ChildApi::creation(process, record.low, record.high))
            return false;
        lstrcpyA(record.executable, exe);
        lstrcpyA(record.sha, executable.sha);
        lstrcpyA(record.installer_sha, j.installer_sha);
        if (!child_path(j, exe, record.sha))
            return false;
        DurableRecord<ChildRecord> receipt(filename);
        bool exists;
        return receipt.load(previous, exists, valid_child) && receipt.save(record, valid_child);
    }
    ChildState child_state(const Journal &j) {
        if (!j.child_pid)
            return ChildState::gone;
        DWORD access = SYNCHRONIZE;
        if (os_ == Os::nt5)
            access |= PROCESS_QUERY_INFORMATION | PROCESS_VM_READ;
        Handle process(OpenProcess(access, FALSE, j.child_pid));
        if (!process.h || process.h == INVALID_HANDLE_VALUE)
            return GetLastError() == ERROR_INVALID_PARAMETER ? ChildState::gone : ChildState::error;
        DWORD wait = WaitForSingleObject(process.h, 0);
        if (wait == WAIT_OBJECT_0)
            return ChildState::gone;
        if (wait != WAIT_TIMEOUT)
            return ChildState::error;
        if (os_ != Os::nt5)
            return ChildState::running; // Keep the conservative Win9x lifetime guard.
        char filename[MAX_PATH];
        ChildRecord record;
        bool exists;
        if (!path(filename, "\\driver-child.bin"))
            return ChildState::error;
        DurableRecord<ChildRecord> receipt(filename);
        if (!receipt.load(record, exists, valid_child, true))
            return ChildState::error;
        if (exists) {
            if (record.pid != j.child_pid || lstrcmpA(record.installer_sha, j.installer_sha) ||
                !child_path(j, record.executable, record.sha))
                return ChildState::error;
            uint32_t low = 0, high = 0;
            if (!ChildApi::creation(process.h, low, high))
                return ChildState::error;
            return low == record.low && high == record.high ? ChildState::running
                                                            : ChildState::gone;
        }
        char executable[MAX_PATH]{};
        if (!ChildApi::executable(process.h, system_, executable))
            return ChildState::error;
        // No old creation identity exists. A live process at an authenticated
        // private helper path remains pending; a different executable is PID reuse.
        const int kind = child_kind(executable);
        if (kind < 0)
            return ChildState::gone;
        return child_path(j, executable, kind == 2 ? j.audit_sha : j.helper_sha)
                   ? ChildState::running
                   : ChildState::error;
    }
    bool run(const char *exe, Journal &j, bool mutation = false) {
        STARTUPINFOA si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        char cmd[MAX_PATH + 4], cwd[MAX_PATH];
        if (lstrlenA(exe) >= MAX_PATH)
            return false;
        wsprintfA(cmd, "\"%s\"", exe);
        lstrcpyA(cwd, exe);
        unsigned n = lstrlenA(cwd);
        while (n && cwd[n - 1] != '\\')
            --n;
        if (!n)
            return false;
        cwd[n - 1] = 0;
        if (!CreateProcessA(exe, cmd, nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, cwd, &si,
                            &pi))
            return false;
        Handle process(pi.hProcess), thread(pi.hThread);
        j.child_pid = pi.dwProcessId;
        if (!capture_child(exe, process.h, pi.dwProcessId, j) || !persist(j) ||
            ResumeThread(thread.h) == DWORD(-1)) {
            TerminateProcess(process.h, 1); // Still suspended: no guest code ran.
            WaitForSingleObject(process.h, 2000);
            return false;
        }
        // Never kill a SetupAPI mutation on timeout. Its resulting binding must
        // be reconciled on continuation; the helper itself bounds its dialog UI.
        DWORD waited = WaitForSingleObject(process.h, 120000);
        if (waited != WAIT_OBJECT_0)
            return waited == WAIT_TIMEOUT && mutation;

        DWORD code = 1;
        if (!GetExitCodeProcess(process.h, &code))
            return false;
        j.child_pid = 0;
        return persist(j) && code == 0;
    }
    bool read_resume(Journal &j) {
        Key key;
        LONG error = RegOpenKeyExA(HKEY_LOCAL_MACHINE, run_key(j), 0, KEY_QUERY_VALUE, &key.h);
        if (error == ERROR_FILE_NOT_FOUND)
            return true;
        if (error)
            return false;
        DWORD size = sizeof(j.resume_value), type = 0;
        error = RegQueryValueExA(key.h, RunName, nullptr, &type, j.resume_value, &size);
        if (error == ERROR_FILE_NOT_FOUND)
            return true;
        if (error || size > sizeof(j.resume_value))
            return false;
        j.resume_existed = 1;
        j.resume_type = type;
        j.resume_bytes = size;
        return true;
    }
    bool resume_value(const Journal &j, bool &ours, bool &prior, bool &missing) {
        Journal current;
        current.os = j.os;
        current.version = j.version;
        if (!read_resume(current))
            return false;
        missing = !current.resume_existed;
        ours = current.resume_existed && current.resume_type == REG_SZ &&
               current.resume_bytes == DWORD(lstrlenA(command_)) + 1;
        if (ours)
            for (DWORD n = 0; n < current.resume_bytes; ++n)
                if (current.resume_value[n] != BYTE(command_[n]))
                    ours = false;
        prior = current.resume_existed == j.resume_existed;
        if (prior && current.resume_existed) {
            prior = current.resume_type == j.resume_type && current.resume_bytes == j.resume_bytes;
            if (prior)
                for (DWORD n = 0; n < current.resume_bytes; ++n)
                    if (current.resume_value[n] != j.resume_value[n])
                        prior = false;
        }
        return true;
    }

  public:
    Win32Store() = default;
    Win32Store(const Win32Store &) = delete;
    ~Win32Store() {
        if (devices_ != INVALID_HANDLE_VALUE)
            SetupDiDestroyDeviceInfoList(devices_);
    }
    bool init(Os os, const char *root, bool generation = false) {
        generation_ = generation;
        os_ = os;
        if (lstrlenA(root) >= MAX_PATH - 80)
            return false;
        lstrcpyA(root_, root);
        DWORD n = GetWindowsDirectoryA(windows_, sizeof(windows_));
        if (!n || n >= sizeof(windows_) - 40)
            return false;
        n = GetSystemDirectoryA(system_, sizeof(system_));
        if (!n || n >= sizeof(system_) - 40 || !path(installer_, "\\setup.exe"))
            return false;
        wsprintfA(command_, "\"%s\" /driver-resume /silent", installer_);
        return refresh();
    }
    // SetupAPI device data becomes stale after DIF_REMOVE. Every continuation
    // opens a fresh set; zero matches is acceptable only for a recorded removal.
    bool refresh(const char *expected = nullptr) {
        if (devices_ != INVALID_HANDLE_VALUE)
            SetupDiDestroyDeviceInfoList(devices_);
        devices_ =
            SetupDiGetClassDevsA(nullptr, nullptr, nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);
        device_present_ = false;
        device_ = {};
        if (devices_ == INVALID_HANDLE_VALUE)
            return false;
        unsigned matches = 0;
        for (DWORD i = 0; i < 4096; ++i) {
            SP_DEVINFO_DATA d{};
            d.cbSize = sizeof(d);
            if (!SetupDiEnumDeviceInfo(devices_, i, &d))
                return GetLastError() == ERROR_NO_MORE_ITEMS && matches <= 1;
            char id[256]{};
            if (CM_Get_Device_IDA(d.DevInst, id, sizeof(id), 0) != CR_SUCCESS)
                return false;
            if (!bounded(id, sizeof(id)))
                return false;
            if (pci(id, sizeof(id))) {
                if (expected && lstrcmpiA(id, expected))
                    return false;
                device_ = d;
                device_present_ = true;
                ++matches;
            }
        }
        return false;
    }
    template <class Payloads> bool capture(Journal &j, const Payloads &payloads) {
        j = {};
        j.os = os_;
        j.version = os_ == Os::win98 ? 3 : 4;
        bool owned = false;
        stage_ = "capture original binding";
        if (!node(j.original))
            return false;
        stage_ = "supported original driver pair";
        const bool unbound = os_ == Os::nt5 && !j.original.inf[0];
        if (!unbound && !current_kind(owned))
            return false;
        stage_ = "capture previous startup value";
        if (!read_resume(j))
            return false;
        j.original_binding = unbound ? Binding::unbound
                             : owned ? Binding::dreamgpu
                                     : Binding::stock;
        j.desired = j.original;
        lstrcpyA(j.desired.provider, "DreamGPU");
        lstrcpyA(j.desired.description, "DreamGPU");
        lstrcpyA(j.desired.section, "Dg");
        const char *dir = os_ == Os::win98 ? "\\drivers\\win98\\" : "\\drivers\\nt5\\";
        char source[MAX_PATH];
        if (!path(source, dir) ||
            !append(j.desired.inf, source, os_ == Os::win98 ? "dg9x.inf" : "dreamgpu.inf"))
            return false;
        const char *inf_payload =
            os_ == Os::win98 ? "drivers/win98/dg9x.inf" : "drivers/nt5/dreamgpu.inf";
        bool inf_found = false;
        for (const auto &p : payloads)
            if (p.os == unsigned(os_) && !lstrcmpA(p.path, inf_payload)) {
                if (!hash_match(j.desired.inf, p.sha))
                    return false;
                lstrcpyA(j.desired.inf_sha, p.sha);
                inf_found = true;
            }
        if (!inf_found)
            return false;
        if (!unbound) {
            stage_ = "select original compatible INF";
            DriverList original(devices_, device_);
            if (!original.select(j.original, !owned))
                return false;
        }
        {
            stage_ = "select packaged compatible INF";
            DriverList desired(devices_, device_);
            if (!desired.select(j.desired))
                return false;
        }
        stage_ = "capture driver destination identities";
        const char *names[2] = {os_ == Os::win98 ? "dgpumini.drv" : "dgpudisp.dll",
                                os_ == Os::win98 ? "dgpumini.vxd" : "dgpumini.sys"};
        for (unsigned n = 0; n < 2; ++n) {
            File &f = j.files[j.count++];
            char suffix[64] = "\\";
            if (os_ == Os::nt5 && n == 1)
                lstrcatA(suffix, "drivers\\");
            lstrcatA(suffix, names[n]);
            if (!append(f.path, system_, suffix) || !image(f.path, f))
                return false;
            f.replaced = 1;
            if ((!owned && f.exists) || (owned && !f.exists))
                return false;
            wsprintfA(f.backup, "\\driver-backup\\%s", names[n]);
            char payload_name[96];
            wsprintfA(payload_name, "drivers/%s/%s", os_ == Os::win98 ? "win98" : "nt5", names[n]);
            bool found = false;
            for (const auto &p : payloads)
                if (p.os == unsigned(os_) && !lstrcmpA(p.path, payload_name)) {
                    if (!append(source, root_, dir) || !append(source, source, names[n]) ||
                        !hash_match(source, p.sha))
                        return false;
                    lstrcpyA(f.desired_sha, p.sha);
                    f.desired_size = p.size;
                    found = true;
                }
            if (!found)
                return false;
        }
        if (!owned) {
            stage_ = "capture stock VGA files";
            const char *vga[2] = {os_ == Os::win98 ? "\\vga.drv" : "\\vga.dll",
                                  "\\drivers\\vga.sys"};
            for (unsigned n = 0; n < (os_ == Os::win98 ? 1u : 2u); ++n) {
                File &f = j.files[j.count++];
                if (!append(f.path, system_, vga[n]) || !image(f.path, f) || !f.exists)
                    return false;
            }
        }
        char self[MAX_PATH];
        DWORD n = GetModuleFileNameA(nullptr, self, sizeof(self));
        File executable;
        if (!n || n >= sizeof(self) || !image(self, executable) || !executable.exists)
            return false;
        lstrcpyA(j.installer_sha, executable.sha);
        const char *audit =
            os_ == Os::win98 ? "tools/common/DG9AUDIT.EXE" : "tools/nt5/DGAUDIT.EXE";
        for (const auto &p : payloads)
            if (p.os == unsigned(os_) && !lstrcmpA(p.path, audit))
                lstrcpyA(j.audit_sha, p.sha);
        if (!hash(j.installer_sha) || !hash(j.audit_sha))
            return false;
        const char *helper = os_ == Os::win98 ? "tools/common/DG9INST.EXE" : "tools/nt5/DGDRV.EXE";
        for (const auto &p : payloads)
            if (p.os == unsigned(os_) && !lstrcmpA(p.path, helper))
                lstrcpyA(j.helper_sha, p.sha);
        if (!hash(j.helper_sha))
            return false;
        // Reserve only absent private capture paths before recording ownership.
        char backup[MAX_PATH], bind[MAX_PATH];
        if (!path(backup, "\\driver-backup") || !path(bind, dir) ||
            !append(bind, bind, "DGDBIND.EXE") || !absent(backup) || !absent(installer_) ||
            !absent(bind))
            return false;
        stage_ = "persist initial driver ownership";
        return persist(j) && complete_capture(j);
    }
    bool complete_capture(Journal &j) {
        stage_ = "complete private driver capture";
        if (tracks_package(j) && (!package_helpers(j) || !package_.capture_before(&j.original)))
            return false;
        if (j.original_binding == Binding::unbound) {
            stage_ = "unbound baseline ownership";
            if (!unbound_helpers(j) || !unbound_.capture_before() || !service_.capture_before())
                return false;
        }

        if (j.phase != Phase::capturing || !valid(j))
            return false;
        char backup[MAX_PATH], source[MAX_PATH], self[MAX_PATH];
        DWORD n = GetModuleFileNameA(nullptr, self, sizeof(self));
        if (!n || n >= sizeof(self) || !hash_match(self, j.installer_sha) ||
            (j.original_binding != Binding::unbound &&
             !hash_match(j.original.inf, j.original.inf_sha)) ||
            !path(backup, "\\driver-backup"))
            return false;
        DWORD a = GetFileAttributesA(backup);
        if (a == INVALID_FILE_ATTRIBUTES) {
            if (GetLastError() != ERROR_FILE_NOT_FOUND || !CreateDirectoryA(backup, nullptr))
                return false;
        } else if (!(a & FILE_ATTRIBUTE_DIRECTORY) || (a & FILE_ATTRIBUTE_REPARSE_POINT))
            return false;
        if (!ensure_copy(self, installer_, j.installer_sha))
            return false;
        for (unsigned k = 0; k < j.count; ++k) {
            const auto &f = j.files[k];
            if (f.replaced && f.exists &&
                (!path(backup, f.backup) || !ensure_copy(f.path, backup, f.sha)))
                return false;
        }
        if (j.original_binding != Binding::unbound) {
            if (!path(backup, (j.original_binding == Binding::dreamgpu)
                                  ? (os_ == Os::win98 ? "\\driver-backup\\dg9x.inf"
                                                      : "\\driver-backup\\dreamgpu.inf")
                                  : "\\driver-backup\\original.inf") ||
                !ensure_copy(j.original.inf, backup, j.original.inf_sha))
                return false;
        }
        if (!path(source,
                  os_ == Os::win98 ? "\\tools\\common\\DG9INST.EXE" : "\\tools\\nt5\\DGDRV.EXE"))
            return false;
        char target[MAX_PATH];
        if (!path(target, os_ == Os::win98 ? "\\drivers\\win98\\DGDBIND.EXE"
                                           : "\\drivers\\nt5\\DGDBIND.EXE") ||
            !ensure_copy(source, target, j.helper_sha))
            return false;
        if (j.original_binding == Binding::dreamgpu &&
            (!path(target, "\\driver-backup\\DGDBIND.EXE") ||
             !ensure_copy(source, target, j.helper_sha)))
            return false;
        j.phase = Phase::captured;
        return persist(j);
    }

    bool first_record(const Journal &j, const char *published) {
        char temporary[MAX_PATH], digest[65];
        if (!path(temporary, "\\driver.tmp"))
            return false;
        Sha256 sha;
        sha.update(reinterpret_cast<const BYTE *>(&j), sizeof(j));
        sha.finish(digest);
        constexpr DWORD total = sizeof(Journal) + 64;
        const auto byte = [&](DWORD offset) -> BYTE {
            return offset < sizeof(j) ? reinterpret_cast<const BYTE *>(&j)[offset]
                                      : BYTE(digest[offset - sizeof(j)]);
        };
        DWORD attributes = GetFileAttributesA(temporary);
        if (attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
            return false;
        if (attributes == INVALID_FILE_ATTRIBUTES && GetLastError() != ERROR_FILE_NOT_FOUND)
            return false;
        {
            Handle file(
                CreateFileA(temporary, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                            attributes == INVALID_FILE_ATTRIBUTES ? CREATE_NEW : OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL, nullptr));
            if (file.h == INVALID_HANDLE_VALUE)
                return false;
            DWORD high = 0, size = GetFileSize(file.h, &high), offset = 0;
            if (high || size > total)
                return false;
            BYTE buffer[4096];
            while (offset < size) {
                DWORD count = size - offset, got = 0;
                if (count > sizeof(buffer))
                    count = sizeof(buffer);
                if (!ReadFile(file.h, buffer, count, &got, nullptr) || got != count)
                    return false;
                for (DWORD n = 0; n < count; ++n)
                    if (buffer[n] != byte(offset + n))
                        return false;
                offset += count;
            }
            while (offset < total) {
                DWORD count = total - offset, wrote = 0;
                if (count > sizeof(buffer))
                    count = sizeof(buffer);
                for (DWORD n = 0; n < count; ++n)
                    buffer[n] = byte(offset + n);
                if (!WriteFile(file.h, buffer, count, &wrote, nullptr) || wrote != count)
                    return false;
                offset += count;
            }
            if (!FlushFileBuffers(file.h))
                return false;
        }
        return absent(published) && MoveFileA(temporary, published);
    }
    bool persist(const Journal &j) {
        if (!valid(j))
            return false;
        char name[MAX_PATH];
        if (!path(name, "\\driver.bin"))
            return false;
        if (absent(name))
            return j.phase == Phase::capturing && first_record(j, name);
        Handle file(
            CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, 0, nullptr));
        if (file.h == INVALID_HANDLE_VALUE)
            return false;
        constexpr DWORD record = sizeof(Journal) + 64;
        DWORD size = GetFileSize(file.h, nullptr);
        if (size == INVALID_FILE_SIZE || size % record || size > 16 * 1024 * 1024 - record ||
            SetFilePointer(file.h, 0, nullptr, FILE_END) == INVALID_SET_FILE_POINTER)
            return false;
        char sha[65];
        Sha256 hash;
        hash.update(reinterpret_cast<const BYTE *>(&j), sizeof(j));
        hash.finish(sha);
        DWORD wrote;
        return WriteFile(file.h, &j, sizeof(j), &wrote, nullptr) && wrote == sizeof(j) &&
               WriteFile(file.h, sha, 64, &wrote, nullptr) && wrote == 64 &&
               FlushFileBuffers(file.h);
    }
    bool load(Journal &out, bool read_only = false) {
        char name[MAX_PATH];
        if (!path(name, "\\driver.bin"))
            return false;
        Handle file(CreateFileA(name, GENERIC_READ | (read_only ? 0 : GENERIC_WRITE), 0, nullptr,
                                OPEN_EXISTING, 0, nullptr));
        if (file.h == INVALID_HANDLE_VALUE)
            return false;
        DWORD size = GetFileSize(file.h, nullptr), good = 0;
        if (size == INVALID_FILE_SIZE || size > 16 * 1024 * 1024)
            return false;
        static Journal candidate;
        while (size - good >= sizeof(Journal) + 64) {
            DWORD got;
            char expected[65]{}, actual[65];
            if (!ReadFile(file.h, &candidate, sizeof(candidate), &got, nullptr) ||
                got != sizeof(candidate) || !ReadFile(file.h, expected, 64, &got, nullptr) ||
                got != 64)
                return false;
            Sha256 hash;
            hash.update(reinterpret_cast<const BYTE *>(&candidate), sizeof(candidate));
            hash.finish(actual);
            if (lstrcmpA(actual, expected) || !valid(candidate) || candidate.os != os_)
                return false;
            out = candidate;
            good += sizeof(candidate) + 64;
        }
        if (!good)
            return false;
        if (!read_only && good != size &&
            (SetFilePointer(file.h, good, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
             !SetEndOfFile(file.h) || !FlushFileBuffers(file.h)))
            return false;
        if (out.phase != Phase::capturing && !hash_match(installer_, out.installer_sha))
            return false;
        // persist() opens this journal exclusively. Release the read handle
        // before clearing an exited child; otherwise Windows rejects the append.
        if (!CloseHandle(file.h))
            return false;
        file.h = INVALID_HANDLE_VALUE;
        if (out.child_pid) {
            const ChildState state = child_state(out);
            if (state == ChildState::error)
                return false;
            if (state == ChildState::gone) {
                out.child_pid = 0;
                if (!read_only && !persist(out))
                    return false;
            }
            // A recognized live child leaves identity inspectable. All physical
            // verification and driver mutations remain gated on child_pid == 0.
        }
        return true;
    }
    void diagnostic(Result result) {
        DWORD error = GetLastError();
        char filename[MAX_PATH];
        if (!path(filename, "\\driver-status.log"))
            return;
        Handle log(CreateFileA(filename, GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, 0,
                               nullptr));
        if (log.h == INVALID_HANDLE_VALUE ||
            SetFilePointer(log.h, 0, nullptr, FILE_END) == INVALID_SET_FILE_POINTER)
            return;
        DWORD wrote;
        WriteFile(log.h, stage_, lstrlenA(stage_), &wrote, nullptr);
        char text[96];
        wsprintfA(text, " result=%u win32_error=%lu\r\n", unsigned(result), error);
        WriteFile(log.h, text, lstrlenA(text), &wrote, nullptr);
        FlushFileBuffers(log.h);
    }
    bool exists() {
        char p[MAX_PATH];
        return !path(p, "\\driver.bin") || !absent(p);
    }
    Actual observe(const Journal &j) {
        if (j.child_pid)
            return Actual::pending;
        if (j.original_binding == Binding::unbound)
            return observe_unbound(j);
        Node current;
        if (!refresh(j.original.device) || !node(current))
            return Actual::error;
        const bool restoring = j.phase == Phase::restoring || j.phase == Phase::restore_pending ||
                               j.phase == Phase::restored;
        bool before_node = same_node(current, j.original),
             after_node = same_node(current, j.desired);
        bool before = before_node, after = after_node, recognized = true;
        for (unsigned n = 0; n < j.count; ++n)
            if (j.files[n].replaced) {
                const auto &f = j.files[n];
                File found;
                if (!image(f.path, found))
                    return Actual::error;
                bool old_file =
                    found.exists == f.exists &&
                    (!found.exists || (found.size == f.size && !lstrcmpA(found.sha, f.sha)));
                bool new_file = found.exists && found.size == f.desired_size &&
                                !lstrcmpA(found.sha, f.desired_sha);
                recognized = recognized && (old_file || new_file);
                before = before &&
                         (old_file || (j.original_binding != Binding::dreamgpu && new_file &&
                                       j.phase != Phase::captured && j.phase != Phase::capturing));
                after = after && new_file;
            }
        if (!recognized)
            return Actual::other;
        if (restoring && before)
            return Actual::original;
        if (after && before_node && restoring &&
            (j.phase == Phase::restoring || j.phase == Phase::restore_pending))
            return Actual::pending;
        if (after) {
            if (tracks_package(j) &&
                (!package_helpers(j) || !package_.capture_published(devices_, device_, j.desired)))
                return Actual::error;
            return Actual::desired;
        }
        if (before && after_node && !restoring &&
            (j.phase == Phase::binding || j.phase == Phase::pending_reboot))
            // An upgrade may retain the exact same INF/node while replacing an
            // in-use display binary. Once binding has begun, matching original
            // files are ambiguous until reboot, not permission to replay setup.
            return Actual::pending;
        if (before)
            return Actual::original;
        // SetupAPI may queue replacement of an in-use driver for reboot. Only
        // exact journal before/after file identities are accepted while pending.
        if ((restoring && before_node) || (!restoring && after_node))
            return Actual::pending;
        return Actual::other;
    }
    Actual observe_unbound(const Journal &j) {
        if (!unbound_helpers(j) || !refresh(j.original.device))
            return Actual::error;
        const bool restoring = j.phase == Phase::restoring || j.phase == Phase::restore_pending ||
                               j.phase == Phase::restored;
        bool files_original = true, files_desired = true;
        for (unsigned n = 0; n < j.count; ++n)
            if (j.files[n].replaced) {
                const auto &f = j.files[n];
                File actual;
                if (!image(f.path, actual))
                    return Actual::error;
                bool old =
                    actual.exists == f.exists &&
                    (!actual.exists || (actual.size == f.size && !lstrcmpA(actual.sha, f.sha)));
                bool desired = actual.exists && actual.size == f.desired_size &&
                               !lstrcmpA(actual.sha, f.desired_sha);
                if (!old && !desired)
                    return Actual::other;
                files_original &= old;
                files_desired &= desired;
            }
        if (restoring && unbound_.removal_pending()) {
            if (!unbound_.reconcile_removed()) {
                stage_ = unbound_.stage();
                return Actual::error;
            }
            if (!service_.remove(unbound_.removal_started_proven()) &&
                !service_.original_verified()) {
                stage_ = service_.stage();
                return Actual::error;
            }
            // XP can keep volatile device metadata after DIF_REMOVE has removed
            // its owned OEM files. A marked kernel service still awaits unload;
            // do not dereference that stale INF while reporting the pending boot.
            if (!service_.original_verified() || !device_present_)
                return Actual::pending;
            Node current;
            if (!node(current))
                return Actual::error;
            if (!same_node(current, j.original))
                return Actual::pending;
            return unbound_.original_verified() && service_.original_verified() ? Actual::original
                                                                                : Actual::pending;
        }
        if (!device_present_)
            return Actual::other;
        Node current;
        if (!node(current))
            return Actual::error;
        if (same_node(current, j.desired)) {
            stage_ = "capture actual published OEM and service";
            if (!unbound_.capture_published(devices_, device_, j.desired)) {
                stage_ = unbound_.stage();
                return Actual::error;
            }
            if (!service_.capture_published()) {
                stage_ = service_.stage();
                return Actual::error;
            }
            return files_desired ? Actual::desired : Actual::pending;
        }
        if (!same_node(current, j.original))
            return Actual::other;
        if (!unbound_.baseline_unchanged() || !service_.baseline_unchanged())
            return Actual::other;
        return files_original ? Actual::original : Actual::pending;
    }
    bool unchanged_originals(const Journal &j) {
        if (j.original_binding != Binding::unbound &&
            !hash_match(j.original.inf, j.original.inf_sha))
            return false;
        for (unsigned n = 0; n < j.count; ++n) {
            const auto &f = j.files[n];
            char backup[MAX_PATH];
            if (!f.replaced && !match(f.path, f))
                return false;
            if (f.replaced && f.exists && (!path(backup, f.backup) || !match(backup, f)))
                return false;
        }
        return true;
    }
    // Read-only admission for a corrected reverse executor. Recovery never
    // adopts changed originals, cached helpers, INF bytes or payload images.
    bool recovery_inputs(const Journal &j) {
        if (!unchanged_originals(j) || !hash_match(j.desired.inf, j.desired.inf_sha))
            return false;
        char source[MAX_PATH];
        if (!path(source, os_ == Os::win98 ? "\\tools\\common\\DG9AUDIT.EXE"
                                           : "\\tools\\nt5\\DGAUDIT.EXE") ||
            !hash_match(source, j.audit_sha))
            return false;
        if (j.original_binding == Binding::dreamgpu &&
            (!path(source, os_ == Os::win98 ? "\\driver-backup\\dg9x.inf"
                                            : "\\driver-backup\\dreamgpu.inf") ||
             !hash_match(source, j.original.inf_sha) ||
             !path(source, "\\driver-backup\\DGDBIND.EXE") || !hash_match(source, j.helper_sha)))
            return false;
        for (unsigned n = 0; n < j.count; ++n) {
            if (!j.files[n].replaced)
                continue;
            lstrcpyA(source, j.desired.inf);
            char *leaf = nullptr;
            const char *name = nullptr;
            for (char *p = source; *p; ++p)
                if (*p == '\\')
                    leaf = p;
            for (const char *p = j.files[n].path; *p; ++p)
                if (*p == '\\')
                    name = p;
            if (!leaf || !name || unsigned(leaf - source) + lstrlenA(name) >= MAX_PATH)
                return false;
            lstrcpyA(leaf, name);
            if (!hash_match(source, j.files[n].desired_sha))
                return false;
        }
        return true;
    }
    void managed_startup(bool enabled) {
        managed_startup_ = enabled;
    }
    bool continuation(const char *executable, const char *sha) {
        if (!bounded(executable, MAX_PATH) || !hash(sha) || !hash_match(executable, sha))
            return false;
        lstrcpyA(resume_installer_, executable);
        lstrcpyA(resume_sha_, sha);
        wsprintfA(command_, "\"%s\" /driver-resume /silent", executable);
        return true;
    }
    bool arm_resume(const Journal &j, bool) {
        // GLOBAL has checked its durable exact G/R registration before invoking
        // this managed component. It owns reboot continuation, not an older D exe.
        if (managed_startup_)
            return true;
        if (j.version < 3)
            return false; // Historical RunOnce is retirement-only in new code.
        if (!hash_match(resume_installer_[0] ? resume_installer_ : installer_,
                        resume_installer_[0] ? resume_sha_ : j.installer_sha))
            return false;
        bool ours, prior, missing;
        if (!resume_value(j, ours, prior, missing) || (!ours && !prior && !missing))
            return false;
        // A persistent Run entry must not be rewritten from itself.
        if (ours) {
            Key key;
            return !RegOpenKeyExA(HKEY_LOCAL_MACHINE, run_key(j), 0, KEY_SET_VALUE, &key.h) &&
                   !RegFlushKey(key.h);
        }
        Key key;
        DWORD disposition;
        if (RegCreateKeyExA(HKEY_LOCAL_MACHINE, run_key(j), 0, nullptr, 0, KEY_SET_VALUE, nullptr,
                            &key.h, &disposition))
            return false;
        return !RegSetValueExA(key.h, RunName, 0, REG_SZ, reinterpret_cast<const BYTE *>(command_),
                               lstrlenA(command_) + 1) &&
               !RegFlushKey(key.h);
    }
    bool disarm_resume(const Journal &j) {
        bool ours, prior, missing;
        if (!resume_value(j, ours, prior, missing) || (!ours && !prior && !missing))
            return false;
        if (prior)
            return true;
        Key key;
        if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, run_key(j), 0, KEY_SET_VALUE, &key.h))
            return false;
        LONG error = j.resume_existed ? RegSetValueExA(key.h, RunName, 0, j.resume_type,
                                                       j.resume_value, j.resume_bytes)
                                      : RegDeleteValueA(key.h, RunName);
        return (error == ERROR_SUCCESS || (!j.resume_existed && error == ERROR_FILE_NOT_FOUND)) &&
               !RegFlushKey(key.h);
    }
    bool install(Journal &j) {
        stage_ = "fixed DreamGPU binding helper";
        char exe[MAX_PATH];
        return hash_match(j.desired.inf, j.desired.inf_sha) &&
               path(exe, os_ == Os::win98 ? "\\drivers\\win98\\DGDBIND.EXE"
                                          : "\\drivers\\nt5\\DGDBIND.EXE") &&
               hash_match(exe, j.helper_sha) && run(exe, j, true);
    }
    bool restore(Journal &j) {
        stage_ = "restore previous compatible driver";
        if (!unchanged_originals(j))
            return false;
        if (j.original_binding == Binding::unbound) {
            stage_ = "remove owned first-install binding";
            if (!unbound_helpers(j) || !refresh(j.original.device))
                return false;
            bool removed = unbound_.removal_pending()
                               ? unbound_.reconcile_removed()
                               : (device_present_ && unbound_.remove(devices_, device_));
            if (!removed)
                return false;
            if (!service_.remove(unbound_.removal_started_proven()) &&
                !service_.original_verified())
                return false;
            return refresh(j.original.device);
        }
        if (j.original_binding == Binding::dreamgpu) {
            char exe[MAX_PATH], inf[MAX_PATH];
            return path(inf, os_ == Os::win98 ? "\\driver-backup\\dg9x.inf"
                                              : "\\driver-backup\\dreamgpu.inf") &&
                   hash_match(inf, j.original.inf_sha) &&
                   path(exe, "\\driver-backup\\DGDBIND.EXE") && hash_match(exe, j.helper_sha) &&
                   run(exe, j, true);
        }
        DriverList original(devices_, device_);
        return original.select(j.original, true) && original.bind();
    }
    bool verify_readonly(const Journal &j, bool original) {
        if (j.child_pid || j.phase != (original ? Phase::restored : Phase::verified) ||
            !refresh(j.original.device) || !device_present_)
            return false;
        Node current;
        if (!node(current) || !same_node(current, original ? j.original : j.desired))
            return false;
        for (unsigned n = 0; n < j.count; ++n) {
            const auto &f = j.files[n];
            File actual;
            if (!image(f.path, actual))
                return false;
            const bool desired = !original && f.replaced;
            if (actual.exists != (desired ? 1u : f.exists) ||
                (actual.exists && (actual.size != (desired ? f.desired_size : f.size) ||
                                   lstrcmpA(actual.sha, desired ? f.desired_sha : f.sha))))
                return false;
        }
        if (original && tracks_package(j) &&
            (!package_helpers(j) || !package_.package_restored(j.original, true)))
            return false;
        if (original && j.original_binding == Binding::unbound)
            return unbound_helpers(j) && unbound_.original_verified(true) &&
                   service_.original_verified(true);
        ULONG status = 0, problem = 0;
        return CM_Get_DevNode_Status(&status, &problem, device_.DevInst, 0) == CR_SUCCESS &&
               !problem && (status & DN_STARTED);
    }
    bool verify(Journal &j, bool original) {
        if (j.child_pid)
            return false;
        stage_ = "started device and protocol verification";
        if (original && tracks_package(j) &&
            (!package_helpers(j) || !package_.cleanup_package(j.original)))
            return false;
        if (original && j.original_binding == Binding::unbound) {
            // A genuinely unbound original need not have DN_STARTED. Exact absent
            // binding/service plus immutable original VGA files are the contract.
            if (observe_unbound(j) != Actual::original || !unchanged_originals(j))
                return false;
            for (unsigned n = 0; n < j.count; ++n)
                if (j.files[n].replaced) {
                    const auto &f = j.files[n];
                    if (!absent(f.path) &&
                        (!hash_match(f.path, f.desired_sha) || !DeleteFileA(f.path)))
                        return false;
                }
            return true;
        }
        ULONG status = 0, problem = 0;
        if (CM_Get_DevNode_Status(&status, &problem, device_.DevInst, 0) != CR_SUCCESS || problem ||
            !(status & DN_STARTED))
            return false;
        if (original && j.original_binding != Binding::dreamgpu) {
            if (observe(j) != Actual::original)
                return false;
            for (unsigned n = 0; n < j.count; ++n)
                if (j.files[n].replaced) {
                    const auto &f = j.files[n];
                    if (!absent(f.path) &&
                        (!hash_match(f.path, f.desired_sha) || !DeleteFileA(f.path)))
                        return false;
                }
            return true;
        }
        char exe[MAX_PATH];
        // Exact desired/original file hashes and started devnode are checked by
        // the engine. This accepted helper validates the real driver protocol;
        // its ABI does not expose a loaded-image build hash, so this is not a
        // claim that GetFileVersion verified the resident binary revision.
        return path(exe, os_ == Os::win98 ? "\\tools\\common\\DG9AUDIT.EXE"
                                          : "\\tools\\nt5\\DGAUDIT.EXE") &&
               hash_match(exe, j.audit_sha) && run(exe, j);
    }
};
} // namespace setup::driver
