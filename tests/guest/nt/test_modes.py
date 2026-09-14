#!/usr/bin/env python3
"""Exercise actual miniport mode enumeration/query/set/map functions with bounded MMIO seams."""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[3]
source=(ROOT/'guest/nt/miniport/bochsmp.cpp').read_text()
header=(ROOT/'guest/nt/miniport/bochsmp.h').read_text()
def function(name):
    location=source.index(name+'(')
    start=source.rfind('static ',0,location)
    body=source.index('{',location)
    depth=1;end=body+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
table=source[source.index('static const BOCHS_SIZE'):source.index('};',source.index('static const BOCHS_SIZE'))+2]
mode=header[header.rfind('typedef struct {',0,header.index('USHORT XResolution')):header.index('} BOCHS_SIZE, *PBOCHS_SIZE;')+len('} BOCHS_SIZE, *PBOCHS_SIZE;')]
program=r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include "gpu.h"
#define _In_
#define _Out_
#define _Inout_
#define TRUE 1
#define FALSE 0
#define VideoDebugPrint(...) ((void)0)
#define ARRAYSIZE(a) (sizeof(a)/sizeof((a)[0]))
#define NO_ERROR 0
#define ERROR_INVALID_PARAMETER 87
#define VIDEO_MEMORY_SPACE_MEMORY 0
#define VIDEO_MODE_GRAPHICS 1
#define VIDEO_MODE_COLOR 2
#define VIDEO_MODE_NO_OFF_SCREEN 4
#define VBE_DISPI_INDEX_ENABLE 4
#define VBE_DISPI_INDEX_XRES 1
#define VBE_DISPI_INDEX_YRES 2
#define VBE_DISPI_INDEX_BPP 3
#define VBE_DISPI_DISABLED 0
#define VBE_DISPI_ENABLED 1
#define VBE_DISPI_LFB_ENABLED 0x40
using ULONG=uint32_t;using USHORT=uint16_t;using UCHAR=uint8_t;using ULONGLONG=uint64_t;
using VOID=void;using BOOLEAN=int;using VP_STATUS=ULONG;using PULONG=ULONG*;using PUSHORT=USHORT*;
struct PHYSICAL_ADDRESS { uint64_t QuadPart; };
struct Range { UCHAR *Mapped;PHYSICAL_ADDRESS RangeStart;ULONG RangeLength;bool RangeInIoSpace; };
''' + mode + r'''
struct BOCHS_DEVICE_EXTENSION { PBOCHS_SIZE AvailableModeInfo;ULONG AvailableModeCount;USHORT CurrentMode;Range FrameBuffer,IoPorts;ULONG MaxXResolution,MaxYResolution,VramSize64K;void *Transport;};
using PBOCHS_DEVICE_EXTENSION=BOCHS_DEVICE_EXTENSION*;
struct VIDEO_MODE_INFORMATION { ULONG Length,ModeIndex,VisScreenWidth,VisScreenHeight,ScreenStride,NumberOfPlanes,BitsPerPlane,Frequency,XMillimeter,YMillimeter,NumberRedBits,NumberGreenBits,NumberBlueBits,RedMask,GreenMask,BlueMask,AttributeFlags,VideoMemoryBitmapWidth,VideoMemoryBitmapHeight;};
using PVIDEO_MODE_INFORMATION=VIDEO_MODE_INFORMATION*;
struct VIDEO_MODE { ULONG RequestedMode; };using PVIDEO_MODE=VIDEO_MODE*;
struct STATUS_BLOCK {ULONG Status,Information;};using PSTATUS_BLOCK=STATUS_BLOCK*;
struct VIDEO_MEMORY {void *RequestedVirtualAddress;};using PVIDEO_MEMORY=VIDEO_MEMORY*;
struct VIDEO_MEMORY_INFORMATION {void *VideoRamBase;ULONG VideoRamLength;void *FrameBufferBase;ULONG FrameBufferLength;};using PVIDEO_MEMORY_INFORMATION=VIDEO_MEMORY_INFORMATION*;
static USHORT dispi[16];static ULONG hz,register_writes;
static ULONG VideoPortReadRegisterUlong(PULONG p){return *p;}
static USHORT VideoPortReadRegisterUshort(PUSHORT){return 0;}
static void VideoPortWriteRegisterUshort(PUSHORT,USHORT){++register_writes;}
static void BochsWriteDispI(PBOCHS_DEVICE_EXTENSION,ULONG i,USHORT v){dispi[i]=v;}
static BOOLEAN BochsWriteDispIAndCheck(PBOCHS_DEVICE_EXTENSION d,ULONG i,USHORT v){BochsWriteDispI(d,i,v);return TRUE;}
static bool DgTransportSetRate(void*,ULONG rate){hz=rate;return true;}
static ULONG VideoPortMapMemory(PBOCHS_DEVICE_EXTENSION,PHYSICAL_ADDRESS,ULONG*,ULONG*,void **base){*base=(void*)0x1000;return 0;}
''' + table + '\n' + '\n'.join(function(n) for n in ['BochsInitializeSuitableModeInfo','BochsGetModeInfo','BochsSetCurrentMode','BochsMapVideoMemory']) + r'''
int main(){
 ULONG registers[DG_MMIO_SIZE/4]={};registers[DG_REG_CAPS/4]=DG_CAP_DISPLAY_TIMING;registers[DG_TIMING_REG_VERSION/4]=DG_TIMING_VERSION;
 constexpr ULONG count=ARRAYSIZE(BochsAvailableResolutions)*5*2;
 BOCHS_SIZE modes[count+1]={};modes[count].XResolution=0xbeef;
 BOCHS_DEVICE_EXTENSION d={};d.AvailableModeInfo=modes;d.MaxXResolution=d.MaxYResolution=4096;d.VramSize64K=4096;d.IoPorts.Mapped=(UCHAR*)registers;
 assert(BochsInitializeSuitableModeInfo(&d,count));assert(d.AvailableModeCount==count&&modes[count].XResolution==0xbeef);
 assert(modes[0].BitsPerPixel==32&&modes[0].Frequency==60);
 for(ULONG i=0;i<count;i++){
  const auto &m=modes[i];assert(m.BitsPerPixel==(i<count/2?32:16));
  VIDEO_MODE_INFORMATION info={};BochsGetModeInfo(&modes[i],&info,i);
  assert(info.BitsPerPlane==m.BitsPerPixel&&info.ScreenStride==m.XResolution*(m.BitsPerPixel/8));
  assert(info.RedMask==(m.BitsPerPixel==16?0xf800:0xff0000));assert(info.GreenMask==(m.BitsPerPixel==16?0x7e0:0xff00));assert(info.BlueMask==(m.BitsPerPixel==16?0x1f:0xff));
  assert(info.NumberRedBits==(m.BitsPerPixel==16?5:8)&&info.NumberGreenBits==(m.BitsPerPixel==16?6:8)&&info.NumberBlueBits==(m.BitsPerPixel==16?5:8));
  VIDEO_MODE request={i};STATUS_BLOCK status={};assert(BochsSetCurrentMode(&d,&request,&status));assert(dispi[VBE_DISPI_INDEX_BPP]==m.BitsPerPixel&&hz==m.Frequency&&d.CurrentMode==i);
  VIDEO_MEMORY memory={};VIDEO_MEMORY_INFORMATION mapped={};assert(BochsMapVideoMemory(&d,&memory,&mapped,&status));assert(mapped.FrameBufferLength==m.XResolution*m.YResolution*(m.BitsPerPixel/8));
 }
 VIDEO_MODE invalid={count};STATUS_BLOCK status={};auto writes=register_writes;assert(!BochsSetCurrentMode(&d,&invalid,&status)&&status.Status==87&&writes==register_writes);
 d.VramSize64K=16;assert(BochsInitializeSuitableModeInfo(&d,count));for(ULONG i=0;i<d.AvailableModeCount;i++)assert(uint64_t(modes[i].XResolution)*modes[i].YResolution*(modes[i].BitsPerPixel/8)<=1024*1024);
 d.VramSize64K=4096;registers[DG_REG_CAPS/4]=0;assert(BochsInitializeSuitableModeInfo(&d,count));assert(d.AvailableModeCount==ARRAYSIZE(BochsAvailableResolutions)*2);for(ULONG i=0;i<d.AvailableModeCount;i++)assert(modes[i].Frequency==60);
}
'''
with tempfile.TemporaryDirectory(prefix='dg-nt-modes-') as temp:
    p=Path(temp);(p/'test.cpp').write_text(program)
    subprocess.run(shlex.split(os.environ.get('CXX','c++'))+['-std=c++23','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(ROOT/'guest/include'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
print('Actual NT modes: RGB565/32-bit masks, stride, aperture bounds, five refresh rates, hardware BPP and mapped extent PASS')
