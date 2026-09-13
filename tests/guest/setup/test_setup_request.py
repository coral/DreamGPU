#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the actual global Win32 adapter, journals and RunOnce via syscall seams."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
compiler = os.environ.get("CXX") or shutil.which("clang++")
if not compiler:
    raise SystemExit("clang++ (or CXX) is required for the actual-source sanitizer gate")
with tempfile.TemporaryDirectory(prefix="dreamgpu-setup-request-") as directory:
    binary = Path(directory) / "setup-request-tests"
    subprocess.run([compiler, "-std=c++23", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                    str(ROOT / "tools/setup/setup-request-tests.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
