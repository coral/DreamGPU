#!/usr/bin/env python3
"""Actual readback marshalling with bounded bulk/legacy transport and pixel oracle."""
from pathlib import Path
import os,shutil,subprocess,tempfile
HERE=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='dg-readback-') as name:
    root=Path(name)
    shim=(HERE/'texture-internal.h').read_text()+'''\ntypedef unsigned char BYTE;
#define GL_TEXTURE_WIDTH 0x1000
#define GL_TEXTURE_HEIGHT 0x1001
ULONG JglReadbackCapacity(void);
ULONG *JglReadbackBuffer(void);
void JglDrawableSize(ULONG *,ULONG *);
BOOL JglQuery(ULONG,const ULONG[3],ULONG,void *,ULONG,ULONG *);
'''
    (root/'internal.h').write_text('#ifndef TEST_READBACK_H\n#define TEST_READBACK_H\n'+shim+'\n#endif\n')
    shutil.copy2((HERE.parents[2] / "guest/opengl")/'readback.cpp',root/'readback.cpp');shutil.copy2(HERE/'readback.cpp',root/'test.cpp')
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++23','-fno-exceptions','-fno-rtti','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I'+str((HERE.parents[2] / "guest")/'include'),str(root/'test.cpp'),'-o',str(root/'test')],check=True)
    subprocess.run([str(root/'test')],check=True)
