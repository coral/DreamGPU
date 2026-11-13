#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Patch 0068 on actual pinned Wine source: P8 display modes are enumerated
from the 32-bit modes a P8 request sets.

wined3d_set_adapter_display_mode never sets an 8-bit desktop: a one-byte
format switches to 32 bits per pixel and the palette is converted on
presentation. Enumeration still matched dmBitsPerPel against the format's own
8 bits, so with a Windows 2000/XP display driver that lists only 16- and
32-bit modes, DirectDraw reported no 8-bit mode at all while setting one
worked. Age of Empires II looks for its 8-bit mode first and stopped with
"Could not initialize graphics system".

The prepared wined3d_get_adapter_mode_count and wined3d_enum_adapter_modes
run here under ASan/UBSan against a mode list like the NT driver's (16 and
32 bpp only) and one that also lists 8 bpp natively (Windows 98).
The check fails without the patch.
"""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('patches', ROOT / 'tests/guest/support/patches.py')
patches = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patches)
MANIFEST = ROOT / 'support/guest/d3d/patches/base/manifest.json'


def function(source, signature):
    start = source.index(signature)
    end = source.index('\n}\n', start) + 3
    return source[start:end]


PREAMBLE = r'''
#include <assert.h>
#include <limits.h>
#include <string.h>
typedef unsigned int UINT, DWORD;
typedef int BOOL, HRESULT;
#define TRUE 1
#define FALSE 0
#define CDECL
#define TRACE(...) do {} while (0)
#define WARN(...) do {} while (0)
#define WINED3DERR_INVALIDCALL ((HRESULT)0x8876086c)
#define WINED3D_OK 0
#define DEFAULT_REFRESH_RATE 0
#define DM_DISPLAYFLAGS 0x200000
#define DM_DISPLAYFREQUENCY 0x400000
#define DM_INTERLACED 2
#define DUMMYACCESS1(m, u, f) ((m).f)
#define debug_d3dformat(id) ""
enum wined3d_format_id { WINED3DFMT_UNKNOWN, WINED3DFMT_P8_UINT, WINED3DFMT_B5G6R5_UNORM, WINED3DFMT_B8G8R8X8_UNORM };
enum wined3d_scanline_ordering { WINED3D_SCANLINE_ORDERING_UNKNOWN, WINED3D_SCANLINE_ORDERING_PROGRESSIVE, WINED3D_SCANLINE_ORDERING_INTERLACED };
struct wined3d_format { UINT byte_count; };
struct wined3d_gl_info { int unused; };
struct wined3d_adapter { const char *DeviceName; struct wined3d_gl_info gl_info; };
struct wined3d { UINT adapter_count; struct wined3d_adapter adapters[1]; };
struct wined3d_display_mode { UINT width, height, refresh_rate; enum wined3d_format_id format_id; enum wined3d_scanline_ordering scanline_ordering; };
typedef struct { DWORD dmSize, dmFields, dmBitsPerPel, dmPelsWidth, dmPelsHeight, dmDisplayFlags, dmDisplayFrequency; } DEVMODEA;
static const struct wined3d_format formats[] = { {0}, {1}, {2}, {4} };
static const struct wined3d_format *wined3d_get_format(const struct wined3d_gl_info *gl, enum wined3d_format_id id)
{ (void)gl; return &formats[id]; }
static enum wined3d_format_id pixelformat_for_depth(DWORD bpp)
{ return bpp == 32 ? WINED3DFMT_B8G8R8X8_UNORM : bpp == 16 ? WINED3DFMT_B5G6R5_UNORM : WINED3DFMT_P8_UINT; }
static const DWORD *modes; static unsigned int mode_count;
static BOOL EnumDisplaySettingsA95(const char *name, DWORD i, DEVMODEA *m)
{
    (void)name;
    if (i >= mode_count) return FALSE;
    m->dmFields = 0; m->dmPelsWidth = modes[3 * i]; m->dmPelsHeight = modes[3 * i + 1]; m->dmBitsPerPel = modes[3 * i + 2];
    return TRUE;
}
'''

TEST = r'''
static void check(const DWORD *list, unsigned int count, UINT expect_p8, UINT expect_16, UINT expect_unknown)
{
    struct wined3d w; struct wined3d_display_mode m; UINT i;
    memset(&w, 0, sizeof w); w.adapter_count = 1;
    modes = list; mode_count = count;
    assert(wined3d_get_adapter_mode_count(&w, 0, WINED3DFMT_P8_UINT, WINED3D_SCANLINE_ORDERING_UNKNOWN) == expect_p8);
    assert(wined3d_get_adapter_mode_count(&w, 0, WINED3DFMT_B5G6R5_UNORM, WINED3D_SCANLINE_ORDERING_UNKNOWN) == expect_16);
    assert(wined3d_get_adapter_mode_count(&w, 0, WINED3DFMT_UNKNOWN, WINED3D_SCANLINE_ORDERING_UNKNOWN) == expect_unknown);
    for (i = 0; i < expect_p8; ++i)
    {
        assert(wined3d_enum_adapter_modes(&w, 0, WINED3DFMT_P8_UINT, WINED3D_SCANLINE_ORDERING_UNKNOWN, i, &m) == WINED3D_OK);
        assert(m.format_id == WINED3DFMT_P8_UINT);
    }
    assert(wined3d_enum_adapter_modes(&w, 0, WINED3DFMT_P8_UINT, WINED3D_SCANLINE_ORDERING_UNKNOWN, expect_p8, &m) == WINED3DERR_INVALIDCALL);
}

int main(void)
{
    /* Windows 2000/XP DreamGPU driver: 32- then 16-bit modes, no 8-bit. */
    static const DWORD nt[] = { 640, 480, 32, 800, 600, 32, 1024, 768, 32, 640, 480, 16, 800, 600, 16, 1024, 768, 16 };
    /* A driver that also lists 8 bpp natively; P8 still follows the 32-bit set. */
    static const DWORD w98[] = { 640, 480, 8, 800, 600, 8, 640, 480, 16, 800, 600, 16, 640, 480, 32, 800, 600, 32 };
    struct wined3d w; struct wined3d_display_mode m;
    check(nt, 6, 3, 3, 6);
    check(w98, 6, 2, 2, 4);
    /* The P8 modes are the 32-bit resolutions, in order. */
    memset(&w, 0, sizeof w); w.adapter_count = 1; modes = nt; mode_count = 6;
    assert(wined3d_enum_adapter_modes(&w, 0, WINED3DFMT_P8_UINT, WINED3D_SCANLINE_ORDERING_UNKNOWN, 1, &m) == WINED3D_OK);
    assert(m.width == 800 && m.height == 600);
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='dreamgpu-p8-modes-') as temporary:
    work = Path(temporary)
    for name in json.loads(MANIFEST.read_text())['files']:
        p = work / name
        p.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'vendor/wine9x' / name, p)
    patches.apply(work, MANIFEST)
    directx = (work / 'wined3d/directx.c').read_text()
    parts = []
    if 'static UINT wined3d_mode_enum_bits(' in directx:
        parts.append(function(directx, 'static UINT wined3d_mode_enum_bits('))
    parts.append(function(directx, 'UINT CDECL wined3d_get_adapter_mode_count('))
    parts.append(function(directx, 'HRESULT CDECL wined3d_enum_adapter_modes('))
    (work / 'test.c').write_text(PREAMBLE + '\n'.join(parts) + TEST)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-function', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    str(work / 'test.c'), '-o', str(work / 'test')], check=True)
    subprocess.run([str(work / 'test')], check=True)
    # The setter still maps every one-byte format to a 32-bit desktop mode.
    setter = function(directx, 'HRESULT CDECL wined3d_set_adapter_display_mode(')
    assert 'else if(format->byte_count == 1)' in setter and 'new_mode.dmBitsPerPel = 4 * CHAR_BIT;' in setter

print('PASS prepared adapter mode enumeration: P8 modes are the 32-bit modes a P8 request sets; '
      '16-bit and d3d8 (UNKNOWN) enumeration unchanged')
