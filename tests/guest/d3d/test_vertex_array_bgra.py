#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Patch 0047 on actual pinned Wine source: WineD3D with DreamGPU's
ARB_vertex_array_bgra (DG_CAP_GL_VERTEX_ARRAY_BGRA).

With the extension a D3DCOLOR's gl_vtx_format is GL_BGRA. Wine passes it as
the size of colour, secondary colour and generic attribute arrays, stops its
in-shader swizzle and buffer conversion and leaves immediate mode for
fixed-function D3DCOLOR streams outside buffer objects. VBox's zero attribute
(VBOX_WITH_WINE_FIX_ZEROVERTATTR), which binds a stride 0 attribute 0 of a
vertex shader draw, took that format as its component count: it refused to
build the array and pointed attribute 0 at a stale buffer or none. The
prepared zero-attribute functions run here under ASan/UBSan against a
recording GL, with and without buffer objects: a D3DCOLOR binds four
normalized bytes in RGBA order (red and blue differ), every other format is
unchanged. Fails without the patch. The other BGRA paths are checked in the
prepared source.
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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned int UINT, DWORD, GLuint, GLenum;
typedef int BOOL, GLint, GLsizei;
typedef unsigned char BYTE, GLubyte, GLboolean;
typedef float GLfloat;
typedef short GLshort;
typedef unsigned short GLushort;
typedef void GLvoid;
typedef intptr_t GLsizeiptrARB, GLintptrARB;
#define TRUE 1
#define FALSE 0
#define GL_TRUE 1
#define GL_FALSE 0
#define GL_BYTE 0x1400
#define GL_UNSIGNED_BYTE 0x1401
#define GL_SHORT 0x1402
#define GL_UNSIGNED_SHORT 0x1403
#define GL_FLOAT 0x1406
#define GL_BGRA 0x80e1
#define GL_ARRAY_BUFFER_ARB 0x8892
#define GL_DYNAMIC_DRAW_ARB 0x88e8
#define max(a, b) ((a) > (b) ? (a) : (b))
#define ERR(...) do {} while (0)
#define GL_EXTCALL(f) (gl_info->gl_ops.ext.p_##f)
enum { ARB_VERTEX_BUFFER_OBJECT, EXT_COUNT };
struct wined3d_gl_info {
    BOOL supported[EXT_COUNT];
    struct {
        struct {
            void (*p_glGenBuffersARB)(GLsizei, GLuint *);
            void (*p_glBindBufferARB)(GLenum, GLuint);
            void (*p_glBufferDataARB)(GLenum, GLsizeiptrARB, const GLvoid *, GLenum);
            void (*p_glBufferSubDataARB)(GLenum, GLintptrARB, GLsizeiptrARB, const GLvoid *);
            void (*p_glDeleteBuffersARB)(GLsizei, const GLuint *);
            void (*p_glVertexAttribPointerARB)(GLuint, GLint, GLenum, GLboolean, GLsizei, const GLvoid *);
            void (*p_glEnableVertexAttribArrayARB)(GLuint);
        } ext;
    } gl_ops;
};
struct wined3d_adapter { struct wined3d_gl_info gl_info; };
struct wined3d_device {
    struct wined3d_adapter *adapter;
    GLuint czvDrawVertices, zvBuffer, cbzvBuffer;
    GLenum enmzvValue;
    GLuint czvValue, czvValueElements;
    union { GLfloat f[4]; GLuint ui[4]; GLubyte ub[4]; GLshort s[4]; GLushort us[4]; } zvValue;
};
struct wined3d_swapchain { struct wined3d_device *device; };
struct wined3d_context {
    struct wined3d_swapchain *swapchain;
    const struct wined3d_gl_info *gl_info;
    DWORD numbered_array_mask;
};
/* format_vertex_info's GL columns of the formats the test binds. */
struct wined3d_format { GLenum gl_vtx_type; GLint gl_vtx_format; GLboolean gl_normalized; };
struct wined3d_stream_info_element { const struct wined3d_format *format; };
'''

TEST = r'''
/* The GL boundary: the attribute 0 pointer and the zero attribute buffer. */
static GLint size;
static GLenum type;
static GLboolean normalized;
static GLsizei stride = -1;
static const GLvoid *pointer;
static GLuint bound, enabled, uploads;
static BYTE contents[64];
static GLsizeiptrARB filled;
static void gen(GLsizei n, GLuint *names) { assert(n == 1); *names = 9; }
static void bind(GLenum target, GLuint name) { assert(target == GL_ARRAY_BUFFER_ARB); bound = name; }
static void data(GLenum target, GLsizeiptrARB bytes, const GLvoid *values, GLenum usage)
{
    assert(target == GL_ARRAY_BUFFER_ARB && bound == 9 && usage == GL_DYNAMIC_DRAW_ARB);
    assert(bytes > 0 && (size_t)bytes <= sizeof(contents));
    memcpy(contents, values, (size_t)bytes);
    filled = bytes;
    ++uploads;
}
static void sub_data(GLenum target, GLintptrARB offset, GLsizeiptrARB bytes, const GLvoid *values)
{
    assert(target == GL_ARRAY_BUFFER_ARB && bound == 9 && !offset && bytes <= filled);
    memcpy(contents, values, (size_t)bytes);
    ++uploads;
}
static void delete_buffers(GLsizei n, const GLuint *names) { (void)n; (void)names; assert(0); }
static void attrib_pointer(GLuint index, GLint s, GLenum t, GLboolean n, GLsizei step, const GLvoid *p)
{
    assert(index == 0);
    size = s;
    type = t;
    normalized = n;
    stride = step;
    pointer = p;
}
static void enable(GLuint index) { assert(index == 0); ++enabled; }

