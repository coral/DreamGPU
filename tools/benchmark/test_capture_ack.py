#!/usr/bin/env python3
"""Exercise the runner's actual bounded capture acknowledgement reader."""
from pathlib import Path
import subprocess,tempfile
source=Path(__file__).with_name('runner.cpp').read_text();start=source.rindex('static BOOL FixedAcknowledged(');end=source.index('\nstatic BOOL ProgramCompatible(',start)
h=r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
typedef uint32_t DWORD;typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define MAXDWORD UINT32_MAX
#define LINE_MAX 256
typedef struct {DWORD ReadIntervalTimeout,ReadTotalTimeoutMultiplier,ReadTotalTimeoutConstant,WriteTotalTimeoutMultiplier,WriteTotalTimeoutConstant;} COMMTIMEOUTS;
static COMMTIMEOUTS state;static DWORD ticks,setcalls,pos,inputbytes;static const char *input;static BOOL disconnected;static int Serial;
static char *Append(char *out,const char *in){while(*in)*out++=*in++;*out=0;return out;}
static BOOL Equal(const char*a,const char*b){return strcmp(a,b)==0;}
static DWORD GetTickCount(void){return ticks;}
static BOOL GetCommTimeouts(int h,COMMTIMEOUTS *out){(void)h;*out=state;return 1;}
static BOOL SetCommTimeouts(int h,const COMMTIMEOUTS *in){(void)h;state=*in;setcalls++;return 1;}
static BOOL ReadFile(int h,void *p,DWORD bytes,DWORD *read,void *overlap){
 (void)h;(void)overlap;assert(bytes==1);ticks+=pos<inputbytes?1:50;if(disconnected)return 0;
 *read=pos<inputbytes;if(*read)*(char*)p=input[pos++];return 1;
}
'''+source[start:end]+r'''
static void check(const char *bytes,DWORD length,BOOL disconnect,BOOL expected){
 COMMTIMEOUTS original={0,0,0,0,5000};state=original;ticks=setcalls=pos=0;input=bytes;inputbytes=length;disconnected=disconnect;
 assert(CaptureAcknowledged("abc123")==expected);assert(ticks<=5050);assert(setcalls==2);assert(!memcmp(&state,&original,sizeof(state)));
}
int main(void){
 const char good[]="CAPTURED abc123\n",crlf[]="CAPTURED abc123\r\n",wrong[]="CAPTURED other\n",extra[]="CAPTURED abc123 extra\n",nul[]="CAPTURED\0abc123\n";char huge[512];memset(huge,'x',sizeof(huge));
 check(good,sizeof(good)-1,0,1);check(crlf,sizeof(crlf)-1,0,1);check(wrong,sizeof(wrong)-1,0,0);check(extra,sizeof(extra)-1,0,0);
 COMMTIMEOUTS original={0,0,0,0,5000};state=original;ticks=setcalls=pos=0;input="MINIMIZED abc123\n";inputbytes=(DWORD)strlen(input);disconnected=0;
 assert(FixedAcknowledged("MINIMIZED","abc123"));assert(setcalls==2);assert(!memcmp(&state,&original,sizeof(state)));
 state=original;ticks=setcalls=pos=0;input="CAPTURED abc123\n";inputbytes=(DWORD)strlen(input);
 assert(!FixedAcknowledged("MINIMIZED","abc123"));assert(setcalls==2);
 for(unsigned i=0;i<2;i++){
  const char *kind=i?"OBSERVED":"CONTINUE";const char *line=i?"OBSERVED abc123\n":"CONTINUE abc123\n";
  state=original;ticks=setcalls=pos=0;input=line;inputbytes=(DWORD)strlen(input);disconnected=0;
  assert(FixedAcknowledged(kind,"abc123"));assert(setcalls==2);assert(!memcmp(&state,&original,sizeof(state)));
 }
 check(nul,sizeof(nul)-1,0,0);check("",0,0,0);check("",0,1,0);check(huge,sizeof(huge),0,0);
}
'''
with tempfile.TemporaryDirectory(prefix='dg-ack-') as folder:
 p=Path(folder);(p/'test.c').write_text(h)
 subprocess.run(['clang','-O1','-g','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS actual capture ACK reader: exact request, malformed/stale/oversize, disconnect/timeout, restored serial blocking mode (ASan/UBSan)')
