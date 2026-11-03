// SPDX-License-Identifier: GPL-2.0-or-later
// Glide 3.x public ABI implemented by DreamGPU glide3x.dll. Each export in
// glide3x.def maps "_grName@N" to "dg3_grName@N"; with stdcall the compiler
// derives N from these exact prototypes, so a wrong argument list cannot link.
// Types use Dg3 names so this header coexists with the Glide 2 SDK headers
// used by the shared OpenGLide core. Layouts are the 32-bit Glide 3 ABI.
#pragma once
#include "sdk2_3dfx.h"

#ifdef __cplusplus
#define DG3_API(type) extern "C" type __stdcall
#else
#define DG3_API(type) extern type __stdcall
#endif

typedef FxU32 Dg3Context;
typedef FxU8 Dg3Fog;
typedef void (*Dg3ErrorCallback)(const char *string, FxBool fatal);
typedef int(__stdcall *Dg3Proc)(void);
typedef struct {
    FxI32 smallLodLog2, largeLodLog2, aspectRatioLog2, format;
    void *data;
} Dg3TexInfo;
typedef struct {
    int size;
    void *lfbPtr;
    FxU32 strideInBytes;
    FxI32 writeMode, origin;
} Dg3LfbInfo;
typedef struct {
    FxU32 resolution, refresh;
    int numColorBuffers, numAuxBuffers;
} Dg3Resolution;
typedef struct {
    FxU32 width, height;
    int small_lod, large_lod;
    FxI32 aspect_ratio, format;
} Dg3Gu3dfHeader;
typedef struct {
    Dg3Gu3dfHeader header;
    FxU32 table[256];
    void *data;
    FxU32 mem_required;
} Dg3Gu3dfInfo;

