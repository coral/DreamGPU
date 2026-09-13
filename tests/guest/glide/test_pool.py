#!/usr/bin/env python3
"""Exercise the actual patched TexDB with GPU calls replaced by pixel storage."""
from pathlib import Path
import subprocess,tempfile
import importlib.util
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2]
from source import patched_source
donor=ROOT/'vendor/qemu-xtra/openglide'
s=patched_source('TexDB.cpp').replace('#include "GlOgl.h"','')
h=patched_source('TexDB.h').replace('#include "sdk2_glide.h"','')
harness=r'''
#include <cassert>
#include <cstdlib>
static void GlideMsg(const char*,...) {}
#include <cstring>
#include <map>
#include <vector>
using FxU32=unsigned int;using GLuint=unsigned int;using GLint=int;using GLsizei=int;using GLenum=unsigned int;
struct GrTexInfo {int smallLod,largeLod,aspectRatio,format;void*data;};
#define GL_TEXTURE_2D 1
#define GL_TEXTURE_WRAP_S 2
#define GL_TEXTURE_WRAP_T 3
#define GL_TEXTURE_MIN_FILTER 4
#define GL_TEXTURE_MAG_FILTER 5
struct G {int w=0,h=0,format=0;std::vector<unsigned char>data;};
static std::map<unsigned,std::map<int,G>> gpu;static unsigned next=1,bound;
static unsigned creates,deletes,images,subs,params;
static void glGenTextures(int n,unsigned *out){while(n--){*out=next++;gpu[*out++];creates++;}}
static void glDeleteTextures(int n,const unsigned *in){while(n--){assert(gpu.erase(*in++)==1);deletes++;}}
static void glTexImage2D(unsigned,int level,int internal,int w,int h,int,unsigned,unsigned,const void*p){
 auto &g=gpu.at(bound)[level];g.w=w;g.h=h;g.format=internal;g.data.assign(w*h*4,0);if(p)memcpy(g.data.data(),p,g.data.size());images++;
}
static void glTexSubImage2D(unsigned,int level,int x,int y,int w,int h,unsigned,unsigned,const void*p){
 auto &g=gpu.at(bound).at(level);assert(!x&&!y&&g.w==w&&g.h==h);memcpy(g.data.data(),p,w*h*4);subs++;
}
static void glTexParameteri(unsigned,unsigned,int){params++;}
'''
tests=r'''
int main(){
 GrTexInfo info={0,0,0,1,nullptr};unsigned first,second;bool changed=false;
 std::vector<unsigned char> red(256*256*4,23),blue(red.size(),117);
 {
 TexDB db(32*1024*1024);
 db.Add(0,128*1024,&info,7,&first,nullptr);bound=first;
 db.Upload(first,0,4,256,256,1,1,red.data());assert(images==1);
 db.Parameters(first,1,2,3,4);assert(params==4);db.Parameters(first,1,2,3,4);assert(params==4);
 // Wrong palette hash does not invalidate; lookup distinguishes it.
 db.WipeRange(0,128*1024,8);assert(db.Find(0,&info,7,&second,nullptr,nullptr));
 assert(!db.Find(0,&info,8,&second,nullptr,nullptr));
 // Overlap beginning three sections after texture start still invalidates.
 db.WipeRange(100*1024,101*1024,0);assert(!db.Find(0,&info,7,&second,nullptr,nullptr));
 assert(!deletes);db.Add(0,128*1024,&info,8,&second,nullptr);assert(second==first);bound=second;
 db.Upload(second,0,4,256,256,1,1,blue.data());assert(images==1&&subs==1);assert(gpu[second][0].data==blue);
 db.Parameters(second,1,2,3,4);assert(params==4);db.Parameters(second,1,2,9,4);assert(params==5);
 // Internal-format and shape changes must redefine storage, not SubImage.
 db.Upload(second,0,3,256,256,1,1,red.data());assert(images==2);
 db.Upload(second,0,3,128,128,1,1,red.data());assert(images==3);
 db.Upload(second,1,3,64,64,1,1,red.data());assert(images==4);
 // Full-level null data is a redefinition; later bytes replace the full level.
 db.Upload(second,1,3,64,64,1,1,nullptr);assert(images==5);
 db.Upload(second,1,3,64,64,1,1,blue.data());assert(subs==2);
 // An out-of-band GLU mipmap definition invalidates tracked levels.
 db.ForgetStorage(second);db.Upload(second,1,3,64,64,1,1,blue.data());assert(images==6);
 // Palette extension changes still report changed, with donor hash matching.
 assert(db.Find(0,&info,9,&second,nullptr,&changed)&&changed);
 db.Clear();assert(gpu.empty());
 // More than 64 tiny compatible retirements cannot exceed pool count.
 for(unsigned i=0;i<80;i++){unsigned id;db.Add(i*128*1024,i*128*1024+16,&info,0,&id,nullptr);bound=id;db.Upload(id,0,4,2,2,1,1,red.data());}
 db.WipeRange(0,32*1024*1024,0);assert(gpu.size()==64);db.Clear();assert(gpu.empty());
 // Byte bound applies even below count bound: 40 base levels plus conservative generated chains =>24 retained.
 for(unsigned i=0;i<40;i++){unsigned id;db.Add(i*128*1024,i*128*1024+128*1024,&info,0,&id,nullptr);bound=id;db.Upload(id,0,4,256,256,1,1,red.data());}
 db.WipeRange(0,32*1024*1024,0);assert(gpu.size()==24);db.Clear();assert(gpu.empty());
 // Dual textures never enter the single-texture pool and both delete.
 unsigned pair;db.Add(0,8,&info,0,&first,&pair);bound=first;db.Upload(first,0,4,2,2,1,1,red.data());db.WipeRange(0,8,0);assert(gpu.empty());
 // Destruction releases both live and retained objects.
 db.Add(0,8,&info,0,&first,nullptr);bound=first;db.Upload(first,0,4,2,2,1,1,red.data());db.WipeRange(0,8,0);
 db.Add(128*1024,128*1024+8,&info,0,&first,nullptr);
 }

 assert(gpu.empty()&&creates==deletes);
 {
 TexDB db(32*1024*1024);
 std::vector<unsigned char> memory(32*1024*1024,0);
 GrTexInfo info={0,0,0,1,nullptr};unsigned a,b,found;
 unsigned stateA[4]={1,2,3,4},stateB[4]={1,2,9,4};
 std::vector<unsigned char> raw(16,11),other(16,22),pixelsA(16,31),pixelsB(16,47);
 db.Write(0,16,memory.data(),raw.data(),16);
 assert(!db.FindState(0,16,&info,stateA,sizeof(stateA),&found));
 db.Add(0,16,&info,7,&a,nullptr);bound=a;db.Upload(a,0,4,2,2,1,1,pixelsA.data());
 db.RememberState(stateA,sizeof(stateA));
 unsigned before=images+subs;
 for(int i=0;i<100;i++) {
  db.Write(0,16,memory.data(),raw.data(),16);
  assert(db.FindState(0,16,&info,stateA,sizeof(stateA),&found)&&found==a);
 }
 assert(images+subs==before&&gpu[a][0].data==pixelsA);
 // Same donor hash does not hide different conversion state. Reuse allocation,
 // then reconvert exact pixels; there is no speculative retired-content cache.
 assert(!db.FindState(0,16,&info,stateB,sizeof(stateB),&found));
 db.Add(0,16,&info,7,&b,nullptr);assert(b==a);bound=b;db.Upload(b,0,4,2,2,1,1,pixelsB.data());
 db.RememberState(stateB,sizeof(stateB));
 assert(!db.FindState(0,16,&info,stateA,sizeof(stateA),&found));
 db.Add(0,16,&info,7,&b,nullptr);assert(b==a);bound=b;db.Upload(b,0,4,2,2,1,1,pixelsA.data());
 db.RememberState(stateA,sizeof(stateA));assert(gpu[a][0].data==pixelsA);
 // Actual writes invalidate even though the compact key is unchanged.
 db.Write(0,16,memory.data(),other.data(),16);
 assert(!db.FindState(0,16,&info,stateA,sizeof(stateA),&found));
 db.Add(0,16,&info,7,&b,nullptr);assert(b==a);bound=b;db.Upload(b,0,4,2,2,1,1,pixelsB.data());
 db.RememberState(stateA,sizeof(stateA));
 db.Write(16,32,memory.data()+16,other.data(),16);
 assert(db.FindState(0,16,&info,stateA,sizeof(stateA),&found)&&found==a);
 db.Write(4,8,memory.data()+4,raw.data(),4);
 assert(!db.FindState(0,16,&info,stateA,sizeof(stateA),&found));
 db.Clear();assert(gpu.empty());
 // Repeated misses must not consume 64 object slots or the 8MiB storage pool:
 // reuse works equally for small textures and byte-cap-sized allocations.
 for(unsigned width : {2u,256u}) {
  std::vector<unsigned char> pixels(width*width*4,31);
  unsigned created=creates,deleted=deletes,img=images;
  for(unsigned i=0;i<200;i++) {
   stateA[0]=i;assert(!db.FindState(0,16,&info,stateA,sizeof(stateA),&found));
   db.Add(0,16,&info,7,&a,nullptr);bound=a;
   db.Upload(a,0,4,width,width,1,1,pixels.data());db.RememberState(stateA,sizeof(stateA));
   assert(gpu[a][0].data==pixels);
  }
  assert(creates==created+1&&deletes==deleted&&images==img+1);
  db.Clear();assert(gpu.empty());
 }
 // Exact active keys remain bounded. Failed key admission never destroys an
 // existing live GPU image or fabricates a state hit; retirement frees budget.
 std::vector<unsigned char> key(4096,3);
 for(unsigned i=0;i<2050;i++) {
  db.Add(i*16,i*16+16,&info,7,&a,nullptr);bound=a;
  db.Upload(a,0,4,2,2,1,1,pixelsA.data());db.RememberState(key.data(),key.size());
 }
 assert(gpu.size()==2050);
 assert(db.FindState(0,16,&info,key.data(),key.size(),&found));
 assert(!db.FindState(2049*16,2050*16,&info,key.data(),key.size(),&found));
 db.Clear();assert(gpu.empty());
 }

 assert(creates==deletes);
}
'''
with tempfile.TemporaryDirectory(prefix='dg-pool-') as folder:
 p=Path(folder);(p/'TexDB.h').write_text(h);(p/'test.cpp').write_text(harness+s+tests)
 subprocess.run(['clang++','-std=c++14','-O1','-g','-fsanitize=address,undefined','-I'+str(ROOT/'guest/glide'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS actual TexDB: overlap/palette invalidation, exact pixels, SubImage/format/miplevel/null-data contracts, parameter changes,64entry/8MiB bounds and complete cleanup (ASan/UBSan)')
