#!/usr/bin/env python3
"""Exercise the actual C++ owner core through its C ABI under sanitizers."""
import os
from pathlib import Path
import subprocess
import tempfile

SOURCE = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='dreamgpu-owner-test-') as directory:
    output = Path(directory)
    common = ['-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined', '-g']
    subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', *common, '-c',
                    str(SOURCE/'owner-test.c'), '-o', str(output/'test.o')], check=True)
    subprocess.run([os.environ.get('CXX', 'clang++'), '-std=c++23', '-fno-exceptions',
                    '-fno-rtti', *common, str(SOURCE.parents[2]/"guest/win9x/owner32.cpp"), str(output/'test.o'),
                    '-o', str(output/'test')], check=True)
    subprocess.run([str(output/'test')], check=True)
