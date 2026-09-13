#!/usr/bin/env python3
"""Actual launch/summary handshake never resumes an unarmed owned game."""
from pathlib import Path
import subprocess,tempfile
HERE=Path(__file__).resolve().parent
source=r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef uint32_t DWORD;typedef int BOOL;
#define TRUE 1
#define FALSE 0
typedef struct {void *hProcess,*hThread;} PROCESS_INFORMATION;
static const char *events[8];static unsigned count,fail_reply,resumes;static BOOL ack_ok,resume_ok;
static BOOL Reply(const char *kind,const char *id,const char *code,BOOL payload){assert(!strcmp(id,"request1")&&!code&&!payload);events[count++]=kind;return count!=fail_reply;}
static BOOL FixedAcknowledged(const char *kind,const char *id){assert(!strcmp(id,"request1"));events[count++]=kind;return ack_ok;}
static DWORD ResumeThread(void *thread){assert(thread==(void*)2);resumes++;return resume_ok?1:UINT32_MAX;}
#include "launch-phase.h"
int main(void){
 PROCESS_INFORMATION p={(void*)1,(void*)2};BOOL resumed;
 ack_ok=resume_ok=1;assert(!LaunchObserved("request1",&p,&resumed));assert(resumed&&resumes==1&&count==4);
 assert(!strcmp(events[0],"STARTED")&&!strcmp(events[1],"PROCESS_READY")&&!strcmp(events[2],"CONTINUE")&&!strcmp(events[3],"PROCESS_RESUMED"));
 for(unsigned f=1;f<=2;f++){count=resumes=0;fail_reply=f;assert(LaunchObserved("request1",&p,&resumed));assert(!resumed&&!resumes);}
 count=resumes=fail_reply=0;ack_ok=0;assert(!strcmp(LaunchObserved("request1",&p,&resumed),"launch-not-armed"));assert(!resumed&&!resumes);
 count=0;ack_ok=1;resume_ok=0;assert(!strcmp(LaunchObserved("request1",&p,&resumed),"resume-failed"));assert(!resumed&&resumes==1);
 count=resumes=0;resume_ok=1;fail_reply=4;assert(LaunchObserved("request1",&p,&resumed));assert(resumed&&resumes==1);
 count=fail_reply=0;assert(!ResultObserved("request1"));assert(count==2&&!strcmp(events[0],"TIMEDEMO_RESULT")&&!strcmp(events[1],"OBSERVED"));
 count=0;ack_ok=0;assert(!strcmp(ResultObserved("request1"),"result-not-observed"));
}
'''
with tempfile.TemporaryDirectory(prefix='dg-launch-phase-') as d:
 p=Path(d);(p/'test.c').write_text(source)
 subprocess.run(['clang','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str(HERE),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
print('PASS actual launch/summary event handshake: ordering, missing ACK, serial/resume failure, no unarmed resume (ASan/UBSan)')
