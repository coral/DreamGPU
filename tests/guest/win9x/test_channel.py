#!/usr/bin/env python3
"""Run production channel/window policy and real memory/packet/owner cores."""
import os
from pathlib import Path
import subprocess
import tempfile
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
with tempfile.TemporaryDirectory(prefix="dreamgpu-channel-") as temporary:
    out=Path(temporary)/"channel"
    common=[os.environ.get("CXX","clang++"),"-std=c++23","-fno-exceptions","-fno-rtti",
            "-Wall","-Wextra","-Werror","-I"+str(ROOT/"guest/include"),"-I"+str(ROOT/"guest/nt/include")]
    subprocess.run([*common,"-fsanitize=address,undefined","-fno-omit-frame-pointer","-g",
        str(HERE/"channel-test.cpp"),*[str(ROOT/"guest/win9x"/(unit+"32.cpp")) for unit in ("memory","packet","owner")],
        "-o",str(out)],check=True)
    subprocess.run([str(out)],check=True)
    subprocess.run([*common,"--analyze","-Xanalyzer","-analyzer-werror","-o",str(out)+".plist",
                    str(ROOT/"guest/win9x/channel32.cpp")],check=True)
