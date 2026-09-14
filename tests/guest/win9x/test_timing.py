#!/usr/bin/env python3
"""Actual Win98 fixed-buffer timing control with pinned-buffer and IRQ seams."""
import os
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[3]
s = (ROOT/'guest/win9x/dg-timing-vxd.c').read_text()
# Watcom IRQ assembly is checked by the real target compiler. Exercise its C
# control transaction here with a deterministic one-shot interrupt/watchdog.
start = s.index('static BOOL Dg9TimingSupported(')
with tempfile.TemporaryDirectory(prefix='dg9-timing-') as tmp:
    p = Path(tmp)
    (p/'control.inc').write_text(s[start:])
    (p/'test.c').write_text(r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
typedef uint32_t DWORD; typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define DG_TIMING_U32 DWORD
#include "display-timing.h"
typedef struct { DWORD Alias,Pages; void *Address; } DG9_USER_LOCK;
struct DIOCParams { DWORD dwIoControlCode,lpOverlapped,cbInBuffer,cbOutBuffer,lpInBuffer,lpOutBuffer,lpcbBytesReturned; };
static unsigned locks, unlocks, waits, commands, mode;
static unsigned char input[16], output[32], count[4];
static DWORD regs[0x2000/4], dg_timing_wait, dg_timing_pending, dg_timing_sequence;
static int dg_memory_services;
static int DreamGpuLockUser(const void *svc,void *context,DWORD address,DWORD bytes,int write,DG9_USER_LOCK *lock) {
 (void)svc;(void)context;(void)write;
 if(address==1&&bytes==16)lock->Address=input;
 else if(address==2&&bytes==32)lock->Address=output;
 else if(address==3&&bytes==4)lock->Address=count;
 else return 0;
 lock->Alias=++locks;return 1;
}
static void DreamGpuUnlockUser(const void *svc,void *context,DG9_USER_LOCK *lock) {
 (void)svc;(void)context;if(lock->Alias){++unlocks;lock->Alias=0;}
}
static DWORD *DgVxdRegisters(void){return regs;}
static DWORD Dg9GlRead(void *unused,DWORD r){(void)unused;return regs[r/4];}
static void Dg9GlWrite(DWORD r,DWORD v){regs[r/4]=v;if(r==DG_TIMING_REG_COMMAND){++commands;if(v)regs[DG_TIMING_REG_STATUS/4]=DG_TIMING_PENDING;}}
static void Dg9TimingSignal(void){dg_timing_pending=0;}
static void Dg9TimingTimeout(void){}
static int Dg9GlCritical(void){return mode==3;}
static DWORD Dg9GlDisableInterrupts(void){return 0;}
static void Dg9GlRestoreInterrupts(DWORD flags){(void)flags;}
static DWORD Create_Semaphore(DWORD n){assert(!n);return 42;}
static void Destroy_Semaphore(DWORD sem){assert(sem==42);}
static DWORD Set_Async_Time_Out(DWORD ms,DWORD flags,void(*fn)(void)){assert(ms==250&&!flags&&fn);return 9;}
static void Cancel_Time_Out(DWORD timer){assert(timer==9);}
static void Wait_Semaphore(DWORD sem,DWORD flags){
 assert(sem==42&&!flags&&dg_timing_pending);++waits;
 if(mode!=1){regs[DG_TIMING_REG_COMPLETED/4]=dg_timing_sequence;regs[DG_TIMING_REG_STATUS/4]=mode==2?DG_TIMING_CANCELLED:DG_TIMING_DONE;}
 Dg9TimingSignal();
}
#include "control.inc"
static void reset(unsigned op) {
 DG_TIMING_REQUEST q={1,op,0,0};memcpy(input,&q,16);memset(output,0xa5,32);
 memset(regs,0,sizeof(regs));regs[DG_REG_CAPS/4]=DG_CAP_DISPLAY_TIMING;
 regs[DG_TIMING_REG_VERSION/4]=1;regs[DG_TIMING_REG_PHASE/4]=120<<16;
 regs[DG_TIMING_REG_HEIGHT/4]=768;locks=unlocks=waits=commands=mode=0;
}
int main(void){
 struct DIOCParams p={DG_TIMING_ESCAPE,0,16,32,1,2,3};DWORD result;
 for(unsigned op=0;op<3;++op){reset(op);assert(Dg9TimingControl(&p,&result)&&!result);DG_TIMING_REPLY r;memcpy(&r,output,32);assert(r.Version==1&&!r.Status&&r.RateHz==120);assert(waits==(op!=0)&&locks==unlocks&&!dg_timing_wait);}
 for(unsigned failure=1;failure<=2;++failure){reset(1);mode=failure;assert(Dg9TimingControl(&p,&result)&&!result);DG_TIMING_REPLY r;memcpy(&r,output,32);assert(r.Status==(failure==1?DG_TIMING_REPLY_TIMEOUT:DG_TIMING_REPLY_CANCELLED));assert(waits==1&&locks==unlocks&&!dg_timing_wait);}
 reset(1);p.cbOutBuffer=31;assert(Dg9TimingControl(&p,&result)&&result==87&&!waits&&!locks);p.cbOutBuffer=32;
 reset(1);p.lpOutBuffer=99;assert(Dg9TimingControl(&p,&result)&&result==87&&!commands&&locks==unlocks);p.lpOutBuffer=2;
 reset(1);input[8]=1;assert(Dg9TimingControl(&p,&result)&&result==87&&!commands&&locks==unlocks);
 reset(1);mode=3;assert(Dg9TimingControl(&p,&result)&&result==87&&!locks);
 reset(1);assert(Dg9TimingSetRate(120)&&!Dg9TimingSetRate(144));Dg9TimingShutdown();
 puts("PASS Win98 actual timing transaction: pinned bounds, malformed/critical refusal, one wait, timeout/cancel and balanced cleanup");
}
''')
    subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-O1',
                    '-fsanitize=address,undefined','-I'+str(ROOT/'guest/include'),'-I'+str(p),
                    str(p/'test.c'),'-o',str(p/'test')],check=True)
    subprocess.run([str(p/'test')],check=True)
