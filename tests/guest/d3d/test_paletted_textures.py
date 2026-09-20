#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise prepared Wine palette conversion and texture ownership."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location("patches", ROOT / "tests/guest/support/patches.py")
patches = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patches)
MANIFEST = ROOT / "support/guest/d3d/patches/base/manifest.json"

PREAMBLE = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t BYTE;
typedef uint32_t DWORD;
typedef int BOOL;
#define FIXME(...) ((void)0)
#define WINED3D_PALETTE_ALPHA 4
struct wined3d_color_key { DWORD color_space_low_value, color_space_high_value; };
struct wined3d_palette {
    unsigned flags;
    struct { BYTE rgbBlue, rgbGreen, rgbRed, rgbReserved; } colors[256];
};
'''

CONVERSION_TEST = r'''
int main(void)
{
    struct wined3d_palette palette = {0};
    struct wined3d_color_key key = {2, 3};
    BYTE indices[10] = {1, 2, 3, 0xcc, 0xcc, 3, 2, 1, 0xcc, 0xcc};
    BYTE original[10];
    DWORD pixels[10];
    memcpy(original, indices, sizeof(indices));
    palette.colors[1].rgbRed = 0x12;
    palette.colors[1].rgbGreen = 0x34;
    palette.colors[1].rgbBlue = 0x56;
    palette.colors[1].rgbReserved = 0x80;
    palette.colors[2].rgbRed = 0xab;
    palette.colors[2].rgbGreen = 0xcd;
    palette.colors[2].rgbBlue = 0xef;
    palette.colors[2].rgbReserved = 0x40;
    palette.colors[3].rgbGreen = 0xff;

    memset(pixels, 0xa5, sizeof(pixels));
    convert_p8_uint_b8g8r8a8_unorm(indices, 5, (BYTE *)pixels, 20, 3, 2, &palette, NULL);
    assert(pixels[0] == 0xff123456 && pixels[1] == 0xffabcdef && pixels[2] == 0xff00ff00);
    assert(pixels[5] == pixels[2] && pixels[6] == pixels[1] && pixels[7] == pixels[0]);
    assert(pixels[3] == 0xa5a5a5a5 && pixels[4] == 0xa5a5a5a5);
    assert(pixels[8] == 0xa5a5a5a5 && pixels[9] == 0xa5a5a5a5);
    assert(!memcmp(indices, original, sizeof(indices)));

    // Color keys compare palette indices, including both range endpoints.
    convert_p8_uint_b8g8r8a8_unorm(indices, 5, (BYTE *)pixels, 20, 3, 2, &palette, &key);
    assert(pixels[0] == 0xff123456 && pixels[1] == 0x00abcdef && pixels[2] == 0x0000ff00);
    palette.flags = WINED3D_PALETTE_ALPHA;
    convert_p8_uint_b8g8r8a8_unorm(indices, 5, (BYTE *)pixels, 20, 3, 2, &palette, NULL);
    assert(pixels[0] == 0x80123456 && pixels[1] == 0x40abcdef && pixels[2] == 0x0000ff00);
    convert_p8_uint_b8g8r8a8_unorm(indices, 5, (BYTE *)pixels, 20, 3, 2, &palette, &key);
    assert(pixels[0] == 0x80123456 && pixels[1] == 0x00abcdef);

    // Missing palette is deterministic and does not touch row padding.
    convert_p8_uint_b8g8r8a8_unorm(indices, 5, (BYTE *)pixels, 20, 3, 2, NULL, NULL);
    assert(!pixels[0] && !pixels[1] && !pixels[2] && !pixels[5] && !pixels[6] && !pixels[7]);
    assert(pixels[3] == 0xa5a5a5a5 && pixels[8] == 0xa5a5a5a5);
    puts("PASS prepared Wine P8 conversion: padded rows, immutable indices, opaque/alpha palettes and index color keys");
}
'''

LIFECYCLE_PREAMBLE = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wine/list.h"
typedef uint8_t BYTE;
typedef uint32_t DWORD;
typedef int HRESULT;
typedef unsigned UINT;
typedef struct { BYTE peRed, peGreen, peBlue, peFlags; } PALETTEENTRY;
#define CDECL
#define TRACE(...) ((void)0)
#define WINED3D_OK 0
#define WINED3DERR_INVALIDCALL -1
#define WINED3D_PALETTE_ALLOW_256 1
#define WINED3D_PALETTE_8BIT_ENTRIES 2
#define WINED3D_RTYPE_TEXTURE 3
#define WINED3D_TEXTURE_RGB_VALID 0x10
#define WINED3D_TEXTURE_SRGB_VALID 0x40
#define WINED3D_LOCATION_TEXTURE_RGB 2
#define WINED3D_LOCATION_TEXTURE_SRGB 4
struct wined3d_device { struct list resources; };
struct wined3d_palette {
    unsigned ref, size, flags;
    struct wined3d_device *device;
    struct { BYTE rgbBlue, rgbGreen, rgbRed, rgbReserved; } colors[256];
};
struct wined3d_resource { unsigned type, locations; struct list resource_list_entry; };
struct wined3d_texture_ops {
    void (*texture_sub_resource_invalidate_location)(struct wined3d_resource *, DWORD);
    void (*texture_sub_resource_cleanup)(struct wined3d_resource *);
};
struct wined3d_texture {
    struct wined3d_resource resource;
    struct wined3d_palette *palette;
    unsigned level_count, layer_count, flags;
    struct wined3d_resource **sub_resources;
    const struct wined3d_texture_ops *texture_ops;
};
static unsigned invalidations, increments, decrements, cleanups;
static void invalidate(struct wined3d_resource *resource, DWORD mask)
{
    assert(mask == (WINED3D_LOCATION_TEXTURE_RGB | WINED3D_LOCATION_TEXTURE_SRGB));
    resource->locations &= ~mask;
    ++invalidations;
}
static void wined3d_palette_incref(struct wined3d_palette *palette) { ++palette->ref; ++increments; }
static void wined3d_palette_decref(struct wined3d_palette *palette)
{
    assert(palette->ref);
    --palette->ref;
    ++decrements;
}
static struct wined3d_texture *wined3d_texture_from_resource(struct wined3d_resource *resource)
{
    return (struct wined3d_texture *)resource;
}
static void cleanup_sub_resource(struct wined3d_resource *resource) { assert(resource); ++cleanups; }
static void resource_cleanup(struct wined3d_resource *resource) { assert(resource); ++cleanups; }
static void wined3d_texture_unload_gl_texture(struct wined3d_texture *texture)
{
    // Attached palette remains alive while GPU resources are being released.
    assert(texture->palette && texture->palette->ref >= 2);
    ++cleanups;
}
'''

