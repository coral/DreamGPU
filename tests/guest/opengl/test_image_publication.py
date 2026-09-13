#!/usr/bin/env python3
"""Actual image-stream ID ownership and final front-buffer publication."""
import os
from pathlib import Path
import shutil
import re
import subprocess
import tempfile
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
with tempfile.TemporaryDirectory(prefix='dreamgpu-copy-publication-') as temporary:
    root=Path(temporary);(root/'GL').mkdir()
    for name in ('frontend.cpp','internal.h','packing.h','transport.h','scalar.inc','names.h','state.h','icd-images.inc'):
        shutil.copyfile(ROOT/'guest/opengl'/name,root/name)
    shutil.copyfile(HERE/'frontend.cpp',root/'frontend-harness.cpp')
    shutil.copyfile(HERE/'frontend-windows.h',root/'windows.h')
    shutil.copyfile(HERE/'frontend-gl.h',root/'GL/gl.h')
    api=(ROOT/'crates/dreamgpu-host/src/gl_api.rs').read_text()
    constants=dict(re.findall(r'pub const (GL_\w+): [^=]+ = (\d+);',api))
    h=(root/'GL/gl.h').read_text()
    required=set(re.findall(r'\bGL_[A-Z0-9_]+\b',(ROOT/'guest/opengl/icd-images.inc').read_text()))
    h+='\n'+'\n'.join(f'#define {name} {constants[name]}' for name in sorted(required) if '#define '+name+' ' not in h)
    (root/'GL/gl.h').write_text(h)
    (root/'test.cpp').write_text(r'''
#define main existing_frontend_suite_not_run
#include "frontend-harness.cpp"
#undef main
using GLvoid=void;
#include "icd-images.inc"
int main(){
 Reset();BindCapabilities=DG_WINDOW_CAP_FRONT_ONLY;HGLRC context=Create();auto*c=Lookup(context);
 assert(JglNextImageId()==1&&JglNextImageId()==2);c->ImageId=0xffffffff;
 ULONG beforeUsed=c->Used;assert(!JglNextImageId()&&c->Used==beforeUsed&&c->Error==GL_OUT_OF_MEMORY);c->ImageId=2;c->Error=0;
 glDrawBuffer(GL_FRONT);ULONG args[]={1,1,0x1908,0x1401,4,0,1,3};BYTE data[4]={1,2,3,4};
 assert(JglData(FEnum_glDrawPixels,args,8,data,2));assert(!c->FrontDirty);
 args[5]=2;args[6]=2;assert(JglData(FEnum_glDrawPixels,args,8,data+2,2));assert(c->FrontDirty);
 ULONG before=Presents;glFlush();assert(Presents==before+1);glFlush();assert(Presents==before+1);
 args[2]=GL_DEPTH_COMPONENT;args[5]=0;args[6]=3;args[7]=4;
 assert(JglData(FEnum_glDrawPixels,args,8,data,4));assert(!c->FrontDirty);
 args[2]=GL_STENCIL_INDEX;args[7]=5;assert(JglData(FEnum_glDrawPixels,args,8,data,4));assert(!c->FrontDirty);
 args[0]=args[1]=args[4]=0;args[2]=0x1900;args[3]=0x1a00;args[7]=6;ULONG moves[4]={};
 assert(JglData(FEnum_glBitmap,args,8,moves,16));assert(c->FrontDirty);glFlush();
 glDrawBuffer(GL_BACK);args[0]=args[1]=1;args[2]=0x1908;args[3]=0x1401;args[4]=4;args[7]=7;
 assert(JglData(FEnum_glDrawPixels,args,8,data,4));assert(!c->FrontDirty);
 glBegin(GL_TRIANGLES);beforeUsed=c->Used;assert(!JglNextImageId()&&c->Used==beforeUsed);
 assert(!JglData(FEnum_glDrawPixels,args,8,data,4)&&c->Used==beforeUsed);glEnd();
 assert(wglMakeCurrent(nullptr,nullptr));assert(wglDeleteContext(context));Reset();assert(!Allocations);
 Reset();context=Create();c=Lookup(context);static BYTE image[200000]={};
 FailOperation=DG_GL_DATA_CALL;FailStatus=DG_ESCAPE_TIMEOUT;
 AliasDrawPixels(250,200,GL_RGBA,GL_UNSIGNED_BYTE,image);
 assert(c->Failed&&c->Uncertain&&!c->FrontDirty&&!c->Used&&c->ImageId==1);
 ULONG calls=Calls;AliasDrawPixels(250,200,GL_RGBA,GL_UNSIGNED_BYTE,image);
 AliasBitmap(0,0,0,0,1,1,nullptr);assert(Calls==calls&&c->ImageId==1);
 assert(wglMakeCurrent(nullptr,nullptr));assert(!wglDeleteContext(context)&&Calls==calls);
 Reset();assert(!Allocations);
 puts("PASS image actual frontend: monotonic/exhausted IDs, final-only color publication, depth/stencil/back deferral, Begin guards");
}
''')
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++23','-O1','-Wall','-Wextra','-Werror',
                    '-fno-exceptions','-fno-rtti','-fsanitize=address,undefined','-g',
                    '-I'+str(root),'-I'+str(ROOT/'guest/include'),'-I'+str(ROOT/'guest/nt/include'),
                    str(root/'test.cpp'),'-o',str(root/'test')],check=True)
    subprocess.run([str(root/'test')],check=True)
