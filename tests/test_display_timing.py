#!/usr/bin/env python3
"""Compile actual QEMU timing callbacks against bounded timer/IRQ seams."""
import os
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / 'vendor/qemu/hw/display/dreamgpu.c').read_text()
with tempfile.TemporaryDirectory(prefix='dreamgpu-timing-') as temp:
    path = Path(temp)
    start = source.index('static uint32_t dg_timing_height(')
    end = source.index('static void dg_complete(', start)
    (path / 'display-timing-source.inc').write_text(source[start:end])
    subprocess.run([os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-O2', '-fsanitize=address,undefined', '-DDG_IRQ_GL_COMPLETION=2',
                    '-I'+str(path), '-I'+str(ROOT/'guest/include'),
                    '-I'+str(ROOT/'vendor/qemu/hw/display'),
                    str(ROOT/'tests/native_cpu/display_timing.c'), '-o', str(path/'test')], check=True)
    subprocess.run([str(path/'test')], check=True)
