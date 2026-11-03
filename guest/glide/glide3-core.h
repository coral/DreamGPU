// SPDX-License-Identifier: GPL-2.0-or-later
// DreamGPU Glide 3.x front end: vertex layouts, primitive assembly, queries,
// texture descriptors and .3df parsing. This header has no GL or Win32
// dependency so the exact production logic also runs under host sanitizers.
// Constant values are the public Glide 3 SDK ABI; no 3dfx source is copied.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace dg3 {
using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using i32 = int32_t;

// grVertexLayout parameters and modes.
constexpr u32 kParamXY = 0x01, kParamZ = 0x02, kParamW = 0x03, kParamQ = 0x04, kParamFogExt = 0x05,
              kParamA = 0x10, kParamRGB = 0x20, kParamPARGB = 0x30, kParamST0 = 0x40,
              kParamST1 = 0x41, kParamQ0 = 0x50, kParamQ1 = 0x51;
constexpr u32 kParamDisable = 0, kParamEnable = 1;
// grDrawVertexArray(Contiguous) primitive modes.
constexpr u32 kPoints = 0, kLineStrip = 1, kLines = 2, kPolygon = 3, kTriangleStrip = 4,
              kTriangleFan = 5, kTriangles = 6, kTriangleStripContinue = 7,
              kTriangleFanContinue = 8;
constexpr u32 kWindowCoords = 0, kClipCoords = 1;

// Internal parameter slots. XY is always read from vertex offset zero, as
// the Glide 3 setup code does; its recorded layout entry is informational.
enum Slot : unsigned { kXY, kZ, kW, kQ, kFog, kA, kRGB, kPARGB, kST0, kST1, kQ0, kQ1, kSlots };
constexpr u32 kSlotBytes[kSlots] = {8, 4, 4, 4, 4, 4, 12, 4, 8, 8, 4, 4};
// Largest vertex extent DreamGPU reads. Every enabled parameter must lie
// entirely inside [0, kMaxVertexBytes); larger or negative offsets are refused.
constexpr i32 kMaxVertexBytes = 1024;
constexpr u32 kLayoutMagic = 0x4c334744u; // "DG3L"
constexpr u32 kLayoutVersion = 1;

inline bool slot_of(u32 param, unsigned &slot) {
    switch (param) {
        case kParamXY:
            slot = kXY;
            return true;
        case kParamZ:
            slot = kZ;
            return true;
        case kParamW:
            slot = kW;
            return true;
        case kParamQ:
            slot = kQ;
            return true;
        case kParamFogExt:
            slot = kFog;
            return true;
        case kParamA:
            slot = kA;
            return true;
        case kParamRGB:
            slot = kRGB;
            return true;
        case kParamPARGB:
            slot = kPARGB;
            return true;
        case kParamST0:
            slot = kST0;
            return true;
        case kParamST1:
            slot = kST1;
            return true;
        case kParamQ0:
            slot = kQ0;
            return true;
        case kParamQ1:
            slot = kQ1;
            return true;
        default:
            return false;
    }
}

struct Param {
    i32 offset;
    u32 enabled;
};
// The opaque grGlideGetVertexLayout blob. packed selects PARGB colour data;
// like Glide 3 it follows the most recently *enabled* colour parameter.
struct Layout {
    u32 magic, version;
    Param param[kSlots];
    u32 packed;
};

inline void reset(Layout &l) {
    memset(&l, 0, sizeof(l));
    l.magic = kLayoutMagic;
    l.version = kLayoutVersion;
}
inline bool offset_ok(unsigned slot, i32 offset) {
    return slot < kSlots && offset >= 0 && offset <= kMaxVertexBytes - i32(kSlotBytes[slot]);
}

