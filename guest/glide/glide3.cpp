// SPDX-License-Identifier: GPL-2.0-or-later
// DreamGPU Glide 3.x provider (glide3x.dll) on the shared OpenGLide core.
// Glide 3 vertex layouts, coordinate spaces, contexts, queries and texture
// descriptors are translated here; rasterization, texturing, LFB and
// presentation stay in the same audited OpenGLide renderer as glide2x.dll.
// The donor g3wrap.cpp is not linked into this provider.
// Standard headers precede OpenGLide's platform min/max macros.
#include <initializer_list>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include "glide3-core.h"
#include "GlOgl.h"
#include "GLRender.h"
#include "glide3-api.h"

static_assert(sizeof(void *) == 4, "Glide 3 is a 32-bit ABI");
static_assert(sizeof(GrVertex) == sizeof(dg3::Vertex2) && GLIDE_NUM_TMU == 2,
              "Glide 2 core vertex");
static_assert(offsetof(GrVertex, ooz) == offsetof(dg3::Vertex2, ooz) &&
                  offsetof(GrVertex, oow) == offsetof(dg3::Vertex2, oow) &&
                  offsetof(GrVertex, a) == offsetof(dg3::Vertex2, a) &&
                  offsetof(GrVertex, tmuvtx[1].oow) == offsetof(dg3::Vertex2, tmu[1].oow),
              "Glide 2 core vertex fields");
static_assert(sizeof(GrTexInfo) == sizeof(Dg3TexInfo) && sizeof(Dg3TexInfo) == 20, "GrTexInfo ABI");
static_assert(sizeof(GrLfbInfo_t) == sizeof(Dg3LfbInfo) && sizeof(Dg3LfbInfo) == 20,
              "GrLfbInfo_t ABI");
static_assert(sizeof(Dg3Resolution) == sizeof(dg3::Resolution), "GrResolution ABI");
static_assert(sizeof(Dg3Gu3dfInfo) == sizeof(Gu3dfInfo) && sizeof(Dg3Gu3dfInfo) == 1056 &&
                  sizeof(dg3::Df3Table) == sizeof(GuTexTable),
              "Gu3dfInfo ABI");
static_assert(sizeof(dg3::NccTable) == sizeof(GuNccTable), "GuNccTable ABI");

namespace {
constexpr dg3::u32 kStateMagic = 0x53334744u; // "DG3S"
constexpr dg3::u32 kStateVersion = 1;
constexpr const char kVersion[] = "3.01";
constexpr FxU32 kPalette6666 = 3; // GR_TEXTABLE_PALETTE_6666_EXT

// The grGlideGetState blob: the core's Glide 2 state plus Glide 3 additions.
struct State3 {
    dg3::u32 magic, version;
    GlideState core;
    dg3::CoreFlags flags;              // order-derived core flags (see glide3-core.h)
    FxU8 fog_table[GR_FOG_TABLE_SIZE]; // the core keeps the table outside GlideState
    dg3::Layout layout;
    dg3::Viewport view;
    dg3::u32 coords, fog_mode, enables, stipple_mode, stipple_pattern;
    float s_scale[2], t_scale[2];
};

struct Provider {
    dg3::Layout layout;
    dg3::Setup setup;
    dg3::Viewport view;
    dg3::u32 coords, fog_mode, enables, stipple_mode, stipple_pattern, pending_buffers;
    float s_scale[2], t_scale[2];
    dg3::u32 points, lines, triangles;
    bool open;
} g;
char context_token;

Dg3Context context() {
    return Dg3Context(uintptr_t(&context_token));
}
bool open() {
    return g.open && OpenGL.WinOpen;
}
void reset_provider() {
    memset(&g, 0, sizeof(g));
    dg3::reset(g.layout);
    dg3::set_depth_range(g.view, 0.f, 1.f);
    for (unsigned t = 0; t < 2; ++t)
        g.s_scale[t] = g.t_scale[t] = 256.f;
}
dg3::Space space() {
    dg3::Space s;
    s.coords = g.coords;
    s.view = g.view;
    for (unsigned t = 0; t < 2; ++t) {
        s.s_scale[t] = g.s_scale[t];
        s.t_scale[t] = g.t_scale[t];
    }
    s.wbuffer = Glide.State.DepthBufferMode == GR_DEPTHBUFFER_WBUFFER ||
                Glide.State.DepthBufferMode == GR_DEPTHBUFFER_WBUFFER_COMPARE_TO_BIAS;
    return s;
}
const GrVertex *core(const dg3::Vertex2 &v) {
    return reinterpret_cast<const GrVertex *>(&v);
}
struct CoreSink {
    void triangle(const dg3::Vertex2 &a, const dg3::Vertex2 &b, const dg3::Vertex2 &c) {
        grDrawTriangle(core(a), core(b), core(c));
        ++g.triangles;
    }
    void line(const dg3::Vertex2 &a, const dg3::Vertex2 &b) {
        grDrawLine(core(a), core(b));
        ++g.lines;
    }
    void point(const dg3::Vertex2 &a) {
        grDrawPoint(core(a));
        ++g.points;
    }
};
bool tex_from(const Dg3TexInfo *info, dg3::TexInfo &t) {
    if (!info)
        return false;
    t = {info->smallLodLog2, info->largeLodLog2, info->aspectRatioLog2, info->format, info->data};
    return dg3::tex_ok(t);
}
GrTexInfo glide2(const dg3::TexInfo &t) {
    const dg3::TexInfo c = dg3::to_glide2(t);
    GrTexInfo info;
    info.smallLod = c.small_lod;
    info.largeLod = c.large_lod;
    info.aspectRatio = c.aspect;
    info.format = c.format;
    info.data = c.data;
    return info;
}
bool even_odd_ok(FxU32 evenOdd) {
    return evenOdd >= GR_MIPMAPLEVELMASK_EVEN && evenOdd <= GR_MIPMAPLEVELMASK_BOTH;
}
bool combine_ok(FxI32 function, FxI32 factor) {
    return function >= GR_COMBINE_FUNCTION_ZERO &&
           function <= GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL_ALPHA &&
           factor >= GR_COMBINE_FACTOR_ZERO && factor <= GR_COMBINE_FACTOR_ONE_MINUS_LOD_FRACTION;
}
bool lfb_buffer_ok(FxI32 buffer) {
    return buffer >= GR_BUFFER_FRONTBUFFER && buffer <= GR_BUFFER_DEPTHBUFFER;
}
// Bytes per pixel of an LFB write format; 0 when DreamGPU cannot write it.
unsigned lfb_bytes(FxI32 format) {
    switch (format) {
        case GR_LFB_SRC_FMT_565:
        case GR_LFB_SRC_FMT_555:
        case GR_LFB_SRC_FMT_1555:
        case GR_LFB_SRC_FMT_ZA16:
            return 2;
        case GR_LFB_SRC_FMT_888:
        case GR_LFB_SRC_FMT_8888:
        case GR_LFB_SRC_FMT_565_DEPTH:
        case GR_LFB_SRC_FMT_555_DEPTH:
        case GR_LFB_SRC_FMT_1555_DEPTH:
            return 4;
        default:
            return 0;
    }
}
dg3::u32 fb_bytes() {
    const int mb = UserConfig.FrameBufferMemorySize;
    return dg3::u32(mb >= OGL_MIN_FRAME_BUFFER && mb <= OGL_MAX_FRAME_BUFFER ? mb : 8) << 20;
}
// Largest .3df file DreamGPU reads: header, palette and a full 256x256
// 16-bit mipmap chain fit comfortably. The buffer exists only during a call.
constexpr size_t k3dfLimit = 256 + 1024 + 256 * 256 * 2 * 2;
struct FileBytes {
    dg3::u8 *data = nullptr;
    size_t size = 0;
    FileBytes() = default;
    FileBytes(const FileBytes &) = delete;
    FileBytes &operator=(const FileBytes &) = delete;
    ~FileBytes() {
        if (data)
            HeapFree(GetProcessHeap(), 0, data);
    }
};
bool read_file(const char *name, FileBytes &out) {
    if (!name)
        return false;
    FILE *file = fopen(name, "rb");
    if (!file)
        return false;
    const long length = fseek(file, 0, SEEK_END) == 0 ? ftell(file) : -1;
    if (length > 0 && fseek(file, 0, SEEK_SET) == 0) {
        const size_t wanted = size_t(length) < k3dfLimit ? size_t(length) : k3dfLimit;
        out.data = static_cast<dg3::u8 *>(HeapAlloc(GetProcessHeap(), 0, wanted));
        if (out.data)
            out.size = fread(out.data, 1, wanted, file);
    }
    fclose(file);
    return out.size > 0;
}
} // namespace

