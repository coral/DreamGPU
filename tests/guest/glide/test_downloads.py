#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual donor download methods: checked ranges and precise write admission."""
from pathlib import Path
import subprocess
import tempfile
from source import patched_source

source = patched_source('PGTexture.cpp')
start = source.index('void PGTexture::DownloadMipMap(')
end = source.index('void PGTexture::DownloadTable(', start)
body = source[start:end]
harness = r'''
#include <cassert>
#include <algorithm>
#include <cstring>
#include <vector>
using FxU32=unsigned;using FxU8=unsigned char;using GLuint=unsigned;
struct GrTexInfo {int smallLod,largeLod,aspectRatio,format;void *data;};
struct TexValues {int width,height,nPixels,lod;};
static struct {float w,h;} texAspects[1]={{1,1}};
static struct {int width,height,numPixels;} texInfo[1][1]={{{4,4,16}}};
static struct {int SClampMode,TClampMode,MinFilterMode,MagFilterMode;} OpenGL;
static struct {bool EnableMipMaps,BuildMipMaps;} InternalConfig;
#define GR_TEXFMT_BGRA_8888 99
#define GR_TEXFMT_16BIT 16
#define GL_TEXTURE_2D 1
#define GL_GENERATE_MIPMAP_SGIS 2
#define GL_BGRA 3
#define GL_UNSIGNED_BYTE 4
#define OGL_LOAD_CREATE_TEXTURE(a,b,c,d) ((void)texVals)
static void glBindTexture(unsigned,unsigned){}
static void glTexParameteri(unsigned,unsigned,int){}
static unsigned mipSize=16,totalSize=32;
static unsigned MipMapMemRequired(int,int,int){return mipSize;}
static unsigned TextureMemRequired(unsigned,GrTexInfo*){return totalSize;}
struct DB {
 unsigned first=0,end=0,bytes=0,writes=0;
 void Write(unsigned a,unsigned b,void*d,const void*s,unsigned n){assert(b-a==n);first=a;end=b;bytes=n;++writes;memcpy(d,s,n);}
 void WipeRange(unsigned,unsigned,unsigned){}
 void Add(unsigned,unsigned,GrTexInfo*,unsigned,unsigned*p,void*){*p=1;}
 void Parameters(unsigned,int,int,int,int){}
};
struct PGTexture { DB *m_db; unsigned m_tex_memory_size;unsigned char *m_memory;
 bool m_valid;unsigned m_startAddress,m_evenOdd;GrTexInfo m_info;float m_wAspect,m_hAspect;
 void Source(unsigned,unsigned,GrTexInfo*);
 void DownloadMipMap(unsigned,unsigned,GrTexInfo*);
 void DownloadMipMapPartial(unsigned,unsigned,GrTexInfo*,int,int);
};
''' + body + r'''
int main(){
 DB db;std::vector<unsigned char> mem(128,3),data(32,7);
 PGTexture p{&db,128,mem.data()};GrTexInfo info{0,0,0,1,data.data()};
 p.DownloadMipMap(32,0,&info);
 assert(db.first==48&&db.end==64&&db.bytes==16);
 assert(mem[47]==3&&mem[48]==7&&mem[63]==7&&mem[64]==3);
 std::fill(mem.begin(),mem.end(),3);
 p.DownloadMipMapPartial(32,0,&info,1,2);
 assert(db.first==52&&db.end==60&&db.bytes==8);
 assert(mem[51]==3&&mem[52]==7&&mem[59]==7&&mem[60]==3);
 unsigned writes=db.writes;info.data=(void*)1;
 p.DownloadMipMapPartial(32,0,&info,-1,0);
 p.DownloadMipMapPartial(32,0,&info,0,2147483647);
 p.DownloadMipMapPartial(32,0,&info,3,2);
 p.DownloadMipMap(0xfffffff0u,0,&info);
 p.DownloadMipMap(127,0,&info);
 totalSize=8;p.DownloadMipMap(0,0,&info);
 assert(db.writes==writes);
 totalSize=32;p.Source(0xfffffff0u,0,&info);assert(!p.m_valid);
 p.Source(96,0,&info);assert(p.m_valid);
 p.Source(97,0,&info);assert(!p.m_valid);
 totalSize=0;p.Source(0,0,&info);assert(!p.m_valid);
}
'''
with tempfile.TemporaryDirectory(prefix='dg-download-') as directory:
    p=Path(directory);(p/'test.cpp').write_text(harness)
    subprocess.run(['clang++','-std=c++23','-O1','-g','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('PASS actual full/partial Glide downloads: exact changed mip/row range, guards, overflow and rejected-pointer safety')
