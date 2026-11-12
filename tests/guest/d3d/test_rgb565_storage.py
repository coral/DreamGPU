#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Patch 0057 on actual pinned Wine source: Direct3D's R5G6B5 keeps a six-bit
green, as a GeForce FX 5800 kept it.

WineD3D maps WINED3DFMT_B5G6R5_UNORM to GL_RGB5, and to GL_RGB565 only with
ARB_ES2_compatibility, which DreamGPU does not report; Mesa keeps GL_RGB5 in
5/5/5 bits. DreamGPU's frontend accepts GL_RGB565 as an internal format when
the host stores it exactly (DG_CAP2_GL_RGB565) and reports a GL error
otherwise. The prepared dg_rgb565_storage probe and init_format_texture_info's
entry selection and field assignment run here under ASan/UBSan against a fake
GL that accepts or rejects GL_RGB565, and with ARB_ES2_compatibility, on the
actual 16-bit entries of format_texture_info: B5G6R5 becomes GL_RGB565
exactly when the probe (or the extension) says so, X1R5G5B5, A1R5G5B5 and
A4R4G4B4 keep their entries, and the probe restores the 2D binding, deletes
its texture and leaves no GL error. It fails without the patch.
"""
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
spec = importlib.util.spec_from_file_location('patches', ROOT / 'tests/guest/support/patches.py')
patches = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patches)
MANIFEST = ROOT / 'support/guest/d3d/patches/base/manifest.json'

# The 16-bit RGB(A) formats WineD3D stores in GL_RGB5/GL_RGB565, GL_RGB5_A1 and GL_RGBA4.
FORMATS = ['WINED3DFMT_B5G6R5_UNORM', 'WINED3DFMT_B5G5R5X1_UNORM', 'WINED3DFMT_B5G5R5A1_UNORM',
           'WINED3DFMT_B4G4R4A4_UNORM']
GL_NAMES = ['GL_RGB5', 'GL_RGB565', 'GL_RGB8', 'GL_RGB5_A1', 'GL_RGBA4', 'GL_SRGB8_ALPHA8_EXT', 'GL_RGB',
            'GL_BGRA', 'GL_UNSIGNED_SHORT_5_6_5', 'GL_UNSIGNED_SHORT_1_5_5_5_REV',
            'GL_UNSIGNED_SHORT_4_4_4_4_REV', 'GL_TEXTURE_2D', 'GL_TEXTURE_BINDING_2D', 'GL_NO_ERROR',
            'GL_INVALID_VALUE', 'GL_INVALID_ENUM']

PREAMBLE = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef int BOOL, GLint, GLsizei;
typedef unsigned int UINT, DWORD, GLuint, GLenum;
typedef unsigned char BYTE;
#define TRUE 1
#define FALSE 0
#define TRACE(...) ((void)0)
%(defines)s
enum wined3d_gl_extension { WINED3D_GL_EXT_NONE, ARB_ES2_COMPATIBILITY, WINED3D_GL_EXT_COUNT };
enum wined3d_format_id { WINED3DFMT_UNKNOWN, %(formats)s, WINED3DFMT_COUNT };
struct wined3d_format { enum wined3d_format_id id; GLint glInternal, glGammaInternal, rtInternal, glFormat, glType; };
struct wined3d_gl_ops { struct {
    GLenum (*p_glGetError)(void);
    void (*p_glGetIntegerv)(GLenum, GLint *);
    void (*p_glGenTextures)(GLsizei, GLuint *);
    void (*p_glBindTexture)(GLenum, GLuint);
    void (*p_glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
    void (*p_glDeleteTextures)(GLsizei, const GLuint *);
} gl; };
struct wined3d_gl_info { BOOL supported[WINED3D_GL_EXT_COUNT]; struct wined3d_gl_ops gl_ops;
        struct wined3d_format formats[WINED3DFMT_COUNT]; };
struct entry { enum wined3d_format_id id; GLint gl_internal, gl_srgb_internal, gl_rt_internal, gl_format, gl_type;
        enum wined3d_gl_extension extension; };
static const struct entry entries[] = {
%(entries)s
};

/* A GL with one texture unit's 2D binding, a name space and an error flag. */
static struct { BOOL accept; GLuint bound, next, live[8]; unsigned int live_count, calls; GLenum error;
        GLint last_internal; } fake;
static void raise_error(GLenum e) { if (!fake.error) fake.error = e; }
static GLenum get_error(void) { GLenum e = fake.error; ++fake.calls; fake.error = GL_NO_ERROR; return e; }
static void get_integer(GLenum name, GLint *v) { ++fake.calls; assert(name == GL_TEXTURE_BINDING_2D); *v = (GLint)fake.bound; }
static void gen_textures(GLsizei n, GLuint *out)
{
    ++fake.calls;
    assert(n == 1);
    *out = ++fake.next;
    fake.live[fake.live_count++] = *out;
}
static void bind_texture(GLenum target, GLuint name) { ++fake.calls; assert(target == GL_TEXTURE_2D); fake.bound = name; }
static void tex_image(GLenum target, GLint level, GLint internal, GLsizei w, GLsizei h, GLint border, GLenum format,
        GLenum type, const void *pixels)
{
    ++fake.calls;
    assert(target == GL_TEXTURE_2D && !level && w == 1 && h == 1 && !border && !pixels);
    assert(format == GL_RGB && type == GL_UNSIGNED_SHORT_5_6_5 && fake.bound);
    fake.last_internal = internal;
    if (internal != GL_RGB565 || !fake.accept)
        raise_error(GL_INVALID_VALUE);
}
static void delete_textures(GLsizei n, const GLuint *names)
{
    unsigned int i;
    ++fake.calls;
    assert(n == 1);
    for (i = 0; i < fake.live_count; ++i)
        if (fake.live[i] == *names)
            fake.live[i] = fake.live[--fake.live_count];
    if (fake.bound == *names)
        fake.bound = 0;
}
'''

TEST = r'''
%(probe)s
/* init_format_texture_info's entry selection and assignment, in table order. */
static void assign(struct wined3d_gl_info *gl_info)
{
    const struct entry *format_texture_info = entries;
    BOOL rgb565;
    unsigned int i;
    memset(gl_info->formats, 0, sizeof(gl_info->formats));
%(rgb565)s
    for (i = 0; i < sizeof(entries) / sizeof(*entries); ++i)
    {
        struct wined3d_format *format;
%(select)s
        format = &gl_info->formats[format_texture_info[i].id];
        format->id = format_texture_info[i].id;
%(fields)s
    }
}

static void expect(const struct wined3d_format *f, GLint internal, GLint rt, GLint format, GLint type)
{
    assert(f->glInternal == internal && f->glGammaInternal == internal);
    assert(f->rtInternal == rt && f->glFormat == format && f->glType == type);
}

int main(void)
{
    static const char *const modes[] = {"frontend rejects GL_RGB565", "frontend accepts GL_RGB565",
            "ARB_ES2_compatibility"};
    struct wined3d_gl_info gl_info;
    unsigned int mode;
    for (mode = 0; mode < 3; ++mode)
    {
        const BOOL exact = mode != 0;
        memset(&gl_info, 0, sizeof(gl_info));
        memset(&fake, 0, sizeof(fake));
        gl_info.gl_ops.gl.p_glGetError = get_error;
        gl_info.gl_ops.gl.p_glGetIntegerv = get_integer;
        gl_info.gl_ops.gl.p_glGenTextures = gen_textures;
        gl_info.gl_ops.gl.p_glBindTexture = bind_texture;
        gl_info.gl_ops.gl.p_glTexImage2D = tex_image;
        gl_info.gl_ops.gl.p_glDeleteTextures = delete_textures;
        gl_info.supported[WINED3D_GL_EXT_NONE] = TRUE;
        gl_info.supported[ARB_ES2_COMPATIBILITY] = mode == 2;
        fake.accept = mode == 1;
        /* A stale error from earlier capability queries, and a bound texture. */
        fake.error = GL_INVALID_ENUM;
        fake.bound = 41;
        fake.next = 100;
        assign(&gl_info);
        if (mode == 2)
            assert(!fake.calls); /* the extension answers without a probe */
        else
            assert(fake.last_internal == GL_RGB565 && fake.next == 101);
        /* Binding restored, texture freed; the probe drains errors, the
         * extension path leaves GL state alone. */
        assert(fake.bound == 41 && !fake.live_count);
        assert(fake.error == (mode == 2 ? GL_INVALID_ENUM : GL_NO_ERROR));
        assert(!gl_info.supported[ARB_ES2_COMPATIBILITY] == (mode != 2));
        expect(&gl_info.formats[WINED3DFMT_B5G6R5_UNORM], exact ? GL_RGB565 : GL_RGB5, GL_RGB8, GL_RGB,
                GL_UNSIGNED_SHORT_5_6_5);
        expect(&gl_info.formats[WINED3DFMT_B5G5R5X1_UNORM], GL_RGB5, 0, GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV);
        expect(&gl_info.formats[WINED3DFMT_B5G5R5A1_UNORM], GL_RGB5_A1, 0, GL_BGRA, GL_UNSIGNED_SHORT_1_5_5_5_REV);
        assert(gl_info.formats[WINED3DFMT_B4G4R4A4_UNORM].glInternal == GL_RGBA4
                && gl_info.formats[WINED3DFMT_B4G4R4A4_UNORM].glGammaInternal == GL_SRGB8_ALPHA8_EXT
                && gl_info.formats[WINED3DFMT_B4G4R4A4_UNORM].glFormat == GL_BGRA
                && gl_info.formats[WINED3DFMT_B4G4R4A4_UNORM].glType == GL_UNSIGNED_SHORT_4_4_4_4_REV);
        printf("  %%-27s B5G6R5 -> %%s\n", modes[mode], exact ? "GL_RGB565" : "GL_RGB5");
    }
    return 0;
}
'''


def block(text, start):
    """text[start:] up to the brace closing its first opening brace."""
    depth, at = 0, text.index('{', start)
    while True:
        if text[at] == '{':
            depth += 1
        elif text[at] == '}':
            depth -= 1
            if not depth:
                return text[start:at + 1]
        at += 1


def split_entries(table):
    """The brace-delimited entries of a C array initializer."""
    table = re.sub(r'/\*.*?\*/', '', table, flags=re.S)
    table = table[table.index('{') + 1:table.rindex('}')]
    out, depth, start = [], 0, None
    for at, c in enumerate(table):
        if c == '{':
            depth += 1
            if depth == 1:
                start = at + 1
        elif c == '}':
            depth -= 1
            if depth == 0:
                out.append(table[start:at])
    return out


def prepared():
    temporary = tempfile.TemporaryDirectory(prefix='dreamgpu-rgb565-')
    work = Path(temporary.name)
    for name in json.loads(MANIFEST.read_text())['files']:
        p = work / name
        p.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / 'vendor/wine9x' / name, p)
    patches.apply(work, MANIFEST)
    return temporary, work


def compile_and_run(work, name, source):
    (work / (name + '.c')).write_text(source)
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c99', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
                    '-Wno-unused-function', '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                    '-fno-omit-frame-pointer', str(work / (name + '.c')), '-o', str(work / name)], check=True)
    result = subprocess.run([str(work / name)], capture_output=True, text=True)
    if result.returncode:
        sys.exit(result.stdout + result.stderr)
    return result.stdout


temporary, work = prepared()
with temporary:
    utils = (work / 'wined3d/utils.c').read_text()
    wgl = (ROOT / 'vendor/wine9x/include/wine/wgl.h').read_text()
    values = dict(re.findall(r'^#define\s+(GL_\w+)\s+(0x[0-9a-fA-F]+|\d+)\b', wgl, re.M))
    defines = '\n'.join('#define %s %s' % (n, values[n]) for n in GL_NAMES)

    # The actual 16-bit entries of format_texture_info, in table order.
    table = utils[utils.index('static const struct wined3d_format_texture_info format_texture_info[] =\n{'):]
    table = table[table.index('{'):table.index('\n};\n') + 2]
    entries = []
    for entry in split_entries(table):
        fields = [f.strip() for f in entry.split(',')]
        if fields[0] in FORMATS:
            entries.append('    {%s, %s, %s, %s, %s, %s, %s}' % (*fields[:6], fields[8]))
    assert len(entries) == 5 and sum('GL_RGB565' in e for e in entries) == 1, entries

    # The probe, the decision before the loop, the entry selection and the
    # field assignment of init_format_texture_info.
    if 'static BOOL dg_rgb565_storage(' not in utils:
        sys.exit('FAIL prepared utils.c has no dg_rgb565_storage probe (patch 0057)')
    probe = block(utils, utils.index('static BOOL dg_rgb565_storage(')) + '\n'
    init = block(utils, utils.index('static BOOL init_format_texture_info('))
    rgb565 = re.search(r'^    rgb565 = .*;$', init, re.M).group(0)
    assert rgb565 == '    rgb565 = gl_info->supported[ARB_ES2_COMPATIBILITY] || dg_rgb565_storage(gl_info);'
    assert init.index(rgb565) < init.index('    for (i = 0; i < sizeof(format_texture_info)')
    start = init.index('        if (!gl_info->supported[format_texture_info[i].extension]')
    select = init[start:init.index('continue;', start) + len('continue;')]
    fields = re.findall(r'^        format->(?:glInternal|glGammaInternal|rtInternal|glFormat|glType) = '
                        r'format_texture_info\[i\]\.\w+;$', init, re.M)
    assert len(fields) == 5, fields
    assert 'supported[ARB_ES2_COMPATIBILITY] = ' not in utils
    source = PREAMBLE % {'defines': defines, 'formats': ', '.join(FORMATS), 'entries': ',\n'.join(entries)}
    source += TEST % {'probe': probe, 'rgb565': rgb565, 'select': select, 'fields': '\n'.join(fields)}
    print(compile_and_run(work, 'rgb565', source), end='')
print('PASS prepared R5G6B5 storage (0057): GL_RGB565 exactly with the frontend\'s exact storage or '
      'ARB_ES2_compatibility; X1R5G5B5/A1R5G5B5/A4R4G4B4 unchanged; probe restores binding, frees, no error')
