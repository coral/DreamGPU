// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "driver-lifecycle.h"
#include "sha256.h"
namespace setup::driver {
// NT-only owned service cleanup for an originally unbound adapter. Never stops
// a live miniport. DeleteService may mark it for deletion; only a later absent
// SCM entry AND absent service registry key prove removal.
// https://learn.microsoft.com/en-us/windows/win32/api/winsvc/nf-winsvc-deleteservice
class ServiceStore {
    enum class Step : uint32_t { baseline, published, deleting, deleted };
    struct Config {
        DWORD type = 0, start = 0, error = 0, tag = 0;
        char binary[MAX_PATH]{}, group[64]{}, account[128]{}, display[256]{};
    };
    struct Receipt {
        uint32_t magic = 0x53424744, version = 1;
        Step step = Step::baseline;
        char device[256]{};
        Config config{};
    } receipt_{};
    using OpenManager = SC_HANDLE(WINAPI *)(const char *, const char *, DWORD);
    using Open = SC_HANDLE(WINAPI *)(SC_HANDLE, const char *, DWORD);
    using Query = BOOL(WINAPI *)(SC_HANDLE, QUERY_SERVICE_CONFIGA *, DWORD, DWORD *);
    using Status = BOOL(WINAPI *)(SC_HANDLE, SERVICE_STATUS *);
    using Delete = BOOL(WINAPI *)(SC_HANDLE);
    using Close = BOOL(WINAPI *)(SC_HANDLE);
    OpenManager manager_fn_ = nullptr;
    Open open_ = nullptr;
    Query query_ = nullptr;
    Status status_ = nullptr;
    Delete delete_ = nullptr;
    Close close_ = nullptr;
    SC_HANDLE manager_ = {};
    char root_[MAX_PATH]{}, system_[MAX_PATH]{}, device_[256]{};
    bool loaded_ = false;
    const char *stage_ = "service init";
    static constexpr const char *Name = "dgpumini";
    static constexpr const char *KeyPath = "SYSTEM\\CurrentControlSet\\Services\\dgpumini";
    template <class Function> static bool resolve(Function &out, HMODULE module, const char *name) {
        FARPROC p = GetProcAddress(module, name);
        if (!p)
            return false;
        static_assert(sizeof(p) == sizeof(out));
        auto *d = reinterpret_cast<BYTE *>(&out);
        auto *s = reinterpret_cast<const BYTE *>(&p);
        for (unsigned n = 0; n < sizeof(p); ++n)
            d[n] = s[n];
        return true;
    }
    struct FileHandle {
        HANDLE h;
        explicit FileHandle(HANDLE value) : h(value) {}
        FileHandle(const FileHandle &) = delete;
        ~FileHandle() {
            if (h && h != INVALID_HANDLE_VALUE)
                CloseHandle(h);
        }
    };
    struct ServiceHandle {
        SC_HANDLE h;
        Close close;
        ServiceHandle(SC_HANDLE value, Close fn) : h(value), close(fn) {}
        ServiceHandle(const ServiceHandle &) = delete;
        ~ServiceHandle() {
            if (h)
                close(h);
        }
    };
    struct Devices {
        HDEVINFO h = INVALID_HANDLE_VALUE;
        ~Devices() {
            if (h != INVALID_HANDLE_VALUE)
                SetupDiDestroyDeviceInfoList(h);
        }
    };
    bool path(char *out, const char *name) const {
        if (lstrlenA(root_) + lstrlenA(name) + 2 > MAX_PATH)
            return false;
        lstrcpyA(out, root_);
        lstrcatA(out, "\\");
        lstrcatA(out, name);
        return true;
    }
    static bool absent(const char *p) {
        return GetFileAttributesA(p) == INVALID_FILE_ATTRIBUTES &&
               GetLastError() == ERROR_FILE_NOT_FOUND;
    }
    bool registry_absent() const {
        HKEY key = {};
        LONG status = RegOpenKeyExA(HKEY_LOCAL_MACHINE, KeyPath, 0, KEY_READ, &key);
        if (!status)
            RegCloseKey(key);
        return status == ERROR_FILE_NOT_FOUND;
    }
    bool valid(const Receipt &r) const {
        return r.magic == 0x53424744 && r.version == 1 &&
               uint32_t(r.step) <= uint32_t(Step::deleted) && bounded(r.device, sizeof(r.device)) &&
               equal_fold(r.device, device_) &&
               (r.step == Step::baseline || (r.config.type == SERVICE_KERNEL_DRIVER &&
                                             (r.config.start == SERVICE_SYSTEM_START ||
                                              r.config.start == SERVICE_DEMAND_START) &&
                                             r.config.error == SERVICE_ERROR_IGNORE &&
                                             bounded(r.config.binary, sizeof(r.config.binary)) &&
                                             bounded(r.config.group, sizeof(r.config.group))));
    }
    bool record(HANDLE h) {
        Sha256 digest;
        char text[65];
        digest.update(reinterpret_cast<const BYTE *>(&receipt_), sizeof(receipt_));
        digest.finish(text);
        DWORD n = 0;
        return WriteFile(h, &receipt_, sizeof(receipt_), &n, nullptr) && n == sizeof(receipt_) &&
               WriteFile(h, text, 64, &n, nullptr) && n == 64 && FlushFileBuffers(h);
    }
    bool save(bool first = false) {
        loaded_ = false;
        if (!valid(receipt_))
            return false;
        char name[MAX_PATH];
        if (!path(name, first ? "service.tmp" : "service.bin"))
            return false;
        DWORD attributes = GetFileAttributesA(name);
        if (attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
            return false;
        {
            FileHandle file(CreateFileA(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                        first ? CREATE_ALWAYS : OPEN_EXISTING, 0, nullptr));
            if (file.h == INVALID_HANDLE_VALUE)
                return false;
            if (!first) {
                DWORD high = 0, size = GetFileSize(file.h, &high);
                if (high || size == INVALID_FILE_SIZE || size % (sizeof(Receipt) + 64) ||
                    size > 1024u * 1024u - sizeof(Receipt) - 64 ||
                    SetFilePointer(file.h, 0, nullptr, FILE_END) == INVALID_SET_FILE_POINTER)
                    return false;
            }
            if (!record(file.h))
                return false;
        }
        if (first) {
            char destination[MAX_PATH];
            if (!path(destination, "service.bin") || !absent(destination) ||
                !MoveFileA(name, destination))
                return false;
        }
        loaded_ = true;
        return true;
    }
    bool load(bool read_only = false) {
        char name[MAX_PATH];
        if (!path(name, "service.bin"))
            return false;
        DWORD attributes = GetFileAttributesA(name);
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
            return false;
        FileHandle file(CreateFileA(name, GENERIC_READ | (read_only ? 0 : GENERIC_WRITE), 0,
                                    nullptr, OPEN_EXISTING, 0, nullptr));
        if (file.h == INVALID_HANDLE_VALUE)
            return false;
        DWORD high = 0, size = GetFileSize(file.h, &high), good = 0;
        constexpr DWORD bytes = sizeof(Receipt) + 64;
        if (high || size == INVALID_FILE_SIZE || size > 1024u * 1024u)
            return false;
        Receipt candidate;
        while (size - good >= bytes) {
            DWORD n = 0;
            char expected[65]{}, actual[65];
            if (!ReadFile(file.h, &candidate, sizeof(candidate), &n, nullptr) ||
                n != sizeof(candidate) || !ReadFile(file.h, expected, 64, &n, nullptr) || n != 64)
                return false;
            Sha256 h;
            h.update(reinterpret_cast<const BYTE *>(&candidate), sizeof(candidate));
            h.finish(actual);
            if (lstrcmpA(expected, actual) || !valid(candidate))
                return false;
            receipt_ = candidate;
            good += bytes;
        }
        if (!good)
            return false;
        if (!read_only && good != size &&
            (SetFilePointer(file.h, good, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
             !SetEndOfFile(file.h)))
            return false;
        if (!read_only && !FlushFileBuffers(file.h))
            return false;
        loaded_ = !read_only;
        return true;
    }
    template <size_t N>
    static bool take(char (&out)[N], const char *input, const BYTE *buffer, size_t size,
                     bool optional = false) {
        if (!input)
            return optional;
        uintptr_t begin = reinterpret_cast<uintptr_t>(buffer),
                  p = reinterpret_cast<uintptr_t>(input);
        if (p < begin || p - begin >= size)
            return false;
        size_t available = size - (p - begin);
        for (size_t n = 0; n < N && n < available; ++n) {
            out[n] = input[n];
            if (!input[n])
                return optional || n > 0;
        }
        return false;
    }
    bool config(SC_HANDLE service, Config &out, bool deleting = false) {
        alignas(QUERY_SERVICE_CONFIGA) BYTE buffer[4096]{};
        auto *value = reinterpret_cast<QUERY_SERVICE_CONFIGA *>(buffer);
        DWORD required = 0;
        out = {};
        if (!query_(service, value, sizeof(buffer), &required) || required > sizeof(buffer))
            return false;
        out.type = value->dwServiceType;
        out.start = value->dwStartType;
        out.error = value->dwErrorControl;
        out.tag = value->dwTagId;
        char dependencies[2]{};
        if (!take(out.binary, value->lpBinaryPathName, buffer, sizeof(buffer)) ||
            !take(out.group, value->lpLoadOrderGroup, buffer, sizeof(buffer)) ||
            !take(out.account, value->lpServiceStartName, buffer, sizeof(buffer), true) ||
            !take(out.display, value->lpDisplayName, buffer, sizeof(buffer), true) ||
            !take(dependencies, value->lpDependencies, buffer, sizeof(buffer), true) ||
            dependencies[0])
            return false;
        char absolute[MAX_PATH];
        if (lstrlenA(system_) + 22 >= MAX_PATH)
            return false;
        lstrcpyA(absolute, system_);
        lstrcatA(absolute, "\\drivers\\dgpumini.sys");
        // XP's PnP installer publishes demand start and a SystemRoot-relative
        // image path even when this INF requests system start. Both start modes
        // are valid for a PnP driver; retain the exact observed config afterward.
        // https://learn.microsoft.com/en-us/windows-hardware/drivers/install/specifying-driver-load-order
        return out.type == SERVICE_KERNEL_DRIVER &&
               (out.start == SERVICE_SYSTEM_START || out.start == SERVICE_DEMAND_START ||
                (deleting && out.start == SERVICE_DISABLED)) &&
               out.error == SERVICE_ERROR_IGNORE && equal_fold(out.group, "Video") &&
               (equal_fold(out.binary, "\\SystemRoot\\System32\\drivers\\dgpumini.sys") ||
                equal_fold(out.binary, "system32\\drivers\\dgpumini.sys") ||
                equal_fold(out.binary, absolute));
    }
    static bool same_config(const Config &a, const Config &b) {
        auto *x = reinterpret_cast<const BYTE *>(&a);
        auto *y = reinterpret_cast<const BYTE *>(&b);
        for (unsigned n = 0; n < sizeof(a); ++n)
            if (x[n] != y[n])
                return false;
        return true;
    }
    bool no_other_users() {
        Devices set;
        set.h = SetupDiGetClassDevsA(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES);
        if (set.h == INVALID_HANDLE_VALUE)
            return false;
        for (DWORD n = 0; n < 4096; ++n) {
            SP_DEVINFO_DATA device{};
            device.cbSize = sizeof(device);
            if (!SetupDiEnumDeviceInfo(set.h, n, &device))
                return GetLastError() == ERROR_NO_MORE_ITEMS;
            char id[256]{};
            if (CM_Get_Device_IDA(device.DevInst, id, sizeof(id), 0) != CR_SUCCESS ||
                !bounded(id, sizeof(id)))
                return false;
            if (equal_fold(id, device_))
                continue;
            char service[256]{};
            DWORD type = 0, bytes = 0;
            if (!SetupDiGetDeviceRegistryPropertyA(set.h, &device, SPDRP_SERVICE, &type,
                                                   reinterpret_cast<BYTE *>(service),
                                                   sizeof(service), &bytes)) {
                DWORD error = GetLastError();
                if (error == ERROR_INVALID_DATA || error == ERROR_FILE_NOT_FOUND)
                    continue;
                return false;
            }
            if (type != REG_SZ || bytes < 2 || bytes > sizeof(service) || service[bytes - 1] ||
                DWORD(lstrlenA(service)) + 1 != bytes || equal_fold(service, Name))
                return false;
        }
        return false;
    }

  public:
    ServiceStore() = default;
    ServiceStore(const ServiceStore &) = delete;
    ~ServiceStore() {
        if (manager_)
            close_(manager_);
    }
    bool init(const char *root, const char *system, const char *device) {
        if (manager_ || !bounded(root, MAX_PATH - 32) || !bounded(system, MAX_PATH - 32) ||
            !bounded(device, 256) || !pci(device, 256))
            return false;
        HMODULE module = GetModuleHandleA("advapi32.dll");
        if (!module || !resolve(manager_fn_, module, "OpenSCManagerA") ||
            !resolve(open_, module, "OpenServiceA") ||
            !resolve(query_, module, "QueryServiceConfigA") ||
            !resolve(status_, module, "QueryServiceStatus") ||
            !resolve(delete_, module, "DeleteService") ||
            !resolve(close_, module, "CloseServiceHandle"))
            return false;
        manager_ = manager_fn_(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (!manager_)
            return false;
        lstrcpyA(root_, root);
        lstrcpyA(system_, system);
        lstrcpyA(device_, device);
        return true;
    }
    const char *stage() const {
        return stage_;
    }
    bool capture_before() {
        stage_ = "unbound service baseline";
        char file[MAX_PATH];
        if (!path(file, "service.bin"))
            return false;
        if (!absent(file))
            return load();
        ServiceHandle service(open_(manager_, Name, SERVICE_QUERY_CONFIG), close_);
        if (service.h || GetLastError() != ERROR_SERVICE_DOES_NOT_EXIST || !registry_absent())
            return false;
        receipt_ = {};
        lstrcpyA(receipt_.device, device_);
        return save(true);
    }
    bool capture_published() {
        stage_ = "unbound service publication";
        if ((!loaded_ && !load()) || receipt_.step == Step::deleted)
            return false;
        ServiceHandle service(open_(manager_, Name, SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS),
                              close_);
        if (!service.h)
            return false;
        Config current;
        if (!config(service.h, current))
            return false;
        if (receipt_.step != Step::baseline)
            return same_config(current, receipt_.config);
        receipt_.config = current;
        receipt_.step = Step::published;
        return save();
    }
    bool remove(bool owned_device_removal_proven) {
        stage_ = "unbound owned service deletion";
        if (!owned_device_removal_proven || (!loaded_ && !load()) ||
            receipt_.step == Step::baseline || !no_other_users())
            return false;
        ServiceHandle service(
            open_(manager_, Name, SERVICE_QUERY_CONFIG | SERVICE_QUERY_STATUS | DELETE), close_);
        if (!service.h) {
            DWORD error = GetLastError();
            if (error == ERROR_SERVICE_MARKED_FOR_DELETE)
                return receipt_.step == Step::deleting;
            if (error != ERROR_SERVICE_DOES_NOT_EXIST || !registry_absent())
                return false;
            // The scoped device installer may already have removed this exact
            // newly created service. The caller proved owned device removal,
            // and absence from both SCM and the registry needs no deletion.
            if (receipt_.step == Step::deleted)
                return true;
            receipt_.step = Step::deleted;
            return save();
        }
        if (receipt_.step == Step::deleted)
            return false;
        Config current;
        const bool deleting = receipt_.step == Step::deleting;
        if (!config(service.h, current, deleting))
            return false;
        // XP exposes a marked-for-deletion kernel service as disabled while
        // the loaded image keeps its SCM object alive. Only our persisted
        // deletion phase permits this one-field transition; all other captured
        // configuration remains exact, and completion still requires absence.
        if (deleting && current.start == SERVICE_DISABLED)
            current.start = receipt_.config.start;
        if (!same_config(current, receipt_.config))
            return false;
        SERVICE_STATUS status{};
        if (!status_(service.h, &status) || status.dwServiceType != SERVICE_KERNEL_DRIVER)
            return false;
        if (status.dwCurrentState != SERVICE_STOPPED && status.dwCurrentState != SERVICE_RUNNING)
            return false;
        // Save intent before deleting. A repeated exact DeleteService is safe only
        // if a prior attempt demonstrably left this same unmarked service object.
        receipt_.step = Step::deleting;
        if (!save())
            return false;
        if (!no_other_users())
            return false;
        if (!delete_(service.h) && GetLastError() != ERROR_SERVICE_MARKED_FOR_DELETE)
            return false;
        return true; // Caller must cold-reconcile absence; running service is never stopped here.
    }
    bool publication_captured() {
        return (loaded_ || load()) && receipt_.step >= Step::published;
    }
    bool baseline_unchanged() {
        if ((!loaded_ && !load()) || receipt_.step != Step::baseline)
            return false;
        ServiceHandle service(open_(manager_, Name, SERVICE_QUERY_CONFIG), close_);
        return !service.h && GetLastError() == ERROR_SERVICE_DOES_NOT_EXIST && registry_absent();
    }
    bool removal_pending() {
        return (loaded_ || load()) && receipt_.step == Step::deleting;
    }
    bool original_verified(bool read_only = false) {
        if ((!loaded_ && !load(read_only)) || receipt_.step == Step::published || !no_other_users())
            return false;
        ServiceHandle service(open_(manager_, Name, SERVICE_QUERY_CONFIG), close_);
        if (service.h || GetLastError() != ERROR_SERVICE_DOES_NOT_EXIST || !registry_absent())
            return false;
        if (receipt_.step == Step::deleted || receipt_.step == Step::baseline)
            return true; // Exact never-created absence also satisfies rollback.
        if (read_only)
            return false;
        receipt_.step = Step::deleted;
        return save();
    }
};
} // namespace setup::driver
