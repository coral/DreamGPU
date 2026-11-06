#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""DG_CAP_GL_VERTEX_BUFFER_OBJECT: actual frontend contract under sanitizers
(gating, BufferData/BufferSubData/DeleteBuffers records, local validation, the
mapping shadow, buffer draws and their client-draw fallbacks admitted by the NT
validator, ArrayElement, bindings as client state, share groups), the exported
entry points of both the private frontend and the ICD, the generated export
list, and the protocol constants shared with QEMU and the host."""
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

# ARB names only, with stdcall argument sizes; no core aliases.
EXPORTS = {"glGenBuffersARB": 8, "glBindBufferARB": 8, "glDeleteBuffersARB": 8,
           "glIsBufferARB": 4, "glBufferDataARB": 16, "glBufferSubDataARB": 16,
           "glGetBufferSubDataARB": 16, "glMapBufferARB": 8, "glUnmapBufferARB": 4,
           "glGetBufferParameterivARB": 12, "glGetBufferPointervARB": 12}
BUFFER_NAME = re.compile(r"gl(?:(?:Gen|Delete)Buffers|(?:Bind|Is|Map|Unmap)Buffer|"
                         r"(?:Get)?Buffer(?:Sub)?Data|GetBuffer(?:Parameteriv|Pointerv))"
                         r"(?:ARB)?$")
for filename in ("frontend.def", "icd.def"):
    exported = dict(re.findall(r"^\s+(\w+)=(\w+@\d+)$", (SOURCE / filename).read_text(), re.M))
    for name, size in EXPORTS.items():
        assert exported[name] == f"{name}@{size}", (filename, name)
    buffers = {name for name in exported if BUFFER_NAME.match(name)}
    assert buffers == set(EXPORTS), (filename, buffers ^ set(EXPORTS))
cmake = (ROOT / "support/guest/cmake/opengl.cmake").read_text()
assert re.search(r"^set\(frontend_sources [^)]*\bbuffer\b", cmake, re.M)
internal = (SOURCE / "internal.h").read_text()
assert "BOOL JglVertexBufferObjectAvailable(void);" in internal
# The reviewed generator reproduces the frontend export list.
with tempfile.TemporaryDirectory(prefix="dreamgpu-vbo-exports-") as directory:
    generated = Path(directory)
    generator = generated / "support/guest/opengl/generate.py"
    generator.parent.mkdir(parents=True)
    (generated / "guest/opengl").mkdir(parents=True)
    shutil.copyfile(ROOT / "support/guest/opengl/generate.py", generator)
    subprocess.run([sys.executable, str(generator)], check=True)
    for filename in ("frontend.def", "scalar.inc"):
        assert (generated / "guest/opengl" / filename).read_bytes() == \
            (SOURCE / filename).read_bytes(), filename
# The host, QEMU and frontend agree on the protocol.
protocol = (ROOT / "guest/include/gl.h").read_text()
assert "#define DG_CAP_GL_VERTEX_BUFFER_OBJECT 0x1000000" in protocol
assert protocol == (ROOT / "vendor/qemu/include/standard-headers/dreamgpu/gl.h").read_text()
bindings = (ROOT / "crates/dreamgpu-host/src/gl_api.rs").read_text()
for name in ("DG_CAP_GL_VERTEX_BUFFER_OBJECT", "DG_GL_MAX_BUFFERS", "DG_GL_MAX_BUFFER_BYTES",
             "DG_GL_MAX_BUFFER_STRIDE", "DG_GL_MAX_BUFFER_ELEMENTS",
             "DG_GL_BUFFER_REFERENCE_BYTES"):
    value = int(re.search(rf"#define {name} (\w+?)U?\b", protocol).group(1), 0)
    assert f"pub const {name}: u32 = {value};" in bindings, name
qemu = (ROOT / "vendor/qemu/hw/display/dreamgpu.c").read_text()
assert "DG_CAP_GL_VERTEX_BUFFER_OBJECT" in qemu
print("PASS vertex buffer object exports: ARB names only, in frontend and ICD", flush=True)

with tempfile.TemporaryDirectory(prefix="dreamgpu-vertex-buffer-object-") as temporary:
    root = Path(temporary)
    (root / "GL").mkdir()
    for name in ("frontend.cpp", "arrays.cpp", "buffers.h", "buffer.cpp", "query.cpp",
                 "program.cpp", "attrib.cpp", "compatibility.cpp", "icd-client.inc",
                 "internal.h", "packing.h", "transport.h", "scalar.inc", "names.h", "state.h"):
        shutil.copyfile(SOURCE / name, root / name)
    shutil.copyfile(HERE / "frontend.cpp", root / "frontend-harness.cpp")
    shutil.copyfile(HERE / "frontend-windows.h", root / "windows.h")
    shutil.copyfile(HERE / "extension-names.h", root / "extension-names.h")
    shutil.copyfile(HERE / "frontend-gl.h", root / "GL/gl.h")
    shutil.copyfile(HERE / "vertex-buffer-object.cpp", root / "test.cpp")
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++23", "-O1", "-Wall", "-Wextra", "-Werror",
                    "-fno-exceptions", "-fno-rtti", "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all", "-g",
                    "-I" + str(root), "-I" + str(ROOT / "guest/include"),
                    "-I" + str(ROOT / "guest/nt/include"), str(root / "test.cpp"),
                    "-o", str(root / "test")], check=True)
    subprocess.run([str(root / "test")], check=True)
print("PASS vertex buffer object: gating, records, validation, mapping, buffer draws, "
      "fallbacks, ArrayElement, client state and share groups")
