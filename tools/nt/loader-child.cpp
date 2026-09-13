// SPDX-License-Identifier: GPL-2.0-or-later
// An ordinary implicit import; no custom loader code in this process.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
extern "C" __declspec(dllimport) DWORD DreamGpuLoaderMarker();
extern "C" void WINAPI WinMainCRTStartup() {
    ExitProcess(DreamGpuLoaderMarker() == 0x44504755 ? 0 : 1);
}