LIFECYCLE_TEST = r'''
int main(void)
{
    struct wined3d_device device;
    struct wined3d_palette first = {0}, second = {0};
    struct wined3d_resource mip0 = {0}, mip1 = {0}, unrelated = {0};
    struct wined3d_resource *mips[] = {&mip0, &mip1}, *other_mips[] = {&unrelated};
    struct wined3d_texture_ops ops = {invalidate, cleanup_sub_resource};
    struct wined3d_texture texture = {0}, other = {0};
    struct wined3d_resource non_texture = {0};
    PALETTEENTRY entries[2] = {{0x12, 0x34, 0x56, 0x78}, {0xab, 0xcd, 0xef, 0xff}};
    first.ref = second.ref = 1;
    first.size = second.size = 256;
    first.device = second.device = &device;
    first.flags = second.flags = WINED3D_PALETTE_ALLOW_256;
    list_init(&device.resources);
    texture.resource.type = other.resource.type = WINED3D_RTYPE_TEXTURE;
    texture.level_count = 2; texture.layer_count = 1;
    other.level_count = other.layer_count = 1;
    texture.texture_ops = other.texture_ops = &ops;
    texture.sub_resources = mips; other.sub_resources = other_mips;
    list_add_tail(&device.resources, &texture.resource.resource_list_entry);
    list_add_tail(&device.resources, &other.resource.resource_list_entry);
    list_add_tail(&device.resources, &non_texture.resource_list_entry);

    wined3d_texture_set_palette(&texture, &first);
    wined3d_texture_set_palette(&other, &second);
    assert(first.ref == 2 && second.ref == 2 && increments == 2);
    unsigned before = invalidations;
    wined3d_texture_set_palette(&texture, &first);
    assert(invalidations == before && increments == 2 && !decrements);

    // A palette edit invalidates all matching mip uploads, retaining CPU indices.
    mip0.locations = mip1.locations = unrelated.locations = 7;
    texture.flags = other.flags = 0x151;
    assert(wined3d_palette_set_entries(&first, 0, 1, 2, entries) == WINED3D_OK);
    assert(first.colors[1].rgbRed == 0x12 && first.colors[1].rgbReserved == 0x78);
    assert(first.colors[2].rgbBlue == 0xef);
    assert(mip0.locations == 1 && mip1.locations == 1 && unrelated.locations == 7);
    assert(texture.flags == 0x101 && other.flags == 0x151);
    assert(invalidations == before + 2);

    // Invalid input leaves entries and upload validity unchanged, including overflow.
    struct wined3d_palette saved = first;
    before = invalidations;
    assert(wined3d_palette_set_entries(&first, 1, 1, 1, entries) == WINED3DERR_INVALIDCALL);
    assert(wined3d_palette_set_entries(&first, 0, 255, 2, entries) == WINED3DERR_INVALIDCALL);
    assert(wined3d_palette_set_entries(&first, 0, UINT32_MAX, 2, entries) == WINED3DERR_INVALIDCALL);
    assert(wined3d_palette_set_entries(&first, 0, 0, UINT32_MAX, entries) == WINED3DERR_INVALIDCALL);
    assert(wined3d_palette_set_entries(&first, 0, 0, 1, NULL) == WINED3DERR_INVALIDCALL);
    assert(!memcmp(&saved, &first, sizeof(first)) && invalidations == before);
    assert(wined3d_palette_set_entries(&first, 0, 256, 0, NULL) == WINED3D_OK);
    assert(invalidations == before);

    // Replacement and detachment release exactly the ownership they acquired.
    wined3d_texture_set_palette(&texture, &second);
    assert(first.ref == 1 && second.ref == 3 && decrements == 1);
    wined3d_texture_set_palette(&texture, NULL);
    wined3d_texture_set_palette(&other, NULL);
    assert(first.ref == 1 && second.ref == 1 && increments == decrements);
    before = invalidations;
    assert(wined3d_palette_set_entries(&first, 0, 1, 1, entries) == WINED3D_OK);
    assert(invalidations == before);

    // Destruction must release the palette even without an explicit detachment.
    texture.sub_resources = malloc(sizeof(*texture.sub_resources) * 2);
    assert(texture.sub_resources);
    texture.sub_resources[0] = &mip0; texture.sub_resources[1] = &mip1;
    wined3d_texture_set_palette(&texture, &first);
    assert(first.ref == 2);
    wined3d_texture_cleanup(&texture);
    assert(first.ref == 1 && cleanups == 4 && increments == decrements);
    puts("PASS prepared Wine palette ownership, replacement, mip invalidation, unrelated-resource preservation and rejected edits");
}
'''


