#!/usr/bin/env python3
"""Exercise actual patched donor mipmap decision and upload macro under ASan."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
from source import patched_source
donor=ROOT/'vendor/qemu-xtra/openglide'
config=patched_source('GLExtensions.cpp')
line=next(x for x in config.splitlines() if 'InternalConfig.BuildMipMaps =' in x)
texture=patched_source('PGTexture.cpp')
macro=texture[texture.index('#define OGL_LOAD_CREATE_TEXTURE('):texture.index('\n\n\nvoid genPaletteMipmaps')]
harness=r'''
#include <cassert>
#include <cstring>
#define GL_TEXTURE_2D 0xde1
static struct {bool EnableMipMaps,BuildMipMaps;} InternalConfig;
static struct {int lod,width,height;} texVals={0,4,2};
static int images,mipmaps;
static unsigned char pixels[32];
static void glTexImage2D(int target,int level,int internal,int w,int h,int border,int format,int type,const void *data){
 assert(target==GL_TEXTURE_2D&&level==0&&internal==4&&w==4&&h==2&&border==0&&format==123&&type==456&&data==pixels);++images;
}
static void gluBuild2DMipmaps(int target,int internal,int w,int h,int format,int type,const void *data){
 assert(target==GL_TEXTURE_2D&&internal==4&&w==4&&h==2&&format==123&&type==456&&data==pixels);++mipmaps;
}
static int forgets;
static unsigned texNum=42;
static struct Storage {
 void ForgetStorage(unsigned name){assert(name==texNum);++forgets;}
 void Upload(unsigned name,int level,int internal,int w,int h,int format,int type,const void *data){
  assert(name==texNum);glTexImage2D(GL_TEXTURE_2D,level,internal,w,h,0,format,type,data);
 }
} storage;
static Storage *m_db=&storage;
'''+macro+r'''
int main(){
 for(int enabled=0;enabled<2;enabled++)for(int supported=0;supported<2;supported++){
   InternalConfig.EnableMipMaps=enabled;
   InternalConfig.BuildMipMaps=enabled&&supported;
'''+line+r'''
   images=mipmaps=forgets=0;OGL_LOAD_CREATE_TEXTURE(4,123,456,pixels);
   assert(mipmaps==(enabled&&!supported));
   assert(forgets==mipmaps);
   assert(images==!(enabled&&!supported));
   assert(images+mipmaps==1); // SGI emits level zero itself; never upload it twice.
 }
}
'''
with tempfile.TemporaryDirectory(prefix='dg-mipmap-') as folder:
    p=Path(folder);(p/'test.cpp').write_text(harness)
    subprocess.run(['clang++','-O1','-g','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('PASS actual Glide mipmap decision and texture upload: disabled/enabled x SGIS absent/present; no duplicate base upload')