enum class SetResult { applied, unknown_param, bad_offset };
// grVertexLayout. An enable with an out-of-range offset disables the slot, so
// no later draw can read outside the declared vertex extent.
inline SetResult set_param(Layout &l, u32 param, i32 offset, u32 mode) {
    unsigned s;
    if (!slot_of(param, s))
        return SetResult::unknown_param;
    const bool enable = mode == kParamEnable;
    if (!offset_ok(s, offset)) {
        l.param[s].offset = 0;
        l.param[s].enabled = 0;
        return enable ? SetResult::bad_offset : SetResult::applied;
    }
    l.param[s].offset = offset;
    l.param[s].enabled = enable ? 1 : 0;
    if (enable && (s == kA || s == kRGB))
        l.packed = 0;
    else if (enable && s == kPARGB)
        l.packed = 1;
    return SetResult::applied;
}
// Validates an application-supplied grGlideSetVertexLayout blob.
inline bool valid(const Layout &l) {
    if (l.magic != kLayoutMagic || l.version != kLayoutVersion || l.packed > 1)
        return false;
    for (unsigned s = 0; s < kSlots; ++s)
        if (l.param[s].enabled > 1 || (l.param[s].enabled && !offset_ok(s, l.param[s].offset)))
            return false;
    return true;
}
// Bytes read from each vertex by the current layout.
inline u32 extent(const Layout &l) {
    u32 bytes = kSlotBytes[kXY];
    for (unsigned s = kZ; s < kSlots; ++s)
        if (l.param[s].enabled && u32(l.param[s].offset) + kSlotBytes[s] > bytes)
            bytes = u32(l.param[s].offset) + kSlotBytes[s];
    return bytes;
}

// Byte-for-byte the OpenGLide Glide 2 GrVertex (two TMU vertices).
struct Vertex2 {
    float x, y, z, r, g, b, ooz, a, oow;
    struct {
        float sow, tow, oow;
    } tmu[2];
};
static_assert(sizeof(Vertex2) == 60, "Glide 2 GrVertex layout");

struct Viewport {
    i32 x, y, width, height;
    float ox, oy, hwidth, hheight;
    float n, f, oz, hdepth;
};
inline void set_viewport(Viewport &v, i32 x, i32 y, i32 width, i32 height) {
    v.x = x;
    v.y = y;
    v.width = width;
    v.height = height;
    v.ox = float(x) + float(width) * 0.5f;
    v.oy = float(y) + float(height) * 0.5f;
    v.hwidth = float(width) * 0.5f;
    v.hheight = float(height) * 0.5f;
}
inline void set_depth_range(Viewport &v, float n, float f) {
    v.n = n;
    v.f = f;
    v.hdepth = (f - n) * 0.5f * 65535.f;
    v.oz = (f + n) * 0.5f * 65535.f;
}

// Per-draw conversion context.
struct Space {
    u32 coords;
    Viewport view;
    float s_scale[2], t_scale[2]; // clip-space texture scale per TMU
    bool wbuffer;                 // depth comes from the FBI W iterator
};

inline float load(const u8 *v, i32 offset) {
    float f;
    memcpy(&f, v + offset, sizeof(f));
    return f;
}
inline float read(const Layout &l, const u8 *v, unsigned s, unsigned component, float fallback) {
    return l.param[s].enabled ? load(v, l.param[s].offset + i32(component * 4)) : fallback;
}

