// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "driver-lifecycle.h"
#include "sha256.h"
namespace setup::driver {
// SetupAPI V1 enumeration follows DreamGPU's driver-node.h. DIF_REMOVE uses
// DI_REMOVEDEVICE_GLOBAL; all-device scans deliberately omit DIGCF_PRESENT so
// phantom consumers also prevent deletion of an OEM package.
// https://learn.microsoft.com/en-us/windows/win32/api/setupapi/ns-setupapi-sp_removedevice_params
// https://learn.microsoft.com/en-us/windows/win32/api/setupapi/nf-setupapi-setupdigetclassdevsa
class UnboundStore {
    static constexpr unsigned max_names = 1024;
    enum class Step : uint32_t { baseline, published, removing, cleanup, removed };
    struct Image {
        uint32_t exists = 0, size = 0;
        char sha[68]{};
    };
    struct Receipt {
        uint32_t magic = 0x55424744, version = 1, count = 0;
        Step step = Step::baseline;
        char device[256]{}, inf[16]{}, pnf[16]{};
        Image inf_image{}, pnf_image{};
        uint32_t pnf_owned = 0, reboot = 0;
        char names[max_names][16]{};
    } receipt_{};
    struct Handle {
        HANDLE h = INVALID_HANDLE_VALUE;
        explicit Handle(HANDLE value) : h(value) {}
        Handle(const Handle &) = delete;
        ~Handle() {
            if (h && h != INVALID_HANDLE_VALUE)
                CloseHandle(h);
        }
    };
    struct Key {
        HKEY h = {};
        ~Key() {
            if (h && h != INVALID_HANDLE_VALUE)
                RegCloseKey(h);
        }
    };
    struct Devices {
        HDEVINFO h = INVALID_HANDLE_VALUE;
        ~Devices() {
            if (h != INVALID_HANDLE_VALUE)
                SetupDiDestroyDeviceInfoList(h);
        }
    };
    char root_[MAX_PATH]{}, windows_[MAX_PATH]{}, system_[MAX_PATH]{}, device_[256]{};
    bool loaded_ = false, package_only_ = false;
    const char *stage_ = "unbound init";
    static bool join(char *out, const char *base, const char *name) {
        if (!bounded(base, MAX_PATH) || !bounded(name, MAX_PATH) ||
            lstrlenA(base) + lstrlenA(name) + 2 > MAX_PATH)
            return false;
        lstrcpyA(out, base);
        lstrcatA(out, "\\");
        lstrcatA(out, name);
        return true;
    }
    bool private_path(char *out, const char *name) const {
        if (package_only_) {
            if (!lstrcmpA(name, "unbound.bin"))
                name = "package.bin";
            if (!lstrcmpA(name, "unbound.tmp"))
                name = "package.tmp";
        }
        return join(out, root_, name);
    }
    bool inf_path(char *out, const char *name) const {
        char directory[MAX_PATH];
        return join(directory, windows_, "INF") && join(out, directory, name);
    }
    static bool name(const char *s) {
        if (!bounded(s, 16) || upper(s[0]) != 'O' || upper(s[1]) != 'E' || upper(s[2]) != 'M')
            return false;
        unsigned n = 3;
        while (s[n] >= '0' && s[n] <= '9')
            ++n;
        return n > 3 && (equal_fold(s + n, ".INF") || equal_fold(s + n, ".PNF"));
    }
    static bool missing(DWORD error) {
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_INVALID_DATA;
    }
    static bool absent(const char *p) {
        return GetFileAttributesA(p) == INVALID_FILE_ATTRIBUTES &&
               GetLastError() == ERROR_FILE_NOT_FOUND;
    }
    static bool image(const char *p, Image &out) {
        out = {};
        DWORD attributes = GetFileAttributesA(p);
        if (attributes == INVALID_FILE_ATTRIBUTES)
            return GetLastError() == ERROR_FILE_NOT_FOUND;
        if (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
            return false;
        Handle file(
            CreateFileA(p, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr));
        if (file.h == INVALID_HANDLE_VALUE)
            return false;
        DWORD high = 0, size = GetFileSize(file.h, &high);
        if (high || !size || size > 4u * 1024u * 1024u)
            return false;
        Sha256 digest;
        BYTE data[4096];
        DWORD total = 0;
        while (total < size) {
            DWORD got = 0, left = size - total;
            if (!ReadFile(file.h, data, left < sizeof(data) ? left : sizeof(data), &got, nullptr) ||
                !got || got > left)
                return false;
            digest.update(data, got);
            total += got;
        }
        out.exists = 1;
        out.size = size;
        digest.finish(out.sha);
        return true;
    }
    static bool matches(const Image &a, const Image &b) {
        return a.exists == b.exists && (!a.exists || (a.size == b.size && !lstrcmpA(a.sha, b.sha)));
    }
    bool valid_receipt(const Receipt &r) const {
        if (r.magic != 0x55424744 || r.version != (package_only_ ? 2u : 1u) ||
            r.count > max_names || uint32_t(r.step) > uint32_t(Step::removed) ||
            !bounded(r.device, sizeof(r.device)) || !equal_fold(r.device, device_) ||
            r.pnf_owned > 1 || r.reboot > 1)
            return false;
        for (unsigned n = 0; n < r.count; ++n) {
            if (!name(r.names[n]))
                return false;
            for (unsigned k = 0; k < n; ++k)
                if (equal_fold(r.names[n], r.names[k]))
                    return false;
        }
        if (r.step != Step::baseline &&
            (!name(r.inf) || !name(r.pnf) || !r.inf_image.exists || !hash(r.inf_image.sha) ||
             (r.pnf_image.exists && !hash(r.pnf_image.sha))))
            return false;
        return true;
    }
    bool write_record(HANDLE file) {
        char digest[65];
        Sha256 h;
        h.update(reinterpret_cast<const BYTE *>(&receipt_), sizeof(receipt_));
        h.finish(digest);
        DWORD wrote = 0;
        return WriteFile(file, &receipt_, sizeof(receipt_), &wrote, nullptr) &&
               wrote == sizeof(receipt_) && WriteFile(file, digest, 64, &wrote, nullptr) &&
               wrote == 64 && FlushFileBuffers(file);
    }
    bool save(bool first = false) {
        loaded_ = false; // Never trust an in-memory transition after a failed
                         // durable write.
        if (!valid_receipt(receipt_))
            return false;
        char path[MAX_PATH];
        if (!private_path(path, first ? "unbound.tmp" : "unbound.bin"))
            return false;
        if (first) {
            // No binding is permitted until this first complete snapshot has been
            // published. An incomplete private temp can therefore be restarted.
            DWORD a = GetFileAttributesA(path);
            if (a != INVALID_FILE_ATTRIBUTES &&
                (a & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
                return false;
            Handle file(CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                    0, nullptr));
            if (file.h == INVALID_HANDLE_VALUE || !write_record(file.h))
                return false;
        } else {
            DWORD attributes = GetFileAttributesA(path);
            if (attributes == INVALID_FILE_ATTRIBUTES ||
                (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
                return false;
            Handle file(CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                    0, nullptr));
            constexpr DWORD record = sizeof(Receipt) + 64;
            if (file.h == INVALID_HANDLE_VALUE)
                return false;
            DWORD high = 0, size = GetFileSize(file.h, &high);
            if (high || size == INVALID_FILE_SIZE || size % record ||
                size > 4u * 1024u * 1024u - record ||
                SetFilePointer(file.h, 0, nullptr, FILE_END) == INVALID_SET_FILE_POINTER ||
                !write_record(file.h))
                return false;
        }
        if (first) {
            char published[MAX_PATH];
            if (!private_path(published, "unbound.bin") || !absent(published) ||
                !MoveFileA(path, published))
                return false;
        }
        loaded_ = true;
        return true;
    }
    bool load(bool read_only = false) {
        char path[MAX_PATH];
        if (!private_path(path, "unbound.bin"))
            return false;
        DWORD attributes = GetFileAttributesA(path);
        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
            return false;
        Handle file(CreateFileA(path, GENERIC_READ | (read_only ? 0 : GENERIC_WRITE), 0, nullptr,
                                OPEN_EXISTING, 0, nullptr));
        if (file.h == INVALID_HANDLE_VALUE)
            return false;
        DWORD high = 0, size = GetFileSize(file.h, &high), good = 0;
        constexpr DWORD record = sizeof(Receipt) + 64;
        if (high || size == INVALID_FILE_SIZE || size > 4u * 1024u * 1024u)
            return false;
        Receipt candidate;
        while (size - good >= record) {
            DWORD got = 0;
            char expected[65]{}, actual[65];
            if (!ReadFile(file.h, &candidate, sizeof(candidate), &got, nullptr) ||
                got != sizeof(candidate) || !ReadFile(file.h, expected, 64, &got, nullptr) ||
                got != 64)
                return false;
            Sha256 h;
            h.update(reinterpret_cast<const BYTE *>(&candidate), sizeof(candidate));
            h.finish(actual);
            if (lstrcmpA(expected, actual) || !valid_receipt(candidate))
                return false;
            receipt_ = candidate;
            good += record;
        }
        if (!good)
            return false;
        if (!read_only && good != size &&
            (SetFilePointer(file.h, good, nullptr, FILE_BEGIN) == INVALID_SET_FILE_POINTER ||
             !SetEndOfFile(file.h) || !FlushFileBuffers(file.h)))
            return false;
        if (!read_only && !FlushFileBuffers(file.h))
            return false;
        loaded_ = !read_only;
        return true;
    }
    bool before_name(const char *n) const {
        for (unsigned i = 0; i < receipt_.count; ++i)
            if (equal_fold(n, receipt_.names[i]))
                return true;
        return false;
    }
    static bool registry_string(HKEY key, const char *n, char *out, DWORD capacity) {
        DWORD type = 0, bytes = capacity;
        return RegQueryValueExA(key, n, nullptr, &type, reinterpret_cast<BYTE *>(out), &bytes) ==
                   ERROR_SUCCESS &&
               type == REG_SZ && bytes > 1 && bytes <= capacity && !out[bytes - 1] &&
               DWORD(lstrlenA(out)) + 1 == bytes;
    }
    static bool driver_property(HDEVINFO set, SP_DEVINFO_DATA &device, DWORD property,
                                bool &present) {
        char value[256]{};
        DWORD type = 0, size = 0;
        present = false;
        if (!SetupDiGetDeviceRegistryPropertyA(set, &device, property, &type,
                                               reinterpret_cast<BYTE *>(value), sizeof(value),
                                               &size))
            return missing(GetLastError());
        if (type != REG_SZ || size < 2 || size > sizeof(value) || value[size - 1] ||
            DWORD(lstrlenA(value)) + 1 != size)
            return false;
        present = true;
        return true;
    }
    bool own_device(SP_DEVINFO_DATA &device) const {
        char id[256]{};
        return CM_Get_Device_IDA(device.DevInst, id, sizeof(id), 0) == CR_SUCCESS &&
               bounded(id, sizeof(id)) && equal_fold(id, device_);
    }
    bool bound_inf(HDEVINFO set, SP_DEVINFO_DATA &device, char *out) {
        Key key;
        key.h = SetupDiOpenDevRegKey(set, &device, DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_READ);
        return key.h != INVALID_HANDLE_VALUE && registry_string(key.h, "InfPath", out, MAX_PATH);
    }
    bool all_references_clear(bool require_unbound, bool allow_absent = false,
                              bool allow_current = false) {
        Devices list;
        list.h = SetupDiGetClassDevsA(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES);
        if (list.h == INVALID_HANDLE_VALUE)
            return false;
        bool found_own = false;
        char full[MAX_PATH]{};
        if (receipt_.inf[0] && !inf_path(full, receipt_.inf))
            return false;
        for (DWORD n = 0; n < 4096; ++n) {
            SP_DEVINFO_DATA device{};
            device.cbSize = sizeof(device);
            if (!SetupDiEnumDeviceInfo(list.h, n, &device))
                return GetLastError() == ERROR_NO_MORE_ITEMS &&
                       (!require_unbound || found_own || allow_absent);
            char id[256]{};
            if (CM_Get_Device_IDA(device.DevInst, id, sizeof(id), 0) != CR_SUCCESS ||
                !bounded(id, sizeof(id)))
                return false;
            bool own = equal_fold(id, device_), driver = false, service = false;
            if (!driver_property(list.h, device, SPDRP_DRIVER, driver))
                return false;
            if (own) {
                found_own = true;
                if (!require_unbound && (!package_only_ || allow_current))
                    continue;
                if (require_unbound &&
                    (driver || !driver_property(list.h, device, SPDRP_SERVICE, service) || service))
                    return false;
                Key key;
                key.h =
                    SetupDiOpenDevRegKey(list.h, &device, DICS_FLAG_GLOBAL, 0, DIREG_DRV, KEY_READ);
                if (require_unbound &&
                    (key.h != INVALID_HANDLE_VALUE ||
                     !(missing(GetLastError()) || GetLastError() == ERROR_KEY_DOES_NOT_EXIST)))
                    return false;
            }
            if ((!own || (package_only_ && !allow_current)) && driver && receipt_.inf[0]) {
                char inf[MAX_PATH]{};
                if (!bound_inf(list.h, device, inf))
                    return false;
                if (equal_fold(inf, receipt_.inf) || equal_fold(inf, full))
                    return false;
                // Noncanonical paths cannot prove that an alias does not name our INF.
                for (unsigned i = 0; inf[i]; ++i)
                    if (inf[i] == '\\' || inf[i] == '/' || inf[i] == ':')
                        return false;
            }
        }
        return false;
    }
    bool previous_package(const Node &previous) {
        Image found;
        return bounded(previous.inf, sizeof(previous.inf)) && hash(previous.inf_sha) &&
               image(previous.inf, found) && found.exists && !lstrcmpA(found.sha, previous.inf_sha);
    }
    bool no_compatible_oem(const Node *allowed = nullptr) {
        Devices list;
        list.h = SetupDiGetClassDevsA(nullptr, nullptr, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
        if (list.h == INVALID_HANDLE_VALUE)
            return false;
        for (DWORD n = 0; n < 4096; ++n) {
            SP_DEVINFO_DATA device{};
            device.cbSize = sizeof(device);
            if (!SetupDiEnumDeviceInfo(list.h, n, &device))
                return false;
            if (!own_device(device))
                continue;
            if (!SetupDiBuildDriverInfoList(list.h, &device, SPDIT_COMPATDRIVER))
                return false;
            bool good = false;
            for (DWORD i = 0; i < 512; ++i) {
                SP_DRVINFO_DATA_A driver{};
                driver.cbSize = sizeof(driver);
                if (!SetupDiEnumDriverInfoA(list.h, &device, SPDIT_COMPATDRIVER, i, &driver)) {
                    good = GetLastError() == ERROR_NO_MORE_ITEMS;
                    break;
                }
                alignas(SP_DRVINFO_DETAIL_DATA_A) BYTE bytes[4096]{};
                auto *detail = reinterpret_cast<SP_DRVINFO_DETAIL_DATA_A *>(bytes);
                detail->cbSize = sizeof(*detail);
                DWORD required = 0;
                if (!SetupDiGetDriverInfoDetailA(list.h, &device, &driver, detail, sizeof(bytes),
                                                 &required) ||
                    required > sizeof(bytes) ||
                    !bounded(detail->InfFileName, sizeof(detail->InfFileName)))
                    break;
                const char *base = detail->InfFileName;
                for (const char *p = base; *p; ++p)
                    if (*p == '\\' || *p == '/')
                        base = p + 1;
                if (name(base)) {
                    Image actual;
                    const char *permitted = allowed ? allowed->inf : "";
                    for (const char *p = permitted; *p; ++p)
                        if (*p == '\\' || *p == '/')
                            permitted = p + 1;
                    if (!allowed || !equal_fold(base, permitted) ||
                        !image(detail->InfFileName, actual) || !actual.exists ||
                        lstrcmpA(actual.sha, allowed->inf_sha))
                        break;
                }
            }
            BOOL released = SetupDiDestroyDriverInfoList(list.h, &device, SPDIT_COMPATDRIVER);
            return good && released;
        }
        return false;
    }
    bool package_absent() {
        char path[MAX_PATH];
        if (package_only_ && before_name(receipt_.inf)) {
            Image actual;
            // A PNF is Windows' compiled cache, not an installed driver image.
            // Rebinding an unchanged borrowed INF can regenerate that cache.
            // Neither its bytes nor its presence belong to this transaction.
            return inf_path(path, receipt_.inf) && image(path, actual) &&
                   matches(actual, receipt_.inf_image);
        }
        if (!inf_path(path, receipt_.inf) || !absent(path))
            return false;
        return inf_path(path, receipt_.pnf) && absent(path);
    }
    bool remove_owned_cache() {
        char inf[MAX_PATH], pnf[MAX_PATH];
        if (before_name(receipt_.inf) || before_name(receipt_.pnf) ||
            !inf_path(inf, receipt_.inf) || !inf_path(pnf, receipt_.pnf))
            return false;
        // Keep the exact owned INF open against replacement while deleting its
        // derived cache. SetupAPI may have regenerated the PNF since capture.
        Handle anchor(CreateFileA(inf, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL, nullptr));
        if (anchor.h == INVALID_HANDLE_VALUE && !missing(GetLastError()))
            return false;
        Image source, cache;
        if (!image(inf, source) ||
            (source.exists &&
             (anchor.h == INVALID_HANDLE_VALUE || !matches(source, receipt_.inf_image))) ||
            !image(pnf, cache))
            return false;
        if (!cache.exists)
            return true;
        // After an interrupted deletion the INF may already be absent. Only
        // the exact captured cache can then be attributed without its anchor.
        if ((!source.exists && !matches(cache, receipt_.pnf_image)) || !all_references_clear(false))
            return false;
        return DeleteFileA(pnf) != FALSE;
    }
    bool cleanup() {
        if (receipt_.step == Step::removed)
            return package_absent() &&
                   ((package_only_ && before_name(receipt_.inf)) || all_references_clear(false));
        if (package_only_ && before_name(receipt_.inf)) {
            if (!package_absent())
                return false;
            receipt_.step = Step::removed;
            return save();
        }
        if (!all_references_clear(false) || !remove_owned_cache())
            return false;
        char path[MAX_PATH];
        Image actual;
        if (!inf_path(path, receipt_.inf) || !image(path, actual))
            return false;
        if (actual.exists) {
            if (!matches(actual, receipt_.inf_image) || !all_references_clear(false) ||
                !DeleteFileA(path))
                return false;
        }
        if (!absent(path))
            return false;
        receipt_.step = Step::removed;
        return save();
    }

  public:
    UnboundStore() = default;
    UnboundStore(const UnboundStore &) = delete;
    bool init(const char *root, const char *windows, const char *system, const char *device,
              bool package_only = false) {
        if (!bounded(root, MAX_PATH - 32) || !bounded(windows, MAX_PATH - 32) ||
            !bounded(system, MAX_PATH) || !bounded(device, 256) || !pci(device, 256))
            return false;
        package_only_ = package_only;
        lstrcpyA(root_, root);
        lstrcpyA(windows_, windows);
        lstrcpyA(system_, system);
        lstrcpyA(device_, device);
        return true;
    }
    const char *stage() const {
        return stage_;
    }
    bool capture_before(const Node *previous = nullptr) {
        stage_ = "unbound baseline capture";
        char path[MAX_PATH];
        if (!private_path(path, "unbound.bin"))
            return false;
        if (!absent(path))
            return load();
        // Bound upgrades restore an explicit captured node. Other compatible
        // packages remain borrowed; they cannot prevent that exact restoration.
        // An unbound baseline still requires no eligible OEM package, otherwise
        // reenumeration could silently bind it after our package is removed.
        if (package_only_ ? (!previous || !previous_package(*previous)) : !no_compatible_oem())
            return false;
        receipt_ = {};
        receipt_.version = package_only_ ? 2 : 1;
        lstrcpyA(receipt_.device, device_);
        char pattern[MAX_PATH];
        if (!inf_path(pattern, "OEM*.*"))
            return false;
        WIN32_FIND_DATAA entry{};
        HANDLE find = FindFirstFileA(pattern, &entry);
        if (find == INVALID_HANDLE_VALUE) {
            if (GetLastError() != ERROR_FILE_NOT_FOUND)
                return false;
            return save(true);
        }
        bool good = true;
        for (;;) {
            if (!bounded(entry.cFileName, sizeof(entry.cFileName))) {
                good = false;
                break;
            }
            if (name(entry.cFileName)) {
                if (receipt_.count == max_names ||
                    (entry.dwFileAttributes &
                     (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
                    good = false;
                    break;
                }
                lstrcpyA(receipt_.names[receipt_.count++], entry.cFileName);
            }
            if (!FindNextFileA(find, &entry)) {
                good = GetLastError() == ERROR_NO_MORE_FILES;
                break;
            }
        }
        BOOL closed = FindClose(find);
        return good && closed && save(true);
    }
    bool capture_published(HDEVINFO set, SP_DEVINFO_DATA &device, const Node &desired) {
        stage_ = "unbound published package capture";
        if ((!loaded_ && !load()) || !own_device(device) || !hash(desired.inf_sha))
            return false;
        if (receipt_.step != Step::baseline)
            return !lstrcmpA(receipt_.inf_image.sha, desired.inf_sha);
        char inf[MAX_PATH]{};
        if (!bound_inf(set, device, inf) || !name(inf) || (!package_only_ && before_name(inf)))
            return false;
        lstrcpyA(receipt_.inf, inf);
        lstrcpyA(receipt_.pnf, inf);
        unsigned n = lstrlenA(inf);
        receipt_.pnf[n - 3] = 'p';
        receipt_.pnf[n - 2] = 'n';
        receipt_.pnf[n - 1] = 'f';
        char path[MAX_PATH];
        if (!inf_path(path, inf) || !image(path, receipt_.inf_image) ||
            !receipt_.inf_image.exists || lstrcmpA(receipt_.inf_image.sha, desired.inf_sha))
            return false;
        if (!inf_path(path, receipt_.pnf) || !image(path, receipt_.pnf_image))
            return false;
        if (before_name(receipt_.pnf) && (!package_only_ || !before_name(receipt_.inf)))
            return false;
        receipt_.pnf_owned = receipt_.pnf_image.exists && !before_name(receipt_.pnf);
        receipt_.step = Step::published;
        return save();
    }
    bool remove(HDEVINFO set, SP_DEVINFO_DATA &device) {
        stage_ = "unbound scoped device removal";
        if (package_only_)
            return false;
        if ((!loaded_ && !load()) || receipt_.step == Step::baseline || !own_device(device) ||
            !all_references_clear(false))
            return false;
        if (receipt_.step >= Step::removing)
            return reconcile_removed(); // Never replay an ambiguously completed
                                        // DIF_REMOVE.
        char current[MAX_PATH], file[MAX_PATH];
        Image found;
        if (!bound_inf(set, device, current) || !equal_fold(current, receipt_.inf) ||
            !inf_path(file, receipt_.inf) || !image(file, found) ||
            !matches(found, receipt_.inf_image))
            return false;
        receipt_.step = Step::removing;
        receipt_.reboot = 1;
        if (!save())
            return false;
        SP_REMOVEDEVICE_PARAMS params{};
        params.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
        params.ClassInstallHeader.InstallFunction = DIF_REMOVE;
        params.Scope = DI_REMOVEDEVICE_GLOBAL;
        if (!SetupDiSetClassInstallParamsA(set, &device, &params.ClassInstallHeader,
                                           sizeof(params)))
            return false;
        BOOL removed = SetupDiCallClassInstaller(DIF_REMOVE, set, &device);
        BOOL cleared = SetupDiSetClassInstallParamsA(set, &device, nullptr, 0);
        if (!removed || !cleared)
            return false;
        receipt_.step = Step::cleanup;
        if (!save())
            return false;
        return cleanup();
    }
    bool reconcile_removed() {
        stage_ = "unbound removal reconciliation";
        if ((!loaded_ && !load()) || receipt_.step < Step::removing)
            return false;
        if (receipt_.step == Step::removing && !all_references_clear(true, true))
            return false;
        return cleanup();
    }
    bool cleanup_package(const Node &previous) {
        if (!package_only_ || (!loaded_ && !load()))
            return false;
        if (receipt_.step == Step::baseline)
            return previous_package(previous);
        return cleanup();
    }
    bool package_restored(const Node &previous, bool read_only = false) {
        if (!package_only_ || (!loaded_ && !load(read_only)))
            return false;
        if (receipt_.step == Step::baseline)
            return previous_package(previous);
        return receipt_.step == Step::removed && package_absent() &&
               (before_name(receipt_.inf) || all_references_clear(false));
    }
    bool publication_captured() {
        return (loaded_ || load()) && receipt_.step >= Step::published;
    }
    bool baseline_unchanged() {
        return (loaded_ || load()) && receipt_.step == Step::baseline && no_compatible_oem();
    }
    bool removal_pending() {
        return (loaded_ || load()) && receipt_.step >= Step::removing;
    }
    bool removal_started_proven() {
        return (loaded_ || load()) && receipt_.step >= Step::cleanup;
    }
    bool original_verified(bool read_only = false) {
        if ((!loaded_ && !load(read_only)) || !all_references_clear(true))
            return false;
        // A published driver journal may be reversed before the first bind.
        // Absence is verified directly; no synthetic remove receipt is written.
        if (receipt_.step == Step::baseline)
            return no_compatible_oem();
        return receipt_.step == Step::removed && package_absent();
    }
};
} // namespace setup::driver
