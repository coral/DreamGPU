#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""DG_CAP_GL_TEXTURE_CUBE_MAP: actual frontend contract under sanitizers. Cube
maps add no entry points; both export tables stay free of cube-specific names."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SOURCE = ROOT / "guest/opengl"

for filename in ("frontend.def", "icd.def"):
    exported = re.findall(r"^\s+(\w+)=\w+@\d+$", (SOURCE / filename).read_text(), re.M)
    assert not [name for name in exported if "Cube" in name], filename
internal = (SOURCE / "internal.h").read_text()
assert "BOOL JglTextureCubeMapAvailable(void);" in internal
# The texture test shim mirrors the production target helpers verbatim.
shim = (HERE / "texture-internal.h").read_text()
for helper in ("JglCubeFace", "JglImage2DTarget"):
    production = re.search(r"static inline BOOL " + helper + r"\(.*?\n}\n", internal, re.S)
    assert production and production.group(0) in shim, helper

with tempfile.TemporaryDirectory(prefix="dreamgpu-cube-map-") as temporary:
    root = Path(temporary)
    (root / "GL").mkdir()
    for name in ("frontend.cpp", "arrays.cpp", "query.cpp", "texture.cpp", "compressed.cpp",
                 "fixed.cpp", "readback.cpp", "compatibility.cpp", "internal.h", "packing.h",
                 "transport.h", "scalar.inc", "names.h", "state.h"):
        shutil.copyfile(SOURCE / name, root / name)
    shutil.copyfile(HERE / "frontend.cpp", root / "frontend-harness.cpp")
    shutil.copyfile(HERE / "frontend-windows.h", root / "windows.h")
    shutil.copyfile(HERE / "frontend-gl.h", root / "GL/gl.h")
    shutil.copyfile(HERE / "cube-map.cpp", root / "test.cpp")
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++23", "-O1", "-Wall", "-Wextra", "-Werror",
                    "-fno-exceptions", "-fno-rtti", "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all", "-g",
                    "-I" + str(root), "-I" + str(ROOT / "guest/include"),
                    "-I" + str(ROOT / "guest/nt/include"), str(root / "test.cpp"),
                    "-o", str(root / "test")], check=True)
    subprocess.run([str(root / "test")], check=True)
print("PASS cube map frontend: no new exports, shim helpers match production")
