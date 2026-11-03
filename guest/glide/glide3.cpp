// SPDX-License-Identifier: GPL-2.0-or-later
// DreamGPU Glide 3.x provider (glide3x.dll) on the shared OpenGLide core.
// Glide 3 vertex layouts, coordinate spaces, contexts, queries and texture
// descriptors are translated here; rasterization, texturing, LFB and
// presentation stay in the same audited OpenGLide renderer as glide2x.dll.
// The donor g3wrap.cpp is not linked into this provider.
// Standard headers precede OpenGLide's platform min/max macros.
#include <initializer_list>
#include <math.h>
#include <new>
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

// DREAMGPU_GLIDE3_TRACE=<file> logs every Glide 3 call with its arguments
// (guest/glide/README.md). It is read once, at the first call; unset, each
// call costs one branch. Glide is single-threaded, as is this log. Lines are
// buffered and written at each swap, flush, finish, close and shutdown.
constexpr dg3::u32 kTraceLimit = 64u << 20;
struct Trace {
    HANDLE file = nullptr;
    dg3::u32 sequence = 0, start = 0, used = 0, written = 0;
    dg3::trace::Line line;
    char buffer[64 * 1024];
};
// Heap-allocated only while tracing: globals of this DLL are file-backed.
Trace *trace_state;
bool trace_checked;
void trace_flush() {
    Trace *t = trace_state;
    DWORD wrote = 0;
    if (t && t->used)
        WriteFile(t->file, t->buffer, t->used, &wrote, nullptr);
    if (t)
        t->used = 0;
}
void trace_raw(const char *text, dg3::u32 bytes) {
    if (!trace_state || trace_state->written >= kTraceLimit)
        return;
    Trace &t = *trace_state;
    static const char full[] = "# trace limit (64 MiB) reached\r\n";
    if (bytes > kTraceLimit - t.written - (sizeof(full) - 1)) {
        text = full;
        bytes = sizeof(full) - 1;
        t.written = kTraceLimit;
    } else
        t.written += bytes;
    if (bytes > sizeof(t.buffer) - t.used)
        trace_flush();
    memcpy(t.buffer + t.used, text, bytes);
    t.used += bytes;
}
// "# <label> <path> bytes=<size>": identifies exactly which copies loaded.
void trace_module(const char *label, HMODULE module) {
    dg3::trace::Line &l = trace_state->line;
    l.n = 0;
    l.str("# ");
    l.str(label);
    l.put(' ');
    char path[MAX_PATH];
    const DWORD n = module ? GetModuleFileNameA(module, path, sizeof(path)) : 0;
    if (n && n < sizeof(path)) {
        path[n] = 0;
        l.str(path);
        HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, 0, nullptr);
        if (f != INVALID_HANDLE_VALUE) {
            l.str(" bytes=");
            l.udec(GetFileSize(f, nullptr));
            CloseHandle(f);
        }
    } else
        l.str("(not loaded)");
    l.str("\r\n");
    trace_raw(l.text, l.n);
}
void trace_open() {
    trace_checked = true;
    char path[MAX_PATH];
    const DWORD n = GetEnvironmentVariableA("DREAMGPU_GLIDE3_TRACE", path, sizeof(path));
    if (!n || n >= sizeof(path))
        return;
    void *memory = HeapAlloc(GetProcessHeap(), 0, sizeof(Trace));
    if (!memory)
        return;
    HANDLE f = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {
        HeapFree(GetProcessHeap(), 0, memory);
        return;
    }
    trace_state = new (memory) Trace;
    Trace &t = *trace_state;
    t.file = f;
    t.start = GetTickCount();
    static const char header[] =
        "# DreamGPU glide3x call trace v1: <sequence> <ms> call(arguments)\r\n";
    trace_raw(header, sizeof(header) - 1);
    MEMORY_BASIC_INFORMATION self;
    trace_module("glide3x",
                 VirtualQuery(reinterpret_cast<const void *>(&trace_open), &self, sizeof(self))
                     ? HMODULE(self.AllocationBase)
                     : nullptr);
    trace_module("dgpugl", GetModuleHandleA("dgpugl.dll"));
    trace_module("process", GetModuleHandleA(nullptr));
    dg3::trace::Line &l = t.line;
    l.n = 0;
    l.str("# windows version=");
    l.hex(GetVersion());
    l.str("\r\n");
    trace_raw(l.text, l.n);
    trace_flush();
}
dg3::trace::Line *trace_call(const char *name) {
    if (!trace_checked)
        trace_open();
    Trace *t = trace_state;
    if (!t || t->written >= kTraceLimit)
        return nullptr;
    t->line.begin(++t->sequence, GetTickCount() - t->start, name);
    return &t->line;
}
void trace_commit(dg3::trace::Line &l) {
    trace_raw(l.text, l.end());
}
// Writes what is still buffered when the process unloads the provider.
struct TraceTail {
    ~TraceTail() {
        trace_flush();
    }
} trace_tail;

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
    if (auto *t = trace_call("grDrawPoint"))
        trace_commit(t->vertex("pt", g.layout, pt));
    if (!open() || !pt)
        return;
    dg3::Vertex2 v;
    dg3::convert(g.layout, space(), pt, v);
    CoreSink sink;
    sink.point(v);
    g.setup.count = 0;
}
DG3_API(void) dg3_grDrawLine(const void *v1, const void *v2) {
    if (auto *t = trace_call("grDrawLine"))
        trace_commit(t->vertex("v1", g.layout, v1).vertex("v2", g.layout, v2));
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
    if (auto *t = trace_call("grDrawTriangle"))
        trace_commit(t->vertex("a", g.layout, a).vertex("b", g.layout, b).vertex("c", g.layout, c));
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
dg3_grAADrawTriangle(const void *a, const void *b, const void *c, FxBool ab_antialias,
                     FxBool bc_antialias, FxBool ca_antialias) {
    if (auto *t = trace_call("grAADrawTriangle"))
        trace_commit(t->p("a", a)
                         .p("b", b)
                         .p("c", c)
                         .u("ab_antialias", ab_antialias)
                         .u("bc_antialias", bc_antialias)
                         .u("ca_antialias", ca_antialias));
    // Edge antialiasing is not emulated; the triangle itself is exact.
    dg3_grDrawTriangle(a, b, c);
}
DG3_API(void) dg3_grVertexLayout(FxU32 param, FxI32 offset, FxU32 mode) {
    if (auto *t = trace_call("grVertexLayout"))
        trace_commit(t->x("param", param).i("offset", offset).u("mode", mode));
    if (dg3::set_param(g.layout, param, offset, mode) == dg3::SetResult::bad_offset)
        GlideMsg("DG_GLIDE3 rejected vertex layout 0x%x offset %d\n", param, offset);
}
DG3_API(void) dg3_grDrawVertexArray(FxU32 mode, FxU32 count, void *pointers) {
    if (auto *t = trace_call("grDrawVertexArray"))
        trace_commit(t->x("mode", mode)
                         .u("count", count)
                         .p("pointers", pointers)
                         .array(g.layout, pointers, count));
    dg3::PointerSource source;
    if (!open() || !dg3::pointer_source(pointers, count, source))
        return;
    CoreSink sink;
    dg3::draw(mode, count, g.layout, space(), g.setup, source, sink);
}
DG3_API(void)
dg3_grDrawVertexArrayContiguous(FxU32 mode, FxU32 count, void *pointers, FxU32 stride) {
    if (auto *t = trace_call("grDrawVertexArrayContiguous"))
        trace_commit(t->x("mode", mode)
                         .u("count", count)
                         .p("pointers", pointers)
                         .u("stride", stride)
                         .strided(g.layout, pointers, count, stride));
    dg3::StrideSource source;
    if (!open() || !dg3::stride_source(pointers, count, stride, g.layout, source))
        return;
    CoreSink sink;
    dg3::draw(mode, count, g.layout, space(), g.setup, source, sink);
}

// ---------------------------------------------------------- buffers, context
DG3_API(void) dg3_grBufferClear(FxU32 color, FxU8 alpha, FxU32 depth) {
    if (auto *t = trace_call("grBufferClear"))
        trace_commit(t->x("color", color).u("alpha", alpha).u("depth", depth));
    if (open())
        grBufferClear(color, alpha, FxU16(depth > 0xffff ? 0xffff : depth));
}
static void grBufferSwap_body(FxU32 swap_interval) {
    if (open())
        grBufferSwap(int(swap_interval & 0xff));
}
DG3_API(void) dg3_grBufferSwap(FxU32 swap_interval) {
    if (auto *t = trace_call("grBufferSwap"))
        trace_commit(t->u("swap_interval", swap_interval));
    grBufferSwap_body(swap_interval);
    trace_flush();
}
DG3_API(void) dg3_grRenderBuffer(FxI32 buffer) {
    if (auto *t = trace_call("grRenderBuffer"))
        trace_commit(t->i("buffer", buffer));
    if (open() && (buffer == GR_BUFFER_FRONTBUFFER || buffer == GR_BUFFER_BACKBUFFER))
        grRenderBuffer(buffer);
}
DG3_API(void) dg3_grErrorSetCallback(Dg3ErrorCallback fnc) {
    if (auto *t = trace_call("grErrorSetCallback"))
        trace_commit(t->x("fnc", dg3::u32(uintptr_t(fnc))));
    grErrorSetCallback(fnc);
}
// grFlush submits all batched Glide work; grFinish also waits for the GPU.
static void grFlush_body(void) {
    if (!open())
        return;
    RenderDrawTriangles();
    glFlush();
}
DG3_API(void) dg3_grFlush(void) {
    if (auto *t = trace_call("grFlush"))
        trace_commit(*t);
    grFlush_body();
    trace_flush();
}
static void grFinish_body(void) {
    if (!open())
        return;
    RenderDrawTriangles();
    glFinish();
}
DG3_API(void) dg3_grFinish(void) {
    if (auto *t = trace_call("grFinish"))
        trace_commit(*t);
    grFinish_body();
    trace_flush();
}
static Dg3Context grSstWinOpen_body(FxU32 hWnd, FxI32 screen_resolution, FxI32 refresh_rate,
                                    FxI32 color_format, FxI32 origin_location, int nColBuffers,
                                    int nAuxBuffers) {
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
DG3_API(Dg3Context)
dg3_grSstWinOpen(FxU32 hWnd, FxI32 screen_resolution, FxI32 refresh_rate, FxI32 color_format,
                 FxI32 origin_location, int nColBuffers, int nAuxBuffers) {
    const Dg3Context result = grSstWinOpen_body(hWnd, screen_resolution, refresh_rate, color_format,
                                                origin_location, nColBuffers, nAuxBuffers);
    if (auto *t = trace_call("grSstWinOpen"))
        trace_commit(t->x("hWnd", hWnd)
                         .i("screen_resolution", screen_resolution)
                         .i("refresh_rate", refresh_rate)
                         .i("color_format", color_format)
                         .i("origin_location", origin_location)
                         .i("nColBuffers", nColBuffers)
                         .i("nAuxBuffers", nAuxBuffers)
                         .x("result", result)
                         .u("width", Glide.WindowWidth)
                         .u("height", Glide.WindowHeight));
    return result;
}
static FxBool grSstWinClose_body(Dg3Context ctx) {
    if (ctx != context())
        return FXFALSE;
    if (g.open && OpenGL.WinOpen)
        grSstWinClose();
    g.open = false;
    dg3::setup_reset(g.setup, false);
    return FXTRUE;
}
DG3_API(FxBool) dg3_grSstWinClose(Dg3Context ctx) {
    const FxBool result = grSstWinClose_body(ctx);
    if (auto *t = trace_call("grSstWinClose"))
        trace_commit(t->x("ctx", ctx).u("result", result));
    trace_flush();
    return result;
}
// One context per board, as on Voodoo hardware; its handle stays stable
// across close/reopen and is selectable only while its window is open.
static FxBool grSelectContext_body(Dg3Context ctx) {
    return ctx == context() && open() ? FXTRUE : FXFALSE;
}
DG3_API(FxBool) dg3_grSelectContext(Dg3Context ctx) {
    const FxBool result = grSelectContext_body(ctx);
    if (auto *t = trace_call("grSelectContext"))
        trace_commit(t->x("ctx", ctx).u("result", result));
    return result;
}
DG3_API(void) dg3_grSstOrigin(FxI32 origin) {
    if (auto *t = trace_call("grSstOrigin"))
        trace_commit(t->i("origin", origin));
    if (open() && (origin == GR_ORIGIN_UPPER_LEFT || origin == GR_ORIGIN_LOWER_LEFT))
        grSstOrigin(origin);
}
DG3_API(void) dg3_grSstSelect(int which_sst) {
    if (auto *t = trace_call("grSstSelect"))
        trace_commit(t->i("which_sst", which_sst));
    if (which_sst == 0)
        grSstSelect(which_sst);
}
DG3_API(void) dg3_grSetNumPendingBuffers(FxI32 count) {
    if (auto *t = trace_call("grSetNumPendingBuffers"))
        trace_commit(t->i("count", count));
    g.pending_buffers = count > 0 ? dg3::u32(count) : 0;
}
DG3_API(void) dg3_grCheckForRoom(FxI32 n) {
    if (auto *t = trace_call("grCheckForRoom"))
        trace_commit(t->i("n", n));
}
DG3_API(void) dg3_grSstConfigPipeline(FxU32 device, FxU32 setting, FxU32 value) {
    if (auto *t = trace_call("grSstConfigPipeline"))
        trace_commit(t->x("device", device).x("setting", setting).x("value", value));
}

// ---------------------------------------------------------- pixel pipeline
DG3_API(void) dg3_grAlphaBlendFunction(FxI32 rgb_sf, FxI32 rgb_df, FxI32 alpha_sf, FxI32 alpha_df) {
    if (auto *t = trace_call("grAlphaBlendFunction"))
        trace_commit(t->i("rgb_sf", rgb_sf)
                         .i("rgb_df", rgb_df)
                         .i("alpha_sf", alpha_sf)
                         .i("alpha_df", alpha_df));
    for (FxI32 f : {rgb_sf, rgb_df, alpha_sf, alpha_df})
        if (f < GR_BLEND_ZERO || f > GR_BLEND_ALPHA_SATURATE)
            return;
    if (open())
        grAlphaBlendFunction(rgb_sf, rgb_df, alpha_sf, alpha_df);
}
DG3_API(void)
dg3_grAlphaCombine(FxI32 function, FxI32 factor, FxI32 local, FxI32 other, FxBool invert) {
    if (auto *t = trace_call("grAlphaCombine"))
        trace_commit(t->i("function", function)
                         .i("factor", factor)
                         .i("local", local)
                         .i("other", other)
                         .u("invert", invert));
    if (open() && combine_ok(function, factor) && local >= 0 && local <= 2 && other >= 0 &&
        other <= 2)
        grAlphaCombine(function, factor, local, other, invert);
}
DG3_API(void) dg3_grAlphaControlsITRGBLighting(FxBool enable) {
    if (auto *t = trace_call("grAlphaControlsITRGBLighting"))
        trace_commit(t->u("enable", enable));
    if (open())
        grAlphaControlsITRGBLighting(enable);
}
DG3_API(void) dg3_grAlphaTestFunction(FxI32 function) {
    if (auto *t = trace_call("grAlphaTestFunction"))
        trace_commit(t->i("function", function));
    if (open() && function >= GR_CMP_NEVER && function <= GR_CMP_ALWAYS)
        grAlphaTestFunction(function);
}
DG3_API(void) dg3_grAlphaTestReferenceValue(FxU8 value) {
    if (auto *t = trace_call("grAlphaTestReferenceValue"))
        trace_commit(t->x("value", value));
    if (open())
        grAlphaTestReferenceValue(value);
}
DG3_API(void) dg3_grChromakeyMode(FxI32 mode) {
    if (auto *t = trace_call("grChromakeyMode"))
        trace_commit(t->i("mode", mode));
    if (open() && (mode == GR_CHROMAKEY_DISABLE || mode == GR_CHROMAKEY_ENABLE))
        grChromakeyMode(mode);
}
DG3_API(void) dg3_grChromakeyValue(FxU32 value) {
    if (auto *t = trace_call("grChromakeyValue"))
        trace_commit(t->x("value", value));
    if (open())
        grChromakeyValue(value);
}
DG3_API(void) dg3_grClipWindow(FxU32 minx, FxU32 miny, FxU32 maxx, FxU32 maxy) {
    if (auto *t = trace_call("grClipWindow"))
        trace_commit(t->u("minx", minx).u("miny", miny).u("maxx", maxx).u("maxy", maxy));
    if (!open())
        return;
    maxx = maxx < Glide.WindowWidth ? maxx : Glide.WindowWidth;
    maxy = maxy < Glide.WindowHeight ? maxy : Glide.WindowHeight;
    grClipWindow(minx < maxx ? minx : maxx, miny < maxy ? miny : maxy, maxx, maxy);
}
DG3_API(void)
dg3_grColorCombine(FxI32 function, FxI32 factor, FxI32 local, FxI32 other, FxBool invert) {
    if (auto *t = trace_call("grColorCombine"))
        trace_commit(t->i("function", function)
                         .i("factor", factor)
                         .i("local", local)
                         .i("other", other)
                         .u("invert", invert));
    if (open() && combine_ok(function, factor) && local >= 0 && local <= 2 && other >= 0 &&
        other <= 2)
        grColorCombine(function, factor, local, other, invert);
}
DG3_API(void) dg3_grColorMask(FxBool rgb, FxBool a) {
    if (auto *t = trace_call("grColorMask"))
        trace_commit(t->u("rgb", rgb).u("a", a));
    if (open())
        grColorMask(rgb, a);
}
DG3_API(void) dg3_grCullMode(FxI32 mode) {
    if (auto *t = trace_call("grCullMode"))
        trace_commit(t->i("mode", mode));
    if (open() && mode >= GR_CULL_DISABLE && mode <= GR_CULL_POSITIVE)
        grCullMode(mode);
}
DG3_API(void) dg3_grConstantColorValue(FxU32 value) {
    if (auto *t = trace_call("grConstantColorValue"))
        trace_commit(t->x("value", value));
    if (open())
        grConstantColorValue(value);
}
DG3_API(void) dg3_grDepthBiasLevel(FxI32 level) {
    if (auto *t = trace_call("grDepthBiasLevel"))
        trace_commit(t->i("level", level));
    if (open())
        grDepthBiasLevel(FxI16(level < -32768 ? -32768 : level > 32767 ? 32767 : level));
}
DG3_API(void) dg3_grDepthBufferFunction(FxI32 function) {
    if (auto *t = trace_call("grDepthBufferFunction"))
        trace_commit(t->i("function", function));
    if (open() && function >= GR_CMP_NEVER && function <= GR_CMP_ALWAYS)
        grDepthBufferFunction(function);
}
DG3_API(void) dg3_grDepthBufferMode(FxI32 mode) {
    if (auto *t = trace_call("grDepthBufferMode"))
        trace_commit(t->i("mode", mode));
    if (open() && mode >= GR_DEPTHBUFFER_DISABLE && mode <= GR_DEPTHBUFFER_WBUFFER_COMPARE_TO_BIAS)
        grDepthBufferMode(mode);
}
DG3_API(void) dg3_grDepthMask(FxBool mask) {
    if (auto *t = trace_call("grDepthMask"))
        trace_commit(t->u("mask", mask));
    if (open())
        grDepthMask(mask);
}
DG3_API(void) dg3_grDisableAllEffects(void) {
    if (auto *t = trace_call("grDisableAllEffects"))
        trace_commit(*t);
    if (!open())
        return;
    grDisableAllEffects();
    g.fog_mode = dg3::kFogDisable;
}
DG3_API(void) dg3_grDitherMode(FxI32 mode) {
    if (auto *t = trace_call("grDitherMode"))
        trace_commit(t->i("mode", mode));
    if (open() && mode >= GR_DITHER_DISABLE && mode <= GR_DITHER_4x4)
        grDitherMode(mode);
}
// Stippling is recorded for state queries only; no stipple is rasterized.
DG3_API(void) dg3_grStippleMode(FxI32 mode) {
    if (auto *t = trace_call("grStippleMode"))
        trace_commit(t->i("mode", mode));
    g.stipple_mode = dg3::u32(mode);
}
DG3_API(void) dg3_grStipplePattern(FxU32 pattern) {
    if (auto *t = trace_call("grStipplePattern"))
        trace_commit(t->x("pattern", pattern));
    g.stipple_pattern = pattern;
}
DG3_API(void) dg3_grFogColorValue(FxU32 fogcolor) {
    if (auto *t = trace_call("grFogColorValue"))
        trace_commit(t->x("fogcolor", fogcolor));
    if (open())
        grFogColorValue(fogcolor);
}
DG3_API(void) dg3_grFogMode(FxU32 mode) {
    if (auto *t = trace_call("grFogMode"))
        trace_commit(t->u("mode", mode));
    dg3::u32 core_mode;
    if (!open() || !dg3::fog_mode(mode, core_mode))
        return;
    grFogMode(core_mode);
    g.fog_mode = mode;
}
DG3_API(void) dg3_grFogTable(const Dg3Fog *ft) {
    if (auto *t = trace_call("grFogTable"))
        trace_commit(t->p("ft", ft).blob(ft, GR_FOG_TABLE_SIZE));
    if (open() && ft)
        grFogTable(ft);
}
DG3_API(void) dg3_grLoadGammaTable(FxU32 nentries, FxU32 *red, FxU32 *green, FxU32 *blue) {
    if (auto *t = trace_call("grLoadGammaTable"))
        trace_commit(t->u("nentries", nentries)
                         .p("red", red)
                         .p("green", green)
                         .p("blue", blue)
                         .gamma(nentries, red, green, blue));
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
    if (auto *t = trace_call("guGammaCorrectionRGB"))
        trace_commit(t->f("red", red).f("green", green).f("blue", blue));
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
    if (auto *t = trace_call("grSplash"))
        trace_commit(
            t->f("x", x).f("y", y).f("width", width).f("height", height).u("frame", frame));
    if (!open())
        return;
    // The core splash renders Glide 2 vertices with a shared TMU 1/w.
    const FxU32 hint = Glide.State.STWHint;
    grHints(GR_HINT_STWHINT, 0);
    grSplash(x, y, width, height, frame);
    grHints(GR_HINT_STWHINT, hint);
}

// ---------------------------------------------------------- queries, modes
static FxU32 grGet_body(FxU32 pname, FxU32 plength, FxI32 *params) {
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
DG3_API(FxU32) dg3_grGet(FxU32 pname, FxU32 plength, FxI32 *params) {
    const FxU32 result = grGet_body(pname, plength, params);
    if (auto *t = trace_call("grGet"))
        trace_commit(t->x("pname", pname)
                         .u("plength", plength)
                         .p("params", params)
                         .u("result", result)
                         .words("values", reinterpret_cast<const dg3::u32 *>(params),
                                params ? (result < 32 ? result : 32) / 4 : 0));
    return result;
}
static const char *grGetString_body(FxU32 pname) {
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
DG3_API(const char *) dg3_grGetString(FxU32 pname) {
    const char *const result = grGetString_body(pname);
    if (auto *t = trace_call("grGetString"))
        trace_commit(t->x("pname", pname).s("result", result));
    return result;
}
static FxI32 grQueryResolutions_body(const Dg3Resolution *resTemplate, Dg3Resolution *output) {
    return dg3::query_resolutions(resTemplate, output, fb_bytes());
}
DG3_API(FxI32) dg3_grQueryResolutions(const Dg3Resolution *resTemplate, Dg3Resolution *output) {
    const FxI32 result = grQueryResolutions_body(resTemplate, output);
    if (auto *t = trace_call("grQueryResolutions"))
        trace_commit(t->p("resTemplate", resTemplate).p("output", output).i("result", result));
    return result;
}
static FxBool grReset_body(FxU32 what) {
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
DG3_API(FxBool) dg3_grReset(FxU32 what) {
    const FxBool result = grReset_body(what);
    if (auto *t = trace_call("grReset"))
        trace_commit(t->x("what", what).u("result", result));
    return result;
}
static Dg3Proc grGetProcAddress_body(char *) {
    return nullptr; // no extension entry points are provided
}
DG3_API(Dg3Proc) dg3_grGetProcAddress(char *procName) {
    const Dg3Proc result = grGetProcAddress_body(procName);
    if (auto *t = trace_call("grGetProcAddress"))
        trace_commit(t->s("procName", procName).x("result", dg3::u32(uintptr_t(result))));
    return result;
}
// GR_AA_ORDERED, GR_ALLOW_MIPMAP_DITHER, GR_PASSTHRU and GR_VIDEO_SMOOTHING
// are recorded only. GR_SHAMELESS_PLUG maps to the core's plug state.
DG3_API(void) dg3_grEnable(FxU32 mode) {
    if (auto *t = trace_call("grEnable"))
        trace_commit(t->u("mode", mode));
    if (mode >= 1 && mode < 32)
        g.enables |= 1u << mode;
    if (mode == 0x04)
        grGlideShamelessPlug(FXTRUE);
}
DG3_API(void) dg3_grDisable(FxU32 mode) {
    if (auto *t = trace_call("grDisable"))
        trace_commit(t->u("mode", mode));
    if (mode >= 1 && mode < 32)
        g.enables &= ~(1u << mode);
    if (mode == 0x04)
        grGlideShamelessPlug(FXFALSE);
}
DG3_API(void) dg3_grCoordinateSpace(FxU32 mode) {
    if (auto *t = trace_call("grCoordinateSpace"))
        trace_commit(t->u("mode", mode));
    if (mode == dg3::kWindowCoords || mode == dg3::kClipCoords)
        g.coords = mode;
}
DG3_API(void) dg3_grDepthRange(FxFloat n, FxFloat f) {
    if (auto *t = trace_call("grDepthRange"))
        trace_commit(t->f("n", n).f("f", f));
    dg3::set_depth_range(g.view, n, f);
}
DG3_API(void) dg3_grViewport(FxI32 x, FxI32 y, FxI32 width, FxI32 height) {
    if (auto *t = trace_call("grViewport"))
        trace_commit(t->i("x", x).i("y", y).i("width", width).i("height", height));
    dg3::set_viewport(g.view, x, y, width, height);
}

// ---------------------------------------------------------- textures
static FxU32 grTexCalcMemRequired_body(FxI32 lodmin, FxI32 lodmax, FxI32 aspect, FxI32 fmt) {
    if (!dg3::tex_ok(lodmin, lodmax, aspect, fmt))
        return 0;
    return grTexCalcMemRequired(dg3::lod2(lodmin), dg3::lod2(lodmax), dg3::aspect2(aspect), fmt);
}
DG3_API(FxU32) dg3_grTexCalcMemRequired(FxI32 lodmin, FxI32 lodmax, FxI32 aspect, FxI32 fmt) {
    const FxU32 result = grTexCalcMemRequired_body(lodmin, lodmax, aspect, fmt);
    if (auto *t = trace_call("grTexCalcMemRequired"))
        trace_commit(t->i("lodmin", lodmin)
                         .i("lodmax", lodmax)
                         .i("aspect", aspect)
                         .x("fmt", fmt)
                         .u("result", result));
    return result;
}
static FxU32 grTexTextureMemRequired_body(FxU32 evenOdd, Dg3TexInfo *info) {
    dg3::TexInfo t;
    if (!even_odd_ok(evenOdd) || !tex_from(info, t))
        return 0;
    GrTexInfo core = glide2(t);
    return grTexTextureMemRequired(evenOdd, &core);
}
DG3_API(FxU32) dg3_grTexTextureMemRequired(FxU32 evenOdd, Dg3TexInfo *info) {
    const FxU32 result = grTexTextureMemRequired_body(evenOdd, info);
    if (auto *t = trace_call("grTexTextureMemRequired"))
        trace_commit(t->x("evenOdd", evenOdd).tex("info", info).u("result", result));
    return result;
}
static FxU32 grTexMinAddress_body(FxI32) {
    return 0;
}
DG3_API(FxU32) dg3_grTexMinAddress(FxI32 tmu) {
    const FxU32 result = grTexMinAddress_body(tmu);
    if (auto *t = trace_call("grTexMinAddress"))
        trace_commit(t->i("tmu", tmu).u("result", result));
    return result;
}
// The largest valid start address of the single emulated TMU.
static FxU32 grTexMaxAddress_body(FxI32 tmu) {
    const FxU32 total = OpenGL.GlideInit ? FxU32(Glide.TextureMemory) : 0;
    return tmu == GR_TMU0 && total >= dg3::kTextureAlignBytes ? total - dg3::kTextureAlignBytes : 0;
}
DG3_API(FxU32) dg3_grTexMaxAddress(FxI32 tmu) {
    const FxU32 result = grTexMaxAddress_body(tmu);
    if (auto *t = trace_call("grTexMaxAddress"))
        trace_commit(t->i("tmu", tmu).u("result", result));
    return result;
}
DG3_API(void) dg3_grTexNCCTable(FxU32 table) {
    if (auto *t = trace_call("grTexNCCTable"))
        trace_commit(t->x("table", table));
    if (open() && table <= GR_NCCTABLE_NCC1)
        grTexNCCTable(GR_TMU0, table);
}
DG3_API(void) dg3_grTexSource(FxI32 tmu, FxU32 startAddress, FxU32 evenOdd, Dg3TexInfo *info) {
    if (auto *t = trace_call("grTexSource"))
        trace_commit(t->i("tmu", tmu)
                         .x("startAddress", startAddress)
                         .x("evenOdd", evenOdd)
                         .tex("info", info));
    dg3::TexInfo t;
    if (!open() || tmu != GR_TMU0 || !even_odd_ok(evenOdd) || !tex_from(info, t))
        return;
    GrTexInfo core = glide2(t);
    grTexSource(GR_TMU0, startAddress, evenOdd, &core);
    dg3::tex_scale(t.aspect, g.s_scale[0], g.t_scale[0]);
}
DG3_API(void) dg3_grTexClampMode(FxI32 tmu, FxI32 s_clampmode, FxI32 t_clampmode) {
    if (auto *t = trace_call("grTexClampMode"))
        trace_commit(t->i("tmu", tmu).i("s_clampmode", s_clampmode).i("t_clampmode", t_clampmode));
    // GR_TEXTURECLAMP_MIRROR_EXT (2) is not advertised; it wraps.
    auto mode = [](FxI32 m) { return m == GR_TEXTURECLAMP_CLAMP ? m : GR_TEXTURECLAMP_WRAP; };
    if (open() && tmu == GR_TMU0 && s_clampmode >= 0 && s_clampmode <= 2 && t_clampmode >= 0 &&
        t_clampmode <= 2)
        grTexClampMode(GR_TMU0, mode(s_clampmode), mode(t_clampmode));
}
DG3_API(void)
dg3_grTexCombine(FxI32 tmu, FxI32 rgb_function, FxI32 rgb_factor, FxI32 alpha_function,
                 FxI32 alpha_factor, FxBool rgb_invert, FxBool alpha_invert) {
    if (auto *t = trace_call("grTexCombine"))
        trace_commit(t->i("tmu", tmu)
                         .i("rgb_function", rgb_function)
                         .i("rgb_factor", rgb_factor)
                         .i("alpha_function", alpha_function)
                         .i("alpha_factor", alpha_factor)
                         .u("rgb_invert", rgb_invert)
                         .u("alpha_invert", alpha_invert));
    if (open() && tmu == GR_TMU0 && combine_ok(rgb_function, rgb_factor) &&
        combine_ok(alpha_function, alpha_factor))
        grTexCombine(GR_TMU0, rgb_function, rgb_factor, alpha_function, alpha_factor, rgb_invert,
                     alpha_invert);
}
DG3_API(void) dg3_grTexDetailControl(FxI32 tmu, int lod_bias, FxU8 detail_scale, float detail_max) {
    if (auto *t = trace_call("grTexDetailControl"))
        trace_commit(t->i("tmu", tmu)
                         .i("lod_bias", lod_bias)
                         .u("detail_scale", detail_scale)
                         .f("detail_max", detail_max));
    if (open() && tmu == GR_TMU0)
        grTexDetailControl(GR_TMU0, lod_bias, detail_scale, detail_max);
}
DG3_API(void) dg3_grTexFilterMode(FxI32 tmu, FxI32 minfilter_mode, FxI32 magfilter_mode) {
    if (auto *t = trace_call("grTexFilterMode"))
        trace_commit(t->i("tmu", tmu)
                         .i("minfilter_mode", minfilter_mode)
                         .i("magfilter_mode", magfilter_mode));
    if (open() && tmu == GR_TMU0 && minfilter_mode >= 0 && minfilter_mode <= 1 &&
        magfilter_mode >= 0 && magfilter_mode <= 1)
        grTexFilterMode(GR_TMU0, minfilter_mode, magfilter_mode);
}
DG3_API(void) dg3_grTexLodBiasValue(FxI32 tmu, float bias) {
    if (auto *t = trace_call("grTexLodBiasValue"))
        trace_commit(t->i("tmu", tmu).f("bias", bias));
    if (open() && tmu == GR_TMU0)
        grTexLodBiasValue(GR_TMU0, bias);
}
DG3_API(void)
dg3_grTexDownloadMipMap(FxI32 tmu, FxU32 startAddress, FxU32 evenOdd, Dg3TexInfo *info) {
    if (auto *t = trace_call("grTexDownloadMipMap"))
        trace_commit(t->i("tmu", tmu)
                         .x("startAddress", startAddress)
                         .x("evenOdd", evenOdd)
                         .tex("info", info)
                         .texels(info));
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
    if (auto *t = trace_call("grTexDownloadMipMapLevel"))
        trace_commit(t->i("tmu", tmu)
                         .x("startAddress", startAddress)
                         .i("thisLod", thisLod)
                         .i("largeLod", largeLod)
                         .i("aspectRatio", aspectRatio)
                         .x("format", format)
                         .x("evenOdd", evenOdd)
                         .p("data", data)
                         .level(thisLod, aspectRatio, format, data, 0, -1));
    if (!open() || tmu != GR_TMU0 || !even_odd_ok(evenOdd) || !data ||
        !dg3::tex_ok(thisLod, largeLod, aspectRatio, format))
        return;
    grTexDownloadMipMapLevel(GR_TMU0, startAddress, dg3::lod2(thisLod), dg3::lod2(largeLod),
                             dg3::aspect2(aspectRatio), format, evenOdd, data);
}
static FxBool grTexDownloadMipMapLevelPartial_body(FxI32 tmu, FxU32 startAddress, FxI32 thisLod,
                                                   FxI32 largeLod, FxI32 aspectRatio, FxI32 format,
                                                   FxU32 evenOdd, void *data, int start, int end) {
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
DG3_API(FxBool)
dg3_grTexDownloadMipMapLevelPartial(FxI32 tmu, FxU32 startAddress, FxI32 thisLod, FxI32 largeLod,
                                    FxI32 aspectRatio, FxI32 format, FxU32 evenOdd, void *data,
                                    int start, int end) {
    const FxBool result = grTexDownloadMipMapLevelPartial_body(
        tmu, startAddress, thisLod, largeLod, aspectRatio, format, evenOdd, data, start, end);
    if (auto *t = trace_call("grTexDownloadMipMapLevelPartial"))
        trace_commit(t->i("tmu", tmu)
                         .x("startAddress", startAddress)
                         .i("thisLod", thisLod)
                         .i("largeLod", largeLod)
                         .i("aspectRatio", aspectRatio)
                         .x("format", format)
                         .x("evenOdd", evenOdd)
                         .p("data", data)
                         .i("start", start)
                         .i("end", end)
                         .level(thisLod, aspectRatio, format, data, start, end)
                         .u("result", result));
    return result;
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
    if (auto *t = trace_call("grTexDownloadTable"))
        trace_commit(t->x("type", type).p("data", data).table(type, data, 0, 255));
    if (!open() || !data)
        return;
    if (type == GR_TEXTABLE_NCC0 || type == GR_TEXTABLE_NCC1)
        grTexDownloadTable(GR_TMU0, type, data);
    else if (type == GR_TEXTABLE_PALETTE || type == kPalette6666)
        download_palette(type, static_cast<const FxU32 *>(data), 0, 255);
}
DG3_API(void) dg3_grTexDownloadTablePartial(FxU32 type, void *data, int start, int end) {
    if (auto *t = trace_call("grTexDownloadTablePartial"))
        trace_commit(t->x("type", type)
                         .p("data", data)
                         .i("start", start)
                         .i("end", end)
                         .table(type, data, start, end));
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
    if (auto *t = trace_call("grTexMipMapMode"))
        trace_commit(t->i("tmu", tmu).i("mode", mode).u("lodBlend", lodBlend));
    if (open() && tmu == GR_TMU0 && mode >= GR_MIPMAP_DISABLE && mode <= GR_MIPMAP_NEAREST_DITHER)
        grTexMipMapMode(GR_TMU0, mode, lodBlend);
}
// Multibase addressing is accepted and validated; the core samples the base.
DG3_API(void) dg3_grTexMultibase(FxI32 tmu, FxBool enable) {
    if (auto *t = trace_call("grTexMultibase"))
        trace_commit(t->i("tmu", tmu).u("enable", enable));
    if (open() && tmu == GR_TMU0)
        grTexMultibase(GR_TMU0, enable);
}
DG3_API(void)
dg3_grTexMultibaseAddress(FxI32 tmu, FxU32 range, FxU32 startAddress, FxU32 evenOdd,
                          Dg3TexInfo *info) {
    if (auto *t = trace_call("grTexMultibaseAddress"))
        trace_commit(t->i("tmu", tmu)
                         .x("range", range)
                         .x("startAddress", startAddress)
                         .x("evenOdd", evenOdd)
                         .tex("info", info));
    dg3::TexInfo t;
    if (!open() || tmu != GR_TMU0 || !even_odd_ok(evenOdd) || !tex_from(info, t))
        return;
    GrTexInfo core = glide2(t);
    grTexMultibaseAddress(GR_TMU0, range, startAddress, evenOdd, &core);
}

// ---------------------------------------------------------- frame buffer
static FxBool grLfbLock_body(FxI32 type, FxI32 buffer, FxI32 writeMode, FxI32 origin,
                             FxBool pixelPipeline, Dg3LfbInfo *info) {
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
DG3_API(FxBool)
dg3_grLfbLock(FxI32 type, FxI32 buffer, FxI32 writeMode, FxI32 origin, FxBool pixelPipeline,
              Dg3LfbInfo *info) {
    const FxBool result = grLfbLock_body(type, buffer, writeMode, origin, pixelPipeline, info);
    if (auto *t = trace_call("grLfbLock"))
        trace_commit(t->x("type", type)
                         .i("buffer", buffer)
                         .x("writeMode", writeMode)
                         .i("origin", origin)
                         .u("pixelPipeline", pixelPipeline)
                         .p("info", info)
                         .u("result", result)
                         .lfb(result ? info : nullptr));
    return result;
}
static FxBool grLfbUnlock_body(FxI32 type, FxI32 buffer) {
    if (!open() || !lfb_buffer_ok(buffer))
        return FXFALSE;
    return grLfbUnlock(GrLock_t(type), buffer);
}
DG3_API(FxBool) dg3_grLfbUnlock(FxI32 type, FxI32 buffer) {
    const FxBool result = grLfbUnlock_body(type, buffer);
    if (auto *t = trace_call("grLfbUnlock"))
        trace_commit(t->x("type", type).i("buffer", buffer).u("result", result));
    return result;
}
DG3_API(void) dg3_grLfbConstantAlpha(FxU8 alpha) {
    if (auto *t = trace_call("grLfbConstantAlpha"))
        trace_commit(t->u("alpha", alpha));
    if (open())
        grLfbConstantAlpha(alpha);
}
DG3_API(void) dg3_grLfbConstantDepth(FxU32 depth) {
    if (auto *t = trace_call("grLfbConstantDepth"))
        trace_commit(t->u("depth", depth));
    if (open())
        grLfbConstantDepth(FxU16(depth > 0xffff ? 0xffff : depth));
}
DG3_API(void) dg3_grLfbWriteColorSwizzle(FxBool swizzleBytes, FxBool swapWords) {
    if (auto *t = trace_call("grLfbWriteColorSwizzle"))
        trace_commit(t->u("swizzleBytes", swizzleBytes).u("swapWords", swapWords));
    if (open())
        grLfbWriteColorSwizzle(swizzleBytes, swapWords);
}
DG3_API(void) dg3_grLfbWriteColorFormat(FxI32 colorFormat) {
    if (auto *t = trace_call("grLfbWriteColorFormat"))
        trace_commit(t->x("colorFormat", colorFormat));
    if (open())
        grLfbWriteColorFormat(colorFormat);
}
// Glide 3 adds pixelPipeline; it reaches the core lock, which applies the
// clip rectangle. Pixels outside the frame buffer are discarded.
static FxBool grLfbWriteRegion_body(FxI32 dst_buffer, FxU32 dst_x, FxU32 dst_y, FxI32 src_format,
                                    FxU32 src_width, FxU32 src_height, FxBool pixelPipeline,
                                    FxI32 src_stride, void *src_data) {
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
DG3_API(FxBool)
dg3_grLfbWriteRegion(FxI32 dst_buffer, FxU32 dst_x, FxU32 dst_y, FxI32 src_format, FxU32 src_width,
                     FxU32 src_height, FxBool pixelPipeline, FxI32 src_stride, void *src_data) {
    const FxBool result = grLfbWriteRegion_body(dst_buffer, dst_x, dst_y, src_format, src_width,
                                                src_height, pixelPipeline, src_stride, src_data);
    if (auto *t = trace_call("grLfbWriteRegion"))
        trace_commit(t->i("dst_buffer", dst_buffer)
                         .u("dst_x", dst_x)
                         .u("dst_y", dst_y)
                         .x("src_format", src_format)
                         .u("src_width", src_width)
                         .u("src_height", src_height)
                         .u("pixelPipeline", pixelPipeline)
                         .i("src_stride", src_stride)
                         .p("src_data", src_data)
                         .u("result", result));
    return result;
}
// Reads 16-bit pixels; the region must lie inside the frame buffer.
static FxBool grLfbReadRegion_body(FxI32 src_buffer, FxU32 src_x, FxU32 src_y, FxU32 src_width,
                                   FxU32 src_height, FxU32 dst_stride, void *dst_data) {
    if (!open() || !dst_data || !lfb_buffer_ok(src_buffer) || src_x > Glide.WindowWidth ||
        src_y > Glide.WindowHeight || src_width > Glide.WindowWidth - src_x ||
        src_height > Glide.WindowHeight - src_y || dst_stride < src_width * 2)
        return FXFALSE;
    if (!src_width || !src_height)
        return FXTRUE;
    return grLfbReadRegion(src_buffer, src_x, src_y, src_width, src_height, dst_stride, dst_data);
}
DG3_API(FxBool)
dg3_grLfbReadRegion(FxI32 src_buffer, FxU32 src_x, FxU32 src_y, FxU32 src_width, FxU32 src_height,
                    FxU32 dst_stride, void *dst_data) {
    const FxBool result =
        grLfbReadRegion_body(src_buffer, src_x, src_y, src_width, src_height, dst_stride, dst_data);
    if (auto *t = trace_call("grLfbReadRegion"))
        trace_commit(t->i("src_buffer", src_buffer)
                         .u("src_x", src_x)
                         .u("src_y", src_y)
                         .u("src_width", src_width)
                         .u("src_height", src_height)
                         .u("dst_stride", dst_stride)
                         .p("dst_data", dst_data)
                         .u("result", result));
    return result;
}

// ---------------------------------------------------------- library state
DG3_API(void) dg3_grGlideInit(void) {
    if (auto *t = trace_call("grGlideInit"))
        trace_commit(*t);
    grGlideInit();
    reset_provider();
}
static void grGlideShutdown_body(void) {
    grGlideShutdown();
    g.open = false;
}
DG3_API(void) dg3_grGlideShutdown(void) {
    if (auto *t = trace_call("grGlideShutdown"))
        trace_commit(*t);
    grGlideShutdown_body();
    trace_flush();
}
DG3_API(void) dg3_grGlideGetState(void *state) {
    if (auto *t = trace_call("grGlideGetState"))
        trace_commit(t->p("state", state));
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
    if (auto *t = trace_call("grGlideSetState"))
        trace_commit(t->p("state", state).blob(state, sizeof(State3)));
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
    if (auto *t = trace_call("grGlideGetVertexLayout"))
        trace_commit(t->p("layout", layout));
    if (layout)
        memcpy(layout, &g.layout, sizeof(g.layout));
}
DG3_API(void) dg3_grGlideSetVertexLayout(const void *layout) {
    if (auto *t = trace_call("grGlideSetVertexLayout"))
        trace_commit(t->p("layout", layout).blob(layout, sizeof(dg3::Layout)));
    if (!layout)
        return;
    dg3::Layout l;
    memcpy(&l, layout, sizeof(l));
    if (dg3::valid(l))
        g.layout = l;
}
DG3_API(void) dg3_grGlideGetVersion(char *version) {
    if (auto *t = trace_call("grGlideGetVersion"))
        trace_commit(t->p("version", version));
    if (version)
        memcpy(version, "Glide Version 3.01", sizeof("Glide Version 3.01"));
}

// ---------------------------------------------------------- utilities
static float guFogTableIndexToW_body(int i) {
    return guFogTableIndexToW(i);
}
DG3_API(float) dg3_guFogTableIndexToW(int i) {
    const float result = guFogTableIndexToW_body(i);
    if (auto *t = trace_call("guFogTableIndexToW"))
        trace_commit(t->i("i", i).f("result", result));
    return result;
}
DG3_API(void) dg3_guFogGenerateExp(Dg3Fog *fogtable, float density) {
    if (auto *t = trace_call("guFogGenerateExp"))
        trace_commit(t->p("fogtable", fogtable).f("density", density));
    if (fogtable)
        guFogGenerateExp(fogtable, density);
}
DG3_API(void) dg3_guFogGenerateExp2(Dg3Fog *fogtable, float density) {
    if (auto *t = trace_call("guFogGenerateExp2"))
        trace_commit(t->p("fogtable", fogtable).f("density", density));
    if (fogtable)
        guFogGenerateExp2(fogtable, density);
}
DG3_API(void) dg3_guFogGenerateLinear(Dg3Fog *fogtable, float nearZ, float farZ) {
    if (auto *t = trace_call("guFogGenerateLinear"))
        trace_commit(t->p("fogtable", fogtable).f("nearZ", nearZ).f("farZ", farZ));
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
static FxBool gu3dfGetInfo_body(const char *filename, Dg3Gu3dfInfo *info) {
    FileBytes file;
    dg3::Df3 h;
    if (!info || !read_file(filename, file) || !dg3::parse_3df(file.data, file.size, h))
        return FXFALSE;
    df3_info(h, *info);
    return FXTRUE;
}
DG3_API(FxBool) dg3_gu3dfGetInfo(const char *filename, Dg3Gu3dfInfo *info) {
    const FxBool result = gu3dfGetInfo_body(filename, info);
    if (auto *t = trace_call("gu3dfGetInfo"))
        trace_commit(t->s("filename", filename).p("info", info).u("result", result));
    return result;
}
// Loads at most info->mem_required bytes, as reported by gu3dfGetInfo.
static FxBool gu3dfLoad_body(const char *filename, Dg3Gu3dfInfo *info) {
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
DG3_API(FxBool) dg3_gu3dfLoad(const char *filename, Dg3Gu3dfInfo *info) {
    const FxBool result = gu3dfLoad_body(filename, info);
    if (auto *t = trace_call("gu3dfLoad"))
        trace_commit(t->s("filename", filename).p("info", info).u("result", result));
    return result;
}
