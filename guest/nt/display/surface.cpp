/*
 * Source: vendor/reactos/win32ss/drivers/displays/framebuf/surface.c
 *         @ 22fb3bb2c1d8196cf501edbb49b3814739f7b016
 * Upstream: https://github.com/reactos/reactos
 * Copied with modifications; translated from C to C++23.
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

extern "C" {
#include "framebuf.h"
#include "dg-ioctl.h"

/*
 * DrvEnableSurface
 *
 * Create engine bitmap around frame buffer and set the video mode requested
 * when PDEV was initialized.
 *
 * Status
 *    @implemented
 */

HSURF APIENTRY DrvEnableSurface(IN DHPDEV dhpdev) {
    PPDEV ppdev = (PPDEV)dhpdev;
    HSURF hSurface;
    ULONG BitmapType;
    SIZEL ScreenSize;
    VIDEO_MEMORY VideoMemory;
    VIDEO_MEMORY_INFORMATION VideoMemoryInfo;
    ULONG ulTemp;

    // Both primaries use the same native RGBA compositor, with explicit
    // RGB565 conversion at the 16-bit CPU framebuffer boundary.
    if ((ppdev->BitsPerPixel != 16 && ppdev->BitsPerPixel != 32) || !ppdev->ScreenWidth ||
        !ppdev->ScreenHeight || ppdev->ScreenDelta <= 0 ||
        (ULONGLONG)ppdev->ScreenWidth * (ppdev->BitsPerPixel / 8) > (ULONG)ppdev->ScreenDelta)
        return NULL;

    /*
     * Set video mode of our adapter.
     */

    if (EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_SET_CURRENT_MODE, &(ppdev->ModeIndex),
                           sizeof(ULONG), NULL, 0, &ulTemp)) {
        return NULL;
    }

    /*
     * Map the framebuffer into our memory.
     */

    VideoMemory.RequestedVirtualAddress = NULL;
    if (EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_MAP_VIDEO_MEMORY, &VideoMemory,
                           sizeof(VIDEO_MEMORY), &VideoMemoryInfo, sizeof(VIDEO_MEMORY_INFORMATION),
                           &ulTemp)) {
        return NULL;
    }

    ppdev->ScreenPtr = VideoMemoryInfo.FrameBufferBase;
    if (!ppdev->ScreenPtr || (ULONGLONG)(ULONG)ppdev->ScreenDelta * ppdev->ScreenHeight >
                                 VideoMemoryInfo.FrameBufferLength) {
        VideoMemory.RequestedVirtualAddress = VideoMemoryInfo.VideoRamBase;
        EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_UNMAP_VIDEO_MEMORY, &VideoMemory,
                           sizeof(VideoMemory), NULL, 0, &ulTemp);
        ppdev->ScreenPtr = NULL;
        return NULL;
    }
    ppdev->NativeCaps = 0;
    memset(&ppdev->Kernel, 0, sizeof(ppdev->Kernel));
    {
        ULONG version = DG_KERNEL_VERSION;
        if (EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_DG_KERNEL, &version, sizeof(version),
                               &ppdev->Kernel, sizeof(ppdev->Kernel), &ulTemp) ||
            ppdev->Kernel.Version != DG_KERNEL_VERSION ||
            ppdev->Kernel.Size != sizeof(ppdev->Kernel) || !ppdev->Kernel.Submit ||
            !ppdev->Kernel.Context || !ppdev->Kernel.Owner || !ppdev->Kernel.Present ||
            !ppdev->Kernel.Cohere || !ppdev->Kernel.DesktopActive || !ppdev->Kernel.Cursor)
            return NULL;
    }
    EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_DG_CAPS, NULL, 0, &ppdev->NativeCaps,
                       sizeof(ppdev->NativeCaps), &ulTemp);

    switch (ppdev->BitsPerPixel) {
        case 8:
            IntSetPalette(dhpdev, ppdev->PaletteEntries, 0, 256);
            BitmapType = BMF_8BPP;
            break;

        case 16:
            BitmapType = BMF_16BPP;
            break;

        case 24:
            BitmapType = BMF_24BPP;
            break;

        case 32:
            BitmapType = BMF_32BPP;
            break;

        default:
            return NULL;
    }

    ppdev->iDitherFormat = BitmapType;

    ScreenSize.cx = ppdev->ScreenWidth;
    ScreenSize.cy = ppdev->ScreenHeight;

    hSurface = (HSURF)EngCreateBitmap(ScreenSize, ppdev->ScreenDelta, BitmapType,
                                      (ppdev->ScreenDelta > 0) ? BMF_TOPDOWN : 0, ppdev->ScreenPtr);
    if (hSurface == NULL) {
        return NULL;
    }

    /*
     * Associate the surface with our device.
     */

    if (!EngAssociateSurface(hSurface, ppdev->hDevEng,
                             HOOK_BITBLT | HOOK_COPYBITS | HOOK_SYNCHRONIZE)) {
        EngDeleteSurface(hSurface);
        return NULL;
    }

    ppdev->hSurfEng = hSurface;

    return hSurface;
}

/*
 * DrvDisableSurface
 *
 * Used by GDI to notify a driver that the surface created by DrvEnableSurface
 * for the current device is no longer needed.
 *
 * Status
 *    @implemented
 */

VOID APIENTRY DrvDisableSurface(IN DHPDEV dhpdev) {
    DWORD ulTemp;
    VIDEO_MEMORY VideoMemory;
    PPDEV ppdev = (PPDEV)dhpdev;

    DgCoherent(ppdev, DG_COHERE_TEARDOWN);
    DgDeleteWindows(ppdev);
    DgDisablePointer(ppdev);
    EngDeleteSurface(ppdev->hSurfEng);
    ppdev->hSurfEng = NULL;

    /*
     * Unmap the framebuffer.
     */

    VideoMemory.RequestedVirtualAddress = ((PPDEV)dhpdev)->ScreenPtr;
    EngDeviceIoControl(((PPDEV)dhpdev)->hDriver, IOCTL_VIDEO_UNMAP_VIDEO_MEMORY, &VideoMemory,
                       sizeof(VIDEO_MEMORY), NULL, 0, &ulTemp);
}

/*
 * DrvAssertMode
 *
 * Sets the mode of the specified physical device to either the mode specified
 * when the PDEV was initialized or to the default mode of the hardware.
 *
 * Status
 *    @implemented
 */

BOOL APIENTRY DrvAssertMode(IN DHPDEV dhpdev, IN BOOL bEnable) {
    PPDEV ppdev = (PPDEV)dhpdev;
    ULONG ulTemp;

    if (!DgCoherent(ppdev, DG_COHERE_MODE))
        return FALSE;
    DgDeleteWindows(ppdev);
    DgDisablePointer(ppdev);
    if (bEnable) {
        /*
         * Reinitialize the device to a clean state.
         */
        if (EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_SET_CURRENT_MODE, &(ppdev->ModeIndex),
                               sizeof(ULONG), NULL, 0, &ulTemp)) {
            /* We failed, bail out */
            return FALSE;
        }
        if (ppdev->BitsPerPixel == 8) {
            IntSetPalette(dhpdev, ppdev->PaletteEntries, 0, 256);
        }

        return TRUE;
    } else {
        /*
         * Call the miniport driver to reset the device to a known state.
         */
        return !EngDeviceIoControl(ppdev->hDriver, IOCTL_VIDEO_RESET_DEVICE, NULL, 0, NULL, 0,
                                   &ulTemp);
    }
}

} /* extern C */