def function(source, signature):
    start = source.index(signature)
    return source[start:source.index("\n}", start) + 2]


with tempfile.TemporaryDirectory(prefix="dreamgpu-paletted-textures-") as directory:
    work = Path(directory)
    names = set(json.loads(MANIFEST.read_text())["files"]) | {"wined3d/utils.c"}
    for name in names:
        destination = work / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / "vendor/wine9x" / name, destination)
    patches.apply(work, MANIFEST)
    # The pinned MinGW build consumes the checked .def directly for both NT5
    # and Win98; it does not regenerate exports from Wine's .spec file.
    assert "DEF_wined3d.dll    = wined3d/wined3d.def" in (work / "Makefile").read_text()
    assert (work / "wined3d/wined3d.def").read_text().splitlines().count(
        "  wined3d_texture_set_palette") == 1
    assert (work / "wined3d/wined3d.spec").read_text().splitlines().count(
        "@ cdecl wined3d_texture_set_palette(ptr ptr)") == 1
    assert "void __cdecl wined3d_texture_set_palette(" in (work / "include/wine/wined3d.h").read_text()
    source = (work / "wined3d/utils.c").read_text()
    code = PREAMBLE + function(source, "static BOOL color_in_range(")
    code += function(source, "static void convert_p8_uint_b8g8r8a8_unorm(") + CONVERSION_TEST
    texture = (work / "wined3d/texture.c").read_text()
    palette = (work / "wined3d/palette.c").read_text()
    lifecycle = LIFECYCLE_PREAMBLE
    for signature in ["void wined3d_texture_set_dirty(", "void wined3d_texture_invalidate_palette(",
                      "void CDECL wined3d_texture_set_palette(", "static void wined3d_texture_cleanup("]:
        lifecycle += function(texture, signature)
    lifecycle += function(palette, "HRESULT CDECL wined3d_palette_set_entries(") + LIFECYCLE_TEST
    for name, contents in [("conversion", code), ("lifecycle", lifecycle)]:
        test = work / (name + ".c")
        test.write_text(contents)
        subprocess.run([os.environ.get("CC", "cc"), "-std=c99", "-Wall", "-Wextra", "-Werror",
                        "-I", str(ROOT / "vendor/wine9x/include"), "-fsanitize=address,undefined",
                        "-fno-omit-frame-pointer", str(test), "-o", str(work / name)], check=True)
        subprocess.run([str(work / name)], check=True)