// Converts one caller vertex to the Glide 2 vertex consumed by the shared
// OpenGLide renderer. Disabled parameters are never read; they take the
// documented defaults below instead of whatever bytes happen to be present.
//
// Window coordinates (Glide 3 setup unit):
//   ooz = Z; FBI 1/w = FOG_EXT if enabled, else Q; TMU n 1/w = Qn, else Q.
//   With Q disabled DreamGPU uses W, else 1 (affine).
//   W-buffering needs the FBI iterator for depth, so it keeps Q there.
// Clip coordinates: oow = 1/W (W disabled -> w = 1), x/y/z use the viewport
//   and depth range, ST are scaled by oow and the texture's aspect scale,
//   float colours are 0..1 and become 0..255.
inline void convert(const Layout &l, const Space &sp, const void *vertex, Vertex2 &o) {
    const u8 *v = static_cast<const u8 *>(vertex);
    const bool clip = sp.coords == kClipCoords;
    memset(&o, 0, sizeof(o));
    float r, g, b, a;
    if (l.packed) {
        if (l.param[kPARGB].enabled) {
            u32 c;
            memcpy(&c, v + l.param[kPARGB].offset, sizeof(c));
            a = float(c >> 24);
            r = float((c >> 16) & 0xff);
            g = float((c >> 8) & 0xff);
            b = float(c & 0xff);
        } else
            r = g = b = a = 255.f;
    } else {
        const float unit = clip ? 1.f : 255.f, scale = clip ? 255.f : 1.f;
        r = read(l, v, kRGB, 0, unit) * scale;
        g = read(l, v, kRGB, 1, unit) * scale;
        b = read(l, v, kRGB, 2, unit) * scale;
        a = read(l, v, kA, 0, unit) * scale;
    }
    o.r = r;
    o.g = g;
    o.b = b;
    o.a = a;
    const float x = load(v, 0), y = load(v, 4);
    const float z = read(l, v, kZ, 0, 0.f);
    const bool fog = l.param[kFog].enabled && !sp.wbuffer;
    if (!clip) {
        const float q = l.param[kQ].enabled ? load(v, l.param[kQ].offset) : read(l, v, kW, 0, 1.f);
        o.x = x;
        o.y = y;
        o.ooz = z;
        o.oow = fog ? load(v, l.param[kFog].offset) : q;
        o.tmu[0].oow = read(l, v, kQ0, 0, q);
        o.tmu[1].oow = read(l, v, kQ1, 0, q);
        o.tmu[0].sow = read(l, v, kST0, 0, 0.f);
        o.tmu[0].tow = read(l, v, kST0, 1, 0.f);
        o.tmu[1].sow = read(l, v, kST1, 0, 0.f);
        o.tmu[1].tow = read(l, v, kST1, 1, 0.f);
    } else {
        const float w = read(l, v, kW, 0, 1.f);
        const float oow = w != 0.f ? 1.f / w : 0.f;
        o.x = x * oow * sp.view.hwidth + sp.view.ox;
        o.y = y * oow * sp.view.hheight + sp.view.oy;
        o.ooz = z * oow * sp.view.hdepth + sp.view.oz;
        o.oow = fog ? load(v, l.param[kFog].offset) * oow : read(l, v, kQ, 0, 1.f) * oow;
        o.tmu[0].oow = read(l, v, kQ0, 0, 1.f) * oow;
        o.tmu[1].oow = read(l, v, kQ1, 0, 1.f) * oow;
        o.tmu[0].sow = read(l, v, kST0, 0, 0.f) * oow * sp.s_scale[0];
        o.tmu[0].tow = read(l, v, kST0, 1, 0.f) * oow * sp.t_scale[0];
        o.tmu[1].sow = read(l, v, kST1, 0, 0.f) * oow * sp.s_scale[1];
        o.tmu[1].tow = read(l, v, kST1, 1, 0.f) * oow * sp.t_scale[1];
    }
    // GR_COMBINE_LOCAL_DEPTH uses the top eight bits of the iterated depth.
    o.z = o.ooz * (1.f / 256.f);
}

// Emulated triangle setup unit. Strips alternate winding exactly like the
// hardware so culling is consistent; *_CONTINUE modes keep the last two
// vertices and strip parity from the preceding primitive, as Glide 3 does.
struct Setup {
    Vertex2 v[2];
    unsigned count, parity;
    bool fan;
};
inline void setup_reset(Setup &s, bool fan) {
    s.count = 0;
    s.parity = 0;
    s.fan = fan;
}
template <class Sink> inline void setup_vertex(Setup &s, const Vertex2 &n, Sink &sink) {
    if (s.count < 2) {
        s.v[s.count++] = n;
        return;
    }
    if (s.fan) {
        sink.triangle(s.v[0], s.v[1], n);
        s.v[1] = n;
        return;
    }
    if (s.parity)
        sink.triangle(s.v[1], s.v[0], n);
    else
        sink.triangle(s.v[0], s.v[1], n);
    s.v[0] = s.v[1];
    s.v[1] = n;
    s.parity ^= 1;
}
// A separately drawn triangle (grDrawTriangle, GR_TRIANGLES) leaves the setup
// unit as a strip after one triangle.
inline void setup_after_triangle(Setup &s, const Vertex2 &b, const Vertex2 &c) {
    s.v[0] = b;
    s.v[1] = c;
    s.count = 2;
    s.parity = 1;
    s.fan = false;
}

// Vertex sources. A null pointer anywhere stops the draw; nothing after it
// is read. Contiguous sources mirror Glide 3's float-granular stride
// (stride & ~3) and reject address arithmetic that would wrap.
struct PointerSource {
    const void *const *pointers;
    const void *operator()(u32 i) const {
        return pointers[i];
    }
};
struct StrideSource {
    const u8 *base;
    uintptr_t stride;
    const void *operator()(u32 i) const {
        return base + uintptr_t(i) * stride;
    }
};
inline bool pointer_source(const void *pointers, u32 count, PointerSource &out) {
    if (!count)
        return true;
    const uintptr_t room = UINTPTR_MAX - uintptr_t(pointers);
    if (!pointers || count > room / sizeof(void *))
        return false;
    out.pointers = static_cast<const void *const *>(pointers);
    return true;
}
inline bool stride_source(const void *base, u32 count, u32 stride, const Layout &l,
                          StrideSource &out) {
    out.base = static_cast<const u8 *>(base);
    out.stride = stride & ~3u;
    if (!count)
        return true;
    const uintptr_t room = UINTPTR_MAX - uintptr_t(base), bytes = extent(l);
    return base && bytes <= room &&
           (!out.stride || uintptr_t(count - 1) <= (room - bytes) / out.stride);
}

