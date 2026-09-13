/* SPDX-License-Identifier: GPL-2.0-or-later
 * Diagnostic Windows ICD adapter. It shares the frontend's actual contexts,
 * transport and typed operations. Missing mandatory GL operations reject calls
 * visibly; this payload must not be advertised as a conforming OpenGL driver.
 */
#include "internal.h"
#include "icd-trace.h"

extern "C" {
int WINAPI wglDescribePixelFormat(HDC, int, UINT, PIXELFORMATDESCRIPTOR *);
int WINAPI wglGetPixelFormat(HDC);
BOOL WINAPI wglSetPixelFormat(HDC, int, const PIXELFORMATDESCRIPTOR *);
BOOL WINAPI wglSwapBuffers(HDC);
}

static volatile LONG UnsupportedSlot = -1;

#include "icd-functions.inc"
#include "icd-numeric.inc"
#include "icd-state.inc"
#include "icd-raster.inc"
#include "icd-client.inc"
#include "icd-pixels.inc"
#include "icd-images.inc"
#include "icd-textures.inc"
#include "icd-fixed.inc"
#include "icd-evaluator.inc"
#include "icd-selection.inc"
#include "icd-lists.inc"

struct Dispatch final {
#define DG_ICD_SLOT(index, result, name, signature) result(APIENTRY *name) signature;
#include "icd-slots.inc"
#undef DG_ICD_SLOT
};
struct ClientTable final {
    DWORD Count;
    Dispatch Functions;
};
static_assert(sizeof(Dispatch) == 336 * sizeof(PROC));
#ifdef _WIN32
static_assert(sizeof(void *) == 4);
static_assert(offsetof(ClientTable, Functions) == sizeof(DWORD));
#endif
static void APIENTRY glIcdFinish(void) {
    IcdTrace("RuntimeFinish", 0, 0);
    glFinish();
}
static const ClientTable Table = {336,
                                  {
#include "icd-table.inc"
                                  }};
using SetTable = void(APIENTRY *)(const ClientTable *);

extern "C" {
LONG WINAPI DgIcdUnsupportedSlot(void) {
    return InterlockedCompareExchange(&UnsupportedSlot, -1, -1);
}
BOOL WINAPI DrvValidateVersion(DWORD version) {
    IcdTrace("DrvValidateVersion", version, 0);
    if (version != 1) {
        SetLastError(ERROR_REVISION_MISMATCH);
        return FALSE;
    }
    return TRUE;
}
HGLRC WINAPI DrvCreateContext(HDC dc) {
    IcdTrace("DrvCreateContext", wglGetPixelFormat(dc), 0);
    if (wglGetPixelFormat(dc) != 1 &&
        (GetPixelFormat(dc) != 1 || !wglSetPixelFormat(dc, 1, nullptr))) {
        SetLastError(ERROR_INVALID_PIXEL_FORMAT);
        return nullptr;
    }
    return wglCreateContext(dc);
}
HGLRC WINAPI DrvCreateLayerContext(HDC dc, int layer) {
    if (layer != 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return nullptr;
    }
    return DrvCreateContext(dc);
}
BOOL WINAPI DrvDeleteContext(HGLRC context) {
    return wglDeleteContext(context);
}
const ClientTable *WINAPI DrvSetContext(HDC dc, HGLRC context, SetTable callback) {
    if (!context || !wglMakeCurrent(dc, context)) {
        IcdTrace("DrvSetContext-failed", GetLastError(), 0);
        return nullptr;
    }
    IcdTrace("DrvSetContext", 336, 0);
    if (callback)
        callback(&Table);
    return &Table;
}
BOOL WINAPI DrvReleaseContext(HGLRC context) {
    if (!context || wglGetCurrentContext() != context) {
        SetLastError(ERROR_INVALID_HANDLE);
        return FALSE;
    }
    return wglMakeCurrent(nullptr, nullptr);
}
BOOL WINAPI DrvShareLists(HGLRC source, HGLRC destination) {
    return wglShareLists(source, destination);
}
BOOL WINAPI DrvCopyContext(HGLRC, HGLRC, UINT) {
    SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
}
int WINAPI DrvDescribePixelFormat(HDC dc, int index, UINT bytes,
                                  PIXELFORMATDESCRIPTOR *description) {
    IcdTrace("DrvDescribePixelFormat", index, bytes);
    return wglDescribePixelFormat(dc, index, bytes, description);
}
BOOL WINAPI DrvSetPixelFormat(HDC dc, int index) {
    IcdTrace("DrvSetPixelFormat", index, 0);
    if (index != 1) {
        SetLastError(ERROR_INVALID_PIXEL_FORMAT);
        return FALSE;
    }
    /* Windows owns the one-time SetPixelFormat rule. It may ask the ICD to
     * attach the same format again while resolving its driver/context path. */
    return wglGetPixelFormat(dc) == index || wglSetPixelFormat(dc, index, nullptr);
}
BOOL WINAPI DrvSwapBuffers(HDC dc) {
    IcdTrace("DrvSwapBuffers", 0, 0);
    return wglSwapBuffers(dc);
}
BOOL WINAPI DrvSwapLayerBuffers(HDC dc, UINT planes) {
    if (planes != WGL_SWAP_MAIN_PLANE) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    return DrvSwapBuffers(dc);
}
PROC WINAPI DrvGetProcAddress(LPCSTR name) {
    return wglGetProcAddress(name);
}
BOOL WINAPI DrvDescribeLayerPlane(HDC, int, int, UINT, LPLAYERPLANEDESCRIPTOR) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}
int WINAPI DrvGetLayerPaletteEntries(HDC, int, int, int, COLORREF *) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
}
int WINAPI DrvSetLayerPaletteEntries(HDC, int, int, int, const COLORREF *) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return 0;
}
BOOL WINAPI DrvRealizeLayerPalette(HDC, int, BOOL) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
}
}
