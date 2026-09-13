/*
 * Source: vendor/reactos/win32ss/drivers/displays/framebuf/framebuf.h
 *         @ 22fb3bb2c1d8196cf501edbb49b3814739f7b016
 * Upstream: https://github.com/reactos/reactos
 * Copied with modifications for the DreamGPU NT5 driver.
 */

/*
 * ReactOS Generic Framebuffer display driver
 *
 * Copyright (C) 2004 Filip Navara
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef _FRAMEBUF_PCH_
#define _FRAMEBUF_PCH_

#include <stdarg.h>
#include <windef.h>
#include <wingdi.h>
#include <winddi.h>
#include <winioctl.h>
#include <ntddvdeo.h>
#include "dg-kernel.h"
#include "dg-window.h"
#ifdef __cplusplus
/* Fixed-width GDI coordinates; no C++ runtime or repeated macro evaluation. */
static inline LONG min(LONG a, LONG b) noexcept {
    return a < b ? a : b;
}
static inline LONG max(LONG a, LONG b) noexcept {
    return a > b ? a : b;
}
#endif

typedef struct _PDEV {
    HANDLE hDriver;
    HDEV hDevEng;
    HSURF hSurfEng;
    ULONG ModeIndex;
    ULONG ScreenWidth;
    ULONG ScreenHeight;
    ULONG ScreenDelta;
    BYTE BitsPerPixel;
    ULONG RedMask;
    ULONG GreenMask;
    ULONG BlueMask;
    BYTE PaletteShift;
    PVOID ScreenPtr;
    ULONG NativeCaps;
    DG_KERNEL_INTERFACE Kernel;
    HSEMAPHORE WindowLock;
    PVOID Windows[DG_GL_MAX_DRAWABLES];
    ULONG NextBinding;
    HPALETTE DefaultPalette;
    PALETTEENTRY *PaletteEntries;

    BOOL PointerNative, PointerHasShape, PointerSoftware;
    ULONG *PointerPixels;

    /* DirectX Support */
    DWORD iDitherFormat;
    ULONG MemHeight;
    ULONG MemWidth;
    DWORD dwHeap;
    VIDEOMEMORY *pvmList;
    BOOL bDDInitialized;
    DDPIXELFORMAT ddpfDisplay;
} PDEV, *PPDEV;

#define DEVICE_NAME L"framebuf"
#define ALLOC_TAG 'FUBF'

BOOL APIENTRY DrvBitBlt(SURFOBJ *, SURFOBJ *, SURFOBJ *, CLIPOBJ *, XLATEOBJ *, RECTL *, POINTL *,
                        POINTL *, BRUSHOBJ *, POINTL *, ROP4);
BOOL APIENTRY DrvCopyBits(SURFOBJ *, SURFOBJ *, CLIPOBJ *, XLATEOBJ *, RECTL *, POINTL *);
ULONG APIENTRY DrvEscape(SURFOBJ *, ULONG, ULONG, PVOID, ULONG, PVOID);
ULONG APIENTRY DrvDrawEscape(SURFOBJ *, ULONG, CLIPOBJ *, RECTL *, ULONG, PVOID);
VOID APIENTRY DrvSynchronizeSurface(SURFOBJ *, RECTL *, FLONG);
ULONG DgBindWindow(SURFOBJ *, ULONG, PVOID, ULONG, PVOID);
VOID DgDeleteWindows(PPDEV);
VOID DgDisablePointer(PPDEV);
BOOL DgDesktopActive(PPDEV);
BOOL DgCoherent(PPDEV, ULONG);

BOOL APIENTRY DrvEnableDirectDraw(DHPDEV dhpdev, DD_CALLBACKS *pCallbacks,
                                  DD_SURFACECALLBACKS *pSurfaceCallbacks,
                                  DD_PALETTECALLBACKS *pPaletteCallbacks);

VOID APIENTRY DrvDisableDirectDraw(DHPDEV dhpdev);

DHPDEV APIENTRY DrvEnablePDEV(IN DEVMODEW *pdm, IN LPWSTR pwszLogAddress, IN ULONG cPat,
                              OUT HSURF *phsurfPatterns, IN ULONG cjCaps, OUT ULONG *pdevcaps,
                              IN ULONG cjDevInfo, OUT DEVINFO *pdi, IN HDEV hdev,
                              IN LPWSTR pwszDeviceName, IN HANDLE hDriver);

VOID APIENTRY DrvCompletePDEV(IN DHPDEV dhpdev, IN HDEV hdev);

VOID APIENTRY DrvDisablePDEV(IN DHPDEV dhpdev);

HSURF APIENTRY DrvEnableSurface(IN DHPDEV dhpdev);

VOID APIENTRY DrvDisableSurface(IN DHPDEV dhpdev);

BOOL APIENTRY DrvAssertMode(IN DHPDEV dhpdev, IN BOOL bEnable);

ULONG APIENTRY DrvGetModes(IN HANDLE hDriver, IN ULONG cjSize, OUT DEVMODEW *pdm);

BOOL APIENTRY DrvSetPalette(IN DHPDEV dhpdev, IN PALOBJ *ppalo, IN FLONG fl, IN ULONG iStart,
                            IN ULONG cColors);

ULONG APIENTRY DrvSetPointerShape(IN SURFOBJ *pso, IN SURFOBJ *psoMask, IN SURFOBJ *psoColor,
                                  IN XLATEOBJ *pxlo, IN LONG xHot, IN LONG yHot, IN LONG x,
                                  IN LONG y, IN RECTL *prcl, IN FLONG fl);

VOID APIENTRY DrvMovePointer(IN SURFOBJ *pso, IN LONG x, IN LONG y, IN RECTL *prcl);

BOOL IntInitScreenInfo(PPDEV ppdev, LPDEVMODEW pDevMode, PGDIINFO pGdiInfo, PDEVINFO pDevInfo);

BOOL IntInitDefaultPalette(PPDEV ppdev, PDEVINFO pDevInfo);

BOOL APIENTRY IntSetPalette(IN DHPDEV dhpdev, IN PPALETTEENTRY ppalent, IN ULONG iStart,
                            IN ULONG cColors);

#endif /* _FRAMEBUF_PCH_ */
