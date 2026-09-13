#!/usr/bin/env python3
"""Failure boundaries of the actual guest image packer, without native GL mocks."""
from test_icd_images import run

if __name__ == '__main__':
    run(r'''
 const BYTE safe[64]={};
 // Reject the entire source layout before a FIRST record or stream ID exists.
 for(unsigned bad=0;bad<10;++bad){
  Reset(); const void* source=safe; GLsizei width=1,height=1;
  GLenum format=GL_RGBA,type=GL_UNSIGNED_BYTE;
  switch(bad){
   case 0:width=-1;break; case 1:height=-1;break;
   case 2:format=0xffffffff;break; case 3:type=0xffffffff;break;
   case 4:source=nullptr;break; case 5:unpack.Alignment=3;break;
   case 6:unpack.SkipRows=-1;break; case 7:unpack.SkipPixels=-1;break;
   case 8:unpack.RowLength=-1;break;
   case 9:type=GL_BITMAP;break;
  }
  AliasDrawPixels(width,height,format,type,source);
  assert(error&&records.empty()&&!attempts&&!identity);
 }
 Reset();AliasDrawPixels(4097,4096,GL_RGBA,GL_UNSIGNED_BYTE,safe);
 assert(error==GL_OUT_OF_MEMORY&&records.empty()&&!identity);
 Reset();unpack.SkipRows=1;unpack.RowLength=4;
 AliasDrawPixels(1,2,GL_RGBA,GL_UNSIGNED_BYTE,(void*)(~uintptr_t(0)-7));
 assert(error==GL_INVALID_VALUE&&records.empty()&&!identity);
 Reset();unpack.SkipPixels=7;
 AliasBitmap(10,1,0,0,0,0,(BYTE*)(~uintptr_t(0)-1));
 assert(error==GL_INVALID_VALUE&&records.empty()&&!identity);
 for(unsigned cap=0;cap<=16;++cap){
  Reset();capacity=cap;AliasBitmap(8,1,0,0,1,1,safe);
  assert(error==GL_OUT_OF_MEMORY&&records.empty()&&!attempts&&!identity);
 }
 Reset();ready=false;AliasDrawPixels(1,1,GL_RGBA,GL_UNSIGNED_BYTE,safe);
 assert(error==GL_INVALID_OPERATION&&records.empty()&&!identity);
 // Failed FIRST, intermediate and final record: no replay and no later commit.
 std::vector<BYTE> pixels(97,0x5a);
 for(unsigned failure=1;failure<=6;++failure){
  Reset();capacity=17;fail_at=failure;
  AliasDrawPixels(97,1,GL_RED,GL_UNSIGNED_BYTE,pixels.data());
  assert(attempts==failure&&records.size()==failure-1&&identity==1);
  for(auto& record:records)assert(!(record.args[6]&2));
 }
 // A bitmap FIRST prefix is never mistaken for image progress. One-byte chunks
 // still carry exactly one prefix, a single immutable ID and contiguous offsets.
 Reset();capacity=17;unpack={1,0,0,0,0,0};BYTE bitmap[]={0xa5,0x7f,0x5a,0xff};
 AliasBitmap(9,2,-0.0f,1.25f,-.125f,3.5f,bitmap);
 assert(records.size()==2&&records[0].args[5]==0&&records[1].args[5]==1);
 assert(records[0].payload.size()==17&&records[1].payload.size()==3);
 assert(records[0].payload[16]==0xa5&&records[1].payload==std::vector<BYTE>({0,0x5a,0x80}));
 uint32_t origin;memcpy(&origin,records[0].payload.data(),4);assert(origin==0x80000000u);
 // Non-byte skip + LSB input + padded rows crosses a source-byte boundary.
 Reset();unpack={4,17,1,3,0,1};BYTE rows[12]={};
 rows[4]=0x28;rows[5]=0x01;rows[8]=0xc0;rows[9]=0x03;
 AliasBitmap(9,2,0,0,0,0,rows);
 assert(records[0].payload[16]==0xa4&&records[0].payload[17]==0);
 assert(records[0].payload[18]==0x1e&&records[0].payload[19]==0);
 // A scalar may straddle transport chunks; swapping preserves raw float bits,
 // including NaN payloads and signed zero, instead of numeric conversion.
 Reset();capacity=17;unpack={1,0,0,0,1,0};
 BYTE words[20]={0x80,0,0,0, 0x7f,0xc1,0x23,0x45, 0x3f,0x80,0,0,
                 0xbf,0x80,0,0, 0x12,0x34,0x56,0x78};
 AliasDrawPixels(5,1,GL_RED,GL_FLOAT,words);
 std::vector<BYTE> joined;for(auto&r:records)joined.insert(joined.end(),r.payload.begin(),r.payload.end());
 assert(joined==std::vector<BYTE>({0,0,0,0x80,0x45,0x23,0xc1,0x7f,0,0,0x80,0x3f,
                                0,0,0x80,0xbf,0x78,0x56,0x34,0x12}));
 // No stream-ID reuse after exhaustion. The actual context counter is covered
 // separately by frontend tests; here ensure the packer never submits zero.
 Reset();identity=0xffffffff;AliasBitmap(0,0,0,0,1,1,nullptr);
 assert(error==GL_OUT_OF_MEMORY&&records.empty()&&!attempts);
 puts("PASS image preflight, chunk failures, no replay, bitmap skips, typed chunk boundaries");
''')
