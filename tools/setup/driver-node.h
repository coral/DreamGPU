// SPDX-License-Identifier: GPL-2.0-or-later
// Extracted from tools/win9x/driver32.cpp (DreamGPU-owned). The V1 SetupAPI
// structure and exact-INF selection work on Win98 and NT5; no class-installer
// mutation occurs until the complete compatible list has one exact match.
#pragma once
#include "driver-lifecycle.h"
namespace setup::driver {
static_assert(sizeof(SP_DRVINFO_DATA_A) == 780, "Win98 driver-info V1 ABI");
class DriverList final {
    HDEVINFO set_;
    SP_DEVINFO_DATA *device_;
    bool built_ = false;
    SP_DEVINSTALL_PARAMS_A saved_{};
    bool params_ = false;
    bool no_copy_ = false;
    char source_[MAX_PATH]{};
    unsigned media_ = 0;
    static bool same_inf(const char *expected, const char *reported) {
        if (!lstrcmpiA(expected, reported))
            return true;
        // Win98 may store an OEM INF's DOS basename while SetupAPI reports
        // its long name. Resolve both existing paths through the filesystem;
        // matching contents alone cannot establish that they are the same INF.
        char left[MAX_PATH]{}, right[MAX_PATH]{};
        DWORD a = GetShortPathNameA(expected, left, sizeof(left));
        DWORD b = GetShortPathNameA(reported, right, sizeof(right));
        return a && a < sizeof(left) && b && b < sizeof(right) && !left[a] && !right[b] &&
               DWORD(lstrlenA(left)) == a && DWORD(lstrlenA(right)) == b &&
               !lstrcmpiA(left, right);
    }
    static UINT CALLBACK queue(void *opaque, UINT notification, UINT_PTR first, UINT_PTR second) {
        auto &self = *static_cast<DriverList *>(opaque);
        switch (notification) {
            case SPFILENOTIFY_NEEDMEDIA: {
                // SetupAPI may retain the old optical source even for an exact local
                // INF. Supply only this package directory, once per fixed file.
                const auto *media = reinterpret_cast<const SOURCE_MEDIA_A *>(first);
                unsigned bit = 0;
                if (media && media->SourceFile && bounded(media->SourceFile, MAX_PATH)) {
                    if (!lstrcmpiA(media->SourceFile, "dgpumini.drv"))
                        bit = 1;
                    if (!lstrcmpiA(media->SourceFile, "dgpumini.vxd"))
                        bit = 2;
                    if (!lstrcmpiA(media->SourceFile, "dgpudisp.dll"))
                        bit = 4;
                    if (!lstrcmpiA(media->SourceFile, "dgpumini.sys"))
                        bit = 8;
                }
                if (!second || !bit || (self.media_ & bit)) {
                    SetLastError(ERROR_FILE_NOT_FOUND);
                    return FILEOP_ABORT;
                }
                self.media_ |= bit;
                lstrcpyA(reinterpret_cast<char *>(second), self.source_);
                return FILEOP_NEWPATH;
            }
            case SPFILENOTIFY_COPYERROR:
            case SPFILENOTIFY_DELETEERROR:
            case SPFILENOTIFY_RENAMEERROR:
                // No interactive source or overwrite dialog, and no unbounded retry.
                SetLastError(ERROR_CANCELLED);
                return FILEOP_ABORT;
            case SPFILENOTIFY_STARTQUEUE:
            case SPFILENOTIFY_STARTSUBQUEUE:
                return TRUE;
            case SPFILENOTIFY_STARTCOPY:
            case SPFILENOTIFY_TARGETEXISTS:
            case SPFILENOTIFY_TARGETNEWER:
            case SPFILENOTIFY_LANGMISMATCH:
                return FILEOP_DOIT;
            case SPFILENOTIFY_STARTDELETE:
            case SPFILENOTIFY_STARTRENAME:
                SetLastError(ERROR_INVALID_DATA);
                return FILEOP_ABORT;
            default:
                return 0; // informational completion/delayed-copy notifications
        }
    }

