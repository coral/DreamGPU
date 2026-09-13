#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual Win32 journal and cache-path ownership regression."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
compiler = os.environ.get("CXX") or shutil.which("clang++")
if not compiler:
    raise SystemExit("clang++ (or CXX) is required")
with tempfile.TemporaryDirectory(prefix="dreamgpu-cache-") as directory:
    binary = Path(directory) / "cache-tests"
    subprocess.run([compiler, "-std=c++23", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                    str(ROOT / "tools/setup/system-cache-tests.cpp"), "-o", str(binary)],
                   check=True)
    subprocess.run([str(binary)], check=True)