// Draws one grDrawVertexArray(Contiguous) call. Returns false for unknown
// modes or a null vertex pointer; primitives before the failure are kept.
template <class Source, class Sink>
inline bool draw(u32 mode, u32 count, const Layout &l, const Space &sp, Setup &st,
                 const Source &source, Sink &sink) {
    Vertex2 a, b, c;
    auto fetch = [&](u32 i, Vertex2 &out) {
        const void *p = source(i);
        if (!p)
            return false;
        convert(l, sp, p, out);
        return true;
    };
    switch (mode) {
        case kPoints:
            for (u32 i = 0; i < count; ++i) {
                if (!fetch(i, a))
                    return false;
                sink.point(a);
            }
            st.count = 0;
            return true;
        case kLineStrip:
        case kLines: {
            const u32 step = mode == kLines ? 2 : 1;
            for (u32 i = 0; count >= 2 && i <= count - 2; i += step) {
                if (!fetch(i, a) || !fetch(i + 1, b))
                    return false;
                sink.line(a, b);
            }
            st.count = 0;
            return true;
        }
        case kTriangles:
            for (u32 i = 0; count >= 3 && i <= count - 3; i += 3) {
                if (!fetch(i, a) || !fetch(i + 1, b) || !fetch(i + 2, c))
                    return false;
                sink.triangle(a, b, c);
                setup_after_triangle(st, b, c);
            }
            return true;
        case kTriangleStrip:
        case kTriangleFan:
        case kPolygon:
            setup_reset(st, mode != kTriangleStrip);
            break;
        case kTriangleStripContinue:
        case kTriangleFanContinue:
            st.fan = mode == kTriangleFanContinue;
            break;
        default:
            return false;
    }
    for (u32 i = 0; i < count; ++i) {
        if (!fetch(i, a))
            return false;
        setup_vertex(st, a, sink);
    }
    return true;
}

// grGet. A pname succeeds only with its exact byte length and a non-null
// buffer; failures return 0 and never write the caller's buffer.
constexpr u32 kBitsDepth = 0x01, kBitsRGBA = 0x02, kFifoFullness = 0x03, kFogTableEntries = 0x04,
              kGammaTableEntries = 0x05, kGlideStateSize = 0x06, kGlideVertexLayoutSize = 0x07,
              kIsBusy = 0x08, kLfbPixelPipe = 0x09, kMaxTextureSize = 0x0a,
              kMaxTextureAspectRatio = 0x0b, kMemoryFB = 0x0c, kMemoryTMU = 0x0d, kMemoryUMA = 0x0e,
              kNumBoards = 0x0f, kNonPowerOfTwoTextures = 0x10, kNumFB = 0x11,
              kNumSwapHistoryBuffer = 0x12, kNumTMU = 0x13, kPendingBufferSwaps = 0x14,
              kRevisionFB = 0x15, kRevisionTMU = 0x16, kStatsLines = 0x17,
              kStatsPixelsAfuncFail = 0x18, kStatsPixelsChromaFail = 0x19,
              kStatsPixelsDepthfuncFail = 0x1a, kStatsPixelsIn = 0x1b, kStatsPixelsOut = 0x1c,
              kStatsPixels = 0x1d, kStatsPoints = 0x1e, kStatsTrianglesIn = 0x1f,
              kStatsTrianglesOut = 0x20, kStatsTriangles = 0x21, kSwapHistory = 0x22,
              kSupportsPassthru = 0x23, kTextureAlign = 0x24, kVideoPosition = 0x25,
              kViewportQuery = 0x26, kWDepthMinMax = 0x27, kZDepthMinMax = 0x28,
              kVertexParameter = 0x29, kBitsGamma = 0x2a, kMemType = 0x1000;
constexpr u32 kTextureAlignBytes = 16;

