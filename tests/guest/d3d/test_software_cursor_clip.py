#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute the prepared WineD3D software cursor at the back buffer's edges (0061).

WineD3D draws a Direct3D cursor that is not a Windows cursor (any size but
32x32, or a 32x32 one SetCursorPos could not reach) into the back buffer at
Present with an alpha-tested wined3d_surface_blt. That blit rejects a
destination rectangle outside the surface (WINEDDERR_INVALIDRECT), so a cursor
crossing an edge vanished entirely; native Direct3D, and later Wine with
wined3d_clip_blit, draw the part inside. The prepared cursor block of
swapchain_gl_present and its clip helper run here under ASan/UBSan with the
prepared rectangle validation of wined3d_surface_blt and an alpha-tested,
unscaled copy, at positions inside, across each edge and corner (also through
a hotspot beyond the position and a windowed client offset), entirely outside,
hidden, and as a Windows cursor. Every back buffer pixel is compared with the
cursor's visible texels. Mutations of the patch must fail the test.
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

PREAMBLE = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef int BOOL, HRESULT;
typedef int32_t LONG; /* the guest's 32-bit LONG */
typedef unsigned int UINT, DWORD;
typedef unsigned char BYTE;
typedef void *HWND, *HCURSOR;
typedef struct { LONG left, top, right, bottom; } RECT;
typedef struct { LONG x, y; } POINT;
#define TRUE 1
#define FALSE 0
#define TRACE(...) ((void)0)
#define WARN(...) ((void)0)
#define WINED3D_OK 0
#define WINEDDERR_INVALIDRECT ((HRESULT)0x88760096)
#define WINEDDBLT_ALPHATEST 0x00000001
#define WINED3D_TEXF_POINT 1

enum { kWidth = 64, kHeight = 48, kCursorWidth = 16, kCursorHeight = 12, kOpaqueColumns = 10 };
static const uint32_t background = 0xff102030;

struct wined3d_resource_model { UINT width, height; };
struct wined3d_surface { struct wined3d_resource_model resource; uint32_t *pixels; };
struct wined3d_texture { struct wined3d_surface surface; };
struct wined3d_device {
    BYTE bCursorVisible : 1;
    UINT xHotSpot, yHotSpot, xScreenSpace, yScreenSpace, cursorWidth, cursorHeight;
    struct wined3d_texture *cursor_texture;
    HCURSOR hardwareCursor;
};
struct wined3d_swapchain { struct wined3d_device *device; struct { BOOL windowed; } desc; HWND win_handle; };

static struct wined3d_surface *wined3d_texture_get_sub_resource(struct wined3d_texture *texture, unsigned int index)
{
    assert(texture && !index);
    return &texture->surface;
}
#define surface_from_resource(resource) (resource)

static int window_object;
static LONG client_x, client_y;
static int maps;
static void MapWindowPoints(HWND from, HWND to, POINT *points, UINT count)
{
    assert(!from && to == &window_object && count == 2);
    for (UINT i = 0; i < count; ++i)
    {
        points[i].x -= client_x;
        points[i].y -= client_y;
    }
    ++maps;
}

static void surface_get_rect(const struct wined3d_surface *surface, const RECT *rect_in, RECT *rect_out)
{
    if (rect_in)
        *rect_out = *rect_in;
    else
    {
        rect_out->left = 0;
        rect_out->top = 0;
        rect_out->right = surface->resource.width;
        rect_out->bottom = surface->resource.height;
    }
}

static int blts, rejected, scaled;

/* The prepared validation of wined3d_surface_blt, then the GPU blitters'
 * alpha test: texels with alpha 0 are dropped, the rest copied. */
static HRESULT blt(struct wined3d_surface *dst_surface, const RECT *dst_rect_in,
        struct wined3d_surface *src_surface, const RECT *src_rect_in)
{
    RECT src_rect, dst_rect;
@VALIDATION@
    if (dst_rect.right - dst_rect.left != src_rect.right - src_rect.left
            || dst_rect.bottom - dst_rect.top != src_rect.bottom - src_rect.top)
    {
        ++scaled; /* WineD3D would stretch the cursor */
        return WINED3D_OK;
    }
    for (LONG y = 0; y < dst_rect.bottom - dst_rect.top; ++y)
        for (LONG x = 0; x < dst_rect.right - dst_rect.left; ++x)
        {
            uint32_t texel = src_surface->pixels[(src_rect.top + y) * src_surface->resource.width + src_rect.left + x];
            if (texel >> 24)
                dst_surface->pixels[(dst_rect.top + y) * dst_surface->resource.width + dst_rect.left + x] = texel;
        }
    return WINED3D_OK;
}

