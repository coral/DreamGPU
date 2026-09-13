/* SPDX-License-Identifier: GPL-2.0-or-later
 * NT5 pixel-format DDIs for the frontend's RGBA8/D24S8 window format.
 */
extern "C" {
#include "framebuf.h"
}
#include "icd-pixel-format.h"

extern "C" LONG APIENTRY DrvDescribePixelFormat(DHPDEV handle, LONG index, ULONG bytes,
                                                PIXELFORMATDESCRIPTOR *output) {
    auto *dev = (PPDEV)handle;
    if (!dev || dev->BitsPerPixel != 32)
        return 0;
    return DgIcdDescribePixelFormat(index, bytes, output);
}
extern "C" BOOL APIENTRY DrvSetPixelFormat(SURFOBJ *surface, LONG index, HWND window) {
    if (!surface || !surface->dhpdev || !window || index != 1)
        return FALSE;
    auto *dev = (PPDEV)surface->dhpdev;
    /* GDI enforces the immutable pixel format on its window/DC. WNDOBJ is
     * created later by our context's WNDOBJ_SETUP escape, never here: that
     * escape supplies the engine's required window lock. */
    return surface->hsurf == dev->hSurfEng && dev->BitsPerPixel == 32;
}
