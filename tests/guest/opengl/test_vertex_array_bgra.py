#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""DG_CAP_GL_VERTEX_ARRAY_BGRA: actual frontend contract under sanitizers
(gating, ARB_vertex_array_bgra errors and size queries, client arrays reordered
into the existing RGBA records, the BGRA buffer draw for buffer slots and its
client-draw fallbacks, display lists and ArrayElement, every record admitted
by the NT validator), and the protocol constants shared with QEMU and the
host."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SOURCE = ROOT / "guest/opengl"

# The host, QEMU and frontend agree on the protocol.
protocol = (ROOT / "guest/include/gl.h").read_text()
assert "#define DG_CAP_GL_VERTEX_ARRAY_BGRA 0x4000000" in protocol
assert "#define DG_GL_ARRAY_BGRA 0x02000000U" in protocol
assert protocol == (ROOT / "vendor/qemu/include/standard-headers/dreamgpu/gl.h").read_text()
bindings = (ROOT / "crates/dreamgpu-host/src/gl_api.rs").read_text()
for name in ("DG_CAP_GL_VERTEX_ARRAY_BGRA", "DG_GL_ARRAY_BGRA"):
    value = int(re.search(rf"#define {name} (\w+?)U?\b", protocol).group(1), 0)
    assert f"pub const {name}: u32 = {value};" in bindings, name
qemu = (ROOT / "vendor/qemu/hw/display/dreamgpu.c").read_text()
assert "DG_CAP_GL_VERTEX_ARRAY_BGRA" in qemu
# No new entry point: BGRA is a size of existing pointer calls.
for filename in ("frontend.def", "icd.def"):
    assert "bgra" not in (SOURCE / filename).read_text().lower(), filename
print("PASS vertex array bgra protocol: capability, descriptor flag, QEMU mirror and host",
      flush=True)

with tempfile.TemporaryDirectory(prefix="dreamgpu-vertex-array-bgra-") as temporary:
    root = Path(temporary)
    (root / "GL").mkdir()
    for name in ("frontend.cpp", "arrays.cpp", "buffers.h", "buffer.cpp", "query.cpp",
                 "program.cpp", "attrib.cpp", "compatibility.cpp", "icd-client.inc",
                 "internal.h", "packing.h", "transport.h", "scalar.inc", "names.h", "state.h"):
        shutil.copyfile(SOURCE / name, root / name)
    shutil.copyfile(HERE / "frontend.cpp", root / "frontend-harness.cpp")
    shutil.copyfile(HERE / "frontend-windows.h", root / "windows.h")
    shutil.copyfile(HERE / "frontend-gl.h", root / "GL/gl.h")
    shutil.copyfile(HERE / "vertex-array-bgra.cpp", root / "test.cpp")
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++23", "-O1", "-Wall", "-Wextra", "-Werror",
                    "-fno-exceptions", "-fno-rtti", "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all", "-g",
                    "-I" + str(root), "-I" + str(ROOT / "guest/include"),
                    "-I" + str(ROOT / "guest/nt/include"), str(root / "test.cpp"),
                    "-o", str(root / "test")], check=True)
    subprocess.run([str(root / "test")], check=True)
print("PASS vertex array bgra: gating, errors, queries, reordered client records, BGRA "
      "buffer draws, fallbacks, lists and ArrayElement")
