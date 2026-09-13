#!/usr/bin/env python3
"""Actual client-array marshaller: bounds, topology, attributes and ownership."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix="jgl-arrays-") as temporary:
    root = Path(temporary)
    (root / "GL").mkdir()
    for name in ("arrays.cpp", "secondary.cpp", "packing.h", "internal.h"):
        shutil.copyfile((HERE.parents[2] / "guest/opengl") / name, root / name)
    shutil.copyfile(HERE / "arrays.cpp", root / "test.cpp")
    shutil.copyfile(HERE / "frontend-windows.h", root / "windows.h")
    shutil.copyfile(HERE / "frontend-gl.h", root / "GL/gl.h")
    command = [os.environ.get("CXX", "c++"), "-O2", "-std=c++23", "-fno-exceptions", "-fno-rtti", "-Wall", "-Wextra", "-Werror",
               "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g",
               "-I" + str(root), "-I" + str((HERE.parents[2] / "guest") / "include"),
               str(root / "test.cpp"), "-o", str(root / "test")]
    subprocess.run(command, check=True)
    subprocess.run([str(root / "test")], check=True)