static HRESULT wined3d_surface_blt(struct wined3d_surface *dst_surface, const RECT *dst_rect_in,
        struct wined3d_surface *src_surface, const RECT *src_rect_in, DWORD flags, const void *fx, int filter)
{
    HRESULT hr;
    assert(flags == WINEDDBLT_ALPHATEST && !fx && filter == WINED3D_TEXF_POINT);
    ++blts;
    hr = blt(dst_surface, dst_rect_in, src_surface, src_rect_in);
    if (hr != WINED3D_OK)
        ++rejected;
    return hr;
}
'''

WRAPPER = r'''
static void present_cursor(struct wined3d_swapchain *swapchain, struct wined3d_surface *back_buffer)
{
@BLOCK@
}
'''

TEST = r'''
static uint32_t cursor_pixels[kCursorWidth * kCursorHeight], frame[kWidth * kHeight];

static uint32_t texel(LONG x, LONG y)
{
    return x < kOpaqueColumns ? 0xff000000u | (uint32_t)(y << 8 | x) << 4 | 0x00800000u : 0x0000ff00u;
}

/* x, y: the position given to SetCursorPosition (screen space). */
static void scenario(const char *name, LONG x, LONG y, UINT hot_x, UINT hot_y, BOOL windowed, LONG origin_x,
        LONG origin_y, BOOL visible, BOOL windows_cursor)
{
    struct wined3d_texture cursor = {{{kCursorWidth, kCursorHeight}, cursor_pixels}};
    struct wined3d_surface back_buffer = {{kWidth, kHeight}, frame};
    struct wined3d_device device;
    struct wined3d_swapchain swapchain = {&device, {windowed}, &window_object};
    LONG left = x - (LONG)hot_x - (windowed ? origin_x : 0), top = y - (LONG)hot_y - (windowed ? origin_y : 0);
    int inside = 0, mismatches = 0;

    memset(&device, 0, sizeof(device));
    device.bCursorVisible = visible;
    device.xHotSpot = hot_x;
    device.yHotSpot = hot_y;
    device.xScreenSpace = (UINT)x;
    device.yScreenSpace = (UINT)y;
    device.cursorWidth = kCursorWidth;
    device.cursorHeight = kCursorHeight;
    device.cursor_texture = &cursor;
    device.hardwareCursor = windows_cursor ? (HCURSOR)&window_object : NULL;
    client_x = origin_x;
    client_y = origin_y;
    blts = rejected = scaled = maps = 0;
    for (int i = 0; i < kWidth * kHeight; ++i)
        frame[i] = background;

    present_cursor(&swapchain, &back_buffer);

    for (LONG cy = 0; cy < kCursorHeight; ++cy)
        for (LONG cx = 0; cx < kCursorWidth; ++cx)
            if (left + cx >= 0 && left + cx < kWidth && top + cy >= 0 && top + cy < kHeight)
                ++inside;
    for (LONG py = 0; py < kHeight; ++py)
        for (LONG px = 0; px < kWidth; ++px)
        {
            LONG cx = px - left, cy = py - top;
            uint32_t expected = background;
            if (visible && !windows_cursor && cx >= 0 && cx < kCursorWidth && cy >= 0 && cy < kCursorHeight
                    && (texel(cx, cy) >> 24))
                expected = texel(cx, cy);
            if (frame[py * kWidth + px] != expected && !mismatches++)
                printf("%s: pixel %d,%d is %08x, expected %08x\n", name, px, py, frame[py * kWidth + px], expected);
        }
    if (mismatches || rejected || scaled || blts != (visible && !windows_cursor && inside ? 1 : 0)
            || maps != (visible && !windows_cursor && windowed ? 1 : 0))
    {
        printf("%s: %d mismatches, %d blits, %d rejected, %d scaled, %d maps\n", name, mismatches, blts, rejected,
                scaled, maps);
        assert(0);
    }
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    for (LONG y = 0; y < kCursorHeight; ++y)
        for (LONG x = 0; x < kCursorWidth; ++x)
            cursor_pixels[y * kCursorWidth + x] = texel(x, y);

    scenario("inside", 20, 10, 0, 0, FALSE, 0, 0, TRUE, FALSE);
    scenario("origin", 0, 0, 0, 0, FALSE, 0, 0, TRUE, FALSE);
    scenario("touching the bottom-right corner", kWidth - kCursorWidth, kHeight - kCursorHeight, 0, 0, FALSE, 0, 0,
            TRUE, FALSE);
    scenario("across the right edge", kWidth - 8, 10, 0, 0, FALSE, 0, 0, TRUE, FALSE);
    scenario("across the right edge, opaque part outside", kWidth - 3, 10, 0, 0, FALSE, 0, 0, TRUE, FALSE);
    scenario("across the bottom edge", 20, kHeight - 5, 0, 0, FALSE, 0, 0, TRUE, FALSE);
    scenario("across the bottom-right corner", kWidth - 4, kHeight - 4, 0, 0, FALSE, 0, 0, TRUE, FALSE);
    scenario("last pixel", kWidth - 1, kHeight - 1, 0, 0, FALSE, 0, 0, TRUE, FALSE);
    scenario("hotspot beyond the position (top-left)", 2, 3, 8, 8, FALSE, 0, 0, TRUE, FALSE);
    scenario("hotspot beyond the position (one column)", 0, 0, 15, 11, FALSE, 0, 0, TRUE, FALSE);
    scenario("hotspot, across the left edge", 5, 20, 9, 2, FALSE, 0, 0, TRUE, FALSE);
    scenario("hotspot, across the top edge", 30, 1, 0, 6, FALSE, 0, 0, TRUE, FALSE);
    scenario("beyond the right edge", kWidth, 10, 0, 0, FALSE, 0, 0, TRUE, FALSE);
    scenario("beyond the bottom edge", 20, kHeight, 0, 0, FALSE, 0, 0, TRUE, FALSE);
    scenario("far beyond", 5000, 4000, 0, 0, FALSE, 0, 0, TRUE, FALSE);
    scenario("windowed inside", 40, 30, 0, 0, TRUE, 30, 20, TRUE, FALSE);
    scenario("windowed across the client's left edge", 25, 25, 0, 0, TRUE, 30, 20, TRUE, FALSE);
    scenario("windowed across the client's top-left corner", 25, 15, 0, 0, TRUE, 30, 20, TRUE, FALSE);
    scenario("windowed left of the client", 5, 25, 0, 0, TRUE, 30, 20, TRUE, FALSE);
    scenario("windowed across the client's bottom-right corner", 30 + kWidth - 2, 20 + kHeight - 2, 0, 0, TRUE, 30,
            20, TRUE, FALSE);
    scenario("hidden", 20, 10, 0, 0, FALSE, 0, 0, FALSE, FALSE);
    scenario("Windows cursor", kWidth - 4, 10, 0, 0, FALSE, 0, 0, TRUE, TRUE);
    puts("PASS prepared software cursor: drawn exactly inside, across every edge and corner (hotspot and windowed "
            "offsets included), nothing outside, no blit beyond the back buffer, hidden and Windows cursors untouched");
    return 0;
}
'''

CLIP_CALL = ('        if (dg_swapchain_clip_cursor((LONG)back_buffer->resource.width, (LONG)back_buffer->resource.height,\n'
             '                &destRect, &cursor_rect))\n'
             '            wined3d_surface_blt(back_buffer, &destRect, cursor, &cursor_rect, WINEDDBLT_ALPHATEST,\n'
             '                    NULL, WINED3D_TEXF_POINT);\n')
MUTATIONS = {
    # Before 0061: the whole rectangle went to wined3d_surface_blt.
    'unclipped': [(CLIP_CALL, '        wined3d_surface_blt(back_buffer, &destRect, cursor, NULL, WINEDDBLT_ALPHATEST,\n'
                              '                NULL, WINED3D_TEXF_POINT);\n')],
    'blit-when-nothing-is-left': [('                &destRect, &cursor_rect))\n', '                &destRect, &cursor_rect) || TRUE)\n')],
    'empty-rectangle-accepted': [('return dst->left < dst->right && dst->top < dst->bottom;',
                                  'return dst->left <= dst->right && dst->top <= dst->bottom;')],
    'no-left-clip': [('        dst->left = 0;\n', '')],
    'no-top-clip': [('        dst->top = 0;\n', '')],
    'source-left-kept': [('        src->left -= dst->left;\n', '')],
    'source-top-kept': [('        src->top -= dst->top;\n', '')],
    'source-right-kept': [('        src->right -= dst->right - width;\n', '')],
    'source-bottom-kept': [('        src->bottom -= dst->bottom - height;\n', '')],
    'clip-to-the-cursor': [('dg_swapchain_clip_cursor((LONG)back_buffer->resource.width, (LONG)back_buffer->resource.height,',
                            'dg_swapchain_clip_cursor((LONG)cursor->resource.width, (LONG)cursor->resource.height,')],
    'whole-source': [('wined3d_surface_blt(back_buffer, &destRect, cursor, &cursor_rect,',
                      'wined3d_surface_blt(back_buffer, &destRect, cursor, NULL,')],
    'clip-before-mapping': [(
        '        if (swapchain->desc.windowed)\n            MapWindowPoints(NULL, swapchain->win_handle, (POINT *)&destRect, 2);\n'
        '#endif\n' + CLIP_CALL,
        '#endif\n        if (dg_swapchain_clip_cursor((LONG)back_buffer->resource.width, (LONG)back_buffer->resource.height,\n'
        '                &destRect, &cursor_rect))\n        {\n'
        '            if (swapchain->desc.windowed)\n                MapWindowPoints(NULL, swapchain->win_handle, (POINT *)&destRect, 2);\n'
        '            wined3d_surface_blt(back_buffer, &destRect, cursor, &cursor_rect, WINEDDBLT_ALPHATEST,\n'
        '                    NULL, WINED3D_TEXF_POINT);\n        }\n')],
    'height-from-width': [('            y + (LONG)swapchain->device->cursorHeight,\n',
                           '            y + (LONG)swapchain->device->cursorWidth,\n')],
}


def function(source, prefix):
    start = source.index(prefix)
    return source[start:source.index('\n}', start) + 2]


def between(source, first, last):
    start = source.index(first)
    return source[start:source.index(last, start)]


def build(work, name, text, expect_pass):
    path = work / (name + '.c')
    path.write_text(text)
    executable = work / name
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function',
                    '-Wno-unused-parameter', '-Wno-unused-variable', '-Wno-sign-compare', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-g', str(path), '-o', str(executable)],
                   check=True)
    result = subprocess.run([str(executable)], capture_output=not expect_pass, text=True)
    if expect_pass:
        result.check_returncode()
    else:
        assert result.returncode, (name, 'not rejected')
        return (result.stdout.strip().splitlines() or ['(abort)'])[0]


with tempfile.TemporaryDirectory(prefix='dreamgpu-software-cursor-') as directory:
    work = Path(directory)
    for name in json.loads(MANIFEST.read_text())['files']:
        destination = work / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'vendor/wine9x' / name, destination)
    patches.apply(work, MANIFEST)
    swapchain = (work / 'wined3d/swapchain.c').read_text()
    surface = (work / 'wined3d/surface.c').read_text()

    # The validation the cursor's blit meets, unchanged: any destination
    # outside the surface is rejected.
    blt_source = function(surface, 'HRESULT CDECL wined3d_surface_blt(')
    validation = between(blt_source, '    surface_get_rect(dst_surface, dst_rect_in, &dst_rect);',
                         '    if (!fx || !(fx->dwDDFX))')
    assert 'return WINEDDERR_INVALIDRECT;' in validation and 'dst_rect.left < 0' in validation
    # The cursor is drawn before the frame is presented.
    present = function(swapchain, 'static void swapchain_gl_present(')
    assert present.index('dg_swapchain_clip_cursor(') < present.index('p_wglSwapBuffers(')

    def program(swapchain):
        block = between(function(swapchain, 'static void swapchain_gl_present('),
                        '    if (swapchain->device->bCursorVisible && swapchain->device->cursor_texture',
                        '    TRACE("Presenting HDC')
        return (PREAMBLE.replace('@VALIDATION@', validation)
                + function(swapchain, 'static BOOL dg_swapchain_clip_cursor(')
                + WRAPPER.replace('@BLOCK@', block) + TEST)

    build(work, 'fixed', program(swapchain), True)
    for name, replacements in MUTATIONS.items():
        mutated = swapchain
        for old, new in replacements:
            assert mutated.count(old) == 1, name
            mutated = mutated.replace(old, new)
        print(name, 'rejected:', build(work, name, program(mutated), False))
    print(f'PASS all {len(MUTATIONS)} mutations of the patch are rejected')
