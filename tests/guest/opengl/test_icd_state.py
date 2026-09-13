#!/usr/bin/env python3
"""Compile actual fixed-vector state and ICD aliases; verify payloads and guards."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
with tempfile.TemporaryDirectory(prefix="dreamgpu-icd-state-") as temporary:
    root=Path(temporary)
    (root/"GL").mkdir()
    for name in ("fixed.cpp","query.cpp","internal.h","icd-state.inc"):
        shutil.copyfile(ROOT/"guest/opengl"/name,root/name)
    shutil.copyfile(HERE/"frontend-windows.h",root/"windows.h")
    shutil.copyfile(HERE/"frontend-gl.h",root/"GL/gl.h")
    source=(HERE/"fixed.cpp").read_text().split('int main(void) {')[0]
    source+=r'''
static GLenum StoreName;static GLint StoreValue;static unsigned Stores;
extern "C" void glPixelStorei(GLenum name,GLint value){StoreName=name;StoreValue=value;++Stores;}
#include "icd-state.inc"
int main(){
    GLint colors[]={(-2147483647-1),0,2147483647,1073741824};
    AliasLightiv(GL_LIGHT0,GL_AMBIENT,colors);
    GLfloat actual[4];memcpy(actual,Data,16);
    assert(Function==FEnum_glLightfv&&Bytes==16);
    assert(actual[0]==-1&&actual[1]>0&&actual[1]<1e-8f&&actual[2]==1);
    GLint position[]={1,-2,3,4};AliasLightiv(GL_LIGHT0,GL_POSITION,position);
    memcpy(actual,Data,16);assert(actual[0]==1&&actual[1]==-2&&actual[3]==4);
    AliasLighti(GL_LIGHT0,GL_SPOT_EXPONENT,32);memcpy(actual,Data,4);
    assert(Bytes==4&&actual[0]==32);
    unsigned saved=Calls;Error=0;
    AliasLighti(GL_LIGHT0,GL_POSITION,1);assert(Calls==saved&&Error==GL_INVALID_ENUM);
    AliasLightiv(GL_LIGHT0,0xdead,(GLint*)1);assert(Calls==saved&&Error==GL_INVALID_ENUM);
    AliasLightiv(GL_LIGHT0,GL_AMBIENT,nullptr);assert(Calls==saved&&Error==GL_INVALID_VALUE);
    Ready=FALSE;AliasLightiv(GL_LIGHT0,GL_AMBIENT,(GLint*)1);assert(Calls==saved);
    AliasMateriali(GL_FRONT,GL_SHININESS,64);assert(Calls==saved+1&&Function==FEnum_glMaterialfv);
    AliasMaterialiv(GL_BACK,GL_DIFFUSE,colors);memcpy(actual,Data,16);assert(actual[0]==-1&&actual[2]==1);
    AliasMaterialiv(GL_FRONT,GL_COLOR_INDEXES,position);memcpy(actual,Data,12);
    assert(Bytes==12&&actual[0]==1&&actual[1]==-2);
    Ready=TRUE;AliasTexGend(GL_S,GL_TEXTURE_GEN_MODE,0x2401);GLdouble doubles[4];memcpy(doubles,Data,8);
    assert(Function==FEnum_glTexGendv&&Bytes==8&&doubles[0]==0x2401);
    AliasTexGeniv(GL_T,GL_EYE_PLANE,colors);memcpy(doubles,Data,32);
    assert(Bytes==32&&doubles[0]==-2147483648.0&&doubles[1]==0&&doubles[2]==2147483647.0);
    saved=Calls;AliasTexGend(GL_S,GL_EYE_PLANE,1);assert(Calls==saved&&Error==GL_INVALID_ENUM);
    AliasTexGeniv(GL_S,0xdead,(GLint*)1);assert(Calls==saved&&Error==GL_INVALID_ENUM);
    AliasPixelStoref(GL_UNPACK_ROW_LENGTH,3.6f);assert(StoreName==GL_UNPACK_ROW_LENGTH&&StoreValue==4);
    AliasPixelStoref(GL_PACK_SKIP_ROWS,-0.4f);assert(StoreValue==0);
    AliasPixelStoref(GL_PACK_SKIP_ROWS,-1.6f);assert(StoreValue==-2);
    AliasPixelStoref(GL_PACK_SWAP_BYTES,0.001f);assert(StoreValue==1);
    AliasPixelStoref(GL_UNPACK_LSB_FIRST,-0.0f);assert(StoreValue==0);
    AliasPixelStoref(GL_UNPACK_ROW_LENGTH,2147483520.0f);assert(StoreValue==2147483520);
    saved=Stores;AliasPixelStoref(GL_PACK_ALIGNMENT,2147483648.0f);
    assert(Stores==saved&&Error==GL_INVALID_VALUE);
    float nan;unsigned bits=0x7fc00000;memcpy(&nan,&bits,4);
    AliasPixelStoref(GL_PACK_ROW_LENGTH,nan);assert(Stores==saved&&Error==GL_INVALID_VALUE);
    AliasPixelStoref(0xdead,nan);assert(Stores==saved&&Error==GL_INVALID_ENUM);
    Ready=FALSE;AliasPixelStoref(GL_PACK_ALIGNMENT,4);assert(Stores==saved);
    puts("PASS ICD seven state aliases; exact payloads, normalization, ranges and Begin guards");
}
'''
    (root/"test.cpp").write_text(source)
    subprocess.run([os.environ.get("CXX","c++"),"-std=c++23","-O2","-Wall","-Wextra","-Werror",
                    "-fno-exceptions","-fno-rtti","-fsanitize=address,undefined","-g","-I"+str(root),
                    "-I"+str(ROOT/"guest/include"),str(root/"test.cpp"),"-o",str(root/"test")],check=True)
    subprocess.run([str(root/"test")],check=True)
