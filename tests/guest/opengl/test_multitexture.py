#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""ARB_multitexture: actual frontend contract under sanitizers, and the i686
float coordinate wrappers' exact bits and x87 status without a Windows guest."""
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SOURCE = ROOT / "guest/opengl"
with tempfile.TemporaryDirectory(prefix="dreamgpu-multitexture-") as temporary:
    root = Path(temporary)
    (root / "GL").mkdir()
    for name in ("frontend.cpp", "arrays.cpp", "query.cpp", "multitexture.cpp", "compatibility.cpp",
                 "icd-client.inc", "internal.h", "packing.h", "transport.h", "scalar.inc", "names.h",
                 "state.h"):
        shutil.copyfile(SOURCE / name, root / name)
    shutil.copyfile(HERE / "frontend.cpp", root / "frontend-harness.cpp")
    shutil.copyfile(HERE / "frontend-windows.h", root / "windows.h")
    shutil.copyfile(HERE / "frontend-gl.h", root / "GL/gl.h")
    shutil.copyfile(HERE / "multitexture.cpp", root / "test.cpp")
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++23", "-O1", "-Wall", "-Wextra", "-Werror",
                    "-fno-exceptions", "-fno-rtti", "-fsanitize=address,undefined", "-g",
                    "-I" + str(root), "-I" + str(ROOT / "guest/include"),
                    "-I" + str(ROOT / "guest/nt/include"), str(root / "test.cpp"),
                    "-o", str(root / "test")], check=True)
    subprocess.run([str(root / "test")], check=True)

if platform.system() != "Linux" or platform.machine() not in ("x86_64", "i386", "i686"):
    raise SystemExit("Run the i686 execution half on an x86 Linux host.")
errors = {1: "x87 seed failed", 2: "wrong function/count/call count", 3: "record bits/defaults changed",
          4: "x87 status changed", 5: "null vector submitted"}
compilers = [os.environ["CXX"]] if "CXX" in os.environ else [
    name for name in ("g++", "clang++") if shutil.which(name)]
if not compilers:
    raise SystemExit("GCC or Clang is required")
with tempfile.TemporaryDirectory(prefix="dreamgpu-multitexture-i686-") as temporary:
    root = Path(temporary)
    for name in ("multitexture.cpp", "packing.h"):
        shutil.copyfile(SOURCE / name, root / name)
    shutil.copyfile(HERE / "vector-internal.h", root / "internal.h")
    shutil.copyfile(HERE / "multitexture-i686.cpp", root / "test.cpp")
    for compiler in compilers:
        binary = root / Path(compiler).name
        subprocess.run([compiler, "-m32", "-O2", "-std=c++23", "-fno-exceptions", "-fno-rtti",
                        "-march=pentium3", "-mno-sse", "-mno-sse2", "-ffreestanding", "-fno-builtin",
                        "-fno-stack-protector", "-fno-pie", "-ffunction-sections", "-fdata-sections",
                        "-nostdlib", "-no-pie", "-Wl,--build-id=none", "-Wl,--gc-sections",
                        "-Wl,-e,_start", "-Wall", "-Wextra", "-Werror", "-I" + str(root),
                        "-I" + str(ROOT / "guest/include"), str(root / "test.cpp"), "-o", str(binary)],
                       check=True)
        result = subprocess.run([str(binary)])
        if result.returncode:
            raise SystemExit(f"{compiler}: " + errors.get(result.returncode, f"exit {result.returncode}"))
        assembly = subprocess.check_output(["objdump", "-d", str(binary)], text=True)
        for count in range(1, 5):
            for suffix in ("", "ARB"):
                for kind in (("f", "fv") if count < 4 else ("fv",)):
                    function = f"glMultiTexCoord{count}{kind}{suffix}"
                    match = re.search(r"<" + function + r">:\n(.*?)(?=\n\n|\Z)", assembly, re.S)
                    if not match or re.search(r"\b(?:fld|fst)[a-z]*\s", match.group(1)):
                        raise SystemExit(f"{compiler}: {function} uses floating-point loads/stores")
        print(f"PASS {compiler}: i686 multitexture float wrappers preserve bits, defaults and x87 status")
print("PASS multitexture frontend and i686 coordinate packing")
