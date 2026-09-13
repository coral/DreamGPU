#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual staging Win32 adapter with checked filesystem/syscall seams."""
import os
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[3]
with tempfile.TemporaryDirectory(prefix="dreamgpu-stage-store-") as temporary:
    binary = Path(temporary)/"test"
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++23", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                    str(ROOT/"tools/setup/stage-store-tests.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
