#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""DG_CAP_GL_FOG_COORD: actual frontend contract under sanitizers, the exported
entry points of both the private frontend and the ICD, and the i686 vector
wrappers' exact bits and x87 status."""
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

# Core and EXT names, with stdcall argument sizes; no NV or other variants.
EXPORTS = {"glFogCoordf": 4, "glFogCoordfv": 4, "glFogCoordd": 8, "glFogCoorddv": 4,
           "glFogCoordPointer": 12}
for filename in ("frontend.def", "icd.def"):
    exported = dict(re.findall(r"^\s+(\w+)=(\w+@\d+)$", (SOURCE / filename).read_text(), re.M))
    for name, size in EXPORTS.items():
        for suffix in ("", "EXT"):
            assert exported[name + suffix] == f"{name}{suffix}@{size}", (filename, name + suffix)
    fog = {name for name in exported if name.startswith("glFogCoord")}
    assert fog == {name + suffix for name in EXPORTS for suffix in ("", "EXT")}, fog
cmake = (ROOT / "support/guest/cmake/opengl.cmake").read_text()
assert re.search(r"^set\(frontend_sources [^)]*\bfog\b", cmake, re.M)

with tempfile.TemporaryDirectory(prefix="dreamgpu-fog-coord-") as temporary:
    root = Path(temporary)
    (root / "GL").mkdir()
    for name in ("frontend.cpp", "arrays.cpp", "query.cpp", "fog.cpp", "compatibility.cpp",
                 "icd-client.inc", "internal.h", "packing.h", "transport.h", "scalar.inc",
                 "names.h", "state.h"):
        shutil.copyfile(SOURCE / name, root / name)
    shutil.copyfile(HERE / "frontend.cpp", root / "frontend-harness.cpp")
    shutil.copyfile(HERE / "frontend-windows.h", root / "windows.h")
    shutil.copyfile(HERE / "frontend-gl.h", root / "GL/gl.h")
    shutil.copyfile(HERE / "fog-coord.cpp", root / "test.cpp")
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++23", "-O1", "-Wall", "-Wextra", "-Werror",
                    "-fno-exceptions", "-fno-rtti", "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all", "-g",
                    "-I" + str(root), "-I" + str(ROOT / "guest/include"),
                    "-I" + str(ROOT / "guest/nt/include"), str(root / "test.cpp"),
                    "-o", str(root / "test")], check=True)
    subprocess.run([str(root / "test")], check=True)
print("PASS fog coordinate exports: core and EXT names only, in frontend and ICD")

if platform.system() != "Linux" or platform.machine() not in ("x86_64", "i386", "i686"):
    raise SystemExit("Run the i686 execution half on an x86 Linux host.")
errors = {1: "x87 seed failed", 2: "wrong function/count/call count", 3: "record bits changed",
          4: "x87 status changed", 5: "null vector submitted"}
compilers = [os.environ["CXX"]] if "CXX" in os.environ else [
    name for name in ("g++", "clang++") if shutil.which(name)]
if not compilers:
    raise SystemExit("GCC or Clang is required")
with tempfile.TemporaryDirectory(prefix="dreamgpu-fog-coord-i686-") as temporary:
    root = Path(temporary)
    for name in ("fog.cpp", "packing.h"):
        shutil.copyfile(SOURCE / name, root / name)
    shutil.copyfile(HERE / "vector-internal.h", root / "internal.h")
    shutil.copyfile(HERE / "fog-coord-i686.cpp", root / "test.cpp")
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
        for function in ("glFogCoordfv", "glFogCoordfvEXT"):
            match = re.search(r"<" + function + r">:\n(.*?)(?=\n\n|\Z)", assembly, re.S)
            if not match or re.search(r"\b(?:fld|fst)[a-z]*\s", match.group(1)):
                raise SystemExit(f"{compiler}: {function} uses floating-point loads/stores")
        print(f"PASS {compiler}: i686 fog coordinate vectors preserve bits and x87 status")
print("PASS fog coordinate frontend and i686 vector packing")
