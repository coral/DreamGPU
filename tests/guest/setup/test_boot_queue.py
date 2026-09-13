#!/usr/bin/env python3
"""Build the actual reboot queue/Win32 adapter with deterministic syscall faults."""
from pathlib import Path
import os
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix='dreamgpu-boot-queue-') as temporary:
    output = Path(temporary) / 'test'
    subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++23', '-fshort-wchar',
                    '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                    str(ROOT / 'tools/setup/boot-queue-tests.cpp'), '-o', str(output)], check=True)
    subprocess.run([str(output)], check=True)
