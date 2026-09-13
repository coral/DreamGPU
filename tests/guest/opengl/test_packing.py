#!/usr/bin/env python3
"""Check actual i686 scalar packing, without a Windows guest or 32-bit libc."""
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

errors = {
    1: "finite float bit pattern changed", 2: "finite float packing raised x87 flags",
    3: "nonfinite float bit pattern changed", 4: "nonfinite float packing raised x87 flags",
    5: "double bit pattern changed", 6: "double packing raised x87 flags",
}
compilers = [os.environ["CC"]] if "CC" in os.environ else [
    name for name in ("gcc", "clang") if shutil.which(name)
]
if not compilers:
    raise SystemExit("GCC or Clang is required")
with tempfile.TemporaryDirectory(prefix="jgl-packing-") as temporary:
    for compiler in compilers:
        binary = Path(temporary) / Path(compiler).name
        command = [compiler, "-m32", "-O2", "-march=pentium3", "-mno-sse", "-mno-sse2",
                   "-ffreestanding", "-fno-builtin", "-fno-stack-protector", "-fno-pie",
                   "-nostdlib", "-no-pie", "-Wl,--build-id=none", "-Wl,-e,_start",
                   "-Wall", "-Wextra", "-Werror", "-I" + str((HERE.parents[2] / "guest/opengl")),
                   "-I" + str((HERE.parents[2] / "guest") / "include"),
                   str(HERE / "packing-i686.c"), "-o", str(binary)]
        subprocess.run(command, check=True)
        result = subprocess.run([str(binary)])
        if result.returncode:
            raise SystemExit(f"{compiler}: " + errors.get(result.returncode, f"exit {result.returncode}"))
        assembly = subprocess.check_output(["objdump", "-d", str(binary)], text=True)
        for function in ("glColor4f", "glOrtho", "glVertex3f"):
            match = re.search(r"<" + function + r">:\n(.*?)(?=\n\n|\Z)", assembly, re.S)
            if not match or re.search(r"\b(?:fld|fst)[a-z]*\s", match.group(1)):
                raise SystemExit(f"{compiler}: {function} uses floating-point loads/stores")
        print(f"PASS {compiler}: i686 scalar bits and x87 status; float/double wrappers use integer moves")
