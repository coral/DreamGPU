/* SPDX-License-Identifier: GPL-2.0-or-later
 * Public Glide2x GPU oracle. Fixed geometry and readback, no menu/input loop. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "sdk2_glide.h"
static HANDLE Log;
static void Record(const char *s) {
    DWORD n = 0, w;
    while (s[n])
        n++;
    WriteFile(Log, s, n, &w, NULL);
    WriteFile(Log, "\r\n", 2, &w, NULL);
    FlushFileBuffers(Log);
}
static LRESULT CALLBACK WindowProc(HWND w, UINT m, WPARAM a, LPARAM b) {
    return DefWindowProcA(w, m, a, b);
}
#define API(name, bytes)                                                                           \
    __typeof__(&name) p##name = (__typeof__(&name))GetProcAddress(dll, "_" #name "@" #bytes);      \
    if (!p##name) {                                                                                \
        Record("FAIL missing " #name);                                                             \
        ExitProcess(1);                                                                            \
    }
void WINAPI WinMainCRTStartup(void) {
    WNDCLASSA cls = {0};
    HWND window;
    HMODULE dll;
    GrVertex a = {0}, b = {0}, c = {0};
    unsigned short pixels[256], texture[64];
    GrTexInfo info = {0};
    unsigned i;
    BOOL passed = TRUE;
    Log = CreateFileA("C:\\DGGLIDE.LOG", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0,
                      NULL);
    if (Log == INVALID_HANDLE_VALUE)
        ExitProcess(1);
    Record("START automated glide");
    dll = LoadLibraryA("C:\\SIERRA\\Half-Life\\glide2x.dll");
    if (!dll) {
        Record("FAIL load glide2x.dll");
        ExitProcess(1);
    }
    API(grGlideInit, 0);
    API(grGlideShutdown, 0);
    API(grSstSelect, 4);
    API(grSstWinOpen, 28);
    API(grSstWinClose, 0);
    API(grBufferClear, 12);
    API(grBufferSwap, 4);
    API(grDrawTriangle, 12);
    API(grColorCombine, 20);
    API(grCullMode, 4);
    API(grLfbReadRegion, 28);
    API(grAlphaCombine, 20);
    API(grAlphaBlendFunction, 16);
    API(grTexDownloadMipMap, 16);
    API(grTexSource, 16);
    API(grTexCombine, 28);
    API(grTexFilterMode, 12);
    cls.lpfnWndProc = WindowProc;
    cls.hInstance = GetModuleHandleA(NULL);
    cls.lpszClassName = "DGGlideProbe";
    RegisterClassA(&cls);
    window = CreateWindowA(cls.lpszClassName, "DreamGPU Glide GPU probe",
                           WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40, 656, 519, NULL, NULL,
                           cls.hInstance, NULL);
    if (!window) {
        Record("FAIL create window");
        ExitProcess(1);
    }
    pgrGlideInit();
    Record("INIT");
    pgrSstSelect(0);
    if (!pgrSstWinOpen((FxU)window, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB,
                       GR_ORIGIN_UPPER_LEFT, 2, 1)) {
        Record("FAIL Glide window");
        pgrGlideShutdown();
        ExitProcess(1);
    }
    Record("CONTEXT");
    pgrCullMode(GR_CULL_DISABLE);
    pgrColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED,
                    GR_COMBINE_OTHER_NONE, FXFALSE);
    pgrBufferClear(0x000000ff, 255, 0);
    a.x = 32;
    a.y = 32;
    a.r = 255;
    a.a = 255;
    a.oow = 1;
    b = a;
    b.x = 320;
    c = a;
    c.y = 320;
    pgrDrawTriangle(&a, &b, &c);
    if (!pgrLfbReadRegion(GR_BUFFER_BACKBUFFER, 64, 64, 16, 16, 32, pixels)) {
        Record("FAIL triangle read");
        passed = FALSE;
    } else
        for (i = 0; i < 256; i++)
            if (pixels[i] != 0xf800) {
                Record("FAIL triangle pixels");
                passed = FALSE;
                break;
            }
    if (!pgrLfbReadRegion(GR_BUFFER_BACKBUFFER, 480, 380, 16, 16, 32, pixels)) {
        Record("FAIL clear read");
        passed = FALSE;
    } else
        for (i = 0; i < 256; i++)
            if (pixels[i] != 0x001f) {
                Record("FAIL clear pixels");
                passed = FALSE;
                break;
            }
    Record("TEXTURE RGB565");
    for (i = 0; i < 64; i++)
        texture[i] = 0x07e0;
    info.smallLod = GR_LOD_8;
    info.largeLod = GR_LOD_8;
    info.aspectRatio = GR_ASPECT_1x1;
    info.format = GR_TEXFMT_RGB_565;
    info.data = texture;
    pgrTexDownloadMipMap(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &info);
    pgrTexSource(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &info);
    pgrTexFilterMode(GR_TMU0, GR_TEXTUREFILTER_POINT_SAMPLED, GR_TEXTUREFILTER_POINT_SAMPLED);
    pgrTexCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE,
                  GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE, FXFALSE);
    pgrColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_NONE,
                    GR_COMBINE_OTHER_TEXTURE, FXFALSE);
    a.tmuvtx[0].oow = b.tmuvtx[0].oow = c.tmuvtx[0].oow = 1;
    a.tmuvtx[0].sow = 0;
    a.tmuvtx[0].tow = 0;
    b.tmuvtx[0].sow = 255;
    b.tmuvtx[0].tow = 0;
    c.tmuvtx[0].sow = 0;
    c.tmuvtx[0].tow = 255;
    pgrDrawTriangle(&a, &b, &c);
    if (!pgrLfbReadRegion(GR_BUFFER_BACKBUFFER, 64, 64, 16, 16, 32, pixels)) {
        Record("FAIL texture read");
        passed = FALSE;
    } else
        for (i = 0; i < 256; i++)
            if (pixels[i] != 0x07e0) {
                Record("FAIL RGB565 texture pixels");
                passed = FALSE;
                break;
            }
    /* Reusing the same emulated texture address must replace every texel,
     * even when the translator retains the private GPU storage allocation. */
    for (i = 0; i < 64; i++)
        texture[i] = 0x001f;
    pgrTexDownloadMipMap(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &info);
    pgrTexSource(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &info);
    pgrTexFilterMode(GR_TMU0, GR_TEXTUREFILTER_BILINEAR, GR_TEXTUREFILTER_BILINEAR);
    pgrDrawTriangle(&a, &b, &c);
    if (!pgrLfbReadRegion(GR_BUFFER_BACKBUFFER, 64, 64, 16, 16, 32, pixels)) {
        Record("FAIL reused texture read");
        passed = FALSE;
    } else
        for (i = 0; i < 256; i++)
            if (pixels[i] != 0x001f) {
                Record("FAIL reused texture pixels");
                passed = FALSE;
                break;
            }
    Record("TEXTURE SAME ADDRESS REPLACED");
    /* Glide combines texture + local color BEFORE blending. The donor's
     * no-secondary fallback adds local color in a separate ONE/ONE pass,
     * incorrectly brightening even a fully transparent source. These exact
     * endpoint cases distinguish the contracts without scene interpretation. */
    Record("COMBINER BEFORE BLEND");
    pgrColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL, GR_COMBINE_FACTOR_ONE,
                    GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
    pgrAlphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED,
                    GR_COMBINE_OTHER_NONE, FXFALSE);
    a.r = b.r = c.r = 255;
    a.g = b.g = c.g = 0;
    a.b = b.b = c.b = 0;
    a.a = b.a = c.a = 255;
    pgrAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
    pgrDrawTriangle(&a, &b, &c);
    if (!pgrLfbReadRegion(GR_BUFFER_BACKBUFFER, 64, 64, 16, 16, 32, pixels)) {
        Record("FAIL combiner read");
        passed = FALSE;
    } else
        for (i = 0; i < 256; i++)
            if (pixels[i] != 0xf81f) {
                Record("FAIL texture plus local combiner pixels");
                passed = FALSE;
                break;
            }
    /* (blue texture + red local) * 0 + green destination * 1 = green.
     * The previous second pass would incorrectly turn this yellow. */
    pgrBufferClear(0x0000ff00, 255, 0);
    a.a = b.a = c.a = 0;
    pgrAlphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA, GR_BLEND_ONE,
                          GR_BLEND_ZERO);
    pgrDrawTriangle(&a, &b, &c);
    if (!pgrLfbReadRegion(GR_BUFFER_BACKBUFFER, 64, 64, 16, 16, 32, pixels)) {
        Record("FAIL transparent combiner read");
        passed = FALSE;
    } else
        for (i = 0; i < 256; i++)
            if (pixels[i] != 0x07e0) {
                Record("FAIL transparent combiner changed destination");
                passed = FALSE;
                break;
            }
    /* Multiplicative lightmap: combined magenta * green destination = black.
     * An unconditionally additive local pass would incorrectly leave red. */
    a.a = b.a = c.a = 255;
    pgrAlphaBlendFunction(GR_BLEND_DST_COLOR, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
    pgrDrawTriangle(&a, &b, &c);
    if (!pgrLfbReadRegion(GR_BUFFER_BACKBUFFER, 64, 64, 16, 16, 32, pixels)) {
        Record("FAIL modulated combiner read");
        passed = FALSE;
    } else
        for (i = 0; i < 256; i++)
            if (pixels[i] != 0) {
                Record("FAIL combiner modulation pixels");
                passed = FALSE;
                break;
            }
    /* Nontrivial normalized alpha also covers the source-built float path:
     * magenta * (64/255) over black is RGB8(64,0,64), RGB565(8,0,8). */
    pgrBufferClear(0, 255, 0);
    a.a = b.a = c.a = 64;
    pgrAlphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA, GR_BLEND_ONE,
                          GR_BLEND_ZERO);
    pgrDrawTriangle(&a, &b, &c);
    if (!pgrLfbReadRegion(GR_BUFFER_BACKBUFFER, 64, 64, 16, 16, 32, pixels)) {
        Record("FAIL fractional combiner read");
        passed = FALSE;
    } else
        for (i = 0; i < 256; i++)
            if (pixels[i] != 0x4008) {
                Record("FAIL fractional combiner pixels");
                passed = FALSE;
                break;
            }
    a.a = b.a = c.a = 255;
    pgrAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
    Record("COMBINER 1024 EXACT PIXELS");
    /* More than retained export credits: each swap must make forward progress. */
    for (i = 0; i < 7; i++) {
        pgrDrawTriangle(&a, &b, &c);
        pgrBufferSwap(0);
    }
    pgrSstWinClose();
    pgrGlideShutdown();
    /* Retail renderers can reject an initial mode and initialize again using
     * the same HWND. Check the real wrapper lifecycle, not only a fresh process. */
    Record("REINITIALIZE SAME WINDOW");
    pgrGlideInit();
    pgrSstSelect(0);
    if (!pgrSstWinOpen((FxU)window, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB,
                       GR_ORIGIN_UPPER_LEFT, 2, 1)) {
        Record("FAIL reopened Glide window");
        passed = FALSE;
    } else {
        pgrBufferClear(0x00ff0000, 255, 0);
        if (!pgrLfbReadRegion(GR_BUFFER_BACKBUFFER, 64, 64, 16, 16, 32, pixels)) {
            Record("FAIL reopened read");
            passed = FALSE;
        } else
            for (i = 0; i < 256; i++)
                if (pixels[i] != 0xf800) {
                    Record("FAIL reopened pixels");
                    passed = FALSE;
                    break;
                }
        pgrBufferSwap(0);
        pgrSstWinClose();
    }
    pgrGlideShutdown();
    DestroyWindow(window);
    FreeLibrary(dll);
    if (passed)
        Record("PASS automated glide: triangle, RGB565 texture and same-address replacement, "
               "combiner-before-alpha/lightmap blending, 2304 exact GPU pixels, eight swaps, "
               "same-window reinitialization and cleanup");
    CloseHandle(Log);
    ExitProcess(passed ? 0 : 1);
}
