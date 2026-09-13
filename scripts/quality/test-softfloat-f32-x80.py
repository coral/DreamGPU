#!/usr/bin/env python3
"""Compile QEMU's actual softfloat source and test exact f32->x80 widening.

Uses an existing configured QEMU build for its platform headers/compiler flags;
never rebuilds or replaces any QEMU binary. Results and compile command are saved.
"""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=Path(__file__).resolve().parents[2] / "target/qemu-build")
    parser.add_argument("--output-dir", type=Path, default=Path("target/softfloat-f32-x80"))
    parser.add_argument("--sanitize", action="store_true")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    build = args.build_dir.resolve()
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    commands = json.loads((build / "compile_commands.json").read_text())
    entry = next(c for c in commands if c["file"].endswith("/fpu/softfloat.c"))
    original = entry.get("arguments") or shlex.split(entry["command"])
    command = []
    it = iter(original)
    for arg in it:
        if arg in ("-o", "-MF", "-MT", "-MQ"):
            next(it)
        elif arg in ("-c", "-MD", "-MMD", "-MP", "-fno-pie"):
            continue
        elif arg == entry["file"]:
            continue
        else:
            command.append(arg)
    command += ["-ffunction-sections", "-fdata-sections"]
    command += ["-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections"]
    if args.sanitize:
        command += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"]
    test = root / "scripts/tests/softfloat-f32-x80.c"
    binary = output / "test-softfloat-f32-x80"
    command += [str(test), "-o", str(binary)]
    command += shlex.split(subprocess.check_output(["pkg-config", "--libs", "glib-2.0"], text=True))
    command += ["-lm"]
    result = {"command": command, "cwd": entry["directory"], "sanitize": args.sanitize}
    for name, path in (("softfloat", root / "vendor/qemu/fpu/softfloat.c"), ("test", test)):
        result[name + "_sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
    (output / "build.json").write_text(json.dumps(result, indent=2) + "\n")
    subprocess.run(command, cwd=entry["directory"], check=True)
    run = subprocess.run([str(binary)], text=True, capture_output=True, timeout=120)
    (output / "stdout.txt").write_text(run.stdout)
    (output / "stderr.txt").write_text(run.stderr)
    if run.returncode:
        print(run.stderr, file=sys.stderr)
        raise SystemExit(run.returncode)
    result.update(json.loads(run.stdout))
    result["binary_sha256"] = hashlib.sha256(binary.read_bytes()).hexdigest()
    (output / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(run.stdout, end="")


if __name__ == "__main__":
    main()
