#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual patched chroma key path: every window colour format keys the same palette entry.

grChromakeyValue takes a colour in the grSstWinOpen colour format and converts it
to 0xAARRGGBB with ConvertColor4B; PGTexture compares that with the ARGB palette.
The donor's RGBA case rotated the wrong way, so Diablo II's key 0x000000ff
(opaque black in RGBA) became green, no palette entry matched and every keyed
sprite (menu text, the logo) was drawn on black. Fails without patch 0005.
"""
from pathlib import Path
import subprocess
import tempfile
from source import patched_source


def function(source, signature):
    start = source.index(signature)
    end = source.index('\n}\n', start) + 3
    return source[start:end]


convert = function(patched_source('GLutil.cpp'), 'void ConvertColor4B(')
texture = patched_source('PGTexture.cpp')
key = function(texture, 'void PGTexture::ChromakeyValue(')
apply_key = function(texture, 'void PGTexture::ApplyKeyToPalette(')

program = r'''
#include <cassert>
#include <cstdint>
using FxU32 = std::uint32_t; using FxU16 = std::uint16_t; using GrColor_t = FxU32;
using GrChromakeyMode_t = int;
enum { GR_COLORFORMAT_ARGB, GR_COLORFORMAT_ABGR, GR_COLORFORMAT_RGBA, GR_COLORFORMAT_BGRA };
static struct { struct { int ColorFormat; } State; } Glide;
static struct { bool IgnorePaletteChange; } InternalConfig;
''' + convert + r'''
struct PGTexture {
    FxU32 m_chromakey_value_8888 = 0; FxU16 m_chromakey_value_565 = 0;
    GrChromakeyMode_t m_chromakey_mode = 1; bool m_palette_dirty = false;
    FxU32 m_palette[256] = {}, m_palette_hash = 0;
    void ChromakeyValue(GrColor_t value);
    void ApplyKeyToPalette(void);
};
''' + key + apply_key + r'''
static FxU32 keyed(int format, FxU32 value) {
    Glide.State.ColorFormat = format;
    FxU32 argb = 0;
    ConvertColor4B(value, argb);
    static PGTexture t;
    t = PGTexture{};
    t.m_palette[0] = 0xff000000;  // Diablo II palette entry 0: opaque black
    t.m_palette[1] = 0xff240000;
    t.m_palette[2] = 0xff0000ff;
    t.m_palette[3] = 0xff00ff00;
    t.ChromakeyValue(argb);
    t.ApplyKeyToPalette();
    FxU32 mask = 0;
    for (unsigned i = 0; i < 4; ++i)
        if (!(t.m_palette[i] >> 24)) mask |= 1u << i;
    return mask;
}
int main() {
    // Each format's encoding of A=0x80 R=0x12 G=0x34 B=0x56 converts to one ARGB value.
    const FxU32 encoded[] = {0x80123456, 0x80563412, 0x12345680, 0x56341280};
    for (int f = 0; f < 4; ++f) {
        Glide.State.ColorFormat = f;
        FxU32 c = 0;
        ConvertColor4B(encoded[f], c);
        assert(c == 0x80123456);
    }
    // Opaque black keys only entry 0 in every format (Diablo II: RGBA 0x000000ff).
    assert(keyed(GR_COLORFORMAT_RGBA, 0x000000ff) == 1);
    assert(keyed(GR_COLORFORMAT_ARGB, 0xff000000) == 1);
    assert(keyed(GR_COLORFORMAT_ABGR, 0xff000000) == 1);
    assert(keyed(GR_COLORFORMAT_BGRA, 0x000000ff) == 1);
    // Pure blue and green key their own entries, not each other.
    assert(keyed(GR_COLORFORMAT_RGBA, 0x0000ffff) == 4);
    assert(keyed(GR_COLORFORMAT_ARGB, 0xff0000ff) == 4);
    assert(keyed(GR_COLORFORMAT_RGBA, 0x00ff00ff) == 8);
    assert(keyed(GR_COLORFORMAT_BGRA, 0xff0000ff) == 4);
}
'''

with tempfile.TemporaryDirectory(prefix='dg-glide-chromakey-') as temporary:
    work = Path(temporary)
    (work / 'test.cpp').write_text(program)
    subprocess.run(['c++', '-std=c++23', '-O2', '-Wall', '-Wextra', '-Werror', '-Wno-unused-variable',
                    '-fsanitize=address,undefined', str(work / 'test.cpp'), '-o', str(work / 'test')], check=True)
    subprocess.run([str(work / 'test')], check=True)
print('PASS actual Glide chroma key: ARGB/ABGR/RGBA/BGRA keys convert to the ARGB palette value')
