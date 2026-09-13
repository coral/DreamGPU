#!/usr/bin/env python3
"""Inventory pinned Wine9x core GL call sites against real DLL exports.

This is a source inventory, not a reachability proof or compatibility result.
Extension slots and version-dependent branches require separate capability
analysis before advertising support.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--wine9x", type=Path, default=ROOT / "vendor/wine9x")
    parser.add_argument("--output", type=Path)
    parser.add_argument("--openglide", type=Path, default=ROOT / "vendor/qemu-xtra/openglide")
    args = parser.parse_args()
    source = args.wine9x / "wined3d"
    if not source.is_dir():
        parser.error(f"Missing pinned source directory: {source}")
    exports = set(re.findall(r"^\s+(\w+)=", (ROOT / "guest/opengl/frontend.def").read_text(), re.M))
    sites = {}
    hashes = {}
    for file in sorted(source.glob("*.c")):
        content = file.read_bytes()
        hashes[file.name] = hashlib.sha256(content).hexdigest()
        for number, line in enumerate(content.decode(errors="replace").splitlines(), 1):
            for name in set(re.findall(r"gl_ops\.gl\.p_(gl\w+)\b", line)):
                sites.setdefault(name, []).append(f"wined3d/{file.name}:{number}")
    data = {"schema": 1, "scope": "Explicit core gl_ops.gl.p_* call sites in pinned Wine9x wined3d/*.c; not extension slots or reachability",
            "exports": sorted(exports), "called_core_functions": len(sites),
            "implemented_called_core": sorted(set(sites) & exports),
            "missing_called_core": {name: sites[name] for name in sorted(set(sites) - exports)},
            "source_sha256": hashes,
            "compatibility": "Not a working D3D claim. GL version remains 0.0; no unimplemented extensions advertised."}
    glide = {}
    if args.openglide.is_dir():
        for file in sorted(args.openglide.glob("*.cpp")):
            for number, line in enumerate(file.read_text(errors="replace").splitlines(),1):
                for name in set(re.findall(r"\b((?:gl|wgl)[A-Z]\w+)\s*\(",line)):
                    if not name.startswith("glX"):
                        glide.setdefault(name,[]).append(f"{file.name}:{number}")
        data["openglide"] = {"scope": "Common top-level C++ source call sites; optional stats code remains in inventory",
            "missing": {name: glide[name] for name in sorted(set(glide)-exports)},
            "implemented": sorted(set(glide)&exports),
            "required_extensions": ["GL_EXT_packed_pixels","GL_EXT_abgr","GL_EXT_bgra"]}
    encoded = json.dumps(data, indent=2) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded)
    else:
        print(encoded, end="")
    print(f"{len(sites)} called core functions; {len(data['implemented_called_core'])} exported; {len(data['missing_called_core'])} missing")

if __name__ == "__main__":
    main()
