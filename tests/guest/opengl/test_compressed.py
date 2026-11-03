#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""DG_CAP_GL_COMPRESSED_TEXTURES: actual frontend contract under sanitizers,
and the exported entry points of both the private frontend and the ICD."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SOURCE = ROOT / "guest/opengl"

# Core and ARB names of the 2D entry points only, with stdcall argument sizes.
EXPORTS = {"glCompressedTexImage2D": 32, "glCompressedTexSubImage2D": 36,
           "glGetCompressedTexImage": 12}
for filename in ("frontend.def", "icd.def"):
    exported = dict(re.findall(r"^\s+(\w+)=(\w+@\d+)$", (SOURCE / filename).read_text(), re.M))
    for name, size in EXPORTS.items():
        for suffix in ("", "ARB"):
            assert exported[name + suffix] == f"{name}{suffix}@{size}", (filename, name + suffix)
    compressed = {name for name in exported if "Compressed" in name}
    assert compressed == {name + suffix for name in EXPORTS for suffix in ("", "ARB")}, compressed
cmake = (ROOT / "support/guest/cmake/opengl.cmake").read_text()
assert re.search(r"^set\(frontend_sources [^)]*\bcompressed\b", cmake, re.M)

with tempfile.TemporaryDirectory(prefix="dreamgpu-compressed-") as temporary:
    root = Path(temporary)
    (root / "GL").mkdir()
    for name in ("frontend.cpp", "arrays.cpp", "query.cpp", "texture.cpp", "compressed.cpp",
                 "compatibility.cpp", "internal.h", "packing.h", "transport.h", "scalar.inc",
                 "names.h", "state.h"):
        shutil.copyfile(SOURCE / name, root / name)
    shutil.copyfile(HERE / "frontend.cpp", root / "frontend-harness.cpp")
    shutil.copyfile(HERE / "frontend-windows.h", root / "windows.h")
    shutil.copyfile(HERE / "frontend-gl.h", root / "GL/gl.h")
    shutil.copyfile(HERE / "compressed.cpp", root / "test.cpp")
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++23", "-O1", "-Wall", "-Wextra", "-Werror",
                    "-fno-exceptions", "-fno-rtti", "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all", "-g",
                    "-I" + str(root), "-I" + str(ROOT / "guest/include"),
                    "-I" + str(ROOT / "guest/nt/include"), str(root / "test.cpp"),
                    "-o", str(root / "test")], check=True)
    subprocess.run([str(root / "test")], check=True)
print("PASS compressed texture exports: core and ARB 2D names only, in frontend and ICD")
