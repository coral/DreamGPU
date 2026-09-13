#!/usr/bin/env python3
"""Actual Win16 ICD response writer + checked donor patch, with far-segment oracle."""
# SPDX-License-Identifier: GPL-2.0-or-later
import hashlib
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
helper = ROOT / 'guest/win9x/dg-icd16.h'
patches = ROOT / 'support/guest/win9x/patches'
spec = importlib.util.spec_from_file_location('checked_patches', ROOT/'tests/guest/support/patches.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
with tempfile.TemporaryDirectory(prefix='dreamgpu-win16-icd-') as temporary:
    work = Path(temporary)
    source = work/'source'
    source.mkdir()
    raw = (ROOT/'vendor/vmdisp9x/control.c').read_bytes()
    (source/'control.c').write_bytes(raw)
    module.apply(source, patches/'icd.json')
    patched = (source/'control.c').read_text()
    assert 'typedef char dg_icd_matches_donor_size' in patched
    case = patched[patched.index('case OPENGL_GETINFO: /* input:'):]
    assert case[:case.index('/* OPENGL_GETINFO */')].count('DgIcdGetInfo16(lpOutput)') == 1
    assert 'case OPENGL_GETINFO:' in patched[:patched.index('case OPENGL_GETINFO: /* input:')]
    assert (ROOT/'vendor/vmdisp9x/control.c').read_bytes() == raw
    shutil.copyfile(helper, work/'dg-icd16.h')
    # Simulate a selected 64KiB segment; FP_OFF retains the offset relative to
    # that selector. The pinned Watcom build checks actual far/near pointer widths.
    (work/'i86.h').write_text('#define FP_OFF(p) ((unsigned)((unsigned char *)(p)-segment))\n')
    (work/'test.c').write_text(r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
typedef uint32_t DWORD; typedef int32_t LONG; typedef void *LPVOID;
static unsigned char segment[65536];
static unsigned copies;
static void *_fmemcpy(void *to,const void *from,size_t bytes) {
    assert(to>= (void*)segment && to< (void*)(segment+sizeof(segment)));
    assert(bytes==270);++copies;return memcpy(to,from,bytes);
}
#include "dg-icd16.h"
int main(void) {
    const unsigned offsets[]={0,1,100,65266};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);++i) {
        unsigned at=offsets[i];memset(segment,0xa5,sizeof(segment));copies=0;
        assert(DgIcdGetInfo16(segment+at)==1 && copies==1);
        const unsigned char expected[]={2,0,0,0,1,0,0,0,'D','G','P','U','I','C','D',0};
        assert(!memcmp(segment+at,expected,sizeof(expected)));
        for(unsigned n=sizeof(expected);n<270;++n) assert(segment[at+n]==0);
        for(unsigned n=0;n<at;++n) assert(segment[n]==0xa5);
        for(unsigned n=at+270;n<sizeof(segment);++n) assert(segment[n]==0xa5);
    }
    memset(segment,0xa5,sizeof(segment));copies=0;
    assert(DgIcdGetInfo16(NULL)==-1);
    assert(DgIcdGetInfo16(segment+65267)==-1);
    assert(DgIcdGetInfo16(segment+65535)==-1);
    assert(!copies);for(unsigned n=0;n<sizeof(segment);++n)assert(segment[n]==0xa5);
    puts("PASS Win16 actual ICD writer:270 ANSI bytes;2/1 DGPUICD;far-offset bounds;null rejection;checked donor patch;tail untouched");
}
''')
    subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror','-O1',
                    '-fsanitize=address,undefined','-g','-I'+str(work),str(work/'test.c'),'-o',str(work/'test')],check=True)
    subprocess.run([str(work/'test')],check=True)
