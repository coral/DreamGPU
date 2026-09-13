#!/usr/bin/env python3
"""Exercise actual function metadata cache boundaries and generation invalidation."""
from pathlib import Path
import subprocess
import tempfile
HERE=Path(__file__).resolve().parent
source=r'''
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
typedef uint32_t ULONG;typedef uint8_t BYTE;
#include "dg-function-cache.h"
static ULONG calls,value,last;
static ULONG query(void *context,ULONG function){assert(context==&value);++calls;last=function;return value;}
int main(void){
 DG9_FUNCTION_CACHE cache={0};ULONG i;
 value=7;assert(Dg9FunctionCacheWords(&cache,12,query,&value)==7&&calls==1);
 Dg9FunctionCacheRefresh(&cache,1,0);
 for(i=0;i<1000000;++i)assert(Dg9FunctionCacheWords(&cache,12,query,&value)==7);
 assert(calls==2);
 value=0xffffffffU;assert(Dg9FunctionCacheWords(&cache,13,query,&value)==value);
 assert(Dg9FunctionCacheWords(&cache,13,query,&value)==value&&calls==3);
 Dg9FunctionCacheRefresh(&cache,1,0);value=9;
 assert(Dg9FunctionCacheWords(&cache,12,query,&value)==7&&calls==3);
 Dg9FunctionCacheRefresh(&cache,2,0);
 assert(Dg9FunctionCacheWords(&cache,12,query,&value)==9&&calls==4);
 Dg9FunctionCacheRefresh(&cache,2,1);value=0;
 assert(Dg9FunctionCacheWords(&cache,12,query,&value)==0&&calls==5);
 value=0xfffffffeU;assert(Dg9FunctionCacheWords(&cache,4095,query,&value)==value&&calls==6);
 assert(Dg9FunctionCacheWords(&cache,4096,query,&value)==value&&calls==7&&last==4096);
 assert(Dg9FunctionCacheWords(&cache,0xffffffffU,query,&value)==value&&calls==8&&last==0xffffffffU);
 assert(Dg9FunctionCacheWords(&cache,4096,query,&value)==value&&calls==9);
 puts("PASS actual arity cache: one million lookups/one query, generation+OPEN invalidation, unsupported/zero/wide values and uncached future IDs");
}
'''
with tempfile.TemporaryDirectory() as temp:
 p=Path(temp);(p/'test.c').write_text(source)
 subprocess.run(['cc','-std=c99','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(HERE.parents[2]/"guest/win9x"),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