// ---------------------------------------------------------------- rendering
DG3_API(void) dg3_grDrawPoint(const void *pt) {
    if (!open() || !pt)
        return;
    dg3::Vertex2 v;
    dg3::convert(g.layout, space(), pt, v);
    CoreSink sink;
    sink.point(v);
    g.setup.count = 0;
}
DG3_API(void) dg3_grDrawLine(const void *v1, const void *v2) {
    if (!open() || !v1 || !v2)
        return;
    const dg3::Space s = space();
    dg3::Vertex2 a, b;
    dg3::convert(g.layout, s, v1, a);
    dg3::convert(g.layout, s, v2, b);
    CoreSink sink;
    sink.line(a, b);
    g.setup.count = 0;
}
DG3_API(void) dg3_grDrawTriangle(const void *a, const void *b, const void *c) {
    if (!open() || !a || !b || !c)
        return;
    const dg3::Space s = space();
    dg3::Vertex2 va, vb, vc;
    dg3::convert(g.layout, s, a, va);
    dg3::convert(g.layout, s, b, vb);
    dg3::convert(g.layout, s, c, vc);
    CoreSink sink;
    sink.triangle(va, vb, vc);
    dg3::setup_after_triangle(g.setup, vb, vc);
}
DG3_API(void)
dg3_grAADrawTriangle(const void *a, const void *b, const void *c, FxBool, FxBool, FxBool) {
    // Edge antialiasing is not emulated; the triangle itself is exact.
    dg3_grDrawTriangle(a, b, c);
}
DG3_API(void) dg3_grVertexLayout(FxU32 param, FxI32 offset, FxU32 mode) {
    if (dg3::set_param(g.layout, param, offset, mode) == dg3::SetResult::bad_offset)
        GlideMsg("DG_GLIDE3 rejected vertex layout 0x%x offset %d\n", param, offset);
}
DG3_API(void) dg3_grDrawVertexArray(FxU32 mode, FxU32 count, void *pointers) {
    dg3::PointerSource source;
    if (!open() || !dg3::pointer_source(pointers, count, source))
        return;
    CoreSink sink;
    dg3::draw(mode, count, g.layout, space(), g.setup, source, sink);
}
DG3_API(void)
dg3_grDrawVertexArrayContiguous(FxU32 mode, FxU32 count, void *pointers, FxU32 stride) {
    dg3::StrideSource source;
    if (!open() || !dg3::stride_source(pointers, count, stride, g.layout, source))
        return;
    CoreSink sink;
    dg3::draw(mode, count, g.layout, space(), g.setup, source, sink);
}

