#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Run the actual Glide 3 front-end logic (guest/glide/glide3-core.h) under
ASan/UBSan: vertex-layout offsets and disabled attributes, window/clip w/q/oow
conversion, strip/fan/continue assembly, caller arrays and strides, grGet
buffer contracts, resolution queries, texture descriptors, fog modes and .3df
parsing. Caller memory is heap-allocated to its exact size so any read past a
declared vertex, array, buffer or file is a sanitizer failure."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
HARNESS = r'''
#include "glide3-core.h"
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
using namespace dg3;

template <class T> static T *exact(size_t bytes) {
    // Exactly sized heap block: ASan reports any byte read beyond it.
    return static_cast<T *>(std::malloc(bytes ? bytes : 1));
}
static void put(void *v, size_t offset, float f) { std::memcpy((u8 *)v + offset, &f, 4); }
static void put32(void *v, size_t offset, u32 x) { std::memcpy((u8 *)v + offset, &x, 4); }
static Space window_space() {
    Space s = {};
    s.coords = kWindowCoords;
    set_viewport(s.view, 0, 0, 640, 480);
    set_depth_range(s.view, 0.f, 1.f);
    s.s_scale[0] = s.t_scale[0] = s.s_scale[1] = s.t_scale[1] = 256.f;
    return s;
}

static void layouts() {
    Layout l;
    reset(l);
    assert(valid(l) && !l.packed && extent(l) == 8);
    for (unsigned s = 0; s < kSlots; ++s)
        assert(!l.param[s].enabled);
    // Offsets are bounded by each parameter's own size.
    assert(set_param(l, kParamZ, kMaxVertexBytes - 4, kParamEnable) == SetResult::applied);
    assert(extent(l) == u32(kMaxVertexBytes));
    assert(set_param(l, kParamZ, kMaxVertexBytes - 3, kParamEnable) == SetResult::bad_offset);
    assert(!l.param[kZ].enabled && l.param[kZ].offset == 0);
    assert(set_param(l, kParamRGB, kMaxVertexBytes - 12, kParamEnable) == SetResult::applied);
    assert(set_param(l, kParamRGB, kMaxVertexBytes - 11, kParamEnable) == SetResult::bad_offset);
    assert(set_param(l, kParamST1, -4, kParamEnable) == SetResult::bad_offset);
    assert(set_param(l, kParamQ, 0x7fffffff, kParamEnable) == SetResult::bad_offset);
    // Disabling with an invalid offset is harmless and never re-enables.
    assert(set_param(l, kParamQ, -8, kParamDisable) == SetResult::applied && !l.param[kQ].enabled);
    // Unknown parameters (e.g. Napalm ST2 0x42 / Q2 0x52) change nothing.
    Layout before = l;
    assert(set_param(l, 0x42, 8, kParamEnable) == SetResult::unknown_param);
    assert(set_param(l, 0x52, 8, kParamEnable) == SetResult::unknown_param);
    assert(!std::memcmp(&before, &l, sizeof(l)));
    // Only GR_PARAM_ENABLE (1) enables; any other mode disables.
    assert(set_param(l, kParamW, 16, 2) == SetResult::applied && !l.param[kW].enabled);
    // Colour type follows the most recently enabled colour parameter.
    reset(l);
    set_param(l, kParamPARGB, 12, kParamEnable);
    assert(l.packed == 1);
    set_param(l, kParamRGB, 16, kParamEnable);
    assert(l.packed == 0);
    set_param(l, kParamRGB, 16, kParamDisable);
    assert(l.packed == 0);
    set_param(l, kParamPARGB, 12, kParamEnable);
    set_param(l, kParamA, 28, kParamEnable);
    assert(l.packed == 0);
    set_param(l, kParamPARGB, 12, kParamDisable);
    assert(l.packed == 0);
    // Extent counts enabled parameters only.
    reset(l);
    set_param(l, kParamST1, 40, kParamEnable);
    set_param(l, kParamQ1, 200, kParamDisable);
    assert(extent(l) == 48);
    // grGlideSetVertexLayout blobs are validated.
    Layout bad = l;
    bad.magic ^= 1;
    assert(!valid(bad));
    bad = l;
    bad.version = 2;
    assert(!valid(bad));
    bad = l;
    bad.param[kZ] = {kMaxVertexBytes, 1};
    assert(!valid(bad));
    bad = l;
    bad.param[kZ] = {kMaxVertexBytes, 0};
    assert(valid(bad));
    bad = l;
    bad.param[kA].enabled = 2;
    assert(!valid(bad));
    bad = l;
    bad.packed = 3;
    assert(!valid(bad));
}

static void window_conversion() {
    const Space s = window_space();
    Layout l;
    reset(l);
    // Only XY: every other attribute takes its documented default and the
    // vertex is exactly 8 bytes, so any extra read is out of bounds.
    float *xy = exact<float>(8);
    put(xy, 0, 12.5f);
    put(xy, 4, 40.25f);
    set_param(l, kParamXY, 0, kParamEnable);
    set_param(l, kParamRGB, 400, kParamDisable); // recorded but disabled
    Vertex2 o;
    convert(l, s, xy, o);
    assert(o.x == 12.5f && o.y == 40.25f && o.ooz == 0.f && o.z == 0.f);
    assert(o.r == 255.f && o.g == 255.f && o.b == 255.f && o.a == 255.f);
    assert(o.oow == 1.f && o.tmu[0].oow == 1.f && o.tmu[1].oow == 1.f);
    assert(o.tmu[0].sow == 0.f && o.tmu[0].tow == 0.f);
    std::free(xy);

    // A non-trivial layout with gaps and every Glide 3 parameter class.
    reset(l);
    set_param(l, kParamXY, 0, kParamEnable);
    set_param(l, kParamZ, 8, kParamEnable);
    set_param(l, kParamQ, 12, kParamEnable);
    set_param(l, kParamPARGB, 16, kParamEnable);
    set_param(l, kParamST0, 20, kParamEnable);
    set_param(l, kParamQ0, 32, kParamEnable);
    set_param(l, kParamW, 28, kParamEnable);
    set_param(l, kParamST1, 36, kParamEnable);
    assert(extent(l) == 44);
    u8 *v = exact<u8>(44);
    std::memset(v, 0xff, 44); // NaN filler outside written fields
    put(v, 0, 100.f);
    put(v, 4, 200.f);
    put(v, 8, 30000.f);
    put(v, 12, 0.5f);
    put32(v, 16, 0x80402010u);
    put(v, 20, 64.f);
    put(v, 24, 128.f);
    put(v, 28, 7.f);
    put(v, 32, 0.25f);
    put(v, 36, 3.f);
    put(v, 40, 4.f);
    convert(l, s, v, o);
    assert(o.x == 100.f && o.y == 200.f && o.ooz == 30000.f && o.z == 30000.f / 256.f);
    assert(o.a == 128.f && o.r == 64.f && o.g == 32.f && o.b == 16.f);
    // Window coordinates: FBI 1/w is Q (W is a clip-space parameter).
    assert(o.oow == 0.5f && o.tmu[0].oow == 0.25f && o.tmu[1].oow == 0.5f);
    assert(o.tmu[0].sow == 64.f && o.tmu[0].tow == 128.f);
    assert(o.tmu[1].sow == 3.f && o.tmu[1].tow == 4.f);
    // Q disabled: W supplies 1/w; Q0 still overrides TMU0.
    set_param(l, kParamQ, 12, kParamDisable);
    convert(l, s, v, o);
    assert(o.oow == 7.f && o.tmu[0].oow == 0.25f && o.tmu[1].oow == 7.f);
    // FOG_EXT feeds the FBI iterator, except while W-buffering.
    set_param(l, kParamQ, 12, kParamEnable);
    set_param(l, kParamFogExt, 40, kParamEnable);
    convert(l, s, v, o);
    assert(o.oow == 4.f && o.tmu[0].oow == 0.25f && o.tmu[1].oow == 0.5f);
    Space w = s;
    w.wbuffer = true;
    convert(l, w, v, o);
    assert(o.oow == 0.5f);
    // Packed colour selected but PARGB later disabled: opaque white.
    set_param(l, kParamPARGB, 16, kParamDisable);
    convert(l, s, v, o);
    assert(o.r == 255.f && o.a == 255.f);
    // Float colours read 0..255 in window coordinates.
    set_param(l, kParamRGB, 8, kParamEnable);
    set_param(l, kParamA, 20, kParamEnable);
    convert(l, s, v, o);
    assert(o.r == 30000.f && o.g == 0.5f && o.b == std::bit_cast<float>(0x80402010u));
    assert(o.a == 64.f);
    std::free(v);
}

static void clip_conversion() {
    Space s = window_space();
    s.coords = kClipCoords;
    set_viewport(s.view, 10, 20, 640, 480);
    set_depth_range(s.view, 0.25f, 0.75f);
    tex_scale(1, s.s_scale[0], s.t_scale[0]); // 2x1 texture on TMU0
    Layout l;
    reset(l);
    set_param(l, kParamXY, 0, kParamEnable);
    set_param(l, kParamZ, 8, kParamEnable);
    set_param(l, kParamW, 12, kParamEnable);
    set_param(l, kParamRGB, 16, kParamEnable);
    set_param(l, kParamA, 28, kParamEnable);
    set_param(l, kParamST0, 32, kParamEnable);
    set_param(l, kParamQ0, 40, kParamEnable);
    u8 *v = exact<u8>(44);
    const float in[11] = {0.5f, -0.5f, 0.5f, 2.f, 1.f, 0.5f, 0.f, 0.25f, 1.f, 0.5f, 4.f};
    std::memcpy(v, in, sizeof(in));
    Vertex2 o;
    convert(l, s, v, o);
    // oow = 1/2; x = 0.5*0.5*320 + 330; y = -0.5*0.5*240 + 260.
    assert(o.x == 410.f && o.y == 200.f);
    // hdepth = 0.25*65535, oz = 0.5*65535.
    assert(std::fabs(o.ooz - (0.25f * 0.25f * 65535.f + 0.5f * 65535.f)) < 0.01f);
    assert(o.r == 255.f && o.g == 127.5f && o.b == 0.f && o.a == 63.75f);
    assert(o.oow == 0.5f && o.tmu[0].oow == 2.f && o.tmu[1].oow == 0.5f);
    assert(o.tmu[0].sow == 1.f * 0.5f * 256.f && o.tmu[0].tow == 0.5f * 0.5f * 128.f);
    // Q scales the FBI iterator; FOG_EXT takes precedence.
    set_param(l, kParamQ, 40, kParamEnable);
    convert(l, s, v, o);
    assert(o.oow == 2.f);
    set_param(l, kParamFogExt, 28, kParamEnable);
    convert(l, s, v, o);
    assert(o.oow == 0.125f);
    // W disabled means w = 1; w = 0 collapses to the viewport centre.
    set_param(l, kParamW, 12, kParamDisable);
    convert(l, s, v, o);
    assert(o.x == 0.5f * 320.f + 330.f && o.tmu[0].sow == 256.f);
    set_param(l, kParamW, 12, kParamEnable);
    put(v, 12, 0.f);
    convert(l, s, v, o);
    assert(o.x == 330.f && o.y == 260.f && o.oow == 0.f);
    // Clip float colour defaults (disabled) are 1.0 -> 255.
    set_param(l, kParamRGB, 16, kParamDisable);
    set_param(l, kParamA, 28, kParamDisable);
    convert(l, s, v, o);
    assert(o.r == 255.f && o.a == 255.f);
    std::free(v);
}

struct Record {
    std::vector<std::vector<float>> triangles, lines, points;
    void triangle(const Vertex2 &a, const Vertex2 &b, const Vertex2 &c) {
        triangles.push_back({a.x, b.x, c.x});
    }
    void line(const Vertex2 &a, const Vertex2 &b) { lines.push_back({a.x, b.x}); }
    void point(const Vertex2 &a) { points.push_back({a.x}); }
};
static Layout xy_layout() {
    Layout l;
    reset(l);
    set_param(l, kParamXY, 0, kParamEnable);
    return l;
}
struct Pool {
    std::vector<float *> vertices;
    explicit Pool(unsigned n) {
        for (unsigned i = 0; i < n; ++i) {
            vertices.push_back(exact<float>(8));
            put(vertices.back(), 0, float(i));
            put(vertices.back(), 4, 0.f);
        }
    }
    ~Pool() {
        for (auto *v : vertices)
            std::free(v);
    }
    const void **array(unsigned first, unsigned count) {
        auto **p = exact<const void *>(count * sizeof(void *));
        for (unsigned i = 0; i < count; ++i)
            p[i] = vertices[first + i];
        return p;
    }
};
static Record run(u32 mode, Pool &pool, unsigned first, unsigned count, Setup &st,
                  bool expect = true) {
    const Layout l = xy_layout();
    const Space s = window_space();
    const void **array = pool.array(first, count);
    PointerSource source;
    assert(pointer_source(array, count, source));
    Record r;
    assert(draw(mode, count, l, s, st, source, r) == expect);
    std::free(array);
    return r;
}
using T = std::vector<std::vector<float>>;

static void assembly() {
    Pool pool(12);
    Setup st;
    setup_reset(st, false);
    Record r = run(kTriangles, pool, 0, 7, st);
    assert((r.triangles == T{{0, 1, 2}, {3, 4, 5}}));
    // After GR_TRIANGLES the setup unit is a strip after one triangle.
    assert(st.count == 2 && st.parity == 1 && !st.fan && st.v[0].x == 4 && st.v[1].x == 5);
    // Strips alternate winding: (0,1,2), (2,1,3), (2,3,4).
    r = run(kTriangleStrip, pool, 0, 5, st);
    assert((r.triangles == T{{0, 1, 2}, {2, 1, 3}, {2, 3, 4}}));
    // A strip split by _CONTINUE equals the unsplit strip.
    Record a = run(kTriangleStrip, pool, 0, 4, st);
    Record b = run(kTriangleStripContinue, pool, 4, 1, st);
    a.triangles.insert(a.triangles.end(), b.triangles.begin(), b.triangles.end());
    assert((a.triangles == T{{0, 1, 2}, {2, 1, 3}, {2, 3, 4}}));
    // Fans and polygons share the first vertex; FAN_CONTINUE keeps it.
    r = run(kTriangleFan, pool, 0, 5, st);
    assert((r.triangles == T{{0, 1, 2}, {0, 2, 3}, {0, 3, 4}}));
    r = run(kPolygon, pool, 0, 4, st);
    assert((r.triangles == T{{0, 1, 2}, {0, 2, 3}}));
    a = run(kTriangleFan, pool, 0, 4, st);
    b = run(kTriangleFanContinue, pool, 4, 2, st);
    assert((b.triangles == T{{0, 3, 4}, {0, 4, 5}}));
    // Lines, strips, points.
    r = run(kLines, pool, 0, 5, st);
    assert((r.lines == T{{0, 1}, {2, 3}}));
    r = run(kLineStrip, pool, 0, 4, st);
    assert((r.lines == T{{0, 1}, {1, 2}, {2, 3}}));
    r = run(kPoints, pool, 0, 3, st);
    assert((r.points == T{{0}, {1}, {2}}) && st.count == 0);
    // After points the setup unit holds no vertices: continue needs three.
    r = run(kTriangleStripContinue, pool, 0, 2, st);
    assert(r.triangles.empty());
    r = run(kTriangleStripContinue, pool, 2, 1, st);
    assert((r.triangles == T{{0, 1, 2}}));
    // Degenerate counts and unknown modes draw nothing.
    for (unsigned count = 0; count < 3; ++count) {
        for (u32 mode : {kTriangles, kTriangleStrip, kTriangleFan, kPolygon})
            assert(run(mode, pool, 0, count, st).triangles.empty());
        assert(run(kLines, pool, 0, count, st).lines.size() == (count == 2));
    }
    assert(run(9, pool, 0, 3, st, false).triangles.empty());
    assert(run(0xffffffffu, pool, 0, 3, st, false).triangles.empty());
}

static void sources() {
    Layout l = xy_layout();
    const Space s = window_space();
    Setup st;
    setup_reset(st, false);
    // A null vertex pointer stops the draw before it is dereferenced.
    Pool pool(6);
    const void **array = pool.array(0, 6);
    array[4] = nullptr;
    PointerSource ps;
    assert(pointer_source(array, 6, ps));
    Record r;
    assert(!draw(kTriangles, 6, l, s, st, ps, r));
    assert((r.triangles == T{{0, 1, 2}}));
    std::free(array);
    assert(!pointer_source(nullptr, 1, ps));
    assert(pointer_source(nullptr, 0, ps));
    assert(!pointer_source(reinterpret_cast<void *>(UINTPTR_MAX - 8), 4, ps));
    // Contiguous arrays use stride & ~3 and read exactly the declared extent.
    set_param(l, kParamQ, 12, kParamEnable);
    const u32 count = 5, stride = 38, used = stride & ~3u, bytes = (count - 1) * used + 16;
    u8 *block = exact<u8>(bytes);
    for (u32 i = 0; i < count; ++i) {
        put(block, i * used, float(10 + i));
        put(block, i * used + 4, 0.f);
        put(block, i * used + 12, 1.f);
    }
    StrideSource ss;
    assert(stride_source(block, count, stride, l, ss) && ss.stride == 36);
    r = {};
    assert(draw(kTriangleStrip, count, l, s, st, ss, r));
    assert((r.triangles == T{{10, 11, 12}, {12, 11, 13}, {12, 13, 14}}));
    // Stride zero repeats one vertex.
    assert(stride_source(block, 3, 0, l, ss));
    r = {};
    assert(draw(kTriangles, 3, l, s, st, ss, r) && (r.triangles == T{{10, 10, 10}}));
    std::free(block);
    // Address arithmetic that would wrap is refused; nothing is read.
    assert(!stride_source(reinterpret_cast<void *>(UINTPTR_MAX - 64), 3, 64, l, ss));
    assert(!stride_source(nullptr, 1, 16, l, ss));
    assert(stride_source(nullptr, 0, 16, l, ss));
}

static void queries() {
    Facts f = {};
    f.fb_bytes = 8u << 20;
    f.tmu_bytes = 16u << 20;
    f.state_bytes = 700;
    f.layout_bytes = sizeof(Layout);
    f.viewport[0] = 1;
    f.viewport[1] = 2;
    f.viewport[2] = 640;
    f.viewport[3] = 480;
    f.triangles = 9;
    struct Case {
        u32 pname, bytes;
        std::vector<i32> value;
    };
    const Case cases[] = {
        {kBitsDepth, 4, {16}},
        {kBitsRGBA, 16, {5, 6, 5, 0}},
        {kFifoFullness, 8, {0x3f, 0x0ffff03f}},
        {kFogTableEntries, 4, {64}},
        {kGammaTableEntries, 4, {256}},
        {kBitsGamma, 4, {8}},
        {kGlideStateSize, 4, {700}},
        {kGlideVertexLayoutSize, 4, {i32(sizeof(Layout))}},
        {kIsBusy, 4, {0}},
        {kLfbPixelPipe, 4, {0}},
        {kMaxTextureSize, 4, {256}},
        {kMaxTextureAspectRatio, 4, {3}},
        {kMemoryFB, 4, {8 << 20}},
        {kMemoryTMU, 4, {16 << 20}},
        {kMemoryUMA, 4, {24 << 20}},
        {kNumBoards, 4, {1}},
        {kNonPowerOfTwoTextures, 4, {0}},
        {kNumFB, 4, {1}},
        {kNumSwapHistoryBuffer, 4, {0}},
        {kNumTMU, 4, {1}},
        {kPendingBufferSwaps, 4, {0}},
        {kRevisionFB, 4, {0x1000}},
        {kRevisionTMU, 4, {0x1000}},
        {kStatsTrianglesIn, 4, {9}},
        {kSupportsPassthru, 4, {0}},
        {kTextureAlign, 4, {16}},
        {kVideoPosition, 8, {0, 0}},
        {kViewportQuery, 16, {1, 2, 640, 480}},
        {kWDepthMinMax, 8, {0, 0xffff}},
        {kZDepthMinMax, 8, {0xffff, 0}},
        {kMemType, 4, {0}},
    };
    for (const auto &c : cases) {
        i32 *exact_buffer = exact<i32>(c.bytes);
        assert(get(c.pname, c.bytes, exact_buffer, f) == c.bytes);
        assert(!std::memcmp(exact_buffer, c.value.data(), c.bytes));
        std::free(exact_buffer);
        // Any other length fails without touching the caller's buffer.
        for (u32 wrong : {0u, 1u, c.bytes - 1, c.bytes + 4, 0xffffffffu}) {
            i32 sentinel[8];
            std::memset(sentinel, 0x5a, sizeof(sentinel));
            assert(get(c.pname, wrong, sentinel, f) == 0);
            for (i32 x : sentinel)
                assert(x == 0x5a5a5a5a);
        }
        assert(get(c.pname, c.bytes, nullptr, f) == 0);
    }
    // Unobservable counters and unknown names are not reported.
    for (u32 pname : {kStatsPixelsAfuncFail, kStatsPixelsChromaFail, kStatsPixelsDepthfuncFail,
                      kStatsPixelsIn, kStatsPixelsOut, kStatsTrianglesOut, kSwapHistory,
                      kStatsPixels, kStatsTriangles, kVertexParameter, 0u, 0x2bu, 0x7777u}) {
        i32 sentinel[8];
        std::memset(sentinel, 0x5a, sizeof(sentinel));
        for (u32 bytes : {4u, 8u, 16u, 32u})
            assert(get(pname, bytes, sentinel, f) == 0);
        for (i32 x : sentinel)
            assert(x == 0x5a5a5a5a);
    }
}

static i32 expected_resolutions(u32 res, u32 ref, i32 col, i32 aux, u32 fb) {
    i32 n = 0;
    for (u32 r = 0; r <= 15; ++r)
        for (u32 f = 0; f <= 8; ++f)
            for (i32 c = 1; c <= 3; ++c)
                for (i32 a = 0; a <= 1; ++a) {
                    if ((res != kQueryAny && r != res) ||
                        (ref != kQueryAny && ref != kRefreshNone && f != ref) ||
                        (u32(col) != kQueryAny && c != col) || (u32(aux) != kQueryAny && a != aux))
                        continue;
                    u32 w, h;
                    assert(resolution_size(r, w, h));
                    if (uint64_t(w) * h * 2 * (c + a) < fb - 0x10000)
                        ++n;
                }
    return n * 16;
}
static void resolutions() {
    const u32 fb = 8u << 20;
    Resolution any = {kQueryAny, kQueryAny, -1, -1};
    const i32 all = query_resolutions(&any, nullptr, fb);
    assert(all == expected_resolutions(kQueryAny, kQueryAny, -1, -1, fb) && all > 0 && all % 16 == 0);
    auto *out = exact<Resolution>(size_t(all));
    assert(query_resolutions(&any, out, fb) == all);
    for (i32 i = 0; i < all / 16; ++i) {
        u32 w, h;
        assert(resolution_size(out[i].resolution, w, h) && out[i].refresh <= 8);
        assert(out[i].color_buffers >= 1 && out[i].color_buffers <= 3);
        assert(out[i].aux_buffers >= 0 && out[i].aux_buffers <= 1);
        assert(uint64_t(w) * h * 2 * (out[i].color_buffers + out[i].aux_buffers) < fb - 0x10000);
    }
    std::free(out);
    Resolution one = {7, kRefreshNone, 2, 1}; // 640x480, any refresh, 2+1 buffers
    assert(query_resolutions(&one, nullptr, fb) == 9 * 16);
    out = exact<Resolution>(9 * 16);
    assert(query_resolutions(&one, out, fb) == 9 * 16);
    for (unsigned i = 0; i < 9; ++i)
        assert(out[i].resolution == 7 && out[i].refresh == i && out[i].color_buffers == 2 &&
               out[i].aux_buffers == 1);
    std::free(out);
    one = {14, 0, 3, 1}; // 1600x1200 with four buffers exceeds 8 MiB
    assert(query_resolutions(&one, nullptr, fb) == 0);
    for (Resolution bad : {Resolution{16, 0, 2, 1}, Resolution{7, 9, 2, 1},
                           Resolution{7, 0, 0, 1}, Resolution{7, 0, 4, 1},
                           Resolution{7, 0, 2, 2}, Resolution{7, 0, 2, -2}})
        assert(query_resolutions(&bad, nullptr, fb) == 0);
    assert(query_resolutions(nullptr, nullptr, fb) == 0);
    assert(query_resolutions(&any, nullptr, 0) == 0);
}

static void textures() {
    for (i32 lod = -1; lod <= 9; ++lod)
        assert(lod_ok(lod) == (lod >= 0 && lod <= 8));
    for (i32 a = -5; a <= 5; ++a)
        assert(aspect_ok(a) == (a >= -3 && a <= 3));
    for (i32 f = -1; f <= 0x13; ++f)
        assert(format_ok(f) == ((f >= 0 && f <= 5) || (f >= 8 && f <= 14)));
    assert(tex_ok(0, 8, 0, 0xa) && tex_ok(3, 3, -3, 0) && !tex_ok(4, 3, 0, 0xa));
    assert(!tex_ok(0, 9, 0, 0xa) && !tex_ok(0, 8, 4, 0xa) && !tex_ok(0, 8, 0, 0x10));
    const TexInfo t = to_glide2({0, 8, 3, 0xa, nullptr});
    assert(t.small_lod == 8 && t.large_lod == 0 && t.aspect == 0 && t.format == 0xa);
    assert(to_glide2({2, 5, -3, 0, nullptr}).aspect == 6);
    const float scale[7][2] = {{32, 256}, {64, 256}, {128, 256}, {256, 256},
                               {256, 128}, {256, 64}, {256, 32}};
    for (i32 a = -3; a <= 3; ++a) {
        float s, u;
        tex_scale(a, s, u);
        assert(s == scale[a + 3][0] && u == scale[a + 3][1]);
    }
    u32 w, h;
    level_size(8, 3, w, h);
    assert(w == 256 && h == 32);
    level_size(0, -3, w, h);
    assert(w == 1 && h == 1);
    level_size(3, -1, w, h);
    assert(w == 4 && h == 8);
    assert(host_bytes({0, 8, 0, 0xa, nullptr}) == 174762);
    assert(host_bytes({0, 3, 1, 0x0, nullptr}) == 8 * 4 + 4 * 2 + 2 * 1 + 1 * 1);
}

static void fog_and_palette() {
    const u32 table[][2] = {{0, 0}, {1, 2}, {2, 2}, {3, 3}, {4, 1}, {0x102, 0x102}, {0x204, 0x201}};
    for (const auto &c : table) {
        u32 out = 0xdead;
        assert(fog_mode(c[0], out) && out == c[1]);
    }
    for (u32 bad : {5u, 0xffu, 0x400u, 0x401u, 0x80000002u}) {
        u32 out = 0xdead;
        assert(!fog_mode(bad, out) && out == 0xdead);
    }
    assert(palette6666(0xffffff) == 0xffffffffu && palette6666(0) == 0);
    assert(palette6666(0xfc0000) == 0xff000000u && palette6666(0x00003f) == 0x000000ffu);
    assert(palette6666(0x002000) == 0x00080000u); // R=2 -> 8
    assert(palette6666(0x020000) == 0x00820000u); // R=32 -> 130
    assert(palette6666(0xff000000u | 0x000fc0) == 0x0000ff00u);       // ignores bits 24..31
}

static std::vector<u8> df3(const std::string &header, size_t payload, u8 fill = 0) {
    std::vector<u8> f(header.begin(), header.end());
    for (size_t i = 0; i < payload; ++i)
        f.push_back(u8(fill + i));
    return f;
}
static bool parse(const std::vector<u8> &file, Df3 &h) {
    u8 *bytes = exact<u8>(file.size());
    if (!file.empty())
        std::memcpy(bytes, file.data(), file.size());
    const bool ok = parse_3df(bytes, file.size(), h);
    std::free(bytes);
    return ok;
}
static void files() {
    const std::string rgb = "3df v1.1\nrgb565\nlod range: 1 8\naspect ratio: 2 1\n";
    const u32 mem = 8 * 4 * 2 + 4 * 2 * 2 + 2 * 1 * 2 + 1 * 1 * 2;
    std::vector<u8> file = df3(rgb, mem);
    Df3 h;
    assert(parse(file, h));
    assert(h.format == 0xa && h.small_lod == 0 && h.large_lod == 3 && h.aspect == 1);
    assert(h.width == 8 && h.height == 4 && h.mem_required == mem && h.table_bytes == 0);
    assert(h.data_offset == rgb.size());
    u8 *bytes = exact<u8>(file.size());
    std::memcpy(bytes, file.data(), file.size());
    Df3Table table;
    u8 *texels = exact<u8>(mem);
    assert(load_3df(bytes, file.size(), h, table, texels, mem));
    for (u32 i = 0; i < mem; i += 2) // big-endian file words become native
        assert(texels[i] == u8(i + 1) && texels[i + 1] == u8(i));
    assert(!load_3df(bytes, file.size(), h, table, texels, mem - 1));
    std::free(texels);
    std::free(bytes);
    // Truncated payload and malformed headers are rejected.
    assert(!parse(df3(rgb, mem - 1), h));
    for (const char *bad :
         {"3dx v1.1\nrgb565\nlod range: 1 8\naspect ratio: 2 1\n",
          "3df v1.1\nrgb566\nlod range: 1 8\naspect ratio: 2 1\n",
          "3df v1.1\nrgb565\nlod range: 3 8\naspect ratio: 2 1\n",
          "3df v1.1\nrgb565\nlod range: 8 1\naspect ratio: 2 1\n",
          "3df v1.1\nrgb565\nlod range: 1 512\naspect ratio: 2 1\n",
          "3df v1.1\nrgb565\nlod range: 1 8\naspect ratio: 3 1\n",
          "3df v1.1\nrgb565\nlod range: 1 8\naspect ratio: 16 1\n",
          "3df v1.1\nrgb565\nlod range: 1 8 9\naspect ratio: 2 1\n",
          "3df v1.1\nrgb565\nlod range: 1\naspect ratio: 2 1\n",
          "3df v1.1\nrgb565\nlod range: 1 99999999999\naspect ratio: 2 1\n",
          "3df v1.1\nrgb565\nlod range: 1 8\naspect ratio: 2 1"})
        assert(!parse(df3(bad, 4096), h));
    assert(!parse(std::vector<u8>(300, 'x'), h));
    assert(!parse(df3("3df v1.1\n" + std::string(400, 'a') + "\n", 64), h));
    assert(!parse({}, h));
    // CRLF headers, upper-case formats and the tall aspect.
    const std::string p8 = "3df v1.1\r\nP8\r\nlod range: 4 16\r\naspect ratio: 1 4\r\n";
    file = df3(p8, 1024 + 4 * 16 + 2 * 8 + 1 * 4);
    assert(parse(file, h) && h.format == 5 && h.aspect == -2 && h.width == 4 && h.height == 16);
    assert(h.table_bytes == 1024 && h.mem_required == 84);
    std::vector<u8> texels8(84);
    assert(load_3df(file.data(), file.size(), h, table, texels8.data(), 84));
    assert(table.palette[0] == 0x00010203u && table.palette[1] == 0x04050607u);
    assert(texels8[0] == u8(1024) && texels8[83] == u8(1024 + 83));
    // NCC tables are decoded and repacked exactly as the shared core expects.
    const std::string yiq = "3df v1.1\nyiq\nlod range: 1 1\naspect ratio: 1 1\n";
    file = df3(yiq, 80 + 1, 0);
    for (size_t i = 0; i < 40; ++i) {
        file[yiq.size() + 2 * i] = u8(i == 20 ? 0xfe : 0);
        file[yiq.size() + 2 * i + 1] = u8(i + 1);
    }
    assert(parse(file, h) && h.table_bytes == 80 && h.mem_required == 1);
    u8 one;
    assert(load_3df(file.data(), file.size(), h, table, &one, 1));
    assert(table.ncc.y[0] == 1 && table.ncc.y[15] == 16);
    assert(table.ncc.i[0][0] == 17 && table.ncc.i[1][1] == ((0xfe15) & 0x1ff));
    assert(table.ncc.q[3][2] == 40);
    assert(table.ncc.packed[0] == (1u | 2u << 8 | 3u << 16 | 4u << 24));
    assert(table.ncc.packed[4] == (17u << 18 | 18u << 9 | 19u));
    assert(table.ncc.packed[11] == (38u << 18 | 39u << 9 | 40u));
}

int main() {
    layouts();
    window_conversion();
    clip_conversion();
    assembly();
    sources();
    queries();
    resolutions();
    textures();
    fog_and_palette();
    files();
    std::puts("PASS actual Glide 3 front end: layout bounds and disabled attributes, window/clip "
              "w/q/oow, strip/fan/continue assembly, arrays and strides, grGet buffers, "
              "resolutions, texture descriptors, fog, palettes, .3df (ASan/UBSan)");
}
'''
compiler = os.environ.get('CXX') or shutil.which('clang++')
if not compiler:
    raise SystemExit('clang++ (or CXX) is required for the actual-source sanitizer gate')
with tempfile.TemporaryDirectory(prefix='dg-glide3-core-') as temporary:
    work = Path(temporary)
    (work / 'test.cpp').write_text(HARNESS)
    subprocess.run([compiler, '-std=c++20', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-fno-omit-frame-pointer', '-I', str(ROOT / 'guest/glide'),
                    str(work / 'test.cpp'), '-o', str(work / 'test')], check=True)
    subprocess.run([str(work / 'test')], check=True)
