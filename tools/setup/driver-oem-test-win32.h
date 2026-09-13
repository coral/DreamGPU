// SPDX-License-Identifier: GPL-2.0-or-later
// Shared file/registry/SetupAPI seams for owned OEM/service tests.
#pragma once
#define DG_DRIVER_OEM_EXTENDED
#define CreateFileA LegacyCreateFileA
#define SetupDiGetClassDevsA LegacyGetClassDevs
#define SetupDiEnumDeviceInfo LegacyEnumDevice
#define CM_Get_Device_IDA LegacyGetDeviceId
#define SetupDiOpenDevRegKey LegacyOpenDriverKey
#define SetupDiGetDeviceRegistryPropertyA LegacyProperty
#define SetupDiEnumDriverInfoA LegacyEnumDriver
#define SetupDiGetDriverInfoDetailA LegacyDetail
#define SetupDiCallClassInstaller LegacyClassInstaller
#include "driver-test-win32.h"
#undef CreateFileA
#undef SetupDiGetClassDevsA
#undef SetupDiEnumDeviceInfo
#undef CM_Get_Device_IDA
#undef SetupDiOpenDevRegKey
#undef SetupDiGetDeviceRegistryPropertyA
#undef SetupDiEnumDriverInfoA
#undef SetupDiGetDriverInfoDetailA
#undef SetupDiCallClassInstaller
constexpr DWORD CREATE_ALWAYS = 5, ERROR_NO_MORE_FILES = 18, DIF_REMOVE = 5,
                DI_REMOVEDEVICE_GLOBAL = 1;