static struct wined3d_adapter adapter = {.gl_info.gl_ops.ext = {
        gen, bind, data, sub_data, delete_buffers, attrib_pointer, enable}};
static struct wined3d_device device = {.adapter = &adapter};
static struct wined3d_swapchain swapchain = {&device};
static struct wined3d_context context = {&swapchain, &adapter.gl_info, 0};

/* WINED3DFMT_B8G8R8A8_UNORM with and without ARB_vertex_array_bgra,
 * R8G8B8A8_UNORM and R32G32B32_FLOAT. */
static const struct wined3d_format d3dcolor = {GL_UNSIGNED_BYTE, GL_BGRA, GL_TRUE},
        d3dcolor_swizzled = {GL_UNSIGNED_BYTE, 4, GL_TRUE}, ubyte4n = {GL_UNSIGNED_BYTE, 4, GL_TRUE},
        float3 = {GL_FLOAT, 3, GL_FALSE};

static void bind_zero(const struct wined3d_format *format, GLuint count, const void *value)
{
    const struct wined3d_stream_info_element element = {format};
    size = 0;
    type = 0;
    normalized = 2;
    stride = -1;
    pointer = (const void *)1;
    zv_bind_by_element(&context, &element, count, value);
    assert(stride == 0 && (context.numbered_array_mask & 1) && enabled);
}
/* Every one of count values holds bytes[0..bytes). */
static void repeated(const BYTE *values, GLuint count, const BYTE *bytes, UINT n)
{
    for (GLuint i = 0; i < count; ++i)
        assert(!memcmp(values + i * n, bytes, n));
}