// ---------------------------------------------------------- buffers, context
DG3_API(void) dg3_grBufferClear(FxU32 color, FxU8 alpha, FxU32 depth) {
    if (open())
        grBufferClear(color, alpha, FxU16(depth > 0xffff ? 0xffff : depth));
}
DG3_API(void) dg3_grBufferSwap(FxU32 swap_interval) {
    if (open())
        grBufferSwap(int(swap_interval & 0xff));
}
DG3_API(void) dg3_grRenderBuffer(FxI32 buffer) {
    if (open() && (buffer == GR_BUFFER_FRONTBUFFER || buffer == GR_BUFFER_BACKBUFFER))
        grRenderBuffer(buffer);
}
DG3_API(void) dg3_grErrorSetCallback(Dg3ErrorCallback fnc) {
    grErrorSetCallback(fnc);
}
// grFlush submits all batched Glide work; grFinish also waits for the GPU.
DG3_API(void) dg3_grFlush(void) {
    if (!open())
        return;
    RenderDrawTriangles();
    glFlush();
}
DG3_API(void) dg3_grFinish(void) {
    if (!open())
        return;
    RenderDrawTriangles();
    glFinish();
}
DG3_API(Dg3Context)
dg3_grSstWinOpen(FxU32 hWnd, FxI32 screen_resolution, FxI32 refresh_rate, FxI32 color_format,
                 FxI32 origin_location, int nColBuffers, int nAuxBuffers) {
    dg3::u32 width, height;
    if (!OpenGL.GlideInit || !dg3::resolution_size(dg3::u32(screen_resolution), width, height) ||
        (origin_location != GR_ORIGIN_UPPER_LEFT && origin_location != GR_ORIGIN_LOWER_LEFT) ||
        color_format < GR_COLORFORMAT_ARGB || color_format > GR_COLORFORMAT_BGRA ||
        nColBuffers < 1 || nColBuffers > 3 || nAuxBuffers < 0 || nAuxBuffers > 1)
        return 0;
    g.open = false;
    if (!grSstWinOpen(hWnd, screen_resolution, refresh_rate, color_format, origin_location,
                      nColBuffers, nAuxBuffers))
        return 0;
    g.open = true;
    // Glide 3 default state differs from the Glide 2 core's texture defaults.
    grTexClampMode(GR_TMU0, GR_TEXTURECLAMP_CLAMP, GR_TEXTURECLAMP_CLAMP);
    grTexMipMapMode(GR_TMU0, GR_MIPMAP_DISABLE, FXFALSE);
    grTexFilterMode(GR_TMU0, GR_TEXTUREFILTER_POINT_SAMPLED, GR_TEXTUREFILTER_POINT_SAMPLED);
    grTexLodBiasValue(GR_TMU0, 0.f);
    grTexCombine(GR_TMU0, GR_COMBINE_FUNCTION_ZERO, GR_COMBINE_FACTOR_NONE,
                 GR_COMBINE_FUNCTION_ZERO, GR_COMBINE_FACTOR_NONE, FXFALSE, FXFALSE);
    // Glide 3 always supplies a TMU 1/w (Qn, else Q), never the FBI value.
    grHints(GR_HINT_STWHINT, GR_STWHINT_W_DIFF_TMU0);
    g.coords = dg3::kWindowCoords;
    g.fog_mode = dg3::kFogDisable;
    dg3::set_viewport(g.view, 0, 0, dg3::i32(width), dg3::i32(height));
    dg3::setup_reset(g.setup, false);
    for (unsigned t = 0; t < 2; ++t)
        g.s_scale[t] = g.t_scale[t] = 256.f;
    GlideMsg("DG_GLIDE3_OPEN_COMPLETE\n");
    return context();
}
DG3_API(FxBool) dg3_grSstWinClose(Dg3Context ctx) {
    if (ctx != context())
        return FXFALSE;
    if (g.open && OpenGL.WinOpen)
        grSstWinClose();
    g.open = false;
    dg3::setup_reset(g.setup, false);
    return FXTRUE;
}
// One context per board, as on Voodoo hardware; its handle stays stable
// across close/reopen and is selectable only while its window is open.
DG3_API(FxBool) dg3_grSelectContext(Dg3Context ctx) {
    return ctx == context() && open() ? FXTRUE : FXFALSE;
}
DG3_API(void) dg3_grSstOrigin(FxI32 origin) {
    if (open() && (origin == GR_ORIGIN_UPPER_LEFT || origin == GR_ORIGIN_LOWER_LEFT))
        grSstOrigin(origin);
}
DG3_API(void) dg3_grSstSelect(int which_sst) {
    if (which_sst == 0)
        grSstSelect(which_sst);
}
DG3_API(void) dg3_grSetNumPendingBuffers(FxI32 count) {
    g.pending_buffers = count > 0 ? dg3::u32(count) : 0;
}
DG3_API(void) dg3_grCheckForRoom(FxI32) {}
DG3_API(void) dg3_grSstConfigPipeline(FxU32, FxU32, FxU32) {}

