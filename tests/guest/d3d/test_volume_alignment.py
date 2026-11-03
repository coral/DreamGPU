#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute the prepared volume upload under ASan/UBSan.

With DreamGPU's EXT_texture3D, WineD3D uploads each volume level with one
glTexSubImage3D. Volume memory pads rows to the device's surface alignment
(wined3d_volume_get_pitch), converted rows are tight, and the context's unpack
alignment is one at creation but the surface alignment after any surface
upload (VBOX_WITH_WINE_FIX_SURFUPDATA). Patch 0035 states the alignment for
the upload and restores the previous one. A fake glTexSubImage3D unpacks the
memory exactly as GL does with the alignment it was given: every texel must
arrive and no byte outside the source may be read. With 0035 removed, this
test fails.
"""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('patches', ROOT / 'tests/guest/support/patches.py')
patches = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patches)
MANIFEST = ROOT / 'support/guest/d3d/patches/base/manifest.json'
PREAMBLE = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned int UINT, GLenum, DWORD;
typedef int GLint, GLsizei, BOOL;
typedef unsigned char BYTE;
#define TRUE 1
#define FALSE 0
#define GL_UNPACK_ALIGNMENT 0x0cf5
#define GL_TEXTURE_3D 0x806f
#define GL_PIXEL_UNPACK_BUFFER 0x88ec
#define WINED3DFMT_FLAG_BLOCKS 0x1
#define TRACE(...) do {} while (0)
#define ERR(...) do {} while (0)
#define checkGLcall(A) do {} while (0)
#define GL_EXTCALL(f) (gl_info->gl_ops.ext.p_##f)
#define debug_d3dformat(id) ""
typedef void (*CONVERT)(const BYTE *src, BYTE *dst, UINT src_row_pitch, UINT src_slice_pitch,
        UINT dst_row_pitch, UINT dst_slice_pitch, UINT width, UINT height, UINT depth);
struct wined3d_format {
    UINT id, byte_count, conv_byte_count, block_width, block_height, block_byte_count;
    GLenum glFormat, glType;
    CONVERT convert;
};
struct wined3d_device { BYTE surface_alignment; };
struct wined3d_resource {
    struct wined3d_device *device;
    const struct wined3d_format *format;
    UINT width, height, depth;
    DWORD format_flags;
};
struct wined3d_texture { struct wined3d_resource resource; };
struct wined3d_volume {
    struct wined3d_resource resource;
    struct wined3d_texture *container;
    UINT texture_level;
};
struct wined3d_const_bo_address { unsigned int buffer_object; const BYTE *addr; };
struct wined3d_gl_info {
    struct {
        struct {
            void (*p_glGetIntegerv)(GLenum, GLint *);
            void (*p_glPixelStorei)(GLenum, GLint);
        } gl;
        struct {
            void (*p_glTexSubImage3D)(GLenum, GLint, GLint, GLint, GLint, GLsizei, GLsizei,
                    GLsizei, GLenum, GLenum, const void *);
            void (*p_glBindBuffer)(GLenum, unsigned int);
        } ext;
    } gl_ops;
};
struct wined3d_context { const struct wined3d_gl_info *gl_info; };

/* The tracked source of the next upload: its bytes, and the texel count. */
static GLint alignment = 1;
static const BYTE *source_base;
static size_t source_bytes;
static BYTE uploaded[256];
static unsigned int uploads, bytes_per_texel;
static void *last_allocation;
static size_t last_allocation_bytes;
static void *tracked_malloc(size_t bytes)
{
    last_allocation = malloc(bytes);
    last_allocation_bytes = bytes;
    return last_allocation;
}
#define malloc tracked_malloc
static void get_integer(GLenum name, GLint *value)
{
    assert(name == GL_UNPACK_ALIGNMENT);
    *value = alignment;
}
static void pixel_store(GLenum name, GLint value)
{
    assert(name == GL_UNPACK_ALIGNMENT && (value == 1 || value == 2 || value == 4 || value == 8));
    alignment = value;
}
/* GL 1.2 unpacking with no row length, skips or image height. */
static void sub_image(GLenum target, GLint level, GLint x, GLint y, GLint z, GLsizei w,
        GLsizei h, GLsizei d, GLenum format, GLenum type, const void *pixels)
{
    const BYTE *base = pixels;
    size_t row = ((size_t)w * bytes_per_texel + alignment - 1) / alignment * alignment;
    int i, j, k;
    (void)format; (void)type;
    assert(target == GL_TEXTURE_3D && level == 2 && !x && !y && !z);
    if (base == last_allocation)
    {
        source_base = base;
        source_bytes = last_allocation_bytes;
    }
    assert(base == source_base);
    for (k = 0; k < d; ++k)
        for (j = 0; j < h; ++j)
            for (i = 0; i < w; ++i)
            {
                size_t at = ((size_t)k * h + j) * row + (size_t)i * bytes_per_texel;
                assert(at + bytes_per_texel <= source_bytes);
                memcpy(uploaded + (((size_t)k * h + j) * w + i) * bytes_per_texel, base + at,
                        bytes_per_texel);
            }
    ++uploads;
}
/* V8U8-style conversion: two bytes in, three tight bytes out. */
static void convert(const BYTE *src, BYTE *dst, UINT src_row_pitch, UINT src_slice_pitch,
        UINT dst_row_pitch, UINT dst_slice_pitch, UINT width, UINT height, UINT depth)
{
    UINT x, y, z;
    assert(dst_row_pitch == width * 3 && dst_slice_pitch == dst_row_pitch * height);
    for (z = 0; z < depth; ++z)
        for (y = 0; y < height; ++y)
            for (x = 0; x < width; ++x)
            {
                const BYTE *in = src + z * src_slice_pitch + y * src_row_pitch + x * 2;
                BYTE *out = dst + z * dst_slice_pitch + y * dst_row_pitch + x * 3;
                out[0] = in[0];
                out[1] = in[1];
                out[2] = 0xff;
            }
}
'''
TEST = r'''
static void upload(struct wined3d_volume *volume, const struct wined3d_context *context,
        const BYTE *memory, size_t bytes, GLint before)
{
    struct wined3d_const_bo_address address = {0, memory};
    alignment = before;
    source_base = memory;
    source_bytes = bytes;
    wined3d_volume_upload_data(volume, context, &address);
    assert(alignment == before);
}

int main(void)
{
    struct wined3d_gl_info gl_info = {{{get_integer, pixel_store}, {sub_image, NULL}}};
    const struct wined3d_context context = {&gl_info};
    struct wined3d_device device = {4};
    struct wined3d_format l8 = {1, 1, 0, 1, 1, 1, 0x1909, 0x1401, NULL};
    struct wined3d_format v8u8 = {2, 2, 3, 1, 1, 2, 0x1907, 0x1401, convert};
    struct wined3d_texture container = {{&device, &l8, 3, 2, 2, 0}};
    struct wined3d_volume volume = {{&device, &l8, 3, 2, 2, 0}, &container, 2};
    UINT row, slice, x, y, z;
    BYTE memory[64];

    /* An L8 3x2x2 level: four-byte rows, eight-byte images. */
    wined3d_volume_get_pitch(&volume, &row, &slice);
    assert(row == 4 && slice == 8);
    memset(memory, 0xee, sizeof(memory));
    for (z = 0; z < 2; ++z)
        for (y = 0; y < 2; ++y)
            for (x = 0; x < 3; ++x)
                memory[z * slice + y * row + x] = (BYTE)(z * 16 + y * 4 + x);
    bytes_per_texel = 1;
    /* Whatever alignment the last surface upload left is restored. */
    for (GLint before = 1; before <= 8; before *= 2)
    {
        memset(uploaded, 0, sizeof(uploaded));
        upload(&volume, &context, memory, slice * 2, before);
        for (z = 0; z < 2; ++z)
            for (y = 0; y < 2; ++y)
                for (x = 0; x < 3; ++x)
                    assert(uploaded[(z * 2 + y) * 3 + x] == z * 16 + y * 4 + x);
    }

    /* A converted 3x2x2 level: tight nine-byte rows from four-byte source rows. */
    volume.resource.format = &v8u8;
    container.resource.format = &v8u8;
    wined3d_volume_get_pitch(&volume, &row, &slice);
    assert(row == 8 && slice == 16);
    for (z = 0; z < 2; ++z)
        for (y = 0; y < 2; ++y)
            for (x = 0; x < 3; ++x)
            {
                memory[z * slice + y * row + x * 2] = (BYTE)(z * 16 + y * 4 + x);
                memory[z * slice + y * row + x * 2 + 1] = 0x80;
            }
    bytes_per_texel = 3;
    for (GLint before = 1; before <= 8; before *= 2)
    {
        memset(uploaded, 0, sizeof(uploaded));
        upload(&volume, &context, memory, slice * 2, before);
        for (z = 0; z < 2; ++z)
            for (y = 0; y < 2; ++y)
                for (x = 0; x < 3; ++x)
                {
                    const BYTE *texel = uploaded + ((z * 2 + y) * 3 + x) * 3;
                    assert(texel[0] == z * 16 + y * 4 + x && texel[1] == 0x80 && texel[2] == 0xff);
                }
    }
    assert(uploads == 8);
    puts("PASS prepared volume upload: padded and converted rows unpacked exactly, alignment restored");
    return 0;
}
'''


def function(source, signature):
    start = source.index(signature)
    end = source.index('\n}\n', start) + 3
    return source[start:end] + '\n'


with tempfile.TemporaryDirectory(prefix='dreamgpu-volume-alignment-') as temporary:
    work = Path(temporary)
    for name in json.loads(MANIFEST.read_text())['files']:
        p = work / name
        p.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'vendor/wine9x' / name, p)
    patches.apply(work, MANIFEST)
    source = (work / 'wined3d/volume.c').read_text()
    body = function(source, 'void wined3d_volume_get_pitch(') + \
        function(source, 'void wined3d_volume_upload_data(')
    (work / 'test.c').write_text(PREAMBLE + body + TEST)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    str(work / 'test.c'), '-o', str(work / 'test')], check=True)
    sys.exit(subprocess.run([str(work / 'test')]).returncode)
