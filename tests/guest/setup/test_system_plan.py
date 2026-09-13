#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile the actual global-provider plan/Win32 adapter with injected failures."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
compiler = os.environ.get('CXX') or shutil.which('clang++')
if not compiler:
    raise SystemExit('clang++ (or CXX) is required for the actual-source sanitizer gate')
with tempfile.TemporaryDirectory(prefix='dreamgpu-system-plan-') as directory:
    for name in ('system-plan-tests', 'native-alias-tests', 'runtime-boot-tests', 'runtime-verify-tests', 'system-engine-tests'):
        binary = Path(directory) / name
        subprocess.run([compiler, '-std=c++23', '-O1', '-Wall', '-Wextra', '-Werror',
                        '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                        str(ROOT / 'tools/setup' / (name + '.cpp')), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