// ---------------------------------------------------------- pixel pipeline
DG3_API(void) dg3_grAlphaBlendFunction(FxI32 rgb_sf, FxI32 rgb_df, FxI32 alpha_sf, FxI32 alpha_df) {
    for (FxI32 f : {rgb_sf, rgb_df, alpha_sf, alpha_df})
        if (f < GR_BLEND_ZERO || f > GR_BLEND_ALPHA_SATURATE)
            return;
    if (open())
        grAlphaBlendFunction(rgb_sf, rgb_df, alpha_sf, alpha_df);
}
DG3_API(void)
dg3_grAlphaCombine(FxI32 function, FxI32 factor, FxI32 local, FxI32 other, FxBool invert) {
    if (open() && combine_ok(function, factor) && local >= 0 && local <= 2 && other >= 0 &&
        other <= 2)
        grAlphaCombine(function, factor, local, other, invert);
}
DG3_API(void) dg3_grAlphaControlsITRGBLighting(FxBool enable) {
    if (open())
        grAlphaControlsITRGBLighting(enable);
}
DG3_API(void) dg3_grAlphaTestFunction(FxI32 function) {
    if (open() && function >= GR_CMP_NEVER && function <= GR_CMP_ALWAYS)
        grAlphaTestFunction(function);
}
DG3_API(void) dg3_grAlphaTestReferenceValue(FxU8 value) {
    if (open())
        grAlphaTestReferenceValue(value);
}
DG3_API(void) dg3_grChromakeyMode(FxI32 mode) {
    if (open() && (mode == GR_CHROMAKEY_DISABLE || mode == GR_CHROMAKEY_ENABLE))
        grChromakeyMode(mode);
}
DG3_API(void) dg3_grChromakeyValue(FxU32 value) {
    if (open())
        grChromakeyValue(value);
}
DG3_API(void) dg3_grClipWindow(FxU32 minx, FxU32 miny, FxU32 maxx, FxU32 maxy) {
    if (!open())
        return;
    maxx = maxx < Glide.WindowWidth ? maxx : Glide.WindowWidth;
    maxy = maxy < Glide.WindowHeight ? maxy : Glide.WindowHeight;
    grClipWindow(minx < maxx ? minx : maxx, miny < maxy ? miny : maxy, maxx, maxy);
}
DG3_API(void)
dg3_grColorCombine(FxI32 function, FxI32 factor, FxI32 local, FxI32 other, FxBool invert) {
    if (open() && combine_ok(function, factor) && local >= 0 && local <= 2 && other >= 0 &&
        other <= 2)
        grColorCombine(function, factor, local, other, invert);
}
DG3_API(void) dg3_grColorMask(FxBool rgb, FxBool a) {
    if (open())
        grColorMask(rgb, a);
}
DG3_API(void) dg3_grCullMode(FxI32 mode) {
    if (open() && mode >= GR_CULL_DISABLE && mode <= GR_CULL_POSITIVE)
        grCullMode(mode);
}
DG3_API(void) dg3_grConstantColorValue(FxU32 value) {
    if (open())
        grConstantColorValue(value);
}
DG3_API(void) dg3_grDepthBiasLevel(FxI32 level) {
    if (open())
        grDepthBiasLevel(FxI16(level < -32768 ? -32768 : level > 32767 ? 32767 : level));
}
DG3_API(void) dg3_grDepthBufferFunction(FxI32 function) {
    if (open() && function >= GR_CMP_NEVER && function <= GR_CMP_ALWAYS)
        grDepthBufferFunction(function);
}
DG3_API(void) dg3_grDepthBufferMode(FxI32 mode) {
    if (open() && mode >= GR_DEPTHBUFFER_DISABLE && mode <= GR_DEPTHBUFFER_WBUFFER_COMPARE_TO_BIAS)
        grDepthBufferMode(mode);
}
DG3_API(void) dg3_grDepthMask(FxBool mask) {
    if (open())
        grDepthMask(mask);
}
DG3_API(void) dg3_grDisableAllEffects(void) {
    if (!open())
        return;
    grDisableAllEffects();
    g.fog_mode = dg3::kFogDisable;
}
DG3_API(void) dg3_grDitherMode(FxI32 mode) {
    if (open() && mode >= GR_DITHER_DISABLE && mode <= GR_DITHER_4x4)
        grDitherMode(mode);
}
// Stippling is recorded for state queries only; no stipple is rasterized.
DG3_API(void) dg3_grStippleMode(FxI32 mode) {
    g.stipple_mode = dg3::u32(mode);
}
DG3_API(void) dg3_grStipplePattern(FxU32 pattern) {
    g.stipple_pattern = pattern;
}
DG3_API(void) dg3_grFogColorValue(FxU32 fogcolor) {
    if (open())
        grFogColorValue(fogcolor);
}
DG3_API(void) dg3_grFogMode(FxU32 mode) {
    dg3::u32 core_mode;
    if (!open() || !dg3::fog_mode(mode, core_mode))
        return;
    grFogMode(core_mode);
    g.fog_mode = mode;
}
DG3_API(void) dg3_grFogTable(const Dg3Fog *ft) {
    if (open() && ft)
        grFogTable(ft);
}
DG3_API(void) dg3_grLoadGammaTable(FxU32 nentries, FxU32 *red, FxU32 *green, FxU32 *blue) {
    if (!open() || !red || !green || !blue)
        return;
    struct {
        WORD red[256], green[256], blue[256];
    } ramp;
    GetGammaTable(&ramp);
    for (FxU32 i = 0; i < nentries && i < 256; ++i) {
        ramp.red[i] = WORD((red[i] & 0xff) * 0x101);
        ramp.green[i] = WORD((green[i] & 0xff) * 0x101);
        ramp.blue[i] = WORD((blue[i] & 0xff) * 0x101);
    }
    SetGammaTable(&ramp);
}
DG3_API(void) dg3_guGammaCorrectionRGB(FxFloat red, FxFloat green, FxFloat blue) {
    if (!open() || !(red > 0.f) || !(green > 0.f) || !(blue > 0.f))
        return;
    struct {
        WORD channel[3][256];
    } ramp;
    const float gamma[3] = {red, green, blue};
    for (unsigned c = 0; c < 3; ++c)
        for (unsigned i = 0; i < 256; ++i)
            ramp.channel[c][i] = WORD(WORD(0xffff * pow(i / 255.0, 1.0 / gamma[c])) & 0xff00);
    SetGammaTable(&ramp);
}
DG3_API(void) dg3_grSplash(float x, float y, float width, float height, FxU32 frame) {
    if (!open())
        return;
    // The core splash renders Glide 2 vertices with a shared TMU 1/w.
    const FxU32 hint = Glide.State.STWHint;
    grHints(GR_HINT_STWHINT, 0);
    grSplash(x, y, width, height, frame);
    grHints(GR_HINT_STWHINT, hint);
}

