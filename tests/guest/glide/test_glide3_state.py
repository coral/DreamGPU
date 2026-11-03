#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Reproduce the grGlideGetState/SetState texturing loss with the actual
shared-core combine code, then check the Glide 3 restore of order-derived flags.

The core's grGlideSetState (actual patched source) replays colour combine
before texture combine and blending last. grColorCombine samples the texture
combine current at that moment, and grAlphaBlendFunction recomputes
OpenGL.Texture from it, so restoring a textured state after the texture
combine was changed left texturing disabled: DGSYSG3 "restored state" read
black instead of the texture. The real colour/alpha/texture combine, blend
and state functions and combine tables are compiled with GL calls stubbed;
unrelated state setters are no-ops."""
import os
from pathlib import Path
import random
import re
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
DONOR = ROOT / 'vendor/qemu-xtra/openglide'
from source import patched_source


def donor(name):
    return (DONOR / name).read_text(encoding='latin-1')


def function(text, name):
    start = text.index(f'FX_ENTRY void FX_CALL\n{name}(')
    end = text.index('\n}\n', start) + 3
    return text[start:end]


glogl = donor('GlOgl.h')
structs = glogl[glogl.index('// Class declarations'):glogl.index('// Extern variables')]
render = donor('GLRender.h')
render_types = render[render.index('struct ColorStruct'):render.index('struct RenderStruct')]
typedefs = render[render.index('typedef float (*ALPHAFACTORFUNCPROC)'):render.index('//*****', render.index('typedef float (*ALPHAFACTORFUNCPROC)'))]
prototypes = render[render.index('// Prototypes for the color combining'):render.index('#endif', render.index('// Prototypes for the color combining'))]
stubs = []
for line in prototypes.splitlines():
    m = re.match(r'(float|void)\s+(FASTCALL\s+)?(\w+)\((.*)\);', line.strip())
    if m:
        stubs.append(f'{m.group(1)} {m.group(2) or ""}{m.group(3)}({m.group(4)}) '
                     f'{{ {"return 0;" if m.group(1) == "float" else ""} }}')
assert len(stubs) >= 25, len(stubs)
tables = donor('OGLTables.h')
table_types = tables[tables.rindex('typedef struct', 0, tables.index('    bool                local;')):tables.index('extern OGLAspect')]
table_data = donor('OGLColorAlphaTables.cpp').split('#include "OGLTables.h"', 1)[1]
color_alpha = donor('grguColorAlpha.cpp')
core = ''.join(function(color_alpha, name)
               for name in ('grColorCombine', 'grAlphaBlendFunction', 'grAlphaCombine'))
core += function(donor('grguTex.cpp'), 'grTexCombine')
sst = patched_source('grguSstGlide.cpp')
core += function(sst, 'grGlideSetState') + function(sst, 'grGlideGetState')

HARNESS = r'''
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "sdk2_glide.h"
#include "glide3-core.h"
typedef unsigned int GLuint;
typedef unsigned int GLenum;
typedef int GLint;
typedef int GLsizei;
typedef float GLfloat;
typedef unsigned char GLboolean;
#define FASTCALL
#define CopyMemory(d, s, n) memcpy(d, s, n)
enum { GL_TEXTURE_ENV = 1, GL_TEXTURE_ENV_MODE, GL_DECAL, GL_BLEND, GL_MODULATE, GL_ZERO,
       GL_ONE, GL_SRC_COLOR, GL_ONE_MINUS_SRC_COLOR, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
       GL_DST_ALPHA, GL_ONE_MINUS_DST_ALPHA, GL_DST_COLOR, GL_ONE_MINUS_DST_COLOR,
       GL_SRC_ALPHA_SATURATE };
static GLint env_mode;
static void glTexEnvi(GLenum, GLenum, GLint mode) { env_mode = mode; }
static void glBlendFunc(GLenum, GLenum) {}
static void RenderDrawTriangles() {}
''' + structs + r'''
GlideStruct Glide;
OpenGLStruct OpenGL;
''' + render_types + typedefs + '\n'.join(stubs) + r'''
ALPHAFACTORFUNCPROC AlphaFactorFunc;
COLORFACTORFUNCPROC ColorFactor3Func;
COLORFUNCTIONPROC ColorFunctionFunc;
''' + table_types + table_data + r'''
// grGlideSetState's other setters do not touch the combine flags.
FX_ENTRY void FX_CALL grRenderBuffer(GrBuffer_t) {}
FX_ENTRY void FX_CALL grDepthBufferMode(GrDepthBufferMode_t) {}
FX_ENTRY void FX_CALL grDepthBufferFunction(GrCmpFnc_t) {}
FX_ENTRY void FX_CALL grDepthMask(FxBool) {}
FX_ENTRY void FX_CALL grDepthBiasLevel(FxI16) {}
FX_ENTRY void FX_CALL grDitherMode(GrDitherMode_t) {}
FX_ENTRY void FX_CALL grChromakeyValue(GrColor_t) {}
FX_ENTRY void FX_CALL grChromakeyMode(GrChromakeyMode_t) {}
FX_ENTRY void FX_CALL grAlphaTestReferenceValue(GrAlpha_t) {}
FX_ENTRY void FX_CALL grAlphaTestFunction(GrCmpFnc_t) {}
FX_ENTRY void FX_CALL grColorMask(FxBool, FxBool) {}
FX_ENTRY void FX_CALL grConstantColorValue(GrColor_t) {}
FX_ENTRY void FX_CALL grFogColorValue(GrColor_t) {}
FX_ENTRY void FX_CALL grFogMode(GrFogMode_t) {}
FX_ENTRY void FX_CALL grCullMode(GrCullMode_t) {}
FX_ENTRY void FX_CALL grTexClampMode(GrChipID_t, GrTextureClampMode_t, GrTextureClampMode_t) {}
FX_ENTRY void FX_CALL grTexFilterMode(GrChipID_t, GrTextureFilterMode_t, GrTextureFilterMode_t) {}
FX_ENTRY void FX_CALL grTexMipMapMode(GrChipID_t, GrMipMapMode_t, FxBool) {}
FX_ENTRY void FX_CALL grClipWindow(FxU32, FxU32, FxU32, FxU32) {}
FX_ENTRY void FX_CALL grSstOrigin(GrOriginLocation_t) {}
FX_ENTRY void FX_CALL grTexSource(GrChipID_t, FxU32, FxU32, GrTexInfo *) {}
FX_ENTRY void FX_CALL grTexLodBiasValue(GrChipID_t, float) {}
''' + core + r'''
struct Derived {
    dg3::CoreFlags flags;
    ALPHAFACTORFUNCPROC alpha;
    COLORFACTORFUNCPROC factor;
    COLORFUNCTIONPROC color;
    GLint env;
};
static Derived derived() {
    return {dg3::capture_flags(Glide, OpenGL), AlphaFactorFunc, ColorFactor3Func,
            ColorFunctionFunc, env_mode};
}
static bool same(const Derived &a, const Derived &b) {
    return !memcmp(&a.flags, &b.flags, sizeof(a.flags)) && a.alpha == b.alpha &&
           a.factor == b.factor && a.color == b.color && a.env == b.env;
}
// Valid combine arguments: functions 0..0x10, factors 0..0xd, local/other 0..2.
static void random_call(unsigned r) {
    const int function = int(r % 17), factor = int((r / 17) % 14), local = int((r / 238) % 3),
              other = int((r / 714) % 3), blend = int((r / 2142) % 16);
    switch ((r / 34272) % 4) {
    case 0: grColorCombine(function, factor, local, other, (r >> 30) & 1); break;
    case 1: grAlphaCombine(function, factor, local, other, (r >> 30) & 1); break;
    case 2:
        grTexCombine(GR_TMU0, function, factor, int((r >> 9) % 17), int((r >> 13) % 14), FXFALSE,
                     FXFALSE);
        break;
    default: grAlphaBlendFunction(blend, int((r >> 7) % 16), GR_BLEND_ONE, GR_BLEND_ZERO); break;
    }
}

int main(int argc, char **argv) {
    (void)argc;
    srand(unsigned(atoi(argv[1])));
    // The DGSYSG3 sequence: a textured combine, saved, changed, restored.
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
    grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_ZERO, GR_COMBINE_LOCAL_ITERATED,
                   GR_COMBINE_OTHER_CONSTANT, FXFALSE);
    grTexCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_ZERO,
                 GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_ZERO, FXFALSE, FXFALSE);
    grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_CONSTANT,
                   GR_COMBINE_OTHER_TEXTURE, FXFALSE);
    assert(OpenGL.Texture && OpenGL.ColorTexture);
    GrState saved;
    grGlideGetState(&saved);
    const Derived before = derived();
    grColorCombine(GR_COMBINE_FUNCTION_ZERO, GR_COMBINE_FACTOR_ZERO, GR_COMBINE_LOCAL_CONSTANT,
                   GR_COMBINE_OTHER_CONSTANT, FXFALSE);
    grTexCombine(GR_TMU0, GR_COMBINE_FUNCTION_ZERO, GR_COMBINE_FACTOR_ZERO,
                 GR_COMBINE_FUNCTION_ZERO, GR_COMBINE_FACTOR_ZERO, FXFALSE, FXFALSE);
    grGlideSetState(&saved);
    // The defect: the core replay leaves texturing off for a textured state.
    assert(!OpenGL.Texture && !OpenGL.ColorTexture);
    dg3::restore_flags(before.flags, Glide, OpenGL);
    assert(OpenGL.Texture && OpenGL.ColorTexture && same(before, derived()));

    // Any order of valid combine/blend calls: replay plus the Glide 3 flag
    // restore reproduces every flag, factor/function callback and texture
    // environment the renderer reads, whatever changed in between.
    unsigned replay_differs = 0;
    for (unsigned trial = 0; trial < 20000; ++trial) {
        for (unsigned n = 1 + rand() % 6; n; --n)
            random_call(unsigned(rand()) << 1 ^ unsigned(rand()));
        grGlideGetState(&saved);
        const Derived want = derived();
        for (unsigned n = 1 + rand() % 6; n; --n)
            random_call(unsigned(rand()) << 1 ^ unsigned(rand()));
        grGlideSetState(&saved);
        replay_differs += !same(want, derived());
        dg3::restore_flags(want.flags, Glide, OpenGL);
        assert(same(want, derived()));
        GrState now;
        grGlideGetState(&now);
        assert(!memcmp(&now, &saved, sizeof(GlideState)));
    }
    assert(replay_differs > 0);
    printf("PASS actual Glide state restore: core replay loses order-derived combine flags in "
           "%u of 20000 random sequences (DGSYSG3 restored-state case reproduced); Glide 3 flag "
           "restore is exact in all\n", replay_differs);
}
'''

compiler = os.environ.get('CXX') or shutil.which('clang++')
if not compiler:
    raise SystemExit('clang++ (or CXX) is required for the actual-source sanitizer gate')
with tempfile.TemporaryDirectory(prefix='dg-glide3-state-') as temporary:
    work = Path(temporary)
    unix = donor('sdk2_unix.h.in')
    for name, value in (('UNSIGNED_CHAR', 1), ('UNSIGNED_SHORT', 2), ('UNSIGNED_INT', 4),
                        ('UNSIGNED_LONG', 8), ('UNSIGNED_LONG_LONG', 8), ('INT_P', 8)):
        unix = unix.replace(f'@SIZEOF_{name}@', str(value))
    (work / 'sdk2_unix.h').write_text(unix)
    (work / 'test.cpp').write_text(HARNESS)
    subprocess.run([compiler, '-std=c++20', '-O1', '-g', '-w', '-fsanitize=address,undefined',
                    '-fno-sanitize-recover=all', '-I', str(work), '-I', str(DONOR),
                    '-I', str(ROOT / 'guest/glide'), str(work / 'test.cpp'), '-o',
                    str(work / 'test')], check=True)
    subprocess.run([str(work / 'test'), str(random.SystemRandom().randrange(1 << 30))],
                   check=True)