// One prototype per line: tests read this block as the export table.
// clang-format off
/* Rendering */
DG3_API(void) dg3_grDrawPoint(const void *pt);
DG3_API(void) dg3_grDrawLine(const void *v1, const void *v2);
DG3_API(void) dg3_grDrawTriangle(const void *a, const void *b, const void *c);
DG3_API(void) dg3_grVertexLayout(FxU32 param, FxI32 offset, FxU32 mode);
DG3_API(void) dg3_grDrawVertexArray(FxU32 mode, FxU32 count, void *pointers);
DG3_API(void) dg3_grDrawVertexArrayContiguous(FxU32 mode, FxU32 count, void *pointers, FxU32 stride);
DG3_API(void) dg3_grAADrawTriangle(const void *a, const void *b, const void *c, FxBool ab_antialias, FxBool bc_antialias, FxBool ca_antialias);
/* Buffers and contexts */
DG3_API(void) dg3_grBufferClear(FxU32 color, FxU8 alpha, FxU32 depth);
DG3_API(void) dg3_grBufferSwap(FxU32 swap_interval);
DG3_API(void) dg3_grRenderBuffer(FxI32 buffer);
DG3_API(void) dg3_grErrorSetCallback(Dg3ErrorCallback fnc);
DG3_API(void) dg3_grFinish(void);
DG3_API(void) dg3_grFlush(void);
DG3_API(Dg3Context) dg3_grSstWinOpen(FxU32 hWnd, FxI32 screen_resolution, FxI32 refresh_rate, FxI32 color_format, FxI32 origin_location, int nColBuffers, int nAuxBuffers);
DG3_API(FxBool) dg3_grSstWinClose(Dg3Context context);
DG3_API(FxBool) dg3_grSelectContext(Dg3Context context);
DG3_API(void) dg3_grSstOrigin(FxI32 origin);
DG3_API(void) dg3_grSstSelect(int which_sst);
DG3_API(void) dg3_grSetNumPendingBuffers(FxI32 count);
DG3_API(void) dg3_grCheckForRoom(FxI32 n);
DG3_API(void) dg3_grSstConfigPipeline(FxU32 chip, FxU32 reg, FxU32 value);
/* Pixel pipeline state */
DG3_API(void) dg3_grAlphaBlendFunction(FxI32 rgb_sf, FxI32 rgb_df, FxI32 alpha_sf, FxI32 alpha_df);
DG3_API(void) dg3_grAlphaCombine(FxI32 function, FxI32 factor, FxI32 local, FxI32 other, FxBool invert);
DG3_API(void) dg3_grAlphaControlsITRGBLighting(FxBool enable);
DG3_API(void) dg3_grAlphaTestFunction(FxI32 function);
DG3_API(void) dg3_grAlphaTestReferenceValue(FxU8 value);
DG3_API(void) dg3_grChromakeyMode(FxI32 mode);
DG3_API(void) dg3_grChromakeyValue(FxU32 value);
DG3_API(void) dg3_grClipWindow(FxU32 minx, FxU32 miny, FxU32 maxx, FxU32 maxy);
DG3_API(void) dg3_grColorCombine(FxI32 function, FxI32 factor, FxI32 local, FxI32 other, FxBool invert);
DG3_API(void) dg3_grColorMask(FxBool rgb, FxBool a);
DG3_API(void) dg3_grCullMode(FxI32 mode);
DG3_API(void) dg3_grConstantColorValue(FxU32 value);
DG3_API(void) dg3_grDepthBiasLevel(FxI32 level);
DG3_API(void) dg3_grDepthBufferFunction(FxI32 function);
DG3_API(void) dg3_grDepthBufferMode(FxI32 mode);
DG3_API(void) dg3_grDepthMask(FxBool mask);
DG3_API(void) dg3_grDisableAllEffects(void);
DG3_API(void) dg3_grDitherMode(FxI32 mode);
DG3_API(void) dg3_grStippleMode(FxI32 mode);
DG3_API(void) dg3_grStipplePattern(FxU32 pattern);
DG3_API(void) dg3_grFogColorValue(FxU32 fogcolor);
DG3_API(void) dg3_grFogMode(FxU32 mode);
DG3_API(void) dg3_grFogTable(const Dg3Fog *ft);
DG3_API(void) dg3_grLoadGammaTable(FxU32 nentries, FxU32 *red, FxU32 *green, FxU32 *blue);
DG3_API(void) dg3_grSplash(float x, float y, float width, float height, FxU32 frame);
/* Queries and modes */
DG3_API(FxU32) dg3_grGet(FxU32 pname, FxU32 plength, FxI32 *params);
DG3_API(const char *) dg3_grGetString(FxU32 pname);
DG3_API(FxI32) dg3_grQueryResolutions(const Dg3Resolution *resTemplate, Dg3Resolution *output);
DG3_API(FxBool) dg3_grReset(FxU32 what);
DG3_API(Dg3Proc) dg3_grGetProcAddress(char *procName);
DG3_API(void) dg3_grEnable(FxU32 mode);
DG3_API(void) dg3_grDisable(FxU32 mode);
DG3_API(void) dg3_grCoordinateSpace(FxU32 mode);
DG3_API(void) dg3_grDepthRange(FxFloat n, FxFloat f);
DG3_API(void) dg3_grViewport(FxI32 x, FxI32 y, FxI32 width, FxI32 height);
/* Texture memory and downloads */
DG3_API(FxU32) dg3_grTexCalcMemRequired(FxI32 lodmin, FxI32 lodmax, FxI32 aspect, FxI32 fmt);
DG3_API(FxU32) dg3_grTexTextureMemRequired(FxU32 evenOdd, Dg3TexInfo *info);
DG3_API(FxU32) dg3_grTexMinAddress(FxI32 tmu);
DG3_API(FxU32) dg3_grTexMaxAddress(FxI32 tmu);
DG3_API(void) dg3_grTexNCCTable(FxU32 table);
DG3_API(void) dg3_grTexSource(FxI32 tmu, FxU32 startAddress, FxU32 evenOdd, Dg3TexInfo *info);
DG3_API(void) dg3_grTexClampMode(FxI32 tmu, FxI32 s_clampmode, FxI32 t_clampmode);
DG3_API(void) dg3_grTexCombine(FxI32 tmu, FxI32 rgb_function, FxI32 rgb_factor, FxI32 alpha_function, FxI32 alpha_factor, FxBool rgb_invert, FxBool alpha_invert);
DG3_API(void) dg3_grTexDetailControl(FxI32 tmu, int lod_bias, FxU8 detail_scale, float detail_max);
DG3_API(void) dg3_grTexFilterMode(FxI32 tmu, FxI32 minfilter_mode, FxI32 magfilter_mode);
DG3_API(void) dg3_grTexLodBiasValue(FxI32 tmu, float bias);
DG3_API(void) dg3_grTexDownloadMipMap(FxI32 tmu, FxU32 startAddress, FxU32 evenOdd, Dg3TexInfo *info);
DG3_API(void) dg3_grTexDownloadMipMapLevel(FxI32 tmu, FxU32 startAddress, FxI32 thisLod, FxI32 largeLod, FxI32 aspectRatio, FxI32 format, FxU32 evenOdd, void *data);
DG3_API(FxBool) dg3_grTexDownloadMipMapLevelPartial(FxI32 tmu, FxU32 startAddress, FxI32 thisLod, FxI32 largeLod, FxI32 aspectRatio, FxI32 format, FxU32 evenOdd, void *data, int start, int end);
DG3_API(void) dg3_grTexDownloadTable(FxU32 type, void *data);
DG3_API(void) dg3_grTexDownloadTablePartial(FxU32 type, void *data, int start, int end);
DG3_API(void) dg3_grTexMipMapMode(FxI32 tmu, FxI32 mode, FxBool lodBlend);
DG3_API(void) dg3_grTexMultibase(FxI32 tmu, FxBool enable);
DG3_API(void) dg3_grTexMultibaseAddress(FxI32 tmu, FxU32 range, FxU32 startAddress, FxU32 evenOdd, Dg3TexInfo *info);
/* Linear frame buffer */
DG3_API(FxBool) dg3_grLfbLock(FxI32 type, FxI32 buffer, FxI32 writeMode, FxI32 origin, FxBool pixelPipeline, Dg3LfbInfo *info);
DG3_API(FxBool) dg3_grLfbUnlock(FxI32 type, FxI32 buffer);
DG3_API(void) dg3_grLfbConstantAlpha(FxU8 alpha);
DG3_API(void) dg3_grLfbConstantDepth(FxU32 depth);
DG3_API(void) dg3_grLfbWriteColorSwizzle(FxBool swizzleBytes, FxBool swapWords);
DG3_API(void) dg3_grLfbWriteColorFormat(FxI32 colorFormat);
DG3_API(FxBool) dg3_grLfbWriteRegion(FxI32 dst_buffer, FxU32 dst_x, FxU32 dst_y, FxI32 src_format, FxU32 src_width, FxU32 src_height, FxBool pixelPipeline, FxI32 src_stride, void *src_data);
DG3_API(FxBool) dg3_grLfbReadRegion(FxI32 src_buffer, FxU32 src_x, FxU32 src_y, FxU32 src_width, FxU32 src_height, FxU32 dst_stride, void *dst_data);
/* Library state */
DG3_API(void) dg3_grGlideInit(void);
DG3_API(void) dg3_grGlideShutdown(void);
DG3_API(void) dg3_grGlideGetState(void *state);
DG3_API(void) dg3_grGlideSetState(const void *state);
DG3_API(void) dg3_grGlideGetVertexLayout(void *layout);
DG3_API(void) dg3_grGlideSetVertexLayout(const void *layout);
DG3_API(void) dg3_grGlideGetVersion(char *version);
/* Utilities (glideutl.h) */
DG3_API(void) dg3_guGammaCorrectionRGB(FxFloat red, FxFloat green, FxFloat blue);
DG3_API(float) dg3_guFogTableIndexToW(int i);
DG3_API(void) dg3_guFogGenerateExp(Dg3Fog *fogtable, float density);
DG3_API(void) dg3_guFogGenerateExp2(Dg3Fog *fogtable, float density);
DG3_API(void) dg3_guFogGenerateLinear(Dg3Fog *fogtable, float nearZ, float farZ);
DG3_API(FxBool) dg3_gu3dfGetInfo(const char *filename, Dg3Gu3dfInfo *info);
DG3_API(FxBool) dg3_gu3dfLoad(const char *filename, Dg3Gu3dfInfo *data);
// clang-format on
