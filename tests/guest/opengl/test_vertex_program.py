#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""DG_CAP_GL_VERTEX_PROGRAM: actual frontend contract under sanitizers (gating,
records, local validation, generic arrays and the attribute draw admitted by
the NT validator), the exported entry points of both the private frontend and
the ICD, the protocol constants shared with the host, and the i686 float
attribute wrappers' exact bits and x87 status."""
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

# ARB names only, with stdcall argument sizes; no core or NV aliases.
EXPORTS = {"glVertexAttrib4NubARB": 20, "glVertexAttribPointerARB": 24,
           "glEnableVertexAttribArrayARB": 4, "glDisableVertexAttribArrayARB": 4,
           "glGetVertexAttribPointervARB": 12}
for count in range(1, 5):
    for kind, size in (("s", 4), ("f", 4), ("d", 8)):
        EXPORTS[f"glVertexAttrib{count}{kind}ARB"] = 4 + count * size
        EXPORTS[f"glVertexAttrib{count}{kind}vARB"] = 8
for kind in ("b", "s", "i", "ub", "us", "ui"):
    EXPORTS[f"glVertexAttrib4{kind}vARB"] = 8
    EXPORTS[f"glVertexAttrib4N{kind}vARB"] = 8
for kind in ("d", "f", "i"):
    EXPORTS[f"glGetVertexAttrib{kind}vARB"] = 12
assert len(EXPORTS) == 43
for filename in ("frontend.def", "icd.def"):
    exported = dict(re.findall(r"^\s+(\w+)=(\w+@\d+)$", (SOURCE / filename).read_text(), re.M))
    for name, size in EXPORTS.items():
        assert exported[name] == f"{name}@{size}", (filename, name)
    attrib = {name for name in exported if "VertexAttrib" in name}
    assert attrib == set(EXPORTS), attrib ^ set(EXPORTS)
cmake = (ROOT / "support/guest/cmake/opengl.cmake").read_text()
assert re.search(r"^set\(frontend_sources [^)]*\battrib\b", cmake, re.M)
internal = (SOURCE / "internal.h").read_text()
assert "BOOL JglVertexProgramAvailable(void);" in internal
# The host and frontend agree on the protocol.
protocol = (ROOT / "guest/include/gl.h").read_text()
assert "#define DG_CAP_GL_VERTEX_PROGRAM 0x400000" in protocol
assert protocol == (ROOT / "vendor/qemu/include/standard-headers/dreamgpu/gl.h").read_text()
bindings = (ROOT / "crates/dreamgpu-host/src/gl_api.rs").read_text()
for name in ("DG_GL_MAX_VERTEX_ATTRIBS", "DG_GL_ATTRIB_NORMALIZED", "DG_GL_ATTRIB_MASK",
             "DG_GL_ATTRIB_SLOTS", "DG_GL_ATTRIB_DESCRIPTOR_BYTES"):
    value = int(re.search(rf"#define {name} (\w+?)U?\b", protocol).group(1), 0)
    assert f"pub const {name}: u32 = {value};" in bindings, name
qemu = (ROOT / "vendor/qemu/hw/display/dreamgpu.c").read_text()
assert "DG_CAP_GL_VERTEX_PROGRAM" in qemu

with tempfile.TemporaryDirectory(prefix="dreamgpu-vertex-program-") as temporary:
    root = Path(temporary)
    (root / "GL").mkdir()
    for name in ("frontend.cpp", "arrays.cpp", "buffers.h", "query.cpp", "program.cpp", "attrib.cpp",
                 "compatibility.cpp", "icd-client.inc", "internal.h", "packing.h", "transport.h",
                 "scalar.inc", "names.h", "state.h"):
        shutil.copyfile(SOURCE / name, root / name)
    shutil.copyfile(HERE / "frontend.cpp", root / "frontend-harness.cpp")
    shutil.copyfile(HERE / "frontend-windows.h", root / "windows.h")
    shutil.copyfile(HERE / "frontend-gl.h", root / "GL/gl.h")
    shutil.copyfile(HERE / "vertex-program.cpp", root / "test.cpp")
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++23", "-O1", "-Wall", "-Wextra", "-Werror",
                    "-fno-exceptions", "-fno-rtti", "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all", "-g",
                    "-I" + str(root), "-I" + str(ROOT / "guest/include"),
                    "-I" + str(ROOT / "guest/nt/include"), str(root / "test.cpp"),
                    "-o", str(root / "test")], check=True)
    subprocess.run([str(root / "test")], check=True)
