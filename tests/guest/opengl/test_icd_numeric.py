#!/usr/bin/env python3
"""Exercise numeric ICD aliases through actual frontend scalar packing/state.

Only Win32/transport services use the existing frontend harness. No replacement
color, normal, rectangle or Begin/End implementation participates in this test.
"""
# SPDX-License-Identifier: GPL-2.0-or-later
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SOURCE = ROOT / 'guest/opengl'
aliases = re.findall(r'static void APIENTRY Alias(\w+)\(', (SOURCE / 'icd-numeric.inc').read_text())
assert len(aliases) == 39
checks = []
types = {'b': 'GLbyte', 's': 'GLshort', 'i': 'GLint', 'ub': 'GLubyte',
         'us': 'GLushort', 'ui': 'GLuint', 'd': 'GLdouble', 'f': 'GLfloat'}
# Explicit GL1.1 table2.6 checkpoints; signed zero is positive. These values
# distinguish the legacy mapping from both max-positive division and clamping.
values = {
    'b': ('-128,-1,0,127', '-1.0f,-0.00392156862745098f,0.00392156862745098f,1.0f'),
    's': ('-32768,-1,0,32767', '-1.0f,-0.0000152590218966964f,0.0000152590218966964f,1.0f'),
    'i': ('(-2147483647-1),-1,0,2147483647', '-1.0f,-0.0000000002328306437080797f,0.0000000002328306437080797f,1.0f'),
    'ub': ('0,1,127,255', '0.0f,0.00392156862745098f,0.4980392156862745f,1.0f'),
    'us': ('0,1,32767,65535', '0.0f,0.0000152590218966964f,0.49999237048905165f,1.0f'),
    'ui': ('0,1,2147483647U,4294967295U', '0.0f,0.0000000002328306437080797f,0.5f,1.0f'),
    'd': ('-2.5,-0.125,1.25,3.75', '-2.5f,-0.125f,1.25f,3.75f'),
}
for name in aliases:
    match = re.fullmatch(r'(Color|Normal)([34])(\w+?)(v?)', name)
    if match:
        kind, count, suffix, vector = match.groups()
        count = int(count)
        inputs, outputs = (part.split(',') for part in values[suffix])
        # Rotations cover every endpoint even for three-component variants.
        for rotate in range(4):
            vs = (inputs[rotate:] + inputs[:rotate])[:count]
            expected = (outputs[rotate:] + outputs[:rotate])[:count]
            if kind == 'Color' and count == 3:
                expected += ['1.0f']
            call = f'Alias{name}(v);' if vector else f'Alias{name}(' + ','.join(vs) + ');'
            decl = f'const {types[suffix]} v[]={{' + ','.join(vs) + '};' if vector else ''
            function = 'FEnum_glColor4f' if kind == 'Color' else 'FEnum_glNormal3f'
            checks.append('{ clear(c); ' + decl + call + f' expect(c,{function},{{' + ','.join(expected) + '}); }')
        if vector:
            checks.append(f'clear(c); Alias{name}(nullptr); assert(c->Error==GL_INVALID_OPERATION && !c->Records);')
    else:
        suffix = name[4:]
        vector = suffix.endswith('v')
        typ = types[suffix[:-1] if vector else suffix]
        call = f'Alias{name}(a,b);' if vector else f'Alias{name}(3,-2,-4,7);'
        decl = f'const {typ} a[]={{3,-2}},b[]={{-4,7}};' if vector else ''
        checks.append('{ clear(c); '+decl+call+' rectangle(c); }')
        checks.append('{ clear(c); glBegin(GL_TRIANGLES); ULONG used=c->Used; '+decl+call+
                      'assert(c->InBegin && c->Error==GL_INVALID_OPERATION && c->Used==used && c->Records==1); glEnd(); }')
        if vector:
            checks.append('{ clear(c); const '+typ+' v[]={1,2}; Alias'+name+'(nullptr,v); assert(!c->Records && c->Error==GL_INVALID_OPERATION);'+
                          ' clear(c); Alias'+name+'(v,nullptr); assert(!c->Records && c->Error==GL_INVALID_OPERATION); }')

