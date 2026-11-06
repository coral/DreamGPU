#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""DG_CAP_GL_TEXTURE_3D: actual frontend contract under sanitizers. Both export
tables carry exactly the six EXT_texture3D entry points (core and EXT names),
the generated copy wrappers submit the core function, and lookup gates them."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SOURCE = ROOT / "guest/opengl"

expected = {"glTexImage3D": 40, "glTexImage3DEXT": 40, "glTexSubImage3D": 44,
            "glTexSubImage3DEXT": 44, "glCopyTexSubImage3D": 36, "glCopyTexSubImage3DEXT": 36}
for filename in ("frontend.def", "icd.def"):
    exported = dict(re.findall(r"^\s+(\w+)=\w+@(\d+)$", (SOURCE / filename).read_text(), re.M))
    found = {name: int(size) for name, size in exported.items() if "Image3D" in name}
    assert found == expected, (filename, found)
scalar = (SOURCE / "scalar.inc").read_text()
for name in ("glCopyTexSubImage3D", "glCopyTexSubImage3DEXT"):
    body = re.search(r"void APIENTRY " + name + r"\(.*?\n}\n", scalar, re.S)
    assert body and "Scalar(FEnum_glCopyTexSubImage3D, 9, words);" in body.group(0), name
internal = (SOURCE / "internal.h").read_text()
assert "BOOL JglTexture3DAvailable(void);" in internal
assert re.search(r"LsbFirst, ImageHeight, SkipImages;\s*} JGL_UNPACK;", internal)
compatibility = (SOURCE / "compatibility.cpp").read_text()
for family in ("glTexImage3D", "glTexSubImage3D", "glCopyTexSubImage3D"):
    assert '{"%s", JglTexture3DAvailable}' % family in compatibility, family
# The texture test shim mirrors the production unpack state and 3D enums.
shim = (HERE / "texture-internal.h").read_text()
for define in ("JGL_TEXTURE_3D 0x806f", "JGL_PROXY_TEXTURE_3D 0x8070", "JGL_TEXTURE_WRAP_R 0x8072"):
    assert "#define " + define in internal and "#define " + define in shim, define
# The protocol limit is mirrored byte-identically into QEMU.
header = (ROOT / "guest/include/gl.h").read_bytes()
assert b"#define DG_CAP_GL_TEXTURE_3D 0x800000\n" in header
assert b"#define DG_GL_MAX_3D_TEXTURE_SIZE 256\n" in header
assert header == (ROOT / "vendor/qemu/include/standard-headers/dreamgpu/gl.h").read_bytes()

with tempfile.TemporaryDirectory(prefix="dreamgpu-texture-3d-") as temporary:
    root = Path(temporary)
    (root / "GL").mkdir()
    for name in ("frontend.cpp", "arrays.cpp", "buffers.h", "query.cpp", "texture.cpp", "compressed.cpp",
                 "fixed.cpp", "readback.cpp", "compatibility.cpp", "internal.h", "packing.h",
                 "transport.h", "scalar.inc", "names.h", "state.h"):
        shutil.copyfile(SOURCE / name, root / name)
    shutil.copyfile(HERE / "frontend.cpp", root / "frontend-harness.cpp")
    shutil.copyfile(HERE / "frontend-windows.h", root / "windows.h")
    shutil.copyfile(HERE / "extension-names.h", root / "extension-names.h")
    shutil.copyfile(HERE / "frontend-gl.h", root / "GL/gl.h")
    shutil.copyfile(HERE / "texture-3d.cpp", root / "test.cpp")
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++23", "-O1", "-Wall", "-Wextra", "-Werror",
                    "-fno-exceptions", "-fno-rtti", "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all", "-g",
                    "-I" + str(root), "-I" + str(ROOT / "guest/include"),
                    "-I" + str(ROOT / "guest/nt/include"), str(root / "test.cpp"),
                    "-o", str(root / "test")], check=True)
    subprocess.run([str(root / "test")], check=True)
print("PASS 3D texture frontend: six gated exports, core copy records, mirrored protocol and shims")
