#!/usr/bin/env python3
"""Compile the real QEMU generator and owned DreamGPU monitor descriptor."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='dg-edid-') as directory:
    tmp = Path(directory)
    (tmp / 'qemu').mkdir()
    (tmp / 'qemu/osdep.h').write_text('''#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
typedef struct MemoryRegion MemoryRegion;
typedef struct Object Object;
''')
    (tmp / 'qemu/bswap.h').write_text('''#include <stdint.h>
static inline void stw_le_p(void *p,unsigned v){uint8_t*b=p;b[0]=v;b[1]=v>>8;}
static inline void stw_be_p(void *p,unsigned v){uint8_t*b=p;b[0]=v>>8;b[1]=v;}
static inline void stl_le_p(void *p,unsigned v){uint8_t*b=p;for(unsigned i=0;i<4;++i)b[i]=v>>(8*i);}
''')
    source = tmp / 'test.c'
    source.write_text('#include "qemu/osdep.h"\n' +
                      (ROOT / 'tests/native_cpu/display_edid.c').read_text())
    binary = tmp / 'test'
    flags = [os.environ.get('CC', 'cc'), '-std=c11', '-O2', '-Wall', '-Wextra',
             '-Werror', '-fsanitize=address,undefined', '-I' + str(tmp),
             '-I' + str(ROOT / 'vendor/qemu/include'),
             '-I' + str(ROOT / 'vendor/qemu/hw/display')]
    # The unchanged upstream generator follows QEMU's warning policy. Keep
    # the owned descriptor and test under the full strict warning set.
    upstream = tmp / 'upstream.o'
    subprocess.run(flags + ['-Wno-sign-compare', '-Wno-unused-parameter', '-c',
                            str(ROOT / 'vendor/qemu/hw/display/edid-generate.c'),
                            '-o', str(upstream)], check=True)
    subprocess.run(flags + [str(source), str(upstream), '-o', str(binary)], check=True)
    args = [str(binary)]
    if os.environ.get('DG_EDID_OUTPUT'):
        args.append(os.environ['DG_EDID_OUTPUT'])
    subprocess.run(args, check=True)
