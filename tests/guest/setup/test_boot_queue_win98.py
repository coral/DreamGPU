#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute the maintained Win98 reboot adapter through the real syscall seam."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
compiler = os.environ.get("CXX") or shutil.which("clang++")
if not compiler:
    raise SystemExit("clang++ (or CXX) is required for the actual-source sanitizer gate")
with tempfile.TemporaryDirectory(prefix="dreamgpu-win98-queue-") as directory:
    binary = Path(directory) / "win98-queue-tests"
    subprocess.run([compiler, "-std=c++23", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                    str(ROOT / "tools/setup/boot-queue-win98-tests.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
