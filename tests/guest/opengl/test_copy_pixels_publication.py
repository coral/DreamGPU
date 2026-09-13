#!/usr/bin/env python3
"""Actual frontend front-buffer publication after a CopyPixels scalar command."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
with tempfile.TemporaryDirectory(prefix='dreamgpu-copy-publication-') as temporary:
    root=Path(temporary);(root/'GL').mkdir()
    for name in ('frontend.cpp','internal.h','packing.h','transport.h','scalar.inc','names.h','state.h'):
        shutil.copyfile(ROOT/'guest/opengl'/name,root/name)
    shutil.copyfile(HERE/'frontend.cpp',root/'frontend-harness.cpp')
    shutil.copyfile(HERE/'frontend-windows.h',root/'windows.h')
    shutil.copyfile(HERE/'frontend-gl.h',root/'GL/gl.h')
    (root/'test.cpp').write_text(r'''
#define main existing_frontend_suite_not_run
#include "frontend-harness.cpp"
#undef main
int main(){
 Reset();BindCapabilities=DG_WINDOW_CAP_FRONT_ONLY;HGLRC context=Create();auto*c=Lookup(context);
 ULONG args[]={0,0,8,8,0x1800};ULONG before=Presents;
 glDrawBuffer(GL_FRONT);JglScalarVector(FEnum_glCopyPixels,5,args);
 assert(c->FrontDirty);glFlush();assert(Presents==before+1&&PresentFlags==DG_WINDOW_PRESENT_FRONT_ONLY);
 glFlush();assert(Presents==before+1);
 args[4]=0x1801;JglScalarVector(FEnum_glCopyPixels,5,args);assert(!c->FrontDirty);glFlush();assert(Presents==before+1);
 args[4]=0x1802;JglScalarVector(FEnum_glCopyPixels,5,args);assert(!c->FrontDirty);glFlush();assert(Presents==before+1);
 args[4]=GL_COLOR;
 glDrawBuffer(GL_BACK);JglScalarVector(FEnum_glCopyPixels,5,args);
 assert(!c->FrontDirty);glFlush();assert(Presents==before+1);
 assert(wglSwapBuffers((HDC)1));assert(Presents==before+2&&PresentFlags==0);
 glDrawBuffer(GL_FRONT);glBegin(GL_TRIANGLES);ULONG used=c->Used;
 JglScalarVector(FEnum_glCopyPixels,5,args);assert(c->Error==GL_INVALID_OPERATION&&c->Used==used);
 glEnd();assert(wglMakeCurrent(nullptr,nullptr));assert(wglDeleteContext(context));Reset();assert(!Allocations);
 puts("PASS CopyPixels actual frontend: front publication once; back deferred; Begin rejected");
}
''')
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++23','-O1','-Wall','-Wextra','-Werror',
                    '-fno-exceptions','-fno-rtti','-fsanitize=address,undefined','-g',
                    '-I'+str(root),'-I'+str(ROOT/'guest/include'),'-I'+str(ROOT/'guest/nt/include'),
                    str(root/'test.cpp'),'-o',str(root/'test')],check=True)
    subprocess.run([str(root/'test')],check=True)
