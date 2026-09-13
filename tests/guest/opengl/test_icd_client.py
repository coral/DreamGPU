#!/usr/bin/env python3
"""Compile actual client arrays, context stack, scalar transport and ICD aliases."""
# SPDX-License-Identifier: GPL-2.0-or-later
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
with tempfile.TemporaryDirectory(prefix='dreamgpu-icd-client-') as temporary:
    root = Path(temporary)
    (root/'GL').mkdir()
    for name in ('frontend.cpp','arrays.cpp','icd-client.inc','internal.h','packing.h','transport.h','scalar.inc','names.h','state.h'):
        shutil.copyfile(ROOT/'guest/opengl'/name, root/name)
    shutil.copyfile(HERE/'frontend.cpp', root/'frontend-harness.cpp')
    shutil.copyfile(HERE/'frontend-windows.h', root/'windows.h')
    shutil.copyfile(HERE/'frontend-gl.h', root/'GL/gl.h')
    shutil.copyfile(HERE/'client.cpp', root/'test.cpp')
    command = [os.environ.get('CXX','c++'),'-std=c++23','-O1','-Wall','-Wextra','-Werror',
               '-fno-exceptions','-fno-rtti','-fsanitize=address,undefined','-g',
               '-I'+str(root),'-I'+str(ROOT/'guest/include'),'-I'+str(ROOT/'guest/nt/include'),
               str(root/'test.cpp'),'-o',str(root/'test')]
    subprocess.run(command,check=True)
    subprocess.run([str(root/'test')],check=True)
