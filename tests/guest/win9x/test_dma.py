#!/usr/bin/env python3
"""Exercise the actual fixed Win98 DMA helper with a bounded Win32 API model."""
from pathlib import Path
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='dg-dma-') as tmp:
    for readonly in (0,1):
        exe=Path(tmp)/('dma-test-'+str(readonly))
        subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-DDG_DMA_READONLY='+str(readonly),'-fsanitize=address,undefined','-fno-omit-frame-pointer',str(ROOT/'dma-test.c'),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
