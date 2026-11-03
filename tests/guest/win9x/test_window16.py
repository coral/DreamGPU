#!/usr/bin/env python3
"""Actual display-source classifier: no tag read from short/foreign bitmaps.

16/16 (RGB565) and 32/32 tagged sources reach the VxD; mixed depths, a 16-bpp
primary without FIVE6FIVE and 1/8/15/24-bpp sources fail exactly as before."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[3]/"guest/win9x"
source=r'''
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define __far
#define TYPE_DIBENG 0x5250
#define VRAM 0x8000
#define BUSY 16
#define SELECTEDDIB 4
#define FIVE6FIVE 0x0040
typedef uint16_t WORD;typedef uint32_t DWORD;
typedef struct {WORD deType,deFlags,deBitsSelector,deWidth,deHeight,deBitsPixel,dePlanes;DWORD deBitsOffset;} DIBENGINE,*LPDIBENGINE;
typedef void *LPPDEVICE;
static DIBENGINE primary;static LPDIBENGINE lpDriverPDevice=&primary;
static DWORD tag[4];static int reads,calls,complete;
static DWORD *Read(DIBENGINE *source) {assert((source->deBitsPixel==16||source->deBitsPixel==32)&&source->deWidth>=16&&source->deHeight);++reads;return tag;}
#define DG9_WINDOW_WORDS(src) Read(src)
static DWORD seen[4];
WORD DgWindowBlt16(DWORD token,DWORD source,DWORD destination,DWORD extent)
{++calls;seen[0]=token;seen[1]=source;seen[2]=destination;seen[3]=extent;return complete;}
#include "dg-window16.c"
static WORD invoke(DIBENGINE *src)
{return DgWindowSource(&primary,src,40,50,10,20,30,40,0x00cc0020);}
int main(void)
{
 DIBENGINE src={TYPE_DIBENG,0,1,16,1,32,1,0};primary.deBitsPixel=32;
 src.deBitsPixel=1;assert(!invoke(&src)&&!reads);src.deBitsPixel=32;
 src.dePlanes=0;assert(!invoke(&src)&&!reads);src.dePlanes=1;
 src.deWidth=15;assert(!invoke(&src)&&!reads);src.deWidth=16;
 src.deBitsOffset=65521;assert(!invoke(&src)&&!reads);src.deBitsOffset=0;
 src.deFlags=VRAM;assert(!invoke(&src)&&!reads);src.deFlags=0;
 assert(!invoke(&src)&&reads==1&&!calls);
 tag[0]=DG9_BITMAP_MAGIC0;tag[1]=DG9_BITMAP_MAGIC1;
 assert(invoke(&src)==2&&!calls);tag[2]=42;tag[3]=1;assert(invoke(&src)==2&&!calls);tag[3]=0;
 complete=1;assert(invoke(&src)==1&&calls==1);assert(seen[0]==42&&seen[1]==(10u|(20u<<16))&&seen[2]==(40u|(50u<<16))&&seen[3]==(30u|(40u<<16)));
 complete=0;assert(invoke(&src)==2&&calls==2);
 src.deFlags=SELECTEDDIB;assert(invoke(&src)==2&&calls==2);src.deFlags=0;
 /* RGB565: a 16-pixel 16-bpp scanline still holds the whole tag. */
 primary.deBitsPixel=16;primary.deFlags=VRAM|FIVE6FIVE;src.deBitsPixel=16;
 reads=0;src.deWidth=15;assert(!invoke(&src)&&!reads);src.deWidth=16;
 complete=1;memset(seen,0,sizeof(seen));assert(invoke(&src)==1&&calls==3&&reads==1);
 assert(seen[0]==42&&seen[1]==(10u|(20u<<16))&&seen[2]==(40u|(50u<<16))&&seen[3]==(30u|(40u<<16)));
 complete=0;assert(invoke(&src)==2&&calls==4);complete=1;
 tag[3]=1;assert(invoke(&src)==2&&calls==4);tag[3]=0;
 tag[1]^=1;assert(!invoke(&src)&&calls==4);tag[1]^=1;
 src.deFlags=BUSY;assert(invoke(&src)==2&&calls==4);src.deFlags=0;
 primary.deFlags=VRAM|FIVE6FIVE|BUSY;assert(invoke(&src)==2&&calls==4);
 /* A 16-bpp primary that is not RGB565 is never described to the device. */
 primary.deFlags=VRAM;assert(invoke(&src)==2&&calls==4);primary.deFlags=VRAM|FIVE6FIVE;
 /* Never mixed: a stale tagged bitmap after a depth change fails closed. */
 primary.deBitsPixel=32;assert(invoke(&src)==2&&calls==4);
 primary.deBitsPixel=16;src.deBitsPixel=32;assert(invoke(&src)==2&&calls==4);
 primary.deBitsPixel=24;assert(invoke(&src)==2&&calls==4);
 primary.deBitsPixel=8;assert(invoke(&src)==2&&calls==4);
 /* 32/32 keeps working whether or not FIVE6FIVE happens to be set. */
 primary.deBitsPixel=32;primary.deFlags=0;assert(invoke(&src)==1&&calls==5);
 /* Unsupported source depths are never read, even if they would be large enough. */
 {static const WORD depths[]={1,4,8,15,24,64};unsigned i;
  for(i=0;i<sizeof(depths)/sizeof(depths[0]);++i){
   reads=0;src.deBitsPixel=depths[i];src.deWidth=64;primary.deBitsPixel=depths[i];
   primary.deFlags=VRAM|FIVE6FIVE;assert(!invoke(&src)&&!reads&&calls==5);}}
 puts("PASS actual Win98 bitmap classifier: bounded reads, magic isolation, scalar GDI rectangles, "
      "16/16 RGB565 and 32/32 acceptance, mixed/non-565/unsupported depths fail closed");return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='dg-window16-') as directory:
 p=Path(directory);(p/'test.c').write_text(source)
 subprocess.run(['clang','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(root),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
