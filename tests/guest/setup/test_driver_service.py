#!/usr/bin/env python3
"""Exercise the actual owned service adapter with bounded SCM syscall seams."""
from pathlib import Path
import os
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix='dreamgpu-service-') as temporary:
    output = Path(temporary) / 'test'
    subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++23', '-O1', '-g',
                    '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-fno-omit-frame-pointer', str(ROOT / 'tools/setup/driver-service-tests.cpp'),
                    '-o', str(output)], check=True)
    subprocess.run([str(output)], check=True)