struct Facts {
    u32 fb_bytes, tmu_bytes;
    u32 state_bytes, layout_bytes;
    i32 viewport[4];
    u32 points, lines, triangles;
    u32 pending_swaps;
};
inline u32 get(u32 pname, u32 plength, i32 *params, const Facts &f) {
    if (!params)
        return 0;
    i32 v[4] = {};
    u32 need = 4;
    switch (pname) {
        case kBitsDepth:
            v[0] = 16;
            break;
        case kBitsRGBA:
            need = 16;
            v[0] = 5;
            v[1] = 6;
            v[2] = 5;
            v[3] = 0;
            break;
        case kFifoFullness:
            // No command FIFO is exposed: report it empty and idle.
            need = 8;
            v[0] = 0x3f;
            v[1] = 0x0ffff03f;
            break;
        case kFogTableEntries:
            v[0] = 64;
            break;
        case kGammaTableEntries:
            v[0] = 256;
            break;
        case kBitsGamma:
            v[0] = 8;
            break;
        case kGlideStateSize:
            v[0] = i32(f.state_bytes);
            break;
        case kGlideVertexLayoutSize:
            v[0] = i32(f.layout_bytes);
            break;
        case kIsBusy:
            v[0] = 0;
            break;
        case kLfbPixelPipe:
            v[0] = 0;
            break; // LFB writes apply only the clip rectangle
        case kMaxTextureSize:
            v[0] = 256;
            break;
        case kMaxTextureAspectRatio:
            v[0] = 3;
            break;
        case kMemoryFB:
            v[0] = i32(f.fb_bytes);
            break;
        case kMemoryTMU:
            v[0] = i32(f.tmu_bytes);
            break;
        case kMemoryUMA:
            v[0] = i32(f.fb_bytes + f.tmu_bytes);
            break;
        case kNumBoards:
            v[0] = 1;
            break;
        case kNonPowerOfTwoTextures:
            v[0] = 0;
            break;
        case kNumFB:
            v[0] = 1;
            break;
        case kNumSwapHistoryBuffer:
            v[0] = 0;
            break; // no swap history is recorded
        case kNumTMU:
            v[0] = 1;
            break;
        case kPendingBufferSwaps:
            v[0] = i32(f.pending_swaps);
            break;
        case kRevisionFB:
        case kRevisionTMU:
            v[0] = 0x1000;
            break; // Voodoo Banshee
        case kStatsLines:
            v[0] = i32(f.lines);
            break;
        case kStatsPoints:
            v[0] = i32(f.points);
            break;
        case kStatsTrianglesIn:
            v[0] = i32(f.triangles);
            break;
        case kSupportsPassthru:
            v[0] = 0;
            break;
        case kTextureAlign:
            v[0] = i32(kTextureAlignBytes);
            break;
        case kVideoPosition:
            need = 8;
            break;
        case kViewportQuery:
            need = 16;
            memcpy(v, f.viewport, sizeof(v));
            break;
        case kWDepthMinMax:
            need = 8;
            v[0] = 0;
            v[1] = 0xffff;
            break;
        case kZDepthMinMax:
            need = 8;
            v[0] = 0xffff;
            v[1] = 0;
            break;
        case kMemType:
            v[0] = 0;
            break;
        default:
            // Pixel counters, rasterized-triangle counts and swap history are not
            // observable through the GL path, so they are not reported.
            return 0;
    }
    if (plength != need)
        return 0;
    memcpy(params, v, need);
    return need;
}