  public:
    DriverList(HDEVINFO set, SP_DEVINFO_DATA &device) : set_(set), device_(&device) {}
    DriverList(const DriverList &) = delete;
    ~DriverList() {
        if (built_)
            SetupDiDestroyDriverInfoList(set_, device_, SPDIT_COMPATDRIVER);
        if (params_)
            SetupDiSetDeviceInstallParamsA(set_, device_, &saved_);
    }
    bool select(const Node &expected, bool no_copy = false) {
        if (built_ || !bounded(expected.inf, sizeof(expected.inf)))
            return false;
        saved_.cbSize = sizeof(saved_);
        if (!SetupDiGetDeviceInstallParamsA(set_, device_, &saved_))
            return false;
        params_ = true;
        no_copy_ = no_copy;
        lstrcpyA(source_, expected.inf);
        unsigned length = unsigned(lstrlenA(source_));
        while (length && source_[length - 1] != '\\')
            --length;
        if (length < 3)
            return false;
        source_[length == 3 ? length : length - 1] = 0;
        auto p = saved_;
        p.Flags |= DI_ENUMSINGLEINF | DI_QUIETINSTALL;
        if (no_copy)
            p.Flags |= DI_NOFILECOPY;
        if (!no_copy) {
            p.InstallMsgHandler = queue;
            p.InstallMsgHandlerContext = this;
        }
        lstrcpyA(p.DriverPath, expected.inf);
        if (!SetupDiSetDeviceInstallParamsA(set_, device_, &p) ||
            !SetupDiBuildDriverInfoList(set_, device_, SPDIT_COMPATDRIVER))
            return false;
        built_ = true;
        SP_DRVINFO_DATA_A chosen{};
        unsigned matches = 0;
        for (DWORD n = 0; n < 512; ++n) {
            SP_DRVINFO_DATA_A item{};
            item.cbSize = sizeof(item);
            if (!SetupDiEnumDriverInfoA(set_, device_, SPDIT_COMPATDRIVER, n, &item)) {
                if (GetLastError() != ERROR_NO_MORE_ITEMS || matches != 1)
                    return false;
                return SetupDiSetSelectedDriverA(set_, device_, &chosen) != FALSE;
            }
            if (!bounded(item.Description, sizeof(item.Description)) ||
                !bounded(item.ProviderName, sizeof(item.ProviderName)))
                return false;
            if (lstrcmpA(item.Description, expected.description) ||
                lstrcmpA(item.ProviderName, expected.provider))
                continue;
            alignas(SP_DRVINFO_DETAIL_DATA_A) BYTE bytes[4096]{};
            auto *detail = reinterpret_cast<SP_DRVINFO_DETAIL_DATA_A *>(bytes);
            detail->cbSize = sizeof(*detail);
            DWORD required = 0;
            if (!SetupDiGetDriverInfoDetailA(set_, device_, &item, detail, sizeof(bytes),
                                             &required) ||
                required > sizeof(bytes) ||
                !bounded(detail->InfFileName, sizeof(detail->InfFileName)) ||
                !bounded(detail->SectionName, sizeof(detail->SectionName)))
                return false;
            if (!same_inf(expected.inf, detail->InfFileName) ||
                lstrcmpA(detail->SectionName, expected.section))
                continue;
            // SetupAPI created a COMPATDRIVER list for this exact present
            // devnode. Saved VGA nodes may match a class/compatible hardware ID,
            // rather than DreamGPU's PCI prefix, and remain valid restores.
            chosen = item;
            ++matches;
        }
        return false;
    }
    bool bind() {
        if (!built_)
            return false;
        if (no_copy_)
            return SetupDiCallClassInstaller(DIF_INSTALLDEVICE, set_, device_);
        const char *sources[] = {source_};
        // Temporary means process-local: never alter Windows' global source MRU.
        if (!SetupSetSourceListA(SRCLIST_TEMPORARY | SRCLIST_NOBROWSE, sources, 1))
            return false;
        BOOL result = SetupDiCallClassInstaller(DIF_INSTALLDEVICE, set_, device_);
        DWORD error = GetLastError();
        BOOL cleared = SetupCancelTemporarySourceList();
        if (!result)
            SetLastError(error);
        return result && cleared;
    }
};
} // namespace setup::driver
