#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The Glide 3 diagnostic call log (DREAMGPU_GLIDE3_TRACE).

Runs the actual line formatter (guest/glide/glide3-core.h) under ASan/UBSan
with exactly sized caller memory, and checks the production front end: every
exported entry point logs itself under its own name, the log is enabled only by
the environment variable, its buffer is heap-allocated (this DLL's globals are
file-backed) and it is written at swap, flush, finish, close and shutdown."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
HARNESS = r'''
#include "glide3-core.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
using namespace dg3;

static std::string text(trace::Line &l) {
    const unsigned n = l.end();
    return std::string(l.text, n);
}
static std::string flt(float v) {
    trace::Line l;
    l.flt(v);
    return std::string(l.text, l.n);
}
template <class T> static T *exact(size_t bytes) {
    return static_cast<T *>(std::malloc(bytes ? bytes : 1));
}
int main() {
    trace::Line l;
    l.begin(7, 1234, "grColorCombine");
    l.i("function", 3).i("factor", -1).u("invert", 1).x("value", 0xff00a0u);
    assert(text(l) == "7 1234 grColorCombine(function=3, factor=-1, invert=1, value=0x00ff00a0)\r\n");
    l.begin(1, 0, "grFlush");
    assert(text(l) == "1 0 grFlush()\r\n");
    l.begin(2, 0, "grGetString");
    l.s("result", "Voodoo \"Banshee\"\n").s("none", nullptr).p("p", nullptr);
    assert(text(l) == "2 0 grGetString(result=\"Voodoo ?Banshee??\", none=null, p=0x00000000)\r\n");

    assert(flt(0.f) == "0.000000" && flt(-0.f) == "-0.000000");
    assert(flt(1.f) == "1.000000" && flt(-1.5f) == "-1.500000");
    assert(flt(640.25f) == "640.250000" && flt(0.00390625f) == "0.003906");
    assert(flt(1e-5f) == "1.000000e-5" && flt(-2.5e10f) == "-2.500000e10");
    assert(flt(std::numeric_limits<float>::quiet_NaN()) == "nan");
    assert(flt(std::numeric_limits<float>::infinity()) == "inf");
    assert(flt(-std::numeric_limits<float>::infinity()) == "-inf");
    assert(flt(std::numeric_limits<float>::denorm_min()) == "1.401298e-45");
    assert(flt(std::numeric_limits<float>::max()) == "3.402823e38");
    assert(flt(9.9999999e8f) == "1.000000e9" || flt(9.9999999e8f) == "1000000000.000000");

    // Enabled layout parameters only; XY is read from offset zero.
    Layout layout;
    reset(layout);
    assert(set_param(layout, kParamXY, 0, kParamEnable) == SetResult::applied);
    assert(set_param(layout, kParamQ, 8, kParamEnable) == SetResult::applied);
    assert(set_param(layout, kParamPARGB, 12, kParamEnable) == SetResult::applied);
    assert(set_param(layout, kParamST0, 16, kParamEnable) == SetResult::applied);
    const unsigned stride = 24;
    auto *verts = exact<unsigned char>(3 * stride);
    for (unsigned k = 0; k < 3; ++k) {
        const float f[6] = {float(k), 2.f, 0.5f, 0, 1.f + k, 0.25f};
        std::memcpy(verts + k * stride, f, sizeof(f));
        const u32 c = 0xff102030u + k;
        std::memcpy(verts + k * stride + 12, &c, 4);
    }
    l.begin(3, 5, "grDrawPoint");
    l.vertex("pt", layout, verts);
    assert(text(l) == "3 5 grDrawPoint(pt={xy:0.000000/2.000000 q:0.500000 pargb:0xff102030 "
                      "st0:1.000000/0.250000})\r\n");
    l.begin(4, 5, "grDrawVertexArrayContiguous");
    l.u("count", 2).strided(layout, verts, 2, stride);
    const std::string run = text(l);
    assert(run.find("v0={xy:0.000000/") != std::string::npos);
    assert(run.find("v1={xy:1.000000/") != std::string::npos && run.find("v2=") == std::string::npos);
    auto **pointers = exact<const void *>(3 * sizeof(void *));
    for (unsigned k = 0; k < 3; ++k)
        pointers[k] = verts + (2 - k) * stride;
    l.begin(5, 5, "grDrawVertexArray");
    l.array(layout, pointers, 3);
    const std::string array = text(l);
    assert(array.find("v0={xy:2.000000/") != std::string::npos);
    assert(array.find("v2={xy:0.000000/") != std::string::npos);
    l.begin(5, 5, "grDrawVertexArray");
    l.array(layout, pointers, 0);
    assert(text(l) == "5 5 grDrawVertexArray()\r\n");

    // A 32x32 P8 texture with one 16x16 mip level: descriptor, bytes and FNV.
    const u32 bytes = 32 * 32 + 16 * 16;
    auto *texels = exact<unsigned char>(bytes);
    for (u32 i = 0; i < bytes; ++i)
        texels[i] = (unsigned char)(i * 7);
    TexInfo info = {4, 5, 0, 0x5, texels};
    l.begin(6, 9, "grTexDownloadMipMap");
    l.tex("info", &info).texels(&info);
    char expected[160];
    std::snprintf(expected, sizeof(expected),
                  "info={small=4, large=5, aspect=0, format=0x00000005, data=0x%08x}, bytes=%u, "
                  "fnv=0x%08x",
                  unsigned(uintptr_t(texels)), bytes, trace::fnv1a(texels, bytes));
    assert(text(l).find(expected) != std::string::npos);
    TexInfo bad = {6, 5, 0, 0x5, texels}; // small > large: no bytes are read
    l.begin(6, 9, "grTexDownloadMipMap");
    l.texels(&bad).tex("null", nullptr);
    assert(text(l) == "6 9 grTexDownloadMipMap(null=0x00000000)\r\n");
    // Rows [2, 3] of the 32-wide level, and an out-of-range row request.
    l.begin(8, 9, "grTexDownloadMipMapLevelPartial");
    l.level(5, 0, 0x5, texels, 2, 3);
    std::snprintf(expected, sizeof(expected), "bytes=64, fnv=0x%08x", trace::fnv1a(texels, 64));
    assert(text(l).find(expected) != std::string::npos);
    l.begin(8, 9, "grTexDownloadMipMapLevelPartial");
    l.level(5, 0, 0x5, texels, 2, 32).level(9, 0, 0x5, texels, 0, -1);
    assert(text(l) == "8 9 grTexDownloadMipMapLevelPartial()\r\n");
    l.begin(8, 9, "grTexDownloadMipMapLevel");
    l.level(4, -1, 0xa, texels, 0, -1); // 16x8 16-bit texels
    std::snprintf(expected, sizeof(expected), "bytes=256, fnv=0x%08x", trace::fnv1a(texels, 256));
    assert(text(l).find(expected) != std::string::npos);

    // Palettes: the first eight entries of the range and its checksum.
    auto *palette = exact<u32>(256 * 4);
    for (u32 i = 0; i < 256; ++i)
        palette[i] = 0xff000000u | i;
    l.begin(9, 9, "grTexDownloadTablePartial");
    l.table(2, palette, 250, 255);
    std::snprintf(expected, sizeof(expected),
                  "entries=[0xff0000fa 0xff0000fb 0xff0000fc 0xff0000fd 0xff0000fe 0xff0000ff], "
                  "fnv=0x%08x)",
                  trace::fnv1a(palette + 250, 24));
    assert(text(l).find(expected) != std::string::npos);
    l.begin(9, 9, "grTexDownloadTable");
    l.table(3, palette, 0, 255);
    const std::string whole = text(l);
    assert(whole.find("0xff000007]") != std::string::npos && whole.find("0xff000008") == std::string::npos);
    l.begin(9, 9, "grTexDownloadTable");
    l.table(0, palette, 0, 11).table(2, palette, 5, 4).table(2, palette, 0, 256);
    assert(text(l) == "9 9 grTexDownloadTable()\r\n");

    trace::Line::LfbView view = {20, texels, 2048, 1, 0};
    l.begin(10, 9, "grLfbLock");
    l.lfb(&view).lfb(nullptr).blob(nullptr, 4);
    std::snprintf(expected, sizeof(expected),
                  "10 9 grLfbLock(lfbPtr=0x%08x, strideInBytes=2048, writeMode=0x00000001, "
                  "origin=0)\r\n",
                  unsigned(uintptr_t(texels)));
    assert(text(l) == expected);
    l.begin(11, 9, "grLoadGammaTable");
    l.gamma(300, palette, palette, palette);
    std::snprintf(expected, sizeof(expected), "red_fnv=0x%08x", trace::fnv1a(palette, 1024));
    assert(text(l).find(expected) != std::string::npos);

    // An oversized line is cut and marked, never overrun.
    l.begin(12, 9, "grGet");
    l.words("values", palette, 256);
    const std::string cut = text(l);
    assert(cut.size() <= trace::kLineBytes && cut.size() > trace::kLineBytes - 32);
    assert(cut.substr(cut.size() - 6) == "...)\r\n");
    assert(trace::fnv1a(nullptr, 9) == 2166136261u && trace::fnv1a("a", 1) == 0xe40c292cu);
    std::free(verts);
    std::free(pointers);
    std::free(texels);
    std::free(palette);
    std::puts("PASS Glide 3 trace formatter: arguments, floats, vertices, textures, tables, "
              "LFB and truncation");
}
'''