// grQueryResolutions. Entries are 16-byte GrResolution records.
struct Resolution {
    u32 resolution, refresh;
    i32 color_buffers, aux_buffers;
};
static_assert(sizeof(Resolution) == 16, "GrResolution layout");
constexpr u32 kQueryAny = 0xffffffffu, kRefreshNone = 0xff;
constexpr u32 kMaxResolution = 0xf, kMaxRefresh = 8;
inline bool resolution_size(u32 res, u32 &width, u32 &height) {
    static const u16 table[16][2] = {{320, 200},  {320, 240},   {400, 256},   {512, 384},
                                     {640, 200},  {640, 350},   {640, 400},   {640, 480},
                                     {800, 600},  {960, 720},   {856, 480},   {512, 256},
                                     {1024, 768}, {1280, 1024}, {1600, 1200}, {400, 300}};
    if (res > kMaxResolution)
        return false;
    width = table[res][0];
    height = table[res][1];
    return true;
}
// t and out are caller GrResolution memory; entries are copied bytewise.
inline i32 query_resolutions(const void *template_bytes, void *out, u32 fb_bytes) {
    if (!template_bytes)
        return 0;
    Resolution request;
    memcpy(&request, template_bytes, sizeof(request));
    const Resolution *t = &request;
    u8 *output = static_cast<u8 *>(out);
    u32 r0 = 0, r1 = kMaxResolution, f0 = 0, f1 = kMaxRefresh;
    i32 c0 = 1, c1 = 3, a0 = 0, a1 = 1;
    if (t->resolution != kQueryAny) {
        if (t->resolution > kMaxResolution)
            return 0;
        r0 = r1 = t->resolution;
    }
    if (t->refresh != kQueryAny && t->refresh != kRefreshNone) {
        if (t->refresh > kMaxRefresh)
            return 0;
        f0 = f1 = t->refresh;
    }
    if (u32(t->color_buffers) != kQueryAny) {
        if (t->color_buffers < 1 || t->color_buffers > 3)
            return 0;
        c0 = c1 = t->color_buffers;
    }
    if (u32(t->aux_buffers) != kQueryAny) {
        if (t->aux_buffers < 0 || t->aux_buffers > 1)
            return 0;
        a0 = a1 = t->aux_buffers;
    }
    const uint64_t budget = fb_bytes > 0x10000 ? fb_bytes - 0x10000 : 0;
    i32 bytes = 0;
    for (u32 r = r0; r <= r1; ++r) {
        u32 w, h;
        resolution_size(r, w, h);
        for (u32 f = f0; f <= f1; ++f)
            for (i32 c = c0; c <= c1; ++c)
                for (i32 a = a0; a <= a1; ++a) {
                    if (uint64_t(w) * h * 2 * uint64_t(c + a) >= budget)
                        continue;
                    if (output) {
                        const Resolution entry = {r, f, c, a};
                        memcpy(output, &entry, sizeof(entry));
                        output += sizeof(entry);
                    }
                    bytes += i32(sizeof(Resolution));
                }
    }
    return bytes;
}

// Glide 3 texture descriptors (log2 LOD and aspect) and their Glide 2 form.
struct TexInfo {
    i32 small_lod, large_lod, aspect, format;
    void *data;
};
inline bool lod_ok(i32 lod) {
    return lod >= 0 && lod <= 8;
}
inline bool aspect_ok(i32 aspect) {
    return aspect >= -3 && aspect <= 3;
}
inline bool format_ok(i32 format) {
    return (format >= 0x0 && format <= 0x5) || (format >= 0x8 && format <= 0xe);
}
inline bool format_16bit(i32 format) {
    return format >= 0x8;
}
inline bool tex_ok(i32 small_lod, i32 large_lod, i32 aspect, i32 format) {
    return lod_ok(small_lod) && lod_ok(large_lod) && small_lod <= large_lod && aspect_ok(aspect) &&
           format_ok(format);
}
inline bool tex_ok(const TexInfo &t) {
    return tex_ok(t.small_lod, t.large_lod, t.aspect, t.format);
}
inline i32 lod2(i32 lod) {
    return 8 - lod;
}
inline i32 aspect2(i32 aspect) {
    return 3 - aspect;
}
inline TexInfo to_glide2(const TexInfo &t) {
    return {lod2(t.small_lod), lod2(t.large_lod), aspect2(t.aspect), t.format, t.data};
}
// Clip-coordinate S/T scale: the long side spans 256 texture units.
inline void tex_scale(i32 aspect, float &s, float &t) {
    s = aspect >= 0 ? 256.f : float(256 >> -aspect);
    t = aspect <= 0 ? 256.f : float(256 >> aspect);
}
// Level dimensions for a log2 LOD and aspect.
inline void level_size(i32 lod, i32 aspect, u32 &w, u32 &h) {
    const u32 large = 1u << lod;
    w = aspect >= 0 ? large : large >> -aspect;
    h = aspect <= 0 ? large : large >> aspect;
    if (!w)
        w = 1;
    if (!h)
        h = 1;
}
inline u32 host_bytes(const TexInfo &t) {
    u32 bytes = 0;
    for (i32 lod = t.large_lod; lod >= t.small_lod; --lod) {
        u32 w, h;
        level_size(lod, t.aspect, w, h);
        bytes += w * h * (format_16bit(t.format) ? 2 : 1);
    }
    return bytes;
}

