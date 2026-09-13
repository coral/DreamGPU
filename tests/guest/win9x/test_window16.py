#!/usr/bin/env python3
"""Actual display-source classifier: no tag read from short/foreign bitmaps."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[3]/"guest/win9x"
source=r'''
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#define __far
#define TYPE_DIBENG 0x5250
#define VRAM 0x8000
#define BUSY 16
#define SELECTEDDIB 4
typedef uint16_t WORD;typedef uint32_t DWORD;
typedef struct {WORD deType,deFlags,deBitsSelector,deWidth,deHeight,deBitsPixel,dePlanes;DWORD deBitsOffset;} DIBENGINE,*LPDIBENGINE;
typedef void *LPPDEVICE;
static DIBENGINE primary;static LPDIBENGINE lpDriverPDevice=&primary;
static DWORD tag[4];static int reads,calls,complete;
static DWORD *Read(DIBENGINE *source) {assert(source->deBitsPixel==32&&source->deWidth>=16&&source->deHeight);++reads;return tag;}
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
 src.deFlags=SELECTEDDIB;assert(invoke(&src)==2&&calls==2);
 puts("PASS actual Win98 bitmap classifier: bounded reads, magic isolation, scalar GDI rectangles, fail closed");return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='dg-window16-') as directory:
 p=Path(directory);(p/'test.c').write_text(source)
 subprocess.run(['clang','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(root),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