compiler = os.environ.get('CXX') or shutil.which('clang++')
if not compiler:
    raise SystemExit('clang++ (or CXX) is required for the actual-source sanitizer gate')
with tempfile.TemporaryDirectory(prefix='dg-glide3-trace-') as temporary:
    work = Path(temporary)
    (work / 'test.cpp').write_text(HARNESS)
    subprocess.run([compiler, '-std=c++20', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-I', str(ROOT / 'guest/glide'), str(work / 'test.cpp'), '-o',
                    str(work / 'test')], check=True)
    subprocess.run([str(work / 'test')], check=True)

# Production front end: one trace per exported entry point, under its name.
source = (ROOT / 'guest/glide/glide3.cpp').read_text()
api = (ROOT / 'guest/glide/glide3-api.h').read_text()
exports = re.findall(r'^DG3_API\([^)]*\)\s*dg3_(\w+)\(', api, re.M)
assert len(exports) == 97, len(exports)
for name in exports:
    match = re.search(r'DG3_API\([^)]*\)\s*dg3_' + name + r'\(([^)]*)\)\s*\{(.*?)\n\}', source, re.S)
    assert match, name
    body = match.group(2)
    assert body.count('trace_call("' + name + '")') == 1, f'{name} is not traced under its name'
    assert body.count('trace_call(') == 1, f'{name} traces twice'