struct SP_CLASSINSTALL_HEADER {
    DWORD cbSize{}, InstallFunction{};
};
struct SP_REMOVEDEVICE_PARAMS {
    SP_CLASSINSTALL_HEADER ClassInstallHeader{};
    DWORD Scope{}, HwProfile{};
};
struct WIN32_FIND_DATAA {
    DWORD dwFileAttributes{};
    char cFileName[260]{};
};
static HANDLE CreateFileA(const char *p, DWORD access, DWORD share, void *security, DWORD mode,
                          DWORD attrs, void *templ) {
    if (mode == CREATE_ALWAYS) {
        if (fake_win32::fault())
            return INVALID_HANDLE_VALUE;
        fake_win32::files[fake_win32::canon(p)] = {};
        mode = OPEN_EXISTING;
    }
    return LegacyCreateFileA(p, access, share, security, mode, attrs, templ);
}
struct Device {
    std::string id, inf;
    bool bound = false, present = true;
};
static std::vector<Device> devices;
static std::vector<std::string> find_names;
static unsigned find_index, removes;
static bool lost_response, pending_remove;
static HDEVINFO SetupDiGetClassDevsA(void *, void *, void *, DWORD flags) {
    assert(flags == DIGCF_ALLCLASSES || flags == (DIGCF_ALLCLASSES | DIGCF_PRESENT));
    return flags & DIGCF_PRESENT ? 3 : 2;
}
static BOOL SetupDiEnumDeviceInfo(HDEVINFO set, DWORD n, SP_DEVINFO_DATA *out) {
    unsigned seen = 0;
    for (unsigned i = 0; i < devices.size(); ++i) {
        if (set == 3 && !devices[i].present)
            continue;
        if (seen++ == n) {
            out->DevInst = i + 1;
            return TRUE;
        }
    }
    fake_win32::error = ERROR_NO_MORE_ITEMS;
    return FALSE;
}
static DWORD CM_Get_Device_IDA(DWORD dev, char *out, DWORD cap, DWORD) {
    if (!dev || dev > devices.size() || devices[dev - 1].id.size() + 1 > cap)
        return 1;
    strcpy(out, devices[dev - 1].id.c_str());
    return CR_SUCCESS;
}
static BOOL SetupDiGetDeviceRegistryPropertyA(HDEVINFO, SP_DEVINFO_DATA *d, DWORD property,
                                              DWORD *type, BYTE *out, DWORD cap, DWORD *size) {
    if (!devices[d->DevInst - 1].bound) {
        fake_win32::error = ERROR_INVALID_DATA;
        return FALSE;
    }
    const char *value = property == SPDRP_SERVICE ? "dgpumini" : "bound";
    unsigned length = unsigned(strlen(value)) + 1;
    assert(cap >= length);
    memcpy(out, value, length);
    *size = length;
    *type = REG_SZ;
    return TRUE;
}
static HKEY SetupDiOpenDevRegKey(HDEVINFO, SP_DEVINFO_DATA *d, DWORD, DWORD, DWORD, DWORD) {
    if (!devices[d->DevInst - 1].bound) {
        fake_win32::error = ERROR_KEY_DOES_NOT_EXIST;
        return INVALID_HANDLE_VALUE;
    }
    auto name = "device" + std::to_string(d->DevInst);
    auto &inf = devices[d->DevInst - 1].inf;
    fake_win32::keys[name]["infpath"] = {REG_SZ, {inf.begin(), inf.end()}};
    fake_win32::keys[name]["infpath"].bytes.push_back(0);
    auto value = [&](const char *key, const char *text) {
        auto *p = reinterpret_cast<const BYTE *>(text);
        fake_win32::keys[name][key] = {REG_SZ, {p, p + strlen(text) + 1}};
    };
    value("infsection", driver_fake::current.section);
    value("providername", driver_fake::current.provider);
    value("driverdesc", driver_fake::current.description);

    auto key = fake_win32::next++;
    fake_win32::key_handles[key] = name;
    return key;
}
static BOOL SetupDiEnumDriverInfoA(HDEVINFO, SP_DEVINFO_DATA *, DWORD, DWORD n,
                                   SP_DRVINFO_DATA_A *out) {
    if (n >= driver_fake::compatible.size()) {
        fake_win32::error = ERROR_NO_MORE_ITEMS;
        return FALSE;
    }
    out->Reserved = n;
    strcpy(out->Description, driver_fake::compatible[n].description);
    strcpy(out->ProviderName, driver_fake::compatible[n].provider);
    return TRUE;
}
static BOOL SetupDiGetDriverInfoDetailA(HDEVINFO, SP_DEVINFO_DATA *, SP_DRVINFO_DATA_A *item,
                                        SP_DRVINFO_DETAIL_DATA_A *out, DWORD, DWORD *size) {
    strcpy(out->InfFileName, driver_fake::compatible[item->Reserved].inf);
    strcpy(out->SectionName, driver_fake::compatible[item->Reserved].section);
    *size = sizeof(*out);
    return TRUE;
}
static BOOL SetupDiSetClassInstallParamsA(HDEVINFO, SP_DEVINFO_DATA *, SP_CLASSINSTALL_HEADER *p,
                                          DWORD size) {
    if (fake_win32::fault())
        return FALSE;
    if (p) {
        assert(size == sizeof(SP_REMOVEDEVICE_PARAMS));
        auto *params = reinterpret_cast<SP_REMOVEDEVICE_PARAMS *>(p);
        assert(params->ClassInstallHeader.InstallFunction == DIF_REMOVE &&
               params->Scope == DI_REMOVEDEVICE_GLOBAL && !params->HwProfile);
    }
    return TRUE;
}
static BOOL SetupDiCallClassInstaller(DWORD dif, HDEVINFO, SP_DEVINFO_DATA *d) {
    assert(dif == DIF_REMOVE);
    if (fake_win32::fault())
        return FALSE;
    ++removes;
    if (!pending_remove)
        devices[d->DevInst - 1].bound = false;
    if (lost_response) {
        lost_response = false;
        return FALSE;
    }
    return TRUE;
}
static bool next_find(WIN32_FIND_DATAA *out) {
    if (find_index == find_names.size()) {
        fake_win32::error = ERROR_NO_MORE_FILES;
        return false;
    }
    strcpy(out->cFileName, find_names[find_index++].c_str());
    out->dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
    return true;
}
static HANDLE FindFirstFileA(const char *pattern, WIN32_FIND_DATAA *out) {
    assert(fake_win32::canon(pattern) == "c:\\windows\\inf\\oem*.*");
    find_names.clear();
    find_index = 0;
    for (const auto &[path, node] : fake_win32::files) {
        (void)node;
        constexpr char prefix[] = "c:\\windows\\inf\\";
        if (path.starts_with(prefix)) {
            auto name = path.substr(sizeof(prefix) - 1);
            if (name.starts_with("oem"))
                find_names.push_back(name);
        }
    }
    if (!next_find(out)) {
        fake_win32::error = ERROR_FILE_NOT_FOUND;
        return INVALID_HANDLE_VALUE;
    }
    return 99;
}
static BOOL FindNextFileA(HANDLE, WIN32_FIND_DATAA *out) {
    return next_find(out);
}
static BOOL FindClose(HANDLE) {
    return TRUE;
}
