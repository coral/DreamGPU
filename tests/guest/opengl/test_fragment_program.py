#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""DG_CAP_GL_FRAGMENT_PROGRAM: actual frontend contract under sanitizers, the
exported entry points of both the private frontend and the ICD, and the i686
float parameter wrappers' exact bits and x87 status."""
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

# ARB names only, with stdcall argument sizes; no NV or vertex-program names.
EXPORTS = {"glGenProgramsARB": 8, "glDeleteProgramsARB": 8, "glBindProgramARB": 8,
           "glIsProgramARB": 4, "glProgramStringARB": 16, "glGetProgramivARB": 12,
           "glGetProgramStringARB": 12}
for kind in ("Env", "Local"):
    for suffix, size in (("4f", 24), ("4fv", 12), ("4d", 40), ("4dv", 12)):
        EXPORTS[f"glProgram{kind}Parameter{suffix}ARB"] = size
    for suffix in ("fv", "dv"):
        EXPORTS[f"glGetProgram{kind}Parameter{suffix}ARB"] = 12
for filename in ("frontend.def", "icd.def"):
    exported = dict(re.findall(r"^\s+(\w+)=(\w+@\d+)$", (SOURCE / filename).read_text(), re.M))
    for name, size in EXPORTS.items():
        assert exported[name] == f"{name}@{size}", (filename, name)
    program = {name for name in exported if "Program" in name}
    assert program == set(EXPORTS), program ^ set(EXPORTS)
cmake = (ROOT / "support/guest/cmake/opengl.cmake").read_text()
assert re.search(r"^set\(frontend_sources [^)]*\bprogram\b", cmake, re.M)
internal = (SOURCE / "internal.h").read_text()
assert "BOOL JglFragmentProgramAvailable(void);" in internal
# The host and frontend agree on the protocol bounds.
protocol = (ROOT / "guest/include/gl.h").read_text()
assert "#define DG_CAP_GL_FRAGMENT_PROGRAM 0x200000" in protocol
assert protocol == (ROOT / "vendor/qemu/include/standard-headers/dreamgpu/gl.h").read_text()
bindings = (ROOT / "crates/dreamgpu-host/src/gl_api.rs").read_text()
for name in ("DG_GL_MAX_PROGRAM_BYTES", "DG_GL_MAX_PROGRAMS", "DG_GL_PROGRAM_NAME_BATCH"):
    value = int(re.search(rf"#define {name} (\w+)", protocol).group(1), 0)
    assert f"pub const {name}: u32 = {value};" in bindings, name

with tempfile.TemporaryDirectory(prefix="dreamgpu-fragment-program-") as temporary:
    root = Path(temporary)
    (root / "GL").mkdir()
    for name in ("frontend.cpp", "arrays.cpp", "query.cpp", "program.cpp", "compatibility.cpp",
                 "internal.h", "packing.h", "transport.h", "scalar.inc", "names.h", "state.h"):
        shutil.copyfile(SOURCE / name, root / name)
    shutil.copyfile(HERE / "frontend.cpp", root / "frontend-harness.cpp")
    shutil.copyfile(HERE / "frontend-windows.h", root / "windows.h")
    shutil.copyfile(HERE / "frontend-gl.h", root / "GL/gl.h")
    shutil.copyfile(HERE / "fragment-program.cpp", root / "test.cpp")
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++23", "-O1", "-Wall", "-Wextra", "-Werror",
                    "-fno-exceptions", "-fno-rtti", "-fsanitize=address,undefined",
                    "-fno-sanitize-recover=all", "-g",
                    "-I" + str(root), "-I" + str(ROOT / "guest/include"),
                    "-I" + str(ROOT / "guest/nt/include"), str(root / "test.cpp"),
                    "-o", str(root / "test")], check=True)
    subprocess.run([str(root / "test")], check=True)
print("PASS fragment program exports: ARB names only, in frontend and ICD")

if platform.system() != "Linux" or platform.machine() not in ("x86_64", "i386", "i686"):
    raise SystemExit("Run the i686 execution half on an x86 Linux host.")
errors = {1: "x87 seed failed", 2: "wrong function/count/call count", 3: "record bits changed",
          4: "x87 status changed", 5: "null vector submitted", 6: "invalid target submitted"}
compilers = [os.environ["CXX"]] if "CXX" in os.environ else [
    name for name in ("g++", "clang++") if shutil.which(name)]
if not compilers:
    raise SystemExit("GCC or Clang is required")
# The freestanding shim adds the frontend services program.cpp references.
SHIM = """
#ifdef __cplusplus
extern "C" {
#endif
typedef unsigned char GLboolean, BYTE;
#define GL_INVALID_OPERATION 0x502
#define GL_FALSE 0
#define JGL_FRAGMENT_PROGRAM 0x8804
#define JGL_VERTEX_PROGRAM 0x8620
#define CopyMemory(d, s, n) __builtin_memcpy(d, s, n)
BOOL JglFragmentProgramAvailable(void);
BOOL JglQuery(ULONG, const ULONG[3], ULONG, void *, ULONG, ULONG *);
ULONG JglReadbackCapacity(void);
ULONG *JglReadbackBuffer(void);
#ifdef __cplusplus
}
#endif
"""
with tempfile.TemporaryDirectory(prefix="dreamgpu-fragment-program-i686-") as temporary:
    root = Path(temporary)
    for name in ("program.cpp", "packing.h"):
        shutil.copyfile(SOURCE / name, root / name)
    (root / "internal.h").write_text((HERE / "vector-internal.h").read_text() + SHIM)
    shutil.copyfile(HERE / "fragment-program-i686.cpp", root / "test.cpp")
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
        for kind in ("Env", "Local"):
            for suffix in ("4f", "4fv"):
                function = f"glProgram{kind}Parameter{suffix}ARB"
                match = re.search(r"<" + function + r">:\n(.*?)(?=\n\n|\Z)", assembly, re.S)
                if not match or re.search(r"\b(?:fld|fst)[a-z]*\s", match.group(1)):
                    raise SystemExit(f"{compiler}: {function} uses floating-point loads/stores")
        print(f"PASS {compiler}: i686 fragment program parameters preserve bits and x87 status")
print("PASS fragment program frontend and i686 parameter packing")
