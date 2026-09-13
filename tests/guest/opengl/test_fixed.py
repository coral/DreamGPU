#!/usr/bin/env python3
"""Actual fixed-function vectors and typed query wrappers under sanitizers."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
HERE = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix="jgl-fixed-") as temporary:
    root = Path(temporary)
    (root / "GL").mkdir()
    for name in ("fixed.cpp", "query.cpp", "internal.h"):
        shutil.copyfile((HERE.parents[2] / "guest/opengl") / name, root / name)
    shutil.copyfile(HERE / "fixed.cpp", root / "test.cpp")
    shutil.copyfile(HERE / "frontend-windows.h", root / "windows.h")
    shutil.copyfile(HERE / "frontend-gl.h", root / "GL/gl.h")
    command = [os.environ.get("CXX", "c++"), "-O2", "-std=c++23", "-fno-exceptions", "-fno-rtti", "-Wall", "-Wextra", "-Werror",
               "-fsanitize=address,undefined", "-g", "-I" + str(root),
               "-I" + str((HERE.parents[2] / "guest") / "include"), str(root / "test.cpp"), "-o", str(root / "test")]
    subprocess.run(command, check=True)
    subprocess.run([str(root / "test")], check=True)