// ---------------------------------------------------------- queries, modes
DG3_API(FxU32) dg3_grGet(FxU32 pname, FxU32 plength, FxI32 *params) {
    dg3::Facts f;
    f.fb_bytes = fb_bytes();
    f.tmu_bytes = OpenGL.GlideInit ? dg3::u32(Glide.TextureMemory) : 0;
    f.state_bytes = sizeof(State3);
    f.layout_bytes = sizeof(dg3::Layout);
    f.viewport[0] = g.view.x;
    f.viewport[1] = g.view.y;
    f.viewport[2] = g.view.width;
    f.viewport[3] = g.view.height;
    f.points = g.points;
    f.lines = g.lines;
    f.triangles = g.triangles;
    f.pending_swaps = dg3::u32(grBufferNumPending());
    return dg3::get(pname, plength, params, f);
}
DG3_API(const char *) dg3_grGetString(FxU32 pname) {
    switch (pname) {
        case 0xa0:
            return " "; // GR_EXTENSION: no Glide 3 extensions are advertised
        case 0xa1:
            return "Voodoo Banshee (tm)";
        case 0xa2:
            return "Glide";
        case 0xa3:
            return "3Dfx Interactive";
        case 0xa4:
            return kVersion;
        default:
            return "ERROR";
    }
}
DG3_API(FxI32) dg3_grQueryResolutions(const Dg3Resolution *resTemplate, Dg3Resolution *output) {
    return dg3::query_resolutions(resTemplate, output, fb_bytes());
}
DG3_API(FxBool) dg3_grReset(FxU32 what) {
    switch (what) {
        case dg3::kStatsPoints:
            g.points = 0;
            return FXTRUE;
        case dg3::kStatsLines:
            g.lines = 0;
            return FXTRUE;
        case dg3::kStatsTriangles:
            g.triangles = 0;
            return FXTRUE;
        case dg3::kStatsPixels:
            return FXTRUE;
        case dg3::kVertexParameter:
            dg3::reset(g.layout);
            dg3::setup_reset(g.setup, false);
            return FXTRUE;
        default:
            return FXFALSE;
    }
}
DG3_API(Dg3Proc) dg3_grGetProcAddress(char *) {
    return nullptr; // no extension entry points are provided
}
// GR_AA_ORDERED, GR_ALLOW_MIPMAP_DITHER, GR_PASSTHRU and GR_VIDEO_SMOOTHING
// are recorded only. GR_SHAMELESS_PLUG maps to the core's plug state.
DG3_API(void) dg3_grEnable(FxU32 mode) {
    if (mode >= 1 && mode < 32)
        g.enables |= 1u << mode;
    if (mode == 0x04)
        grGlideShamelessPlug(FXTRUE);
}
DG3_API(void) dg3_grDisable(FxU32 mode) {
    if (mode >= 1 && mode < 32)
        g.enables &= ~(1u << mode);
    if (mode == 0x04)
        grGlideShamelessPlug(FXFALSE);
}
DG3_API(void) dg3_grCoordinateSpace(FxU32 mode) {
    if (mode == dg3::kWindowCoords || mode == dg3::kClipCoords)
        g.coords = mode;
}
DG3_API(void) dg3_grDepthRange(FxFloat n, FxFloat f) {
    dg3::set_depth_range(g.view, n, f);
}
DG3_API(void) dg3_grViewport(FxI32 x, FxI32 y, FxI32 width, FxI32 height) {
    dg3::set_viewport(g.view, x, y, width, height);
}

