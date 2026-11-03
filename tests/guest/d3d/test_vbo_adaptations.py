#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Patches 0037-0040 on actual pinned Wine source: WineD3D with DreamGPU's
ARB_vertex_buffer_object.

0037: buffer objects keep their system memory copy (DOUBLEBUFFER), so locks
never map GL (a DreamGPU map copies and re-sends the whole buffer) and
buffer_direct_upload sends exactly the dirty ranges with glBufferSubData;
creation no longer marks the just-uploaded copy dirty again.
0038: this tree's VBOX_WITH_WINE_FIX_BUFOFFSET conversion never filled in the
element offset, marked only one byte of each converted attribute, left the
D3DCOLOR swizzle commented out and uploaded a stale length. Fixed-function
D3DCOLOR and XYZRHW data in buffer objects (DreamGPU has no BGRA arrays) then
reached GL unconverted. buffer_process_converted_attribute runs here under
ASan/UBSan.
0039: streams are preloaded before any buffer object is taken; immediate mode
(mixed buffer object and system memory streams, FLOAT16 with vertex shaders,
which now always takes it) reads system memory copies, never offsets.
0040: the zero attribute buffer covers every vertex a draw reaches and is
deleted with the device's contexts.
Each check fails without its patch.
"""
import importlib.util
import json
import os
from pathlib import Path
import re
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
#include <stdlib.h>
#include <string.h>
typedef unsigned int UINT, DWORD;
typedef unsigned long DWORD_PTR;
typedef int BOOL, INT;
typedef unsigned char BYTE;
#define TRUE 1
#define FALSE 0
#define VBOX_WITH_WINE_FIX_BUFOFFSET
#define TRACE(...) do {} while (0)
#define ERR(...) do {} while (0)
#define FIXME(...) do {} while (0)
#define debug_d3dformat(id) ""
enum wined3d_buffer_conversion_type { CONV_NONE, CONV_D3DCOLOR, CONV_POSITIONT };
struct wined3d_format { UINT id, component_count, component_size; };
struct wined3d_bo_address { UINT buffer_object; BYTE *addr; };
struct wined3d_stream_info_element {
    const struct wined3d_format *format;
    struct wined3d_bo_address data;
    UINT offset;
    UINT stride;
};
struct wined3d_buffer {
    UINT stride, offset;
    enum wined3d_buffer_conversion_type *conversion_map;
};
'''
TEST = r'''
static const struct wined3d_format color = {1, 4, 1}, position = {2, 4, 4};
/* An element as context_stream_info_from_declaration fills it in: the
 * address is stream offset plus element offset (before the buffer's own). */
static struct wined3d_stream_info_element element(const struct wined3d_format *f, UINT stream,
        UINT offset) {
    struct wined3d_stream_info_element e = {f, {7, (BYTE *)NULL + stream + offset}, offset, 32};
    return e;
}
int main(void) {
    struct wined3d_buffer b = {0, 0, NULL};
    DWORD stride = 0, offset = 0;
    /* XYZRHW at 0 and D3DCOLOR at 16 of a 32-byte vertex, stream offset 64. */
    struct wined3d_stream_info_element p = element(&position, 64, 0), d = element(&color, 64, 16);
    assert(buffer_process_converted_attribute(&b, CONV_POSITIONT, &p, &stride, &offset));
    assert(b.stride == 32 && b.offset == 64 && stride == 32 && offset == 64);
    assert(buffer_process_converted_attribute(&b, CONV_D3DCOLOR, &d, &stride, &offset));
    for (UINT i = 0; i < 32; ++i)
        assert(b.conversion_map[i] == (i < 16 ? CONV_POSITIONT : i < 20 ? CONV_D3DCOLOR : CONV_NONE));
    /* The same declaration again changes nothing and keeps the map. */
    enum wined3d_buffer_conversion_type *map = b.conversion_map;
    stride = offset = 0;
    assert(!buffer_process_converted_attribute(&b, CONV_POSITIONT, &p, &stride, &offset));
    assert(!buffer_process_converted_attribute(&b, CONV_D3DCOLOR, &d, &stride, &offset));
    assert(b.conversion_map == map);
    /* The vertex start realigns to the stream offset. */
    assert(wined3d_buffer_stream_offset_left(100, 64, 32) == 96);
    assert(wined3d_buffer_stream_offset_right(100, 64, 32) == 128);
    /* D3DCOLOR swaps red and blue for GL's RGBA. */
    DWORD c = 0x11223344;
    fixup_d3dcolor(&c);
    assert(c == 0x11443322);
    free(b.conversion_map);
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='dreamgpu-vbo-adaptations-') as temporary:
    work = Path(temporary)
    for name in json.loads(MANIFEST.read_text())['files']:
        p = work / name
        p.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'vendor/wine9x' / name, p)
    patches.apply(work, MANIFEST)
    buffer = (work / 'wined3d/buffer.c').read_text()
    context = (work / 'wined3d/context.c').read_text()
    drawprim = (work / 'wined3d/drawprim.c').read_text()
    device = (work / 'wined3d/device.c').read_text()

    # 0037: every buffer object is double-buffered; uploads are dirty ranges.
    init = function(buffer, 'static HRESULT buffer_init(')
    created = init.index('buffer->flags |= WINED3D_BUFFER_CREATEBO;')
    assert init.index('buffer->flags |= WINED3D_BUFFER_DOUBLEBUFFER;', created) < init.index('}', created)
    upload = function(buffer, 'static void buffer_direct_upload(')
    ranges = upload.index('if (!gl_info->supported[ARB_MAP_BUFFER_RANGE] && !(This->flags & WINED3D_BUFFER_APPLESYNC))')
    sub = upload.index('GL_EXTCALL(glBufferSubData(This->buffer_type_hint, start, len,', ranges)
    assert ranges < sub < upload.index('return;', sub) < upload.index('glMapBuffer(This->buffer_type_hint, GL_WRITE_ONLY)')
    create = function(buffer, 'static void buffer_create_buffer_object(')
    assert re.search(r'if \(This->flags & WINED3D_BUFFER_DOUBLEBUFFER\)\s+buffer_clear_dirty_areas\(This\);', create)

    # 0038: execute the converted-attribute bookkeeping; textual conversion loop.
    sources = [function(buffer, 'static BOOL buffer_process_converted_attribute('),
               function(buffer, 'static UINT wined3d_buffer_stream_offset_left('),
               function(buffer, 'static UINT wined3d_buffer_stream_offset_right('),
               function(buffer, 'static inline void fixup_d3dcolor(')]
    (work / 'test.c').write_text(PREAMBLE + '\n'.join(sources) + TEST)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-function',
                    # The guest is 32-bit: addresses there fit the UINT stream offset.
                    '-Wno-pointer-to-int-cast', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    str(work / 'test.c'), '-o', str(work / 'test')], check=True)
    subprocess.run([str(work / 'test')], check=True)
    assert 'stream_info->elements[idx].offset = element->offset;' in context
    preload = function(buffer, 'void buffer_internal_preload(')
    assert re.search(r'#ifdef VBOX_WITH_WINE_FIX_BUFOFFSET\s+/\*[^*]*\*/\s+fixup_d3dcolor\(\(DWORD \*\) \(cur_ptr \+ j\)\);', preload)
    assert 'glBufferSubData(buffer->buffer_type_hint, start, end - start, data + start)' in preload

    # 0039: preload every stream first, then take buffer objects.
    update = function(context, 'static void context_update_stream_info(')
    first = update.index('buffer_internal_preload(state->streams[stream_info->elements[i].stream_idx].buffer,')
    assert first < update.index('buffer_get_memory(buffer, context, &data);')
    assert update.count('buffer_internal_preload(') == 1
    assert 'if (state->vertex_declaration->half_float_conv_needed)\n' in update
    draw = function(drawprim, 'void draw_primitive(')
    immediate = draw.index('if (context->use_immediate_mode_draw && !emulation)')
    assert draw.index('remove_vbos(context, state, &si_emulated);', immediate) < draw.index('drawStridedSlow(')
    assert draw.index('idx_data = buffer_get_sysmem(state->index_buffer, context);', immediate) < draw.index('drawStridedSlow(')

    # 0040: the zero attribute reaches every vertex and dies with the contexts.
    assert 'device->czvDrawVertices = indexed ? max(index_count, 65536) : start_idx + index_count;' in draw
    for signature in ('static void delete_opengl_contexts(', 'HRESULT CDECL wined3d_device_uninit_3d('):
        teardown = function(device, signature)
        assert teardown.index('zv_destroy(device);') < teardown.index('context_release(context);'), signature

print('PASS prepared VBO adaptations: double-buffered dirty-range uploads, converted attributes, '
      'preload order and immediate-mode memory, zero attribute coverage and lifetime')