print("PASS vertex program exports: ARB names only, in frontend and ICD")

if platform.system() != "Linux" or platform.machine() not in ("x86_64", "i386", "i686"):
    raise SystemExit("Run the i686 execution half on an x86 Linux host.")
errors = {1: "x87 seed failed", 2: "wrong function/count/call count", 3: "record bits changed",
          4: "x87 status changed", 5: "null vector submitted"}
compilers = [os.environ["CXX"]] if "CXX" in os.environ else [
    name for name in ("g++", "clang++") if shutil.which(name)]
if not compilers:
    raise SystemExit("GCC or Clang is required")
# The freestanding shim adds the frontend services attrib.cpp references.
SHIM = """
typedef unsigned char GLboolean, GLubyte, BYTE;
typedef signed char GLbyte;
#define GL_INVALID_OPERATION 0x502
#define GL_FALSE 0
#define GL_BYTE 0x1400
#define GL_UNSIGNED_BYTE 0x1401
#define GL_SHORT 0x1402
#define GL_UNSIGNED_SHORT 0x1403
#define GL_INT 0x1404
#define GL_UNSIGNED_INT 0x1405
#define GL_FLOAT 0x1406
#define GL_DOUBLE 0x140a
typedef struct {
    GLint Size, Stride;
    GLenum Type;
    const void *Pointer;
    BOOL Enabled, Normalized;
    GLuint Buffer;
    ULONG Serial;
    BOOL Bgra;
} JGL_ARRAY;
#define JGL_BGRA 0x80e1
typedef struct {
    GLuint Name;
    ULONG Serial;
} JGL_BUFFER_BINDING;
typedef struct {
    JGL_ARRAY Generic[DG_GL_MAX_VERTEX_ATTRIBS];
    JGL_BUFFER_BINDING ArrayBuffer;
} JGL_ARRAY_STATE;
#ifdef __cplusplus
extern "C" {
#endif
JGL_ARRAY_STATE *JglArrays(void);
BOOL JglVertexBufferObjectAvailable(void);
BOOL JglVertexArrayBgraAvailable(void);
GLfloat JglColorComponent(const BYTE *value, GLenum type);
BOOL JglQuery(ULONG, const ULONG[3], ULONG, void *, ULONG, ULONG *);
#ifdef __cplusplus
}
#endif
"""
with tempfile.TemporaryDirectory(prefix="dreamgpu-vertex-program-i686-") as temporary:
    root = Path(temporary)
    for name in ("attrib.cpp", "packing.h"):
        shutil.copyfile(SOURCE / name, root / name)
    (root / "internal.h").write_text((HERE / "vector-internal.h").read_text() + SHIM)
    shutil.copyfile(HERE / "vertex-program-i686.cpp", root / "test.cpp")
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
            for suffix in ("f", "fv"):
                function = f"glVertexAttrib{count}{suffix}ARB"
                match = re.search(r"<" + function + r">:\n(.*?)(?=\n\n|\Z)", assembly, re.S)
                if not match or re.search(r"\b(?:fld|fst)[a-z]*\s", match.group(1)):
                    raise SystemExit(f"{compiler}: {function} uses floating-point loads/stores")
        print(f"PASS {compiler}: i686 generic attributes preserve bits and x87 status")
print("PASS vertex program frontend and i686 attribute packing")