// ---------------------------------------------------------- textures
DG3_API(FxU32) dg3_grTexCalcMemRequired(FxI32 lodmin, FxI32 lodmax, FxI32 aspect, FxI32 fmt) {
    if (!dg3::tex_ok(lodmin, lodmax, aspect, fmt))
        return 0;
    return grTexCalcMemRequired(dg3::lod2(lodmin), dg3::lod2(lodmax), dg3::aspect2(aspect), fmt);
}
DG3_API(FxU32) dg3_grTexTextureMemRequired(FxU32 evenOdd, Dg3TexInfo *info) {
    dg3::TexInfo t;
    if (!even_odd_ok(evenOdd) || !tex_from(info, t))
        return 0;
    GrTexInfo core = glide2(t);
    return grTexTextureMemRequired(evenOdd, &core);
}
DG3_API(FxU32) dg3_grTexMinAddress(FxI32) {
    return 0;
}
// The largest valid start address of the single emulated TMU.
DG3_API(FxU32) dg3_grTexMaxAddress(FxI32 tmu) {
    const FxU32 total = OpenGL.GlideInit ? FxU32(Glide.TextureMemory) : 0;
    return tmu == GR_TMU0 && total >= dg3::kTextureAlignBytes ? total - dg3::kTextureAlignBytes : 0;
}
DG3_API(void) dg3_grTexNCCTable(FxU32 table) {
    if (open() && table <= GR_NCCTABLE_NCC1)
        grTexNCCTable(GR_TMU0, table);
}
DG3_API(void) dg3_grTexSource(FxI32 tmu, FxU32 startAddress, FxU32 evenOdd, Dg3TexInfo *info) {
    dg3::TexInfo t;
    if (!open() || tmu != GR_TMU0 || !even_odd_ok(evenOdd) || !tex_from(info, t))
        return;
    GrTexInfo core = glide2(t);
    grTexSource(GR_TMU0, startAddress, evenOdd, &core);
    dg3::tex_scale(t.aspect, g.s_scale[0], g.t_scale[0]);
}
DG3_API(void) dg3_grTexClampMode(FxI32 tmu, FxI32 s_clampmode, FxI32 t_clampmode) {
    // GR_TEXTURECLAMP_MIRROR_EXT (2) is not advertised; it wraps.
    auto mode = [](FxI32 m) { return m == GR_TEXTURECLAMP_CLAMP ? m : GR_TEXTURECLAMP_WRAP; };
    if (open() && tmu == GR_TMU0 && s_clampmode >= 0 && s_clampmode <= 2 && t_clampmode >= 0 &&
        t_clampmode <= 2)
        grTexClampMode(GR_TMU0, mode(s_clampmode), mode(t_clampmode));
}
DG3_API(void)
dg3_grTexCombine(FxI32 tmu, FxI32 rgb_function, FxI32 rgb_factor, FxI32 alpha_function,
                 FxI32 alpha_factor, FxBool rgb_invert, FxBool alpha_invert) {
    if (open() && tmu == GR_TMU0 && combine_ok(rgb_function, rgb_factor) &&
        combine_ok(alpha_function, alpha_factor))
        grTexCombine(GR_TMU0, rgb_function, rgb_factor, alpha_function, alpha_factor, rgb_invert,
                     alpha_invert);
}
DG3_API(void) dg3_grTexDetailControl(FxI32 tmu, int lod_bias, FxU8 detail_scale, float detail_max) {
    if (open() && tmu == GR_TMU0)
        grTexDetailControl(GR_TMU0, lod_bias, detail_scale, detail_max);
}
DG3_API(void) dg3_grTexFilterMode(FxI32 tmu, FxI32 minfilter_mode, FxI32 magfilter_mode) {
    if (open() && tmu == GR_TMU0 && minfilter_mode >= 0 && minfilter_mode <= 1 &&
        magfilter_mode >= 0 && magfilter_mode <= 1)
        grTexFilterMode(GR_TMU0, minfilter_mode, magfilter_mode);
}
DG3_API(void) dg3_grTexLodBiasValue(FxI32 tmu, float bias) {
    if (open() && tmu == GR_TMU0)
        grTexLodBiasValue(GR_TMU0, bias);
}
DG3_API(void)
dg3_grTexDownloadMipMap(FxI32 tmu, FxU32 startAddress, FxU32 evenOdd, Dg3TexInfo *info) {
    dg3::TexInfo t;
    if (!open() || tmu != GR_TMU0 || !even_odd_ok(evenOdd) || !tex_from(info, t) || !t.data)
        return;
    // The core rewrites its descriptor; the application's stays untouched.
    GrTexInfo core = glide2(t);
    grTexDownloadMipMap(GR_TMU0, startAddress, evenOdd, &core);
}
DG3_API(void)
dg3_grTexDownloadMipMapLevel(FxI32 tmu, FxU32 startAddress, FxI32 thisLod, FxI32 largeLod,
                             FxI32 aspectRatio, FxI32 format, FxU32 evenOdd, void *data) {
    if (!open() || tmu != GR_TMU0 || !even_odd_ok(evenOdd) || !data ||
        !dg3::tex_ok(thisLod, largeLod, aspectRatio, format))
        return;
    grTexDownloadMipMapLevel(GR_TMU0, startAddress, dg3::lod2(thisLod), dg3::lod2(largeLod),
                             dg3::aspect2(aspectRatio), format, evenOdd, data);
}
DG3_API(FxBool)
dg3_grTexDownloadMipMapLevelPartial(FxI32 tmu, FxU32 startAddress, FxI32 thisLod, FxI32 largeLod,
                                    FxI32 aspectRatio, FxI32 format, FxU32 evenOdd, void *data,
                                    int start, int end) {
    dg3::u32 width, rows;
    if (!open() || tmu != GR_TMU0 || !even_odd_ok(evenOdd) || !data ||
        !dg3::tex_ok(thisLod, largeLod, aspectRatio, format))
        return FXFALSE;
    dg3::level_size(thisLod, aspectRatio, width, rows);
    if (start < 0 || end < start || dg3::u32(end) >= rows)
        return FXFALSE;
    grTexDownloadMipMapLevelPartial(GR_TMU0, startAddress, dg3::lod2(thisLod), dg3::lod2(largeLod),
                                    dg3::aspect2(aspectRatio), format, evenOdd, data, start, end);
    return FXTRUE;
}
// GR_TEXTABLE_PALETTE_6666_EXT is not advertised. Its colours are accepted;
// the shared palette path keeps palette texels opaque.
static void download_palette(FxU32 type, const FxU32 *data, int start, int end) {
    if (type == GR_TEXTABLE_PALETTE) {
        grTexDownloadTablePartial(GR_TMU0, GR_TEXTABLE_PALETTE, const_cast<FxU32 *>(data), start,
                                  end);
        return;
    }
    FxU32 converted[256] = {};
    for (int i = start; i <= end; ++i)
        converted[i] = dg3::palette6666(data[i]);
    grTexDownloadTablePartial(GR_TMU0, GR_TEXTABLE_PALETTE, converted, start, end);
}
DG3_API(void) dg3_grTexDownloadTable(FxU32 type, void *data) {
    if (!open() || !data)
        return;
    if (type == GR_TEXTABLE_NCC0 || type == GR_TEXTABLE_NCC1)
        grTexDownloadTable(GR_TMU0, type, data);
    else if (type == GR_TEXTABLE_PALETTE || type == kPalette6666)
        download_palette(type, static_cast<const FxU32 *>(data), 0, 255);
}
DG3_API(void) dg3_grTexDownloadTablePartial(FxU32 type, void *data, int start, int end) {
    if (!open() || !data)
        return;
    if (type == GR_TEXTABLE_NCC0 || type == GR_TEXTABLE_NCC1) {
        // NCC tables are only ever loaded whole (entries 0..11).
        if (start == 0 && end == 11)
            grTexDownloadTable(GR_TMU0, type, data);
    } else if ((type == GR_TEXTABLE_PALETTE || type == kPalette6666) && start >= 0 &&
               start <= end && end <= 255)
        download_palette(type, static_cast<const FxU32 *>(data), start, end);
}
DG3_API(void) dg3_grTexMipMapMode(FxI32 tmu, FxI32 mode, FxBool lodBlend) {
    if (open() && tmu == GR_TMU0 && mode >= GR_MIPMAP_DISABLE && mode <= GR_MIPMAP_NEAREST_DITHER)
        grTexMipMapMode(GR_TMU0, mode, lodBlend);
}
// Multibase addressing is accepted and validated; the core samples the base.
DG3_API(void) dg3_grTexMultibase(FxI32 tmu, FxBool enable) {
    if (open() && tmu == GR_TMU0)
        grTexMultibase(GR_TMU0, enable);
}
DG3_API(void)
dg3_grTexMultibaseAddress(FxI32 tmu, FxU32 range, FxU32 startAddress, FxU32 evenOdd,
                          Dg3TexInfo *info) {
    dg3::TexInfo t;
    if (!open() || tmu != GR_TMU0 || !even_odd_ok(evenOdd) || !tex_from(info, t))
        return;
    GrTexInfo core = glide2(t);
    grTexMultibaseAddress(GR_TMU0, range, startAddress, evenOdd, &core);
}

