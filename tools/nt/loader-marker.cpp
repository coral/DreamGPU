// SPDX-License-Identifier: GPL-2.0-or-later
// Marker for an isolated NT loader experiment, not a graphics runtime.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
extern "C" __declspec(dllexport) DWORD DreamGpuLoaderMarker() {
#ifdef DG_LOADER_ORIGINAL
    return 0x4e415449;
#else
    return 0x44504755;
#endif
}
extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) {
    return TRUE;
}