int main(void)
{
    /* D3DCOLOR 0x80402010 (alpha 0x80, red 0x40, green 0x20, blue 0x10) at an
     * unaligned address: memory B, G, R, A; GL wants R, G, B, A. */
    static const BYTE stream[8] = {0xee, 0x10, 0x20, 0x40, 0x80, 0xee, 0xee, 0xee};
    static const BYTE rgba[4] = {0x40, 0x20, 0x10, 0x80}, bgra[4] = {0x10, 0x20, 0x40, 0x80};
    static const BYTE other[5] = {0xee, 0x01, 0x02, 0x03, 0x04}, other_rgba[4] = {0x03, 0x02, 0x01, 0x04};
    static const float position[3] = {1.5f, -2.0f, 0.25f};

    /* Without buffer objects: a client array of max(count, 65536) values. */
    bind_zero(&d3dcolor, 6, stream + 1);
    assert(size == 4 && type == GL_UNSIGNED_BYTE && normalized == GL_TRUE);
    assert(pointer);
    repeated(pointer, 65536, rgba, 4);
    /* Without the extension the format is four bytes in memory order, which
     * the shader swizzles (swizzle_map): unchanged. */
    bind_zero(&d3dcolor_swizzled, 6, stream + 1);
    assert(size == 4 && type == GL_UNSIGNED_BYTE && normalized == GL_TRUE && pointer);
    repeated(pointer, 65536, bgra, 4);
    bind_zero(&ubyte4n, 6, other + 1);
    assert(size == 4 && type == GL_UNSIGNED_BYTE && normalized == GL_TRUE && pointer);
    repeated(pointer, 65536, other + 1, 4);
    bind_zero(&float3, 6, position);
    assert(size == 3 && type == GL_FLOAT && normalized == GL_FALSE && pointer);
    repeated(pointer, 65536, (const BYTE *)position, sizeof(position));

    /* With buffer objects: the device's zero attribute buffer of count values. */
    adapter.gl_info.supported[ARB_VERTEX_BUFFER_OBJECT] = TRUE;
    context.numbered_array_mask = 0;
    bind_zero(&d3dcolor, 6, stream + 1);
    assert(size == 4 && type == GL_UNSIGNED_BYTE && normalized == GL_TRUE && !pointer && bound == 9);
    assert(uploads == 1 && filled == 24 && device.czvValueElements == 4);
    repeated(contents, 6, rgba, 4);
    /* The same colour again keeps the buffer; another colour replaces it. */
    bind_zero(&d3dcolor, 6, stream + 1);
    assert(uploads == 1 && size == 4 && !pointer);
    bind_zero(&d3dcolor, 6, other + 1);
    assert(uploads == 2 && size == 4 && type == GL_UNSIGNED_BYTE && normalized == GL_TRUE && !pointer);
    repeated(contents, 6, other_rgba, 4);
    /* Other formats keep their own type and count. */
    bind_zero(&float3, 2, position);
    assert(uploads == 3 && size == 3 && type == GL_FLOAT && normalized == GL_FALSE && !pointer);
    assert(filled == 24);
    repeated(contents, 2, (const BYTE *)position, sizeof(position));
    bind_zero(&ubyte4n, 2, other + 1);
    assert(uploads == 4 && size == 4 && type == GL_UNSIGNED_BYTE && normalized == GL_TRUE);
    repeated(contents, 2, other + 1, 4);
    return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='dreamgpu-vertex-array-bgra-') as temporary:
    work = Path(temporary)
    for name in json.loads(MANIFEST.read_text())['files']:
        p = work / name
        p.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'vendor/wine9x' / name, p)
    patches.apply(work, MANIFEST)
    device = (work / 'wined3d/device.c').read_text()
    utils = (work / 'wined3d/utils.c').read_text()
    context = (work / 'wined3d/context.c').read_text()
    buffer = (work / 'wined3d/buffer.c').read_text()
    state = (work / 'wined3d/state.c').read_text()
    drawprim = (work / 'wined3d/drawprim.c').read_text()
    directx = (work / 'wined3d/directx.c').read_text()

    # 0047: execute the prepared zero attribute functions.
    sources = [function(device, 'static GLuint zv_value_el_size('),
               function(device, 'static void zv_create('),
               function(device, 'static const GLvoid *zv_client_array('),
               function(device, 'void zv_bind(struct wined3d_context *context,'),
               function(device, 'void zv_bind_by_element(')]
    (work / 'test.c').write_text(PREAMBLE + '\n'.join(sources) + TEST)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-Wno-unused-function', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    str(work / 'test.c'), '-o', str(work / 'test')], check=True)
    subprocess.run([str(work / 'test')], check=True)

    # The extension name Wine looks for, and the D3DCOLOR vertex format it sets.
    assert re.search(r'\{"GL_ARB_vertex_array_bgra",\s+ARB_VERTEX_ARRAY_BGRA\s+\}', directx)
    assert re.search(r'if \(gl_info->supported\[ARB_VERTEX_ARRAY_BGRA\]\)\s*\{\s*'
                     r'idx = getFmtIdx\(WINED3DFMT_B8G8R8A8_UNORM\);\s*(//[^\n]*\s*)?'
                     r'gl_info->formats\[idx\]\.gl_vtx_format = GL_BGRA;', utils)
    # Only renderers DreamGPU never reports mark BGRA arrays broken.
    frontend = (ROOT / 'guest/opengl/query.cpp').read_text()
    identity = re.findall(r'static const GLubyte (?:vendor|renderer|version)\[\] = "([^"]*)";', frontend)
    assert len(identity) == 3
    assert directx.count('wined3d_settings.vertex_array_brga_broken = TRUE;') == 2
    for name in ('Humper', 'Chromium', 'SVGA3D'):
        assert f'"{name}"' in directx and not any(name in s for s in identity), name
    # No in-shader swizzle and no immediate mode for D3DCOLOR once supported.
    declaration = function(context, 'void context_stream_info_from_declaration(')
    assert re.search(r'if \(\(wined3d_settings\.vertex_array_brga_broken \|\| '
                     r'!context->gl_info->supported\[ARB_VERTEX_ARRAY_BGRA\]\)\s*'
                     r'&& element->format->id == WINED3DFMT_B8G8R8A8_UNORM\)\s*\{\s*'
                     r'stream_info->swizzle_map \|= 1u << idx;', declaration)
    update = function(context, 'static void context_update_stream_info(')
    assert re.search(r'slow_mask \|= -!gl_info->supported\[ARB_VERTEX_ARRAY_BGRA\] & '
                     r'\(\(1u << WINED3D_FFP_DIFFUSE\) \| \(1u << WINED3D_FFP_SPECULAR\)\);', update)
    assert re.search(r'if \(\(\(stream_info->position_transformed && !d3d_info->xyzrhw\)\s*'
                     r'\|\| \(stream_info->use_map & slow_mask\)\) && !stream_info->all_vbo\)', update)
    # Buffer objects convert D3DCOLOR only without the extension (0038 keeps XYZRHW).
    preload = function(buffer, 'void buffer_internal_preload(')
    assert re.search(r'if \(!context->gl_info->supported\[ARB_VERTEX_ARRAY_BGRA\]\)\s*\{\s*'
                     r'fixup_flags \|= WINED3D_BUFFER_FIXUP_D3DCOLOR;\s*\}\s*'
                     r'if \(!context->d3d_info->xyzrhw\)\s*\{\s*fixup_flags \|= WINED3D_BUFFER_FIXUP_XYZRHW;',
                     preload)
    # Named arrays pass the format as the size; specular takes it as well.
    named = function(state, 'static void load_vertex_data(')
    assert 'p_glColorPointer(e->format->gl_vtx_format, e->format->gl_vtx_type, e->stride,' in named
    assert re.search(r'if \(format != 4 \|\| \(gl_info->quirks & WINED3D_QUIRK_ALLOWS_SPECULAR_ALPHA\)\)'
                     r'(.|\n)*?glSecondaryColorPointerEXT\(format, type, e->stride,', named)
    # Generic arrays pass it normalized; the stride 0 and immediate-mode
    # D3DCOLOR attributes swizzle to RGBA themselves.
    numbered = function(state, 'static void load_numbered_arrays(')
    assert re.search(r'glVertexAttribPointer\(i, stream_info->elements\[i\]\.format->gl_vtx_format,\s*'
                     r'stream_info->elements\[i\]\.format->gl_vtx_type,\s*'
                     r'stream_info->elements\[i\]\.format->gl_normalized,', numbered)
    swizzle = (r'case WINED3DFMT_B8G8R8A8_UNORM:\s*if \(gl_info->supported\[ARB_VERTEX_ARRAY_BGRA\]\)\s*\{'
               r'[^}]*c \|= \(\*src & 0xff0000u\) >> 16;\s*c \|= \(\*src & 0xffu\) << 16;\s*'
               r'GL_EXTCALL\(glVertexAttrib4Nubv\((i|index), \(GLubyte \*\)&c\)\);')
    assert re.search(swizzle, numbered)
    assert re.search(swizzle, function(drawprim, 'static inline void send_attribute('))
    assert 'zv_bind_by_element(context, &stream_info->elements[i], device->czvDrawVertices, ptr);' in numbered

print('PASS prepared BGRA vertex arrays: the D3DCOLOR zero attribute binds RGBA bytes with and '
      'without buffer objects, other formats unchanged; format, swizzle, immediate-mode, '
      'conversion and array paths follow ARB_vertex_array_bgra')
