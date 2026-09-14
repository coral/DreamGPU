#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual maintained upload switch: packed data bypasses guest conversion."""
from pathlib import Path
import subprocess
import tempfile
from source import patched_source
s=patched_source('PGTexture.cpp')
start=s.index('        case GR_TEXFMT_RGB_565:',s.index('switch ( m_info.format )'))
end=s.index('        case GR_TEXFMT_P_8:',start)
body=s[start:end]
assert 'Convert565to5551' not in body and 'Convert4444to4444special' not in body
h=r'''
#include <cassert>
using FxU16=unsigned short;
enum {GR_TEXFMT_RGB_565, GR_TEXFMT_ARGB_4444, GR_TEXFMT_ARGB_1555};
enum {GL_RGB=0x1907,GL_RGBA=0x1908,GL_BGRA_EXT=0x80e1,GL_UNSIGNED_BYTE=0x1401,
GL_UNSIGNED_SHORT_5_6_5=0x8363,GL_UNSIGNED_SHORT_4_4_4_4_REV=0x8365,GL_UNSIGNED_SHORT_1_5_5_5_REV=0x8366};
static unsigned channels,format,type,converted;static const void*pointer;
static void Convert565Kto8888(FxU16*,unsigned,unsigned*,unsigned n){converted+=n;}
#define OGL_LOAD_CREATE_TEXTURE(c,f,t,p) do{channels=c;format=f;type=t;pointer=p;}while(0)
static void upload(unsigned f,bool m_chromakey_mode,const void*data){
 unsigned temporary[9],*m_tex_temp=temporary,m_chromakey_value_565=0;
 struct {unsigned nPixels;} texVals={9};
 switch(f){
''' + body + r'''
 }
 if(m_chromakey_mode)assert(pointer==temporary);
}
int main(){
 FxU16 original[9]={0,1,0x7e0,0xffff,0xfc00,0x83e0,0x801f,0xf0f0,0xff00};
 for(unsigned f=0;f<3;++f){upload(f,false,original);assert(pointer==original && !converted);
 assert(channels==(f?4U:3U));assert(format==(f?GL_BGRA_EXT:GL_RGB));
 assert(type==(f==0?0x8363U:f==1?0x8365U:0x8366U));}
 upload(GR_TEXFMT_RGB_565,true,original);assert(converted==9&&format==GL_RGBA&&type==GL_UNSIGNED_BYTE);
 assert(original[2]==0x7e0 && original[8]==0xff00);
}
'''
with tempfile.TemporaryDirectory(prefix='dg-packed-glide-') as t:
 p=Path(t);(p/'test.cpp').write_text(h)
 subprocess.run(['c++','-std=c++23','-O2','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS actual Glide packed source: RGB565/ARGB4444/ARGB1555 retain words, chromakey expansion retained')
