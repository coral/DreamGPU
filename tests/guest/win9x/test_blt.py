#!/usr/bin/env python3
"""Execute and analyze production freestanding 2D packet preparation."""
import os
from pathlib import Path
import subprocess
import tempfile
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
with tempfile.TemporaryDirectory(prefix="dreamgpu-blt-") as temporary:
    out=Path(temporary)/"blt"
    common=[os.environ.get("CXX","clang++"),"-std=c++23","-fno-exceptions","-fno-rtti",
            "-Wall","-Wextra","-Werror","-I"+str(ROOT/"guest/include")]
    core=str(ROOT/"guest/win9x/blt32.cpp")
    subprocess.run([*common,"-fsanitize=address,undefined","-g",core,str(HERE/"blt-test.cpp"),"-o",str(out)],check=True)
    subprocess.run([str(out)],check=True)
    subprocess.run([*common,"--analyze","-Xanalyzer","-analyzer-werror","-o",str(out)+".plist",core],check=True)
