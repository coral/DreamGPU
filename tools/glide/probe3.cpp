/* SPDX-License-Identifier: GPL-2.0-or-later
 * Public Glide 3.x GPU oracle for the installed system glide3x.dll (DGSYSG3).
 * Fixed geometry through non-trivial vertex layouts, exact GPU readback,
 * context close/reopen; no menu or input loop. Constants below are the
 * public Glide 3 SDK values, kept independent of the provider sources. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bit>
#include "glide3-api.h"
static HANDLE Log;
static void Record(const char *s) {
    DWORD n = 0, w;
    while (s[n])
        n++;
    WriteFile(Log, s, n, &w, NULL);
    WriteFile(Log, "\r\n", 2, &w, NULL);
    FlushFileBuffers(Log);
}
#include "display.h"
#include "provider3.h"

namespace {
constexpr FxU32 kParamXY = 0x01, kParamZ = 0x02, kParamW = 0x03, kParamQ = 0x04,
                kParamFogExt = 0x05, kParamA = 0x10, kParamRGB = 0x20, kParamPARGB = 0x30,
                kParamST0 = 0x40, kParamQ0 = 0x50, kEnable = 1, kDisable = 0;
constexpr FxU32 kTriangleStrip = 4, kTriangleFan = 5, kTriangles = 6, kFanContinue = 8;
constexpr FxU32 kWindowCoords = 0, kClipCoords = 1;
constexpr FxU32 kBitsRGBA = 0x02, kStateSize = 0x06, kLayoutSize = 0x07, kNumBoards = 0x0f,
                kNumTMU = 0x13, kViewport = 0x26, kVertexParameter = 0x29, kRenderer = 0xa2,
                kVersion = 0xa4;
constexpr FxI32 kRes640x480 = 7, kBack = 1, kCullDisable = 0, kCullNegative = 1, kCullPositive = 2,
                kFnZero = 0, kFnLocal = 1, kFnScaleOther = 3, kFactorZero = 0, kFactorOne = 8,
                kLocalIterated = 0, kLocalNone = 1, kOtherTexture = 1, kOtherNone = 2,
                kBlendZero = 0, kBlendOne = 4, kTmu0 = 0, kLod8 = 3, kAspect1x1 = 0, kRgb565 = 0xa,
                kPointSampled = 0, kLfb565 = 0;
constexpr FxU32 kBothLevels = 3;

// Every decorated export of the 3dfx glide3x.dll ABI that DreamGPU provides.
constexpr const char *kExports[] = {"_grAADrawTriangle@24",
                                    "_grAlphaBlendFunction@16",
                                    "_grAlphaCombine@20",
                                    "_grAlphaControlsITRGBLighting@4",
                                    "_grAlphaTestFunction@4",
                                    "_grAlphaTestReferenceValue@4",
                                    "_grBufferClear@12",
                                    "_grBufferSwap@4",
                                    "_grCheckForRoom@4",
                                    "_grChromakeyMode@4",
                                    "_grChromakeyValue@4",
                                    "_grClipWindow@16",
                                    "_grColorCombine@20",
                                    "_grColorMask@8",
                                    "_grConstantColorValue@4",
                                    "_grCoordinateSpace@4",
                                    "_grCullMode@4",
                                    "_grDepthBiasLevel@4",
                                    "_grDepthBufferFunction@4",
                                    "_grDepthBufferMode@4",
                                    "_grDepthMask@4",
                                    "_grDepthRange@8",
                                    "_grDisable@4",
                                    "_grDisableAllEffects@0",
                                    "_grDitherMode@4",
                                    "_grDrawLine@8",
                                    "_grDrawPoint@4",
                                    "_grDrawTriangle@12",
                                    "_grDrawVertexArray@12",
                                    "_grDrawVertexArrayContiguous@16",
                                    "_grEnable@4",
                                    "_grErrorSetCallback@4",
                                    "_grFinish@0",
                                    "_grFlush@0",
                                    "_grFogColorValue@4",
                                    "_grFogMode@4",
                                    "_grFogTable@4",
                                    "_grGet@12",
                                    "_grGetProcAddress@4",
                                    "_grGetString@4",
                                    "_grGlideGetState@4",
                                    "_grGlideGetVersion@4",
                                    "_grGlideGetVertexLayout@4",
                                    "_grGlideInit@0",
                                    "_grGlideSetState@4",
                                    "_grGlideSetVertexLayout@4",
                                    "_grGlideShutdown@0",
                                    "_grLfbConstantAlpha@4",
                                    "_grLfbConstantDepth@4",
                                    "_grLfbLock@24",
                                    "_grLfbReadRegion@28",
                                    "_grLfbUnlock@8",
                                    "_grLfbWriteColorFormat@4",
                                    "_grLfbWriteColorSwizzle@8",
                                    "_grLfbWriteRegion@36",
                                    "_grLoadGammaTable@16",
                                    "_grQueryResolutions@8",
                                    "_grRenderBuffer@4",
                                    "_grReset@4",
                                    "_grSelectContext@4",
                                    "_grSetNumPendingBuffers@4",
                                    "_grSplash@20",
                                    "_grSstConfigPipeline@12",
                                    "_grSstOrigin@4",
                                    "_grSstSelect@4",
                                    "_grSstWinClose@4",
                                    "_grSstWinOpen@28",
                                    "_grStippleMode@4",
                                    "_grStipplePattern@4",
                                    "_grTexCalcMemRequired@16",
                                    "_grTexClampMode@12",
                                    "_grTexCombine@28",
                                    "_grTexDetailControl@16",
                                    "_grTexDownloadMipMap@16",
                                    "_grTexDownloadMipMapLevel@32",
                                    "_grTexDownloadMipMapLevelPartial@40",
                                    "_grTexDownloadTable@8",
                                    "_grTexDownloadTablePartial@16",
                                    "_grTexFilterMode@12",
                                    "_grTexLodBiasValue@8",
                                    "_grTexMaxAddress@4",
                                    "_grTexMinAddress@4",
                                    "_grTexMipMapMode@12",
                                    "_grTexMultibase@8",
                                    "_grTexMultibaseAddress@20",
                                    "_grTexNCCTable@4",
                                    "_grTexSource@16",
                                    "_grTexTextureMemRequired@8",
                                    "_grVertexLayout@12",
                                    "_grViewport@16",
                                    "_gu3dfGetInfo@8",
                                    "_gu3dfLoad@8",
                                    "_guFogGenerateExp2@8",
                                    "_guFogGenerateExp@8",
                                    "_guFogGenerateLinear@12",
                                    "_guFogTableIndexToW@4",
                                    "_guGammaCorrectionRGB@12"};
static_assert(sizeof(kExports) / sizeof(kExports[0]) == 97, "glide3x export count");

// Window-coordinate vertex: XY, a NaN field that is named by a disabled
// parameter, packed colour, Q, Z, ST0 and Q0 (40 bytes).
struct Vertex {
    float x, y, poison;
    FxU32 argb;
    float q, z, s, t, q0, pad;
};
// Clip-coordinate vertex: XY, W and float RGB.
struct ClipVertex {
    float x, y, w, r, g, b;
};

template <typename Handle, auto Release> class Owned {
    Handle value_;

  public:
    explicit Owned(Handle value) : value_(value) {}
    Owned(const Owned &) = delete;
    Owned &operator=(const Owned &) = delete;
    ~Owned() {
        if (value_)
            Release(value_);
    }
};
bool Same(const char *a, const char *b) {
    while (*a && *a == *b)
        ++a, ++b;
    return *a == *b;
}
LRESULT CALLBACK WindowProc(HWND w, UINT m, WPARAM a, LPARAM b) {
    return DefWindowProcA(w, m, a, b);
}
Vertex Make(float x, float y, FxU32 argb, float s = 0, float t = 0) {
    Vertex v{};
    v.x = x;
    v.y = y;
    v.poison = std::bit_cast<float>(0x7fc00000u);
    v.argb = argb;
    v.q = v.q0 = 1.f;
    v.z = 0.f;
    v.s = s;
    v.t = t;
    v.pad = v.poison;
    return v;
}
unsigned short frame[640 * 480];
FxU8 state[8192], layout[1024];
Dg3Resolution modes[64];
} // namespace

#define API(name, bytes)                                                                           \
    auto p##name =                                                                                 \
        std::bit_cast<decltype(&dg3_##name)>(GetProcAddress(dll, "_" #name "@" #bytes));           \
    if (!p##name) {                                                                                \
        Record("FAIL missing " #name);                                                             \
        return false;                                                                              \
    }

static bool Run() {
    Record("START automated sysglide3");
    if (!CleanSystemGlide3Launch()) {
        Record("FAIL clean application/cwd system-loader preflight");
        return false;
    }
    Record("LOAD glide3x.dll");
    HMODULE dll = LoadLibraryA("glide3x.dll");
    Owned<HMODULE, FreeLibrary> library(dll);
    if (!dll) {
        Record("FAIL load glide3x.dll");
        return false;
    }
    if (!SystemModule(dll, "glide3x.dll") ||
        !SystemModule(GetModuleHandleA("dgpugl.dll"), "dgpugl.dll")) {
        Record("FAIL system Glide 3/OpenGL provider identity");
        return false;
    }
    for (const char *name : kExports)
        if (!GetProcAddress(dll, name)) {
            Record("FAIL missing decorated export");
            Record(name);
            return false;
        }
    Record("EXPORTS 97 decorated");
    API(grGlideInit, 0);
    API(grGlideShutdown, 0);
    API(grSstSelect, 4);
    API(grSstWinOpen, 28);
    API(grSstWinClose, 4);
    API(grSelectContext, 4);
    API(grGet, 12);
    API(grGetString, 4);
    API(grQueryResolutions, 8);
    API(grReset, 4);
    API(grVertexLayout, 12);
    API(grDrawTriangle, 12);
    API(grDrawVertexArray, 12);
    API(grDrawVertexArrayContiguous, 16);
    API(grCoordinateSpace, 4);
    API(grBufferClear, 12);
    API(grBufferSwap, 4);
    API(grColorCombine, 20);
    API(grAlphaCombine, 20);
    API(grAlphaBlendFunction, 16);
    API(grCullMode, 4);
    API(grLfbReadRegion, 28);
    API(grLfbWriteRegion, 36);
    API(grTexMinAddress, 4);
    API(grTexMaxAddress, 4);
    API(grTexTextureMemRequired, 8);
    API(grTexCalcMemRequired, 16);
    API(grTexDownloadMipMap, 16);
    API(grTexSource, 16);
    API(grTexCombine, 28);
    API(grTexFilterMode, 12);
    API(grGlideGetVertexLayout, 4);
    API(grGlideSetVertexLayout, 4);
    API(grGlideGetState, 4);
    API(grGlideSetState, 4);
    API(grFlush, 0);
    API(grFinish, 0);

    WNDCLASSA cls = {};
    cls.lpfnWndProc = WindowProc;
    cls.hInstance = GetModuleHandleA(NULL);
    cls.lpszClassName = "DGGlide3Probe";
    if (!RegisterClassA(&cls)) {
        Record("FAIL register window class");
        return false;
    }
    HWND window = CreateWindowA(cls.lpszClassName, "DreamGPU Glide 3 GPU probe",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE, 40, 40, 656, 519, NULL, NULL,
                                cls.hInstance, NULL);
    Owned<HWND, DestroyWindow> owned_window(window);
    if (!window) {
        Record("FAIL create window");
        return false;
    }
    bool passed = true, stage_ok = true, initialized = false;
    Dg3Context context = 0;
    char line[224];
    auto fail = [&](const char *what) {
        Record(what);
        passed = stage_ok = false;
    };
    // A stage line is a PASS marker only when none of its checks failed.
    auto stage = [&](const char *name) {
        if (stage_ok)
            Record(name);
        else {
            wsprintfA(line, "FAIL stage %s", name);
            Record(line);
        }
        stage_ok = true;
    };
    // One full-frame read per stage: the shared core reads the whole buffer
    // for every region anyway, and Win98 pays for each transfer.
    auto snapshot = [&]() {
        if (pgrLfbReadRegion(kBack, 0, 0, 640, 480, 1280, frame))
            return true;
        fail("FAIL readback of the 640x480 back buffer");
        return false;
    };
    // Reports the first wrong pixel of a block with its value and position.
    auto check = [&](FxU32 x, FxU32 y, FxU32 w, FxU32 h, unsigned short expected,
                     const char *what) {
        DWORD wrong = 0, wx = 0, wy = 0, actual = 0;
        for (FxU32 j = y; j < y + h; ++j)
            for (FxU32 i = x; i < x + w; ++i)
                if (frame[j * 640 + i] != expected && !wrong++) {
                    wx = i;
                    wy = j;
                    actual = frame[j * 640 + i];
                }
        if (!wrong)
            return;
        wsprintfA(line,
                  "FAIL pixels %s: %lu of %lu wrong in (%lu,%lu)+%lux%lu, first at (%lu,%lu) "
                  "actual 0x%04lx expected 0x%04lx",
                  what, wrong, DWORD(w * h), DWORD(x), DWORD(y), DWORD(w), DWORD(h), wx, wy, actual,
                  DWORD(expected));
        fail(line);
    };
    auto region = [&](FxU32 x, FxU32 y, unsigned short expected, const char *what) {
        if (snapshot())
            check(x, y, 16, 16, expected, what);
    };
    auto shutdown = [&]() {
        if (context)
            pgrSstWinClose(context);
        context = 0;
        if (initialized)
            pgrGlideShutdown();
        initialized = false;
    };
    auto open = [&]() {
        Record("OPEN grGlideInit/grSstWinOpen");
        pgrGlideInit();
        initialized = true;
        FxI32 boards = 0;
        if (pgrGet(kNumBoards, 4, &boards) != 4 || boards != 1)
            fail("FAIL GR_NUM_BOARDS before open");
        pgrSstSelect(0);
        context = pgrSstWinOpen(FxU32(UINT_PTR(window)), kRes640x480, 0, 0, 0, 2, 1);
        const FxBool selected = context ? pgrSelectContext(context) : FXFALSE,
                     zero = pgrSelectContext(0), other = pgrSelectContext(context + 1);
        if (!context || !selected || zero || other) {
            wsprintfA(line,
                      "FAIL Glide 3 context 0x%08lx: select(context) %ld, select(0) %ld, "
                      "select(other) %ld",
                      DWORD(context), long(selected), long(zero), long(other));
            fail(line);
            return false;
        }
        return true;
    };
    struct Session {
        decltype(shutdown) &end;
        ~Session() {
            end();
        }
    } session{shutdown};
    const DesktopSize desktop = RecordDesktop("before");
    if (!open())
        return false;
    RecordDesktop("open");
    stage("CONTEXT");

    // grGet succeeds only with the exact length; failures leave the buffer.
    FxI32 v[4] = {0x5a5a5a5a, 0x5a5a5a5a, 0x5a5a5a5a, 0x5a5a5a5a};
    const FxU32 wrong_length = pgrGet(kNumTMU, 8, v);
    const FxI32 untouched = v[0];
    const FxU32 tmu_bytes = pgrGet(kNumTMU, 4, v), unknown = pgrGet(0x7777, 4, v),
                null_buffer = pgrGet(kNumTMU, 4, nullptr);
    if (wrong_length || untouched != 0x5a5a5a5a || tmu_bytes != 4 || v[0] != 1 || unknown ||
        null_buffer) {
        wsprintfA(line,
                  "FAIL grGet length contract: NUM_TMU/8 -> %lu (buffer 0x%08lx), NUM_TMU/4 -> "
                  "%lu value %ld, unknown -> %lu, null -> %lu",
                  DWORD(wrong_length), DWORD(untouched), DWORD(tmu_bytes), long(v[0]),
                  DWORD(unknown), DWORD(null_buffer));
        fail(line);
    }
    FxU32 got = pgrGet(kBitsRGBA, 16, v);
    if (got != 16 || v[0] != 5 || v[1] != 6 || v[2] != 5 || v[3] != 0) {
        wsprintfA(line, "FAIL GR_BITS_RGBA: %lu bytes {%ld,%ld,%ld,%ld} expected 16 {5,6,5,0}",
                  DWORD(got), long(v[0]), long(v[1]), long(v[2]), long(v[3]));
        fail(line);
    }
    got = pgrGet(kViewport, 16, v);
    if (got != 16 || v[0] || v[1] || v[2] != 640 || v[3] != 480) {
        wsprintfA(line, "FAIL GR_VIEWPORT: %lu bytes {%ld,%ld,%ld,%ld} expected 16 {0,0,640,480}",
                  DWORD(got), long(v[0]), long(v[1]), long(v[2]), long(v[3]));
        fail(line);
    }
    FxI32 state_size = 0, layout_size = 0;
    if (pgrGet(kStateSize, 4, &state_size) != 4 || state_size <= 0 ||
        state_size > FxI32(sizeof(state)) || pgrGet(kLayoutSize, 4, &layout_size) != 4 ||
        layout_size <= 0 || layout_size > FxI32(sizeof(layout))) {
        wsprintfA(line, "FAIL state sizes: state %ld (max %lu), layout %ld (max %lu)",
                  long(state_size), DWORD(sizeof(state)), long(layout_size), DWORD(sizeof(layout)));
        fail(line);
    }
    const char *version = pgrGetString(kVersion);
    if (!version || version[0] != '3' || version[1] != '.' ||
        !Same(pgrGetString(kRenderer), "Glide"))
        fail("FAIL grGetString");
    Dg3Resolution query = {FxU32(kRes640x480), 0xffffffffu, -1, -1};
    const FxI32 bytes = pgrQueryResolutions(&query, nullptr);
    if (bytes <= 0 || bytes % 16 || bytes > FxI32(sizeof(modes)) ||
        pgrQueryResolutions(&query, modes) != bytes) {
        wsprintfA(line, "FAIL grQueryResolutions size: %ld bytes (buffer %lu)", long(bytes),
                  DWORD(sizeof(modes)));
        fail(line);
    } else
        for (FxI32 i = 0; i < bytes / 16; ++i)
            if (modes[i].resolution != FxU32(kRes640x480))
                fail("FAIL grQueryResolutions template");
    stage("QUERIES");

    // Non-trivial layout. RGB and A name the NaN field and are disabled
    // (after RGB, PARGB is enabled again so colours stay packed); FOG_EXT
    // gets an out-of-range offset and must be refused, never dereferenced.
    pgrReset(kVertexParameter);
    pgrVertexLayout(kParamXY, 0, kEnable);
    pgrVertexLayout(kParamRGB, 8, kEnable);
    pgrVertexLayout(kParamRGB, 8, kDisable);
    pgrVertexLayout(kParamA, 8, kDisable);
    pgrVertexLayout(kParamPARGB, 12, kEnable);
    pgrVertexLayout(kParamQ, 16, kEnable);
    pgrVertexLayout(kParamZ, 20, kEnable);
    pgrVertexLayout(kParamST0, 24, kEnable);
    pgrVertexLayout(kParamQ0, 32, kEnable);
    pgrVertexLayout(kParamFogExt, 0x7ffffff0, kEnable);
    pgrCullMode(kCullDisable);
    pgrColorCombine(kFnLocal, kFactorZero, kLocalIterated, kOtherNone, FXFALSE);
    pgrAlphaCombine(kFnLocal, kFactorZero, kLocalIterated, kOtherNone, FXFALSE);
    pgrAlphaBlendFunction(kBlendOne, kBlendZero, kBlendOne, kBlendZero);
    pgrBufferClear(0x000000ff, 0, 0);
    Vertex a = Make(32, 32, 0xffff0000), b = Make(320, 32, 0xffff0000),
           c = Make(32, 320, 0xffff0000);
    const void *triangle[3] = {&a, &b, &c};
    pgrDrawVertexArray(kTriangles, 3, triangle);
    if (snapshot()) {
        check(64, 64, 16, 16, 0xf800, "grDrawVertexArray GR_TRIANGLES");
        check(480, 380, 16, 16, 0x001f, "clear");
    }
    stage("LAYOUT TRIANGLES");

    // A contiguous strip drawn with each cull sign: both triangles must share
    // a winding (alternating strip order), so exactly one pass covers both.
    Vertex strip[4] = {Make(360, 40, 0xff00ff00), Make(600, 40, 0xff00ff00),
                       Make(360, 200, 0xff00ff00), Make(600, 200, 0xff00ff00)};
    pgrCullMode(kCullNegative);
    pgrDrawVertexArrayContiguous(kTriangleStrip, 4, strip, sizeof(Vertex));
    for (Vertex &s : strip)
        s.argb = 0xffffff00;
    pgrCullMode(kCullPositive);
    pgrDrawVertexArrayContiguous(kTriangleStrip, 4, strip, sizeof(Vertex));
    pgrCullMode(kCullDisable);
    if (snapshot()) {
        const unsigned short first = frame[50 * 640 + 370];
        if (first != 0x07e0 && first != 0xffe0) {
            wsprintfA(line,
                      "FAIL strip coverage at (370,50): actual 0x%04lx expected 0x07e0 or 0xffe0",
                      DWORD(first));
            fail(line);
        }
        check(370, 50, 16, 16, first, "strip first triangle");
        check(570, 170, 16, 16, first, "strip second triangle winding");
    }
    stage("CONTIGUOUS STRIP WINDING");

    // A fan continued twice by GR_TRIANGLE_FAN_CONTINUE.
    Vertex fan[6] = {Make(120, 400, 0xffff00ff), Make(40, 340, 0xffff00ff),
                     Make(200, 340, 0xffff00ff), Make(200, 460, 0xffff00ff),
                     Make(40, 460, 0xffff00ff),  Make(40, 340, 0xffff00ff)};
    const void *fan_start[4] = {&fan[0], &fan[1], &fan[2], &fan[3]},
               *fan_more[2] = {&fan[4], &fan[5]};
    pgrDrawVertexArray(kTriangleFan, 4, fan_start);
    pgrDrawVertexArray(kFanContinue, 2, fan_more);
    if (snapshot()) {
        check(52, 392, 16, 16, 0xf81f, "fan continue left");
        check(112, 436, 16, 16, 0xf81f, "fan continue bottom");
        check(170, 392, 16, 16, 0xf81f, "fan right");
    }
    stage("FAN CONTINUE");

    // Clip coordinates with float colours; the saved layout is restored after.
    pgrGlideGetVertexLayout(layout);
    pgrReset(kVertexParameter);
    pgrVertexLayout(kParamXY, 0, kEnable);
    pgrVertexLayout(kParamW, 8, kEnable);
    pgrVertexLayout(kParamRGB, 12, kEnable);
    pgrCoordinateSpace(kClipCoords);
    // Window (400,260) (620,260) (400,460) with w = 2 and the default viewport.
    ClipVertex ca = {0.5f, 1.f / 6.f, 2.f, 1.f, 1.f, 0.f},
               cb = {1.875f, 1.f / 6.f, 2.f, 1.f, 1.f, 0.f},
               cc = {0.5f, 11.f / 6.f, 2.f, 1.f, 1.f, 0.f};
    pgrDrawTriangle(&ca, &cb, &cc);
    region(410, 270, 0xffe0, "clip coordinates");
    pgrCoordinateSpace(kWindowCoords);
    pgrGlideSetVertexLayout(layout);
    stage("CLIP COORDINATES");

    // Glide 3 log2 texture descriptors on TMU0 with the restored layout.
    static unsigned short texels[64];
    for (unsigned short &t : texels)
        t = 0x07e0;
    Dg3TexInfo info = {kLod8, kLod8, kAspect1x1, kRgb565, texels};
    const FxU32 start = pgrTexMinAddress(kTmu0), mem = pgrTexTextureMemRequired(kBothLevels, &info);
    Dg3TexInfo invalid = {kLod8, 9, kAspect1x1, kRgb565, texels};
    const FxU32 calc = pgrTexCalcMemRequired(kLod8, kLod8, kAspect1x1, kRgb565),
                rejected = pgrTexTextureMemRequired(kBothLevels, &invalid),
                top = pgrTexMaxAddress(kTmu0);
    if (mem != 128 || calc != mem || rejected || top < start + 256 + 176) {
        wsprintfA(line,
                  "FAIL texture memory queries: required %lu calc %lu (expected 128), invalid "
                  "LOD %lu, min 0x%lx max 0x%lx",
                  DWORD(mem), DWORD(calc), DWORD(rejected), DWORD(start), DWORD(top));
        fail(line);
    }
    // A full 8x8..1x1 chain elsewhere: the application's descriptor must not
    // be rewritten by the shared core's top-level-only download.
    static unsigned short chain_texels[64 + 16 + 4 + 1];
    Dg3TexInfo chain = {0, kLod8, kAspect1x1, kRgb565, chain_texels};
    pgrTexDownloadMipMap(kTmu0, start + 256, kBothLevels, &chain);
    if (chain.smallLodLog2 != 0 || chain.largeLodLog2 != kLod8 || chain.data != chain_texels)
        fail("FAIL texture descriptor modified");
    pgrTexDownloadMipMap(kTmu0, start, kBothLevels, &info);
    pgrTexSource(kTmu0, start, kBothLevels, &info);
    pgrTexFilterMode(kTmu0, kPointSampled, kPointSampled);
    pgrTexCombine(kTmu0, kFnLocal, kFactorZero, kFnLocal, kFactorZero, FXFALSE, FXFALSE);
    pgrColorCombine(kFnScaleOther, kFactorOne, kLocalNone, kOtherTexture, FXFALSE);
    a = Make(32, 32, 0xffff0000, 0, 0);
    b = Make(320, 32, 0xffff0000, 255, 0);
    c = Make(32, 320, 0xffff0000, 0, 255);
    pgrDrawTriangle(&a, &b, &c);
    region(64, 64, 0x07e0, "texture");
    stage("TEXTURE LOG2 LOD");

    // grGlideGetState/SetState restores the textured combine.
    pgrGlideGetState(state);
    pgrColorCombine(kFnZero, kFactorZero, kLocalNone, kOtherNone, FXFALSE);
    pgrTexCombine(kTmu0, kFnZero, kFactorZero, kFnZero, kFactorZero, FXFALSE, FXFALSE);
    pgrGlideSetState(state);
    pgrBufferClear(0, 0, 0);
    pgrDrawTriangle(&a, &b, &c);
    region(64, 64, 0x07e0, "restored state");
    stage("STATE RESTORED");

    // LFB writes (Glide 3 pixelPipeline argument), clipped edges and bounds.
    static unsigned short block[256];
    for (unsigned short &p : block)
        p = 0xf81f;
    if (!pgrLfbWriteRegion(kBack, 200, 200, kLfb565, 16, 16, FXFALSE, 32, block) ||
        !pgrLfbWriteRegion(kBack, 632, 0, kLfb565, 16, 16, FXFALSE, 32, block))
        fail("FAIL grLfbWriteRegion");
    if (snapshot()) {
        check(200, 200, 16, 16, 0xf81f, "lfb write");
        check(632, 0, 8, 16, 0xf81f, "clipped lfb write");
    }
    // Out-of-frame regions and short strides are refused before any read.
    if (pgrLfbReadRegion(kBack, 630, 470, 16, 16, 32, frame) ||
        pgrLfbReadRegion(kBack, 0, 0, 16, 16, 16, frame))
        fail("FAIL lfb read bounds");
    pgrFlush();
    pgrFinish();
    stage("LFB 16-BIT");

    for (unsigned i = 0; i < 8; ++i) {
        pgrDrawTriangle(&a, &b, &c);
        pgrBufferSwap(0);
    }
    const FxBool foreign = pgrSstWinClose(context + 1), closed = pgrSstWinClose(context),
                 stale = pgrSelectContext(context);
    if (foreign || !closed || stale) {
        wsprintfA(line, "FAIL context close: foreign %ld, own %ld, select after close %ld",
                  long(foreign), long(closed), long(stale));
        fail(line);
    }
    context = 0;
    pgrGlideShutdown();
    initialized = false;
    if (!SameDesktop(RecordDesktop("closed"), desktop))
        fail("FAIL display mode not restored after close");
    stage("EIGHT SWAPS AND CLOSE");

    // Retail renderers reinitialize on the same HWND.
    Record("REINITIALIZE SAME WINDOW");
    if (!open())
        return false;
    pgrVertexLayout(kParamXY, 0, kEnable);
    pgrVertexLayout(kParamPARGB, 12, kEnable);
    pgrColorCombine(kFnLocal, kFactorZero, kLocalIterated, kOtherNone, FXFALSE);
    pgrBufferClear(0x00ff0000, 0, 0);
    a = Make(32, 32, 0xff0000ff);
    b = Make(320, 32, 0xff0000ff);
    c = Make(32, 320, 0xff0000ff);
    pgrDrawTriangle(&a, &b, &c);
    if (snapshot()) {
        check(480, 380, 16, 16, 0xf800, "reopened clear");
        check(64, 64, 16, 16, 0x001f, "reopened triangle");
    }
    pgrBufferSwap(0);
    stage("REOPENED");
    shutdown();
    if (!SameDesktop(RecordDesktop("closed"), desktop))
        fail("FAIL display mode not restored after reopened close");
    stage("CLOSED");
    return passed;
}

extern "C" void WINAPI WinMainCRTStartup(void) {
    Log = CreateFileA("C:\\DGSYSG3.LOG", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0,
                      NULL);
    if (Log == INVALID_HANDLE_VALUE)
        ExitProcess(1);
    const bool passed = Run();
    if (passed) {
        Record("PASS automated sysglide3: verified system Glide 3/OpenGL providers, ");
        Record("97 decorated exports, grGet/grQueryResolutions contracts, layout triangles with "
               "disabled and out-of-range parameters, contiguous strip winding, fan continue, "
               "clip coordinates, log2 texture, state and layout restore, 16-bit LFB, eight "
               "swaps, context close, same-window reinitialization and display restored after "
               "each close");
    }
    CloseHandle(Log);
    ExitProcess(passed ? 0 : 1);
}