with tempfile.TemporaryDirectory(prefix='dreamgpu-icd-numeric-') as temporary:
    root = Path(temporary)
    (root / 'GL').mkdir()
    for name in ('frontend.cpp', 'internal.h', 'packing.h', 'transport.h', 'scalar.inc',
                 'names.h', 'state.h', 'icd-numeric.inc'):
        shutil.copyfile(SOURCE / name, root / name)
    shutil.copyfile(HERE / 'frontend.cpp', root / 'frontend-harness.cpp')
    shutil.copyfile(HERE / 'frontend-windows.h', root / 'windows.h')
    shutil.copyfile(HERE / 'frontend-gl.h', root / 'GL/gl.h')
    source = r'''
#define main existing_frontend_suite_not_run
#include "frontend-harness.cpp"
#undef main
using GLbyte=int8_t; using GLshort=int16_t;
#include "icd-numeric.inc"
static void clear(JGL_CONTEXT*c) { assert(!c->InBegin);c->Used=c->Records=0;c->Error=0; }
static void expect(JGL_CONTEXT*c,ULONG function,std::initializer_list<float> values) {
    assert(c->Records==1&&!c->Error);
    assert(c->Used==9+values.size());
    const ULONG*r=c->Packet.Words;
    assert(r[0]==DG_GL_CALL&&r[1]==c->Used*4&&r[8]==function);
    unsigned n=9;for(float value:values) { float actual;memcpy(&actual,r+n++,4);assert(actual==value); }
}
static void rectangle(JGL_CONTEXT*c) {
    assert(!c->InBegin&&!c->Error&&c->Records==6);
    const ULONG*r=c->Packet.Words;
    assert(r[8]==FEnum_glBegin&&r[9]==GL_POLYGON);r+=r[1]/4;
    const float vertices[][2]={{3,-2},{-4,-2},{-4,7},{3,7}};
    for(auto&v:vertices) {
        assert(r[8]==FEnum_glVertex2f&&r[1]==44);
        float xy[2];memcpy(xy,r+9,8);assert(xy[0]==v[0]&&xy[1]==v[1]);r+=r[1]/4;
    }
    assert(r[8]==FEnum_glEnd);r+=r[1]/4;assert(r==c->Packet.Words+c->Used);
}
int main() {
    Reset();HGLRC context=Create();auto*c=Lookup(context);
'''
    source += '\n'.join(checks)
    source += r'''
    // Color/normal may be changed inside a primitive; Rect must not change
    // those current attributes, nor emit any extra attribute operations.
    clear(c);glBegin(GL_TRIANGLES);AliasColor3b(127,0,-128);AliasNormal3i(0,2147483647,-2147483647-1);
    assert(c->InBegin&&!c->Error&&c->Records==3);glEnd();
    clear(c);AliasColor4d(-2.5,1.25,4,0.125);AliasNormal3d(7,-3,2);
    ULONG attribute_words=c->Used;ULONG attributes[32];assert(attribute_words<32);
    memcpy(attributes,c->Packet.Words,attribute_words*4);AliasRectf(3,-2,-4,7);
    assert(c->Records==8&&!memcmp(attributes,c->Packet.Words,attribute_words*4));
    // Failed/no-current contexts cannot be revived by these aliases.
    clear(c);c->Failed=TRUE;AliasRectf(1,2,3,4);
    assert(!c->InBegin&&!c->Records&&c->Error==GL_INVALID_OPERATION);c->Failed=FALSE;
    assert(wglMakeCurrent(nullptr,nullptr));AliasRectf(1,2,3,4);AliasColor3d(1,2,3);AliasNormal3b(1,2,3);
    assert(wglDeleteContext(context));Reset();assert(!Allocations);
    // A transport timeout in a split Rect retains uncertain host ownership;
    // its remaining vertices and cleanup must never replay the executed prefix.
    context=Create();c=Lookup(context);MaxRecords=2;
    FailOperation=DG_GL_CALL;FailStatus=DG_ESCAPE_TIMEOUT;
    AliasRecti(3,-2,-4,7);
    assert(c->Failed&&c->Uncertain&&!c->InBegin);
    assert(wglMakeCurrent(nullptr,nullptr));ULONG calls=Calls;
    assert(!wglDeleteContext(context)&&Calls==calls);
    Reset();assert(!Allocations);
    puts("PASS ICD39 numeric aliases: GL1.1 normalization; actual scalar packing, Rect polygon/order, Begin rejection, null vectors, context lifetime");
}
'''
    (root / 'test.cpp').write_text(source)
    command = [os.environ.get('CXX', 'c++'), '-std=c++23', '-O1', '-Wall', '-Wextra', '-Werror',
               '-fno-exceptions', '-fno-rtti', '-fsanitize=address,undefined', '-g',
               '-I'+str(root), '-I'+str(ROOT/'guest/include'), '-I'+str(ROOT/'guest/nt/include'),
               str(root/'test.cpp'), '-o', str(root/'test')]
    subprocess.run(command, check=True)
    subprocess.run([str(root/'test')], check=True)
