#!/usr/bin/env python3
"""Run patched donor array setup and second-pass draw with real packed layouts."""
import importlib.util
from pathlib import Path
import subprocess,tempfile
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2]
from source import patched_source
donor=ROOT/'vendor/qemu-xtra/openglide'
s=patched_source('GLRender.cpp')
a=s.index('void RenderUpdateArrays(');z=s.index('\n}\n',a)+3;setup=s[a:z]
a=s.index('            glColorPointer( 4, GL_FLOAT, 0, OGLRender.TColor2 );');z=s.index('\n        }',a);second=s[a:z]
header=(donor/'GLRender.h').read_text();layouts=header[header.index('struct TColorStruct'):header.index('struct RenderStruct')]
config=patched_source('GLExtensions.cpp');assert 'InternalConfig.EXT_vertex_array = true;' in config
h=r'''
#include <cassert>
#include <cstring>
using GLfloat=float;
#define GL_FLOAT 0x1406
#define GL_TRIANGLES 4
#define GL_TEXTURE0_ARB 0x84c0
#define GL_TEXTURE1_ARB 0x84c1
'''+layouts+r'''
static struct {TColorStruct *TColor,*TColor2;TTextureStruct *TTexture;TVertexStruct *TVertex;TFogStruct *TFog;int NumberOfTriangles;} OGLRender;
static struct {bool ARB_multitexture,EXT_secondary_color,EXT_fog_coord;} InternalConfig={false,false,false};
static const float *v,*c,*t;static float expected=.7f;static int draws;
static void glVertexPointer(int n,int type,int stride,const void *p){assert(n==3&&type==GL_FLOAT&&stride==16);v=(const float*)p;}
static void glColorPointer(int n,int type,int stride,const void *p){assert(n==4&&type==GL_FLOAT&&stride==0);c=(const float*)p;}
static void glTexCoordPointer(int n,int type,int stride,const void *p){assert(n==4&&type==GL_FLOAT&&stride==0);t=(const float*)p;}
static void (*p_glSecondaryColorPointerEXT)(int,int,int,const void*)=nullptr;
static void (*p_glFogCoordPointerEXT)(int,int,const void*)=nullptr;
static void p_glClientActiveTexture(int){assert(false);}
static void glDrawArrays(int mode,int start,int count){
 assert(mode==GL_TRIANGLES&&start==0&&count==6);
 for(int i=0;i<count;i++){assert(v[i*4]==.2f);assert(t[i*4]==.4f);for(int k=0;k<4;k++)assert(c[i*4+k]==expected);}draws++;
}
'''+setup+r'''
int main(){
 TColorStruct colors[2],secondColors[2];TVertexStruct vertices[2];TTextureStruct tex[2];
 float row[12];for(auto &x:row)x=.3f;for(auto &x:colors)memcpy(&x,row,sizeof(x));
 for(auto &x:row)x=.7f;for(auto &x:secondColors)memcpy(&x,row,sizeof(x));
 for(auto &x:row)x=.2f;for(auto &x:vertices)memcpy(&x,row,sizeof(x));
 for(auto &x:row)x=.4f;for(auto &x:tex)memcpy(&x,row,sizeof(x));
 OGLRender={colors,secondColors,tex,vertices,nullptr,2};RenderUpdateArrays();
 assert(v==(float*)vertices&&c==(float*)colors&&t==(float*)tex);
 expected=.3f;glDrawArrays(GL_TRIANGLES,0,6);expected=.7f;
'''+second+r'''
 assert(draws==2&&c==(float*)colors); // Second pass restores primary colors.
}
'''
with tempfile.TemporaryDirectory(prefix='dg-arrays-') as folder:
 p=Path(folder);(p/'test.cpp').write_text(h)
 subprocess.run(['clang++','-std=c++14','-O1','-g','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS actual Glide core arrays: six packed vertices/colors/texcoords, second-pass allocation pointers, absent extension callbacks (ASan/UBSan)')