// Combine flags the shared core derives from call order. grGlideSetState in
// the core replays colour, alpha and texture combine and then blending, but
// grColorCombine samples the texture combine current at that moment, never
// clears ALocal/AOther, and every one of the four calls recomputes
// OpenGL.Texture. Replaying a saved state after the texture combine changed
// therefore disabled texturing. Glide 3 state blobs carry these flags and put
// them back exactly once the core has replayed the state.
struct CoreFlags {
    u8 c_local, c_other, a_local, a_other, color_texture, alpha_texture, texture, blend;
};
template <class GlideT, class OpenGLT>
inline CoreFlags capture_flags(const GlideT &glide, const OpenGLT &gl) {
    return {u8(glide.CLocal),    u8(glide.COther),    u8(glide.ALocal), u8(glide.AOther),
            u8(gl.ColorTexture), u8(gl.AlphaTexture), u8(gl.Texture),   u8(gl.Blend)};
}
inline bool flags_ok(const CoreFlags &f) {
    return f.c_local <= 1 && f.c_other <= 1 && f.a_local <= 1 && f.a_other <= 1 &&
           f.color_texture <= 1 && f.alpha_texture <= 1 && f.texture <= 1 && f.blend <= 1;
}
template <class GlideT, class OpenGLT>
inline void restore_flags(const CoreFlags &f, GlideT &glide, OpenGLT &gl) {
    glide.CLocal = f.c_local;
    glide.COther = f.c_other;
    glide.ALocal = f.a_local;
    glide.AOther = f.a_other;
    gl.ColorTexture = f.color_texture;
    gl.AlphaTexture = f.alpha_texture;
    gl.Texture = f.texture;
    gl.Blend = f.blend;
}

// grFogMode: Glide 3 source values to the shared Glide 2 core's values.
constexpr u32 kFogDisable = 0, kFogTableOnFogCoord = 1, kFogTableOnQ = 2, kFogIteratedZ = 3,
              kFogIteratedAlpha = 4, kFogMult2 = 0x100, kFogAdd2 = 0x200;
inline bool fog_mode(u32 mode3, u32 &mode2) {
    if (mode3 & ~(0xffu | kFogMult2 | kFogAdd2))
        return false;
    static const u32 source[5] = {0, 2, 2, 3, 1};
    const u32 s = mode3 & 0xff;
    if (s > kFogIteratedAlpha)
        return false;
    mode2 = source[s] | (mode3 & (kFogMult2 | kFogAdd2));
    return true;
}

// GR_TEXTABLE_PALETTE_6666_EXT entry to ARGB8888.
inline u32 palette6666(u32 v) {
    auto expand = [](u32 c) { return (c << 2) | (c >> 4); };
    return expand((v >> 18) & 63) << 24 | expand((v >> 12) & 63) << 16 |
           expand((v >> 6) & 63) << 8 | expand(v & 63);
}

