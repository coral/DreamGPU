#!/usr/bin/env python3
"""Exercise actual Win16 cursor capture and VxD transport with ASan/UBSan."""
import os
from pathlib import Path
import subprocess
import tempfile
HERE=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix="dg-cursor-") as temporary:
    output=Path(temporary)/"cursor-test"
    subprocess.run([os.environ.get("CC","cc"),"-O2","-std=c99","-Wall","-Wextra","-Werror",
        "-fsanitize=address,undefined","-fno-omit-frame-pointer","-g",
        "-I"+str((HERE.parents[2] / "guest")/"include"),"-c",str(HERE/"cursor-test.c"),"-o",str(output)+".o"],check=True)
    cpp = [os.environ.get("CXX", "clang++"), "-std=c++23", "-fno-exceptions", "-fno-rtti",
           "-Wall", "-Wextra", "-Werror", "-I"+str(HERE.parents[2]/"guest/include")]
    core = str(HERE.parents[2]/"guest/win9x/cursor32.cpp")
    subprocess.run([*cpp, "-fsanitize=address,undefined", "-g", core, str(output)+".o", "-o", str(output)], check=True)
    subprocess.run([str(output)],check=True)
    subprocess.run([*cpp, "--analyze", "-Xanalyzer", "-analyzer-werror", "-o", str(output)+".plist", core], check=True)