for name in ('grBufferSwap', 'grFlush', 'grFinish', 'grSstWinClose', 'grGlideShutdown'):
    body = re.search(r'dg3_' + name + r'\([^)]*\)\s*\{(.*?)\n\}', source, re.S).group(1)
    assert 'trace_flush();' in body, f'{name} must write buffered trace lines'
sink = source[source.index('constexpr dg3::u32 kTraceLimit'):source.index(' rendering\n')]
assert sink.count('GetEnvironmentVariableA("DREAMGPU_GLIDE3_TRACE"') == 1
assert 'Trace *trace_state;' in sink and 'HeapAlloc(GetProcessHeap(), 0, sizeof(Trace))' in sink
assert re.search(r'\bTrace\s+trace_state\b', sink) is None, 'trace buffer must not be a global'
assert 'CREATE_ALWAYS' in sink and 'trace_module("glide3x"' in sink and 'trace_module("dgpugl"' in sink
assert 'kTraceLimit = 64u << 20' in sink
readme = (ROOT / 'guest/glide/README.md').read_text()
assert 'DREAMGPU_GLIDE3_TRACE' in readme
print(f'PASS Glide 3 trace wiring: {len(exports)} entry points traced, env-gated, heap buffer, '
      'flushed at swap/flush/finish/close/shutdown')
