// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "policy.h"
namespace setup {
struct Providers {
    bool display, opengl_icd, glide2, direct3d_system;
    constexpr bool ready() const {
        return display && opengl_icd && glide2 && direct3d_system;
    }
};
// Completed system providers may enter the journalled installer. Readiness
// authorizes mechanics, not success: activation still requires cold driver
// identity and all six normal-loader pixel proofs.
constexpr Providers providers(Os os) {
#if defined(DG_SETUP_ADAPTER_TEST) && defined(DG_SETUP_UNREADY_PROVIDER_TEST)
    (void)os;
    return {false, false, true, false};
#else
    const bool supported = os == Os::win98 || os == Os::nt5;
    return {supported, supported, supported, supported};
#endif
}
constexpr const char *shared_runtime[] = {"dgpugl.dll", "glide2x.dll", "wined3d.dll",
                                          "winedd.dll", "wined8.dll",  "wined9.dll"};
// Exact system destinations; public DirectX replacement is owned by the
// boot transaction, and aliases derive from captured OS files, never payloads.
constexpr const char *public_runtime[] = {"ddraw.dll", "d3d8.dll", "d3d9.dll"};
// Win98 installer self-repair can copy these existing SYSBCKUP images back
// over the public DLL. Cache files are part of the same owned transaction.
constexpr const char *win98_runtime_cache[] = {"SYSBCKUP\\ddraw.dll", "SYSBCKUP\\d3d8.dll",
                                               "SYSBCKUP\\d3d9.dll"};
constexpr const char *native_aliases[] = {"ddsys.dll", "msd3d8.dll", "msd3d9.dll"};
constexpr const char *system_probes[] = {"DGSYSGL.EXE", "DGSYSGR.EXE", "DGSYS6.EXE",
                                         "DGSYS7.EXE",  "DGSYS8.EXE",  "DGSYS9.EXE"};
constexpr const char *nt_icd_key =
    "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\OpenGLDrivers\\DGPUICD";
constexpr const char *win98_icd_key = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\OpenGLDrivers";
} // namespace setup
