#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual setup mutex ownership and preflight serialization regression."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
compiler = os.environ.get("CXX") or shutil.which("clang++")
if not compiler:
    raise SystemExit("clang++ (or CXX) is required")
source = (ROOT / "tools/setup/main.cpp").read_text()
run = source[source.index("unsigned run(bool stage_only, int action)"):]
# Mutable phase/device observations belong inside the ownership interval.
assert run.index("setup::SetupLock lock") < run.index("setup::global::presence()")
assert run.index("setup::SetupLock lock") < run.index("Devices devices")
with tempfile.TemporaryDirectory(prefix="dreamgpu-setup-lock-") as directory:
    binary = Path(directory) / "lock-tests"
    subprocess.run([compiler, "-std=c++23", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                    str(ROOT / "tools/setup/setup-lock-tests.cpp"), "-o", str(binary)],
                   check=True)
    subprocess.run([str(binary)], check=True)
