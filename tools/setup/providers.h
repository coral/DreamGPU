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
// Diagnostic ICD table is not a conforming production provider. NT5 Direct3D
// needs its own HAL/integration milestone; never replace protected API DLLs.
constexpr Providers providers(Os) {
    return {false, false, true, false};
}
constexpr const char *shared_runtime[] = {"dgpugl.dll", "glide2x.dll", "wined3d.dll",
                                          "winedd.dll", "wined8.dll",  "wined9.dll"};
constexpr const char *nt_icd_key =
    "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\OpenGLDrivers\\DGPUICD";
constexpr const char *win98_icd_key = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\OpenGLDrivers";
} // namespace setup