// .3df texture files: four text header lines, then big-endian table and texel
// data. Every field and every byte read is bounded by the supplied file size.
struct Df3 {
    u32 width, height;
    i32 small_lod, large_lod, aspect, format;
    u32 data_offset, table_bytes, mem_required;
};
constexpr size_t kDf3HeaderLimit = 256;
inline bool df3_line(const u8 *p, size_t size, size_t &at, char (&line)[96]) {
    size_t n = 0;
    while (at < size && at < kDf3HeaderLimit && p[at] != '\n') {
        if (n + 1 >= sizeof(line))
            return false;
        const char c = char(p[at++]);
        if (!c)
            return false; // header lines are text
        if (c != '\r')
            line[n++] = c;
    }
    if (at >= size || at >= kDf3HeaderLimit)
        return false;
    ++at;
    line[n] = 0;
    return true;
}
inline bool df3_prefix(const char *&s, const char *prefix) {
    while (*prefix)
        if (*s++ != *prefix++)
            return false;
    return true;
}
inline bool df3_number(const char *&s, i32 &out) {
    while (*s == ' ' || *s == '\t')
        ++s;
    if (*s < '0' || *s > '9')
        return false;
    i32 v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s++ - '0');
        if (v > 4096)
            return false;
    }
    out = v;
    return true;
}
inline bool df3_end(const char *s) {
    while (*s == ' ' || *s == '\t')
        ++s;
    return !*s;
}
inline i32 df3_log2(i32 v) {
    for (i32 l = 0; l <= 8; ++l)
        if (v == (1 << l))
            return l;
    return -1;
}
inline bool parse_3df(const u8 *p, size_t size, Df3 &h) {
    static const struct {
        const char *name;
        i32 format;
    } formats[] = {{"i8", 0x3},       {"a8", 0x2},       {"ai44", 0x4},     {"yiq", 0x1},
                   {"rgb332", 0x0},   {"rgb565", 0xa},   {"argb8332", 0x8}, {"argb1555", 0xb},
                   {"ayiq8422", 0x9}, {"argb4444", 0xc}, {"ai88", 0xd},     {"p8", 0x5},
                   {"ap88", 0xe}};
    char line[96];
    size_t at = 0;
    if (!p || !df3_line(p, size, at, line) || strncmp(line, "3df v", 5))
        return false;
    if (!df3_line(p, size, at, line))
        return false;
    for (char *c = line; *c; ++c)
        if (*c >= 'A' && *c <= 'Z')
            *c = char(*c - 'A' + 'a');
    for (char *c = line + strlen(line); c > line && (c[-1] == ' ' || c[-1] == '\t');)
        *--c = 0;
    h.format = -1;
    for (const auto &f : formats)
        if (!strcmp(line, f.name))
            h.format = f.format;
    i32 small_size, large_size, aw, ah;
    const char *s = line;
    if (h.format < 0 || !df3_line(p, size, at, line) || !df3_prefix(s = line, "lod range:") ||
        !df3_number(s, small_size) || !df3_number(s, large_size) || !df3_end(s))
        return false;
    if (!df3_line(p, size, at, line) || !df3_prefix(s = line, "aspect ratio:") ||
        !df3_number(s, aw) || !df3_number(s, ah) || !df3_end(s))
        return false;
    h.small_lod = df3_log2(small_size);
    h.large_lod = df3_log2(large_size);
    if (h.small_lod < 0 || h.large_lod < 0 || h.small_lod > h.large_lod)
        return false;
    h.aspect = 9;
    for (i32 i = 0; i <= 3; ++i) {
        if ((aw << i) == ah)
            h.aspect = -i;
        else if ((ah << i) == aw)
            h.aspect = i;
    }
    if (!aspect_ok(h.aspect))
        return false;
    level_size(h.large_lod, h.aspect, h.width, h.height);
    h.data_offset = u32(at);
    h.table_bytes = (h.format == 0x5 || h.format == 0xe)   ? 1024
                    : (h.format == 0x1 || h.format == 0x9) ? 80
                                                           : 0;
    h.mem_required = host_bytes({h.small_lod, h.large_lod, h.aspect, h.format, nullptr});
    return uint64_t(h.data_offset) + h.table_bytes + h.mem_required <= size;
}
inline u32 df3_be16(const u8 *p) {
    return u32(p[0]) << 8 | p[1];
}
// GuTexTable as used by the shared core: a palette, or an NCC table.
struct NccTable {
    u8 y[16];
    int16_t i[4][3], q[4][3];
    u32 packed[12];
};
union Df3Table {
    NccTable ncc;
    u32 palette[256];
};
static_assert(sizeof(Df3Table) == 1024, "GuTexTable layout");
inline bool load_3df(const u8 *p, size_t size, const Df3 &h, Df3Table &table, u8 *data,
                     u32 capacity) {
    if (!p || !data || capacity < h.mem_required ||
        uint64_t(h.data_offset) + h.table_bytes + h.mem_required > size)
        return false;
    const u8 *at = p + h.data_offset;
    if (h.table_bytes == 1024)
        for (unsigned i = 0; i < 256; ++i, at += 4)
            table.palette[i] = u32(at[0]) << 24 | u32(at[1]) << 16 | u32(at[2]) << 8 | at[3];
    else if (h.table_bytes == 80) {
        NccTable &n = table.ncc;
        for (unsigned i = 0; i < 16; ++i, at += 2)
            n.y[i] = u8(df3_be16(at));
        for (unsigned k = 0; k < 2; ++k)
            for (unsigned i = 0; i < 4; ++i)
                for (unsigned c = 0; c < 3; ++c, at += 2)
                    (k ? n.q : n.i)[i][c] = int16_t(df3_be16(at) & 0x1ff);
        for (unsigned i = 0; i < 4; ++i)
            n.packed[i] = u32(n.y[i * 4]) | u32(n.y[i * 4 + 1]) << 8 | u32(n.y[i * 4 + 2]) << 16 |
                          u32(n.y[i * 4 + 3]) << 24;
        for (unsigned k = 0; k < 2; ++k)
            for (unsigned i = 0; i < 4; ++i) {
                const int16_t *e = (k ? n.q : n.i)[i];
                n.packed[4 + k * 4 + i] = u32(e[0]) << 18 | u32(e[1]) << 9 | u32(e[2]);
            }
    }
    if (format_16bit(h.format))
        for (u32 i = 0; i < h.mem_required; i += 2) {
            const u16 texel = u16(df3_be16(at + i));
            memcpy(data + i, &texel, 2);
        }
    else
        memcpy(data, at, h.mem_required);
    return true;
}
} // namespace dg3
