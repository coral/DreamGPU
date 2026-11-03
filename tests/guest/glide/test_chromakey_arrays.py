#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual patched chroma-keyed draw: one array draw with RGB-only colours under the alpha test.

The donor drew chroma-keyed batches (blending off, chroma key on) in immediate
mode, five GL calls per vertex, while every other batch uses glDrawArrays.
Patch 0006 draws them from the same arrays. The colour pointer is three
components with the packed 16-byte stride, so every vertex has alpha 1 as
glColor3fv gave, and the alpha test sees only the keyed texture alpha.

Voodoo hardware discards keyed pixels in every blend mode; the donor keyed only
unblended batches, so Diablo II's menu text (blended ZERO, SRC_COLOR) multiplied
the buttons to black around every glyph. Patch 0007 keys blended batches whose
blend never reads the source alpha while alpha writes are off, where replacing
the fragment alpha is invisible, and restores the application's alpha function.
Fails without either patch.
"""
from pathlib import Path
import subprocess
import tempfile
from source import patched_source

ROOT = Path(__file__).resolve().parents[3]
s = patched_source('GLRender.cpp')
helper_start = s.index('static bool KeyedAlphaUnused( void )')
helper = s[helper_start:s.index('\n}\n', helper_start) + 3]
start = s.index('    if( Glide.State.ChromaKeyMode && KeyedAlphaUnused( )')
end = s.index('    if ( ! InternalConfig.EXT_secondary_color )', start)
branch = s[start:end]
header = (ROOT / 'vendor/qemu-xtra/openglide/GLRender.h').read_text()
layouts = header[header.index('struct TColorStruct'):header.index('struct RenderStruct')]

program = r'''
#include <cassert>
#include <cstring>
using GLfloat = float; using GLenum = unsigned;
#define GL_FLOAT 0x1406
#define GL_TRIANGLES 4
#define GL_ALPHA_TEST 0x0bc0
#define GL_GREATER 0x0204
#define GL_GEQUAL 0x0206
#define GL_TEXTURE1_ARB 0x84c1
#define GL_ZERO 0
#define GL_ONE 1
#define GL_SRC_COLOR 0x0300
#define GL_ONE_MINUS_SRC_COLOR 0x0301
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_DST_ALPHA 0x0304
#define GL_ONE_MINUS_DST_ALPHA 0x0305
#define GL_DST_COLOR 0x0306
#define GL_ONE_MINUS_DST_COLOR 0x0307
#define GL_SRC_ALPHA_SATURATE 0x0308
''' + layouts + r'''
static struct { TColorStruct *TColor, *TColor2; TTextureStruct *TTexture; TVertexStruct *TVertex;
                TFogStruct *TFog; int NumberOfTriangles; } OGLRender;
static struct { bool EXT_vertex_array; } InternalConfig;
static struct { bool Blend; GLenum SrcBlend, DstBlend, AlphaTestFunction; GLfloat AlphaReferenceValue; } OpenGL;
static struct { struct { int ChromaKeyMode; bool AlphaMask; } State; } Glide;
static int colorSize = 4, colorStride = 0, keyed = 0, ordinary = 0, immediate = 0;
static const void *colorPointer;
static bool alphaTest = false;
static GLenum alphaFunc = 0; static float alphaRef = 0;
static void glColorPointer(int n, int type, int stride, const void *p) {
    assert(type == GL_FLOAT); colorSize = n; colorStride = stride; colorPointer = p; }
static void glDrawArrays(int mode, int first, int count) {
    assert(mode == GL_TRIANGLES && first == 0 && count == 9);
    if (colorSize == 3) {
        // The keyed draw: RGB only, packed four-float stride, alpha test GEQUAL 0.5.
        assert(colorStride == 16 && colorPointer == OGLRender.TColor);
        assert(alphaTest && alphaFunc == GL_GEQUAL && alphaRef == 0.5f);
        ++keyed;
    } else {
        assert(colorSize == 4 && colorStride == 0 && !alphaTest);
        ++ordinary;
    }
}
static void glAlphaFunc(GLenum f, float r) { alphaFunc = f; alphaRef = r; }
static void glEnable(int cap) { if (cap == GL_ALPHA_TEST) alphaTest = true; }
static void glDisable(int cap) { if (cap == GL_ALPHA_TEST) alphaTest = false; }
static void glBegin(int) { ++immediate; }
static void glEnd() {}
static void glColor3fv(const float *) { ++immediate; }
static void glTexCoord4fv(const float *) { ++immediate; }
static void glVertex3fv(const float *) { ++immediate; }
static void glColor4fv(const float *) { ++immediate; }
static void p_glSecondaryColor3fvEXT(const float *) { ++immediate; }
static void p_glFogCoordfEXT(float) { ++immediate; }
static void p_glMultiTexCoord4fvARB(int, const float *) { ++immediate; }
''' + helper + r'''
static void draw(bool use_two_tex) {
''' + branch + r'''
}
// One batch: returns true if it took the keyed path. The application's alpha
// function (GREATER 0.25) survives either way.
static bool batch(bool blend, GLenum src, GLenum dst, bool alphaWrites, bool key = true) {
    OpenGL.Blend = blend; OpenGL.SrcBlend = src; OpenGL.DstBlend = dst;
    Glide.State.AlphaMask = alphaWrites; Glide.State.ChromaKeyMode = key;
    alphaFunc = OpenGL.AlphaTestFunction; alphaRef = OpenGL.AlphaReferenceValue;
    const int before = keyed;
    draw(false);
    assert(!alphaTest && colorSize == 4 && colorStride == 0 && colorPointer == OGLRender.TColor);
    assert(alphaFunc == GL_GREATER && alphaRef == 0.25f);
    return keyed != before;
}
int main() {
    TColorStruct colors[3]; TVertexStruct vertices[3]; TTextureStruct tex[3]; TColorStruct second[3]; TFogStruct fog[3];
    std::memset(colors, 0, sizeof colors);
    OGLRender = {colors, second, tex, vertices, fog, 3};
    InternalConfig.EXT_vertex_array = true;
    OpenGL.AlphaTestFunction = GL_GREATER; OpenGL.AlphaReferenceValue = 0.25f;
    // Unblended (Diablo II sprites and logo), with or without alpha writes.
    assert(batch(false, GL_ONE, GL_ZERO, false));
    assert(batch(false, GL_ONE, GL_ZERO, true));
    // Diablo II menu text (ZERO, SRC_COLOR) and additive flames (ONE, ONE), alpha writes off.
    assert(batch(true, GL_ZERO, GL_SRC_COLOR, false));
    assert(batch(true, GL_ONE, GL_ONE, false));
    assert(batch(true, GL_DST_COLOR, GL_ZERO, false));
    assert(batch(true, GL_ONE_MINUS_DST_ALPHA, GL_DST_ALPHA, false));
    // The source alpha is read, or written to the framebuffer: keep the ordinary draw.
    assert(!batch(true, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, false));
    assert(!batch(true, GL_ONE, GL_ONE_MINUS_SRC_ALPHA, false));
    assert(!batch(true, GL_SRC_ALPHA_SATURATE, GL_ONE, false));
    assert(!batch(true, GL_ZERO, GL_SRC_COLOR, true));
    // No chroma key: always the ordinary draw.
    assert(!batch(false, GL_ONE, GL_ZERO, false, false));
    assert(!batch(true, GL_ZERO, GL_SRC_COLOR, false, false));
    assert(keyed == 6 && ordinary == 6 && immediate == 0);
}
'''

with tempfile.TemporaryDirectory(prefix='dg-glide-chroma-arrays-') as temporary:
    work = Path(temporary)
    (work / 'test.cpp').write_text(program)
    subprocess.run(['c++', '-std=c++23', '-O1', '-g', '-Wall', '-Wextra', '-Wno-unused-parameter',
                    '-Wno-unused-function', '-fsanitize=address,undefined', str(work / 'test.cpp'),
                    '-o', str(work / 'test')], check=True)
    subprocess.run([str(work / 'test')], check=True)
print('PASS actual Glide chroma-keyed draw: unblended and alpha-free blended batches keyed from arrays, '
      'source-alpha blends and alpha writes untouched, application alpha function restored')
