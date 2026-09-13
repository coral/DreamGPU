#!/usr/bin/env python3
"""Exercise the actual bounded PE subsystem gate under ASan/UBSan."""
from pathlib import Path
import subprocess
import tempfile
here = Path(__file__).resolve().parent
source = r'''
#include <cassert>
#include "pe-version.h"
int main() {
    unsigned char pe[256]{};
    pe[0]='M';pe[1]='Z';pe[60]=64;pe[64]='P';pe[65]='E';
    pe[68]=0x4c;pe[69]=1;pe[84]=72;pe[88]=0xb;pe[89]=1;
    pe[136]=4;
    assert(DgCompatiblePe(pe,sizeof(pe),4,10));
    assert(DgCompatiblePe(pe,sizeof(pe),5,0));
    pe[136]=5;assert(!DgCompatiblePe(pe,sizeof(pe),4,10));
    assert(DgCompatiblePe(pe,sizeof(pe),5,0));
    pe[138]=1;assert(!DgCompatiblePe(pe,sizeof(pe),5,0));
    for(unsigned n=0;n<160;n++)assert(!DgCompatiblePe(pe,n,6,0));
    pe[60]=255;pe[61]=255;pe[62]=255;pe[63]=255;
    assert(!DgCompatiblePe(pe,sizeof(pe),6,0));
    pe[60]=64;pe[61]=pe[62]=pe[63]=0;pe[89]=2;
    assert(!DgCompatiblePe(pe,sizeof(pe),6,0));
}
'''
with tempfile.TemporaryDirectory(prefix='dreamgpu-pe-') as temp:
    root=Path(temp); (root/'test.cpp').write_text(source)
    subprocess.run(['c++','-std=c++23','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                    '-I'+str(here),str(root/'test.cpp'),'-o',str(root/'test')],check=True)
    subprocess.run([str(root/'test')],check=True)
print('PASS PE4/5 subsystem, truncation, offset overflow and PE32 ABI')