// ---------------------------------------------------------- frame buffer
DG3_API(FxBool)
dg3_grLfbLock(FxI32 type, FxI32 buffer, FxI32 writeMode, FxI32 origin, FxBool pixelPipeline,
              Dg3LfbInfo *info) {
    // Read locks expose the buffer's own 16-bit format; writeMode is a
    // write-lock parameter and must not change a read lock's stride.
    const bool write = type & GR_LFB_WRITE_ONLY;
    if (!open() || !info || !lfb_buffer_ok(buffer) ||
        (type & ~FxI32(GR_LFB_WRITE_ONLY | GR_LFB_NOIDLE)) ||
        (origin != GR_ORIGIN_UPPER_LEFT && origin != GR_ORIGIN_LOWER_LEFT &&
         origin != GR_ORIGIN_ANY) ||
        (write && writeMode != GR_LFBWRITEMODE_ANY && !lfb_bytes(writeMode)))
        return FXFALSE;
    return grLfbLock(GrLock_t(type), buffer, write ? writeMode : GR_LFBWRITEMODE_ANY, origin,
                     pixelPipeline, reinterpret_cast<GrLfbInfo_t *>(info));
}
DG3_API(FxBool) dg3_grLfbUnlock(FxI32 type, FxI32 buffer) {
    if (!open() || !lfb_buffer_ok(buffer))
        return FXFALSE;
    return grLfbUnlock(GrLock_t(type), buffer);
}
DG3_API(void) dg3_grLfbConstantAlpha(FxU8 alpha) {
    if (open())
        grLfbConstantAlpha(alpha);
}
DG3_API(void) dg3_grLfbConstantDepth(FxU32 depth) {
    if (open())
        grLfbConstantDepth(FxU16(depth > 0xffff ? 0xffff : depth));
}
DG3_API(void) dg3_grLfbWriteColorSwizzle(FxBool swizzleBytes, FxBool swapWords) {
    if (open())
        grLfbWriteColorSwizzle(swizzleBytes, swapWords);
}
DG3_API(void) dg3_grLfbWriteColorFormat(FxI32 colorFormat) {
    if (open())
        grLfbWriteColorFormat(colorFormat);
}
// Glide 3 adds pixelPipeline; it reaches the core lock, which applies the
// clip rectangle. Pixels outside the frame buffer are discarded.
DG3_API(FxBool)
dg3_grLfbWriteRegion(FxI32 dst_buffer, FxU32 dst_x, FxU32 dst_y, FxI32 src_format, FxU32 src_width,
                     FxU32 src_height, FxBool pixelPipeline, FxI32 src_stride, void *src_data) {
    const unsigned bytes = lfb_bytes(src_format);
    if (!open() || !src_data || !bytes || !lfb_buffer_ok(dst_buffer))
        return FXFALSE;
    if (dst_x >= Glide.WindowWidth || dst_y >= Glide.WindowHeight || !src_width || !src_height)
        return FXTRUE;
    const FxU32 width =
        src_width < Glide.WindowWidth - dst_x ? src_width : Glide.WindowWidth - dst_x;
    const FxU32 height =
        src_height < Glide.WindowHeight - dst_y ? src_height : Glide.WindowHeight - dst_y;
    GrLfbInfo_t info;
    info.size = sizeof(info);
    if (!grLfbLock(GR_LFB_WRITE_ONLY | GR_LFB_NOIDLE, dst_buffer, src_format, GR_ORIGIN_UPPER_LEFT,
                   pixelPipeline, &info))
        return FXFALSE;
    const dg3::u8 *source = static_cast<const dg3::u8 *>(src_data);
    dg3::u8 *target = static_cast<dg3::u8 *>(info.lfbPtr);
    for (FxU32 row = 0; row < height; ++row)
        memcpy(target + size_t(dst_y + row) * info.strideInBytes + size_t(dst_x) * bytes,
               source + ptrdiff_t(row) * src_stride, size_t(width) * bytes);
    grLfbUnlock(GR_LFB_WRITE_ONLY, dst_buffer);
    return FXTRUE;
}
// Reads 16-bit pixels; the region must lie inside the frame buffer.
DG3_API(FxBool)
dg3_grLfbReadRegion(FxI32 src_buffer, FxU32 src_x, FxU32 src_y, FxU32 src_width, FxU32 src_height,
                    FxU32 dst_stride, void *dst_data) {
    if (!open() || !dst_data || !lfb_buffer_ok(src_buffer) || src_x > Glide.WindowWidth ||
        src_y > Glide.WindowHeight || src_width > Glide.WindowWidth - src_x ||
        src_height > Glide.WindowHeight - src_y || dst_stride < src_width * 2)
        return FXFALSE;
    if (!src_width || !src_height)
        return FXTRUE;
    return grLfbReadRegion(src_buffer, src_x, src_y, src_width, src_height, dst_stride, dst_data);
}

