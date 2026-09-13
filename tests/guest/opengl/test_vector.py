#!/usr/bin/env python3
"""Check actual i686 vector aliases without Windows or a 32-bit libc."""
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
if platform.system() != "Linux" or platform.machine() not in ("x86_64", "i386", "i686"):
    raise SystemExit("Run this i686 execution test on an x86 Linux host (Framework).")

errors = {1: "test failed to seed x87 status", 2: "wrong scalar function/count/call count",
          3: "vector did not reach the packer directly", 4: "input bits changed",
          5: "x87 status changed", 6: "null vector behavior changed"}
compilers = [os.environ["CXX"]] if "CXX" in os.environ else [
    name for name in ("g++", "clang++") if shutil.which(name)
]
if not compilers:
    raise SystemExit("GCC or Clang is required")
with tempfile.TemporaryDirectory(prefix="jgl-vector-") as temporary:
    root = Path(temporary)
    for name in ("compatibility.cpp", "packing.h"):
        shutil.copyfile((HERE.parents[2] / "guest/opengl") / name, root / name)
    shutil.copyfile(HERE / "vector-internal.h", root / "internal.h")
    shutil.copyfile(HERE / "vector-i686.c", root / "test.cpp")
    for compiler in compilers:
        binary = root / Path(compiler).name
        subprocess.run([compiler, "-m32", "-Os", "-std=c++23", "-fno-exceptions", "-fno-rtti", "-march=pentium3", "-mno-sse", "-mno-sse2",
                        "-ffreestanding", "-fno-builtin", "-fno-stack-protector", "-fno-pie",
                        "-ffunction-sections", "-fdata-sections", "-nostdlib", "-no-pie",
                        "-Wl,--build-id=none", "-Wl,--gc-sections", "-Wl,-e,_start",
                        "-Wall", "-Wextra", "-Werror", "-I" + str((HERE.parents[2] / "guest") / "include"),
                        str(root / "test.cpp"), "-o", str(binary)], check=True)
        result = subprocess.run([str(binary)])
        if result.returncode:
            raise SystemExit(f"{compiler}: " + errors.get(result.returncode, f"exit {result.returncode}"))
        assembly = subprocess.check_output(["objdump", "-d", str(binary)], text=True)
        for function in ("glVertex3fv", "glColor3fv", "glColor4fv", "glTexCoord2fv", "glNormal3fv"):
            match = re.search(r"<" + function + r">:\n(.*?)(?=\n\n|\Z)", assembly, re.S)
            if not match or re.search(r"\b(?:fld|fst)[a-z]*\s", match.group(1)):
                raise SystemExit(f"{compiler}: {function} uses floating-point loads/stores")
        print(f"PASS {compiler}: all five actual i686 vector aliases preserve pointer, bits and x87 status")
