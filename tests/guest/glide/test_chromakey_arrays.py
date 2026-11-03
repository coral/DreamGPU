#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual patched chroma-keyed draw: one array draw with RGB-only colours under the alpha test.

The donor drew chroma-keyed batches (blending off, chroma key on) in immediate
mode, five GL calls per vertex, while every other batch uses glDrawArrays.
Patch 0006 draws them from the same arrays. The colour pointer is three
components with the packed 16-byte stride, so every vertex has alpha 1 as
glColor3fv gave, and the alpha test sees only the keyed texture alpha. The
normal four-component pointer and the disabled alpha test are restored
afterwards. Fails without the patch (no array draw for the keyed batch).
"""
from pathlib import Path
import subprocess
import tempfile
from source import patched_source

ROOT = Path(__file__).resolve().parents[3]
s = patched_source('GLRender.cpp')
start = s.index('    if( !OpenGL.Blend && Glide.State.ChromaKeyMode')
end = s.index('    if ( ! InternalConfig.EXT_secondary_color )', start)
branch = s[start:end]
header = (ROOT / 'vendor/qemu-xtra/openglide/GLRender.h').read_text()
layouts = header[header.index('struct TColorStruct'):header.index('struct RenderStruct')]

program = r'''
#include <cassert>
#include <cstring>
using GLfloat = float;
#define GL_FLOAT 0x1406
#define GL_TRIANGLES 4
#define GL_ALPHA_TEST 0x0bc0
#define GL_GEQUAL 0x0206
#define GL_TEXTURE1_ARB 0x84c1
''' + layouts + r'''
static struct { TColorStruct *TColor, *TColor2; TTextureStruct *TTexture; TVertexStruct *TVertex;
                TFogStruct *TFog; int NumberOfTriangles; } OGLRender;
static struct { bool EXT_vertex_array; } InternalConfig;
static struct { bool Blend; } OpenGL;
static struct { struct { int ChromaKeyMode; } State; } Glide;
static int colorSize = 4, colorStride = 0, draws = 0, drawCount = 0, immediate = 0;
static const void *colorPointer;
static bool alphaTest = false, testedDuringDraw = false;
static int alphaFunc = 0; static float alphaRef = 0;
static void glColorPointer(int n, int type, int stride, const void *p) {
    assert(type == GL_FLOAT); colorSize = n; colorStride = stride; colorPointer = p; }
static void glDrawArrays(int mode, int first, int count) {
    assert(mode == GL_TRIANGLES && first == 0);
    // The keyed draw: RGB only, packed four-float stride, alpha test GEQUAL 0.5.
    assert(colorSize == 3 && colorStride == 16 && colorPointer == OGLRender.TColor);
    testedDuringDraw = alphaTest && alphaFunc == GL_GEQUAL && alphaRef == 0.5f;
    ++draws; drawCount = count; }
static void glAlphaFunc(int f, float r) { alphaFunc = f; alphaRef = r; }
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
static void draw(bool use_two_tex) {
''' + branch + r'''
}
int main() {
    TColorStruct colors[3]; TVertexStruct vertices[3]; TTextureStruct tex[3]; TColorStruct second[3]; TFogStruct fog[3];
    std::memset(colors, 0, sizeof colors);
    OGLRender = {colors, second, tex, vertices, fog, 3};
    InternalConfig.EXT_vertex_array = true; OpenGL.Blend = false; Glide.State.ChromaKeyMode = 1;
    draw(false);
    assert(draws == 1 && drawCount == 9 && immediate == 0 && testedDuringDraw);
    assert(!alphaTest && colorSize == 4 && colorStride == 0 && colorPointer == colors);
    // Blending on or no chroma key: the ordinary draw path, not this branch.
    OpenGL.Blend = true; draw(false); OpenGL.Blend = false; Glide.State.ChromaKeyMode = 0; draw(false);
    assert(draws == 3 && immediate == 0);
}
'''

# The ordinary path also calls glDrawArrays with the normal 4-component pointer.
program = program.replace(
    '    // The keyed draw: RGB only, packed four-float stride, alpha test GEQUAL 0.5.\n'
    '    assert(colorSize == 3 && colorStride == 16 && colorPointer == OGLRender.TColor);\n',
    '    // The keyed draw: RGB only, packed four-float stride, alpha test GEQUAL 0.5.\n'
    '    if (Glide.State.ChromaKeyMode && !OpenGL.Blend)\n'
    '        assert(colorSize == 3 && colorStride == 16 && colorPointer == OGLRender.TColor);\n'
    '    else\n'
    '        assert(colorSize == 4 && colorStride == 0 && !alphaTest);\n')

with tempfile.TemporaryDirectory(prefix='dg-glide-chroma-arrays-') as temporary:
    work = Path(temporary)
    (work / 'test.cpp').write_text(program)
    subprocess.run(['c++', '-std=c++23', '-O1', '-g', '-Wall', '-Wextra', '-Wno-unused-parameter',
                    '-Wno-unused-function', '-fsanitize=address,undefined', str(work / 'test.cpp'),
                    '-o', str(work / 'test')], check=True)
    subprocess.run([str(work / 'test')], check=True)
print('PASS actual Glide chroma-keyed draw: one RGB-only array draw under GEQUAL 0.5, state restored, no immediate mode')
