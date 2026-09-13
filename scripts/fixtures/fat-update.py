#!/usr/bin/env python3
"""Replace the Win9x driver pair in an offline disposable FAT32 qcow2 fixture.

Stop the fixture before invoking this command. qemu-img opens the source with
normal exclusive-consistency checks (never --force-share). The original image,
including its internal snapshots, is retained as a named backup; the updated
image is flattened. No guest registry or unrelated files are changed.
"""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile


def run(args):
    subprocess.run([str(arg) for arg in args], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--qemu-img", default="qemu-img")
    args = parser.parse_args()
    image = args.image.resolve()
    package = args.package.resolve()
    if not shutil.which("mcopy") or not shutil.which("mtype"):
        raise SystemExit("mtools is required for offline FAT32 updates")
    manifest = json.loads((package / "manifest.json").read_text())
    files = [package / name for name in ("dgpumini.drv", "dgpumini.vxd")]
    for file in files:
        digest = hashlib.sha256(file.read_bytes()).hexdigest()
        if digest != manifest["artifacts"][file.name]:
            raise SystemExit(f"package hash mismatch: {file}")
    from scripts.fixtures.windows_image import audit_vxd, audit_display_drv
    audit_vxd(files[1])
    audit_display_drv(files[0])
    backup = image.with_name(image.stem + ".before-" + package.name + image.suffix)
    if backup.exists():
        raise SystemExit(f"backup already exists: {backup}")
    metadata = json.loads(subprocess.check_output([args.qemu_img, "info", "--output=json", str(image)]))
    if metadata["format"] != "qcow2":
        raise SystemExit("expected a disposable qcow2 fixture")
    with tempfile.TemporaryDirectory(prefix="dg-fat-update-", dir=image.parent) as temporary:
        raw, updated = Path(temporary) / "disk.raw", Path(temporary) / "updated.qcow2"
        run([args.qemu_img, "convert", "-f", "qcow2", "-O", "raw", image, raw])
        with raw.open("rb") as file:
            mbr = file.read(512)
        partitions = [mbr[n:n + 16] for n in range(446, 510, 16)]
        matches = [p for p in partitions if p[4] in (0x0b, 0x0c, 0x1b, 0x1c)]
        if len(matches) != 1 or mbr[510:512] != b"\x55\xaa":
            raise SystemExit("exactly one FAT32 MBR partition required")
        offset = struct.unpack_from("<I", matches[0], 8)[0] * 512
        volume = f"{raw}@@{offset}"
        for file in files:
            destination = "::/WINDOWS/SYSTEM/" + file.name.upper()
            # Verify the file already exists: this utility updates an installed
            # matching pair and does not replace the guest's installation API.
            subprocess.check_output(["mtype", "-i", volume, destination])
            run(["mcopy", "-o", "-i", volume, file, destination])
            copied = subprocess.check_output(["mtype", "-i", volume, destination])
            if copied != file.read_bytes():
                raise SystemExit(f"FAT32 readback mismatch: {file.name}")
        run([args.qemu_img, "convert", "-f", "raw", "-O", "qcow2", raw, updated])
        run([args.qemu_img, "check", "-f", "qcow2", updated])
        os.rename(image, backup)
        try:
            os.rename(updated, image)
        except OSError:
            os.rename(backup, image)
            raise
    print(f"Updated {image}\nPreserved original fixture and snapshots: {backup}")


if __name__ == "__main__":
    main()
