#!/usr/bin/env python3
"""Execute the complete optimized frontend packet path as freestanding i686."""
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
compilers = [os.environ["CXX"]] if "CXX" in os.environ else [
    name for name in ("g++", "clang++") if shutil.which(name)
]
if not compilers:
    raise SystemExit("GCC or Clang is required")
errors = {1: "x87 seed failed", 2: "actual float path changed x87 status",
          3: "actual double path changed x87 status", 4: "live scalar packet/header/TLS mismatch",
          5: "actual request/flush failed", 6: "immutable packet differs after reuse",
          7: "actual vector helper packet/bits/status mismatch",
          8: "Begin/End record limit, TLS or x87 mismatch", 9: "byte budget split mismatch",
          10: "byte-budget immutable payload mismatch", 11: "cached float state changed bits/x87 status", 12: "secondary float state changed bits/x87 status"}

with tempfile.TemporaryDirectory(prefix="jgl-full-packing-") as temporary:
    root = Path(temporary)
    (root / "GL").mkdir()
    for name in ("frontend.cpp", "internal.h", "packing.h", "transport.h", "scalar.inc", "names.h", "state.h"):
        shutil.copyfile((HERE.parents[2] / "guest/opengl") / name, root / name)
    # Reuse the strict lifecycle declarations while supplying real i686 public
    # stdcall and freestanding integer/libc declarations (no 32-bit libc SDK).
    windows = (HERE / "frontend-windows.h").read_text()
    windows = windows.replace("#define WINAPI\n", "#define WINAPI __attribute__((stdcall))\n")
    windows = windows.replace("#define APIENTRY\n", "#define APIENTRY __attribute__((stdcall))\n")
    (root / "windows.h").write_text(windows)
    shutil.copyfile(HERE / "frontend-gl.h", root / "GL/gl.h")
    (root / "stdint.h").write_text("""#ifndef JGL_I686_STDINT_H
#define JGL_I686_STDINT_H
typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef short int16_t;
typedef unsigned int uint32_t, uintptr_t;
typedef int int32_t;
typedef unsigned long long uint64_t;
#define UINT32_MAX 0xffffffffU
#endif
""")
    (root / "stddef.h").write_text("""#ifndef JGL_I686_STDDEF_H
#define JGL_I686_STDDEF_H
typedef unsigned int size_t;
#define NULL nullptr
#endif
""")
    (root / "string.h").write_text("""#include <stddef.h>
extern "C" void *memcpy(void *, const void *, size_t);
extern "C" void *memset(void *, int, size_t);
""")
    shutil.copyfile(HERE / "frontend-packing-i686.cpp", root / "test.cpp")
    for compiler in compilers:
        binary = root / Path(compiler).name
        subprocess.run([compiler, "-m32", "-O2", "-std=c++23", "-fno-exceptions", "-fno-rtti", "-march=pentium3", "-mno-sse", "-mno-sse2",
                        "-ffreestanding", "-fno-builtin", "-fno-stack-protector", "-fno-pie",
                        "-ffunction-sections", "-fdata-sections", "-nostdlib", "-no-pie",
                        "-Wl,--build-id=none", "-Wl,--gc-sections", "-Wl,-e,_start",
                        "-Wall", "-Wextra", "-Werror", "-I" + str(root),
                        "-I" + str((HERE.parents[2] / "guest") / "include"),
                        "-I" + str((HERE.parents[2] / "guest") / "nt/include"),
                        str(root / "test.cpp"), "-o", str(binary)], check=True)
        result = subprocess.run([str(binary)])
        if result.returncode:
            raise SystemExit(f"{compiler}: " + errors.get(result.returncode, f"exit {result.returncode}"))
        assembly = subprocess.check_output(["objdump", "-d", str(binary)], text=True)
        for function in ("glColor4f", "glOrtho", "glVertex3f", "glPolygonOffset", "glSecondaryColor3fEXT"):
            match = re.search(r"<" + function + r">:\n(.*?)(?=\n\n|\Z)", assembly, re.S)
            if not match or re.search(r"\b(?:fld|fst)[a-z]*\s", match.group(1)):
                raise SystemExit(f"{compiler}: actual {function} packet path uses FP loads/stores")
            if re.search(r"\bcall\b[^\n]*<(?:Scalar|Record)(?:\.|>)", match.group(1)):
                raise SystemExit(f"{compiler}: actual {function} did not inline Scalar/Record")
        print(f"PASS {compiler}: O2 actual frontend immutable packets, float/double bits, x87 status, TLS and batch boundaries")
