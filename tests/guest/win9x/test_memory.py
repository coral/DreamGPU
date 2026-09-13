#!/usr/bin/env python3
"""Actual C++23 Win9x memory core: C ABI, ASan/UBSan and static analysis."""
import os
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SOURCE = ROOT / "guest/win9x/memory32.cpp"
with tempfile.TemporaryDirectory(prefix="dreamgpu-memory-test-") as directory:
    out = Path(directory)
    cc, cxx = os.environ.get("CC", "clang"), os.environ.get("CXX", "clang++")
    flags = ["-Wall", "-Wextra", "-Werror", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g"]
    subprocess.run([cc, "-std=c11", *flags, "-c", str(HERE / "memory-test.c"), "-o", str(out / "test.o")], check=True)
    cpp = [cxx, "-std=c++23", "-fno-exceptions", "-fno-rtti"]
    subprocess.run([*cpp, *flags, str(SOURCE), str(out / "test.o"), "-o", str(out / "span")], check=True)
    subprocess.run([str(out / "span")], check=True)
    subprocess.run([*cpp, *flags, str(SOURCE), str(HERE / "memory-owner-test.cpp"), "-o", str(out / "owner")], check=True)
    subprocess.run([str(out / "owner")], check=True)
    subprocess.run([*cpp, "--analyze", "-Xanalyzer", "-analyzer-werror", "-o", str(out / "analysis.plist"), str(SOURCE)], check=True)
