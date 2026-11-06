#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Bulk indexed client-array draws (the buffer draw without buffer slots, with
DG_CAP_GL_VERTEX_BUFFER_OBJECT) and EXT_compiled_vertex_array: actual frontend
contract under sanitizers (gating, the same drawn vertices as the per-index
expansion for every array kind, stride, index type and mode, fallbacks,
errors, display lists, the lock as client state, ArrayElement records), every
record admitted by the NT validator with every inline index within its client
vertices, the exported entry points of both the private frontend and the ICD
and the generated export list. Then prints the records, bytes and frontend
time of a Quake III-like batch along the old and new paths (an optimized
build without sanitizers; the time is host CPU, indicative only)."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
SOURCE = ROOT / "guest/opengl"

EXPORTS = {"glLockArraysEXT": 8, "glUnlockArraysEXT": 0}
for filename in ("frontend.def", "icd.def"):
    exported = dict(re.findall(r"^\s+(\w+)=(\w+@\d+)$", (SOURCE / filename).read_text(), re.M))
    for name, size in EXPORTS.items():
        assert exported[name] == f"{name}@{size}", (filename, name)
    found = {name for name in exported if re.match(r"gl(?:Un)?[Ll]ockArrays", name)}
    assert found == set(EXPORTS), (filename, found ^ set(EXPORTS))
compatibility = (SOURCE / "compatibility.cpp").read_text()
for name in EXPORTS:
    assert f'{{"{name}", JglVertexBufferObjectAvailable}}' in compatibility, name
with tempfile.TemporaryDirectory(prefix="dreamgpu-cva-exports-") as directory:
    generated = Path(directory)
    generator = generated / "support/guest/opengl/generate.py"
    generator.parent.mkdir(parents=True)
    (generated / "guest/opengl").mkdir(parents=True)
    shutil.copyfile(ROOT / "support/guest/opengl/generate.py", generator)
    subprocess.run([sys.executable, str(generator)], check=True)
    for filename in ("frontend.def", "scalar.inc"):
        assert (generated / "guest/opengl" / filename).read_bytes() == \
            (SOURCE / filename).read_bytes(), filename
print("PASS compiled vertex array exports: EXT names, in frontend and ICD", flush=True)

with tempfile.TemporaryDirectory(prefix="dreamgpu-compiled-vertex-array-") as temporary:
    root = Path(temporary)
    (root / "GL").mkdir()
    for name in ("frontend.cpp", "arrays.cpp", "buffers.h", "buffer.cpp", "query.cpp",
                 "program.cpp", "attrib.cpp", "compatibility.cpp", "icd-client.inc",
                 "internal.h", "packing.h", "transport.h", "scalar.inc", "names.h", "state.h"):
        shutil.copyfile(SOURCE / name, root / name)
    shutil.copyfile(HERE / "frontend.cpp", root / "frontend-harness.cpp")
    shutil.copyfile(HERE / "frontend-windows.h", root / "windows.h")
    shutil.copyfile(HERE / "frontend-gl.h", root / "GL/gl.h")
    shutil.copyfile(HERE / "compiled-vertex-array.cpp", root / "test.cpp")
    common = [os.environ.get("CXX", "c++"), "-std=c++23", "-Wall", "-Wextra", "-Werror",
              "-fno-exceptions", "-fno-rtti", "-I" + str(root),
              "-I" + str(ROOT / "guest/include"), "-I" + str(ROOT / "guest/nt/include"),
              str(root / "test.cpp")]
    subprocess.run(common + ["-O1", "-fsanitize=address,undefined", "-fno-sanitize-recover=all",
                             "-g", "-o", str(root / "test")], check=True)
    subprocess.run([str(root / "test")], check=True)
    print("PASS compiled vertex array: gating, bulk indexed draws equal to the expansion, "
          "fallbacks, errors, display lists, lock state and ArrayElement records", flush=True)
    # The guest builds without builtins: CopyMemory is a call there too.
    subprocess.run(common + ["-O2", "-fno-builtin", "-o", str(root / "bench")], check=True)
    subprocess.run([str(root / "bench"), "bench"], check=True)
