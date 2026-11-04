#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""DG_CAP_GL_OCCLUSION_QUERY (ARB_occlusion_query, NV_fence): actual frontend
contract under sanitizers (gating, exact CALL/DATA/QUERY records within the
host's shapes and admitted by the NT validator, local validation, name
batches, display lists, and the answers the frontend keeps for repeated
polls), the exported entry points of both the private frontend and the ICD,
the generated export list, and the protocol constants shared with QEMU and the
host."""
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

# Extension names only, with stdcall argument sizes; no core aliases.
EXPORTS = {"glGenQueriesARB": 8, "glDeleteQueriesARB": 8, "glIsQueryARB": 4,
           "glBeginQueryARB": 8, "glEndQueryARB": 4, "glGetQueryivARB": 12,
           "glGetQueryObjectivARB": 12, "glGetQueryObjectuivARB": 12, "glGenFencesNV": 8,
           "glDeleteFencesNV": 8, "glSetFenceNV": 8, "glTestFenceNV": 4,
           "glFinishFenceNV": 4, "glIsFenceNV": 4, "glGetFenceivNV": 12}
QUERY_NAME = re.compile(r"gl\w*(?:Quer(?:y|ies)|Fence)\w*$")
for filename in ("frontend.def", "icd.def"):
    exported = dict(re.findall(r"^\s+(\w+)=(\w+@\d+)$", (SOURCE / filename).read_text(), re.M))
    for name, size in EXPORTS.items():
        assert exported[name] == f"{name}@{size}", (filename, name)
    found = {name for name in exported if QUERY_NAME.match(name)}
    assert found == set(EXPORTS), (filename, found ^ set(EXPORTS))
cmake = (ROOT / "support/guest/cmake/opengl.cmake").read_text()
assert re.search(r"^set\(frontend_sources [^)]*\bocclusion\b", cmake, re.M)
internal = (SOURCE / "internal.h").read_text()
assert "BOOL JglOcclusionQueryAvailable(void);" in internal
compatibility = (SOURCE / "compatibility.cpp").read_text()
# wglGetProcAddress gates every entry point, and only these, on the capability.
prefixes = re.findall(r'\{"(gl\w+)", JglOcclusionQueryAvailable\}', compatibility)
for name in EXPORTS:
    assert any(name.startswith(prefix) for prefix in prefixes), name
for prefix in prefixes:
    assert any(name.startswith(prefix) for name in EXPORTS), prefix
public = set(re.findall(r"^\s+(\w+)=", (SOURCE / "frontend.def").read_text(), re.M))
for prefix in prefixes:
    assert all(name in EXPORTS for name in public if name.startswith(prefix)), prefix
# The reviewed generator reproduces the frontend export list.
with tempfile.TemporaryDirectory(prefix="dreamgpu-query-exports-") as directory:
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
assert "#define DG_CAP_GL_OCCLUSION_QUERY 0x8000000" in protocol
assert protocol == (ROOT / "vendor/qemu/include/standard-headers/dreamgpu/gl.h").read_text()
bindings = (ROOT / "crates/dreamgpu-host/src/gl_api.rs").read_text()
for name in ("DG_CAP_GL_OCCLUSION_QUERY", "DG_GL_MAX_QUERIES", "DG_GL_QUERY_NAME_BATCH",
             "DG_GL_QUERY_REPORTS", "DG_GL_QUERY_PENDING", "DG_GL_QUERY_AVAILABLE"):
    value = int(re.search(rf"#define {name} (\w+?)U?\b", protocol).group(1), 0)
    assert f"pub const {name}: u32 = {value};" in bindings, name
# Every capability bit is distinct.
caps = [int(v, 0) for v in re.findall(r"#define DG_CAP_GL_\w+ (0x[0-9a-f]+)", protocol)]
assert len(caps) == len(set(caps)) and all(c & (c - 1) == 0 for c in caps)
qemu = (ROOT / "vendor/qemu/hw/display/dreamgpu.c").read_text()
assert "DG_CAP_GL_OCCLUSION_QUERY" in qemu
meson = (ROOT / "vendor/qemu/hw/display/meson.build").read_text()
assert "'src/occlusion.rs'" in meson
print("PASS occlusion query exports: extension names only, in frontend and ICD", flush=True)

UNITS = ("frontend.cpp", "arrays.cpp", "buffers.h", "buffer.cpp", "query.cpp", "program.cpp",
         "attrib.cpp", "occlusion.cpp", "compatibility.cpp", "internal.h",
         "packing.h", "transport.h", "scalar.inc", "names.h", "state.h")
with tempfile.TemporaryDirectory(prefix="dreamgpu-occlusion-query-") as temporary:
    root = Path(temporary)
    (root / "GL").mkdir()
    for name in UNITS:
        shutil.copyfile(SOURCE / name, root / name)
    shutil.copyfile(HERE / "frontend.cpp", root / "frontend-harness.cpp")
    shutil.copyfile(HERE / "frontend-windows.h", root / "windows.h")
    shutil.copyfile(HERE / "frontend-gl.h", root / "GL/gl.h")
    shutil.copyfile(HERE / "occlusion-query.cpp", root / "test.cpp")
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++23", "-O1", "-Wall", "-Wextra", "-Werror",
                    "-fno-exceptions", "-fno-rtti", "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all", "-g",
                    "-I" + str(root), "-I" + str(ROOT / "guest/include"),
                    "-I" + str(ROOT / "guest/nt/include"), str(root / "test.cpp"),
                    "-o", str(root / "test")], check=True)
    subprocess.run([str(root / "test")], check=True)
print("PASS occlusion query: gating, records, host shapes, validation, names, lists, kept "
      "results and signalled fences")