// ---------------------------------------------------------- library state
DG3_API(void) dg3_grGlideInit(void) {
    grGlideInit();
    reset_provider();
}
DG3_API(void) dg3_grGlideShutdown(void) {
    grGlideShutdown();
    g.open = false;
}
DG3_API(void) dg3_grGlideGetState(void *state) {
    if (!state)
        return;
    State3 s;
    memset(&s, 0, sizeof(s));
    s.magic = kStateMagic;
    s.version = kStateVersion;
    grGlideGetState(reinterpret_cast<GrState *>(&s.core));
    s.flags = dg3::capture_flags(Glide, OpenGL);
    memcpy(s.fog_table, Glide.FogTable, sizeof(s.fog_table));
    s.layout = g.layout;
    s.view = g.view;
    s.coords = g.coords;
    s.fog_mode = g.fog_mode;
    s.enables = g.enables;
    s.stipple_mode = g.stipple_mode;
    s.stipple_pattern = g.stipple_pattern;
    for (unsigned t = 0; t < 2; ++t) {
        s.s_scale[t] = g.s_scale[t];
        s.t_scale[t] = g.t_scale[t];
    }
    memcpy(state, &s, sizeof(s));
}
DG3_API(void) dg3_grGlideSetState(const void *state) {
    if (!open() || !state)
        return;
    State3 s;
    memcpy(&s, state, sizeof(s));
    dg3::u32 core_fog;
    if (s.magic != kStateMagic || s.version != kStateVersion || !dg3::valid(s.layout) ||
        !dg3::flags_ok(s.flags) || s.coords > dg3::kClipCoords ||
        !dg3::fog_mode(s.fog_mode, core_fog))
        return;
    // The core replays its Glide 2 state (and flushes batched triangles);
    // the order-derived combine flags and the rest are then put back exactly.
    grGlideSetState(reinterpret_cast<const GrState *>(&s.core));
    grFogMode(core_fog);
    grFogTable(s.fog_table);
    grHints(GR_HINT_STWHINT, s.core.STWHint);
    dg3::restore_flags(s.flags, Glide, OpenGL);
    g.layout = s.layout;
    g.view = s.view;
    g.coords = s.coords;
    g.fog_mode = s.fog_mode;
    g.enables = s.enables;
    g.stipple_mode = s.stipple_mode;
    g.stipple_pattern = s.stipple_pattern;
    for (unsigned t = 0; t < 2; ++t) {
        g.s_scale[t] = s.s_scale[t];
        g.t_scale[t] = s.t_scale[t];
    }
}
DG3_API(void) dg3_grGlideGetVertexLayout(void *layout) {
    if (layout)
        memcpy(layout, &g.layout, sizeof(g.layout));
}
DG3_API(void) dg3_grGlideSetVertexLayout(const void *layout) {
    if (!layout)
        return;
    dg3::Layout l;
    memcpy(&l, layout, sizeof(l));
    if (dg3::valid(l))
        g.layout = l;
}
DG3_API(void) dg3_grGlideGetVersion(char *version) {
    if (version)
        memcpy(version, "Glide Version 3.01", sizeof("Glide Version 3.01"));
}

// ---------------------------------------------------------- utilities
DG3_API(float) dg3_guFogTableIndexToW(int i) {
    return guFogTableIndexToW(i);
}
DG3_API(void) dg3_guFogGenerateExp(Dg3Fog *fogtable, float density) {
    if (fogtable)
        guFogGenerateExp(fogtable, density);
}
DG3_API(void) dg3_guFogGenerateExp2(Dg3Fog *fogtable, float density) {
    if (fogtable)
        guFogGenerateExp2(fogtable, density);
}
DG3_API(void) dg3_guFogGenerateLinear(Dg3Fog *fogtable, float nearZ, float farZ) {
    if (fogtable)
        guFogGenerateLinear(fogtable, nearZ, farZ);
}
static void df3_info(const dg3::Df3 &h, Dg3Gu3dfInfo &info) {
    info.header.width = h.width;
    info.header.height = h.height;
    info.header.small_lod = h.small_lod;
    info.header.large_lod = h.large_lod;
    info.header.aspect_ratio = h.aspect;
    info.header.format = h.format;
    info.mem_required = h.mem_required;
}
DG3_API(FxBool) dg3_gu3dfGetInfo(const char *filename, Dg3Gu3dfInfo *info) {
    FileBytes file;
    dg3::Df3 h;
    if (!info || !read_file(filename, file) || !dg3::parse_3df(file.data, file.size, h))
        return FXFALSE;
    df3_info(h, *info);
    return FXTRUE;
}
// Loads at most info->mem_required bytes, as reported by gu3dfGetInfo.
DG3_API(FxBool) dg3_gu3dfLoad(const char *filename, Dg3Gu3dfInfo *info) {
    FileBytes file;
    dg3::Df3 h;
    if (!info || !info->data || !read_file(filename, file) ||
        !dg3::parse_3df(file.data, file.size, h))
        return FXFALSE;
    dg3::Df3Table table;
    memcpy(&table, info->table, sizeof(table));
    if (!dg3::load_3df(file.data, file.size, h, table, static_cast<dg3::u8 *>(info->data),
                       info->mem_required))
        return FXFALSE;
    memcpy(info->table, &table, sizeof(table));
    df3_info(h, *info);
    return FXTRUE;
}
