#!/usr/bin/env python3
"""Package built NT drivers into a deterministic FAT12 test-install floppy."""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import argparse
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--driver-dir", type=Path, default=ROOT / "target/guest/nt")
    parser.add_argument("--output", type=Path, default=ROOT / "target/guest/nt-drivers.img")
    parser.add_argument("--probe", type=Path, help="Optional DGPUPROB.EXE to include")
    args = parser.parse_args()
    files = [(name.upper(), (args.driver_dir / name).read_bytes())
             for name in ["dreamgpu.inf", "dgpumini.sys", "dgpudisp.dll", "dginst.exe", "dgdiag.exe", "COPYING.txt"]]
    if args.probe:
        files.append(("DGPUPROB.EXE", args.probe.read_bytes()))
    image = bytearray(1440 * 1024)
    image[:11] = b"\xeb\x3c\x90DREAMGPU"
    struct.pack_into("<HBHBHHBHHHII", image, 11, 512, 1, 1, 2, 224, 2880, 0xf0, 9, 18, 2, 0, 0)
    image[38] = 0x29
    struct.pack_into("<I", image, 39, 0x4a524731)
    image[43:54] = b"DREAMGPU   "
    image[54:62] = b"FAT12   "
    # Same source-built chain-loader as benchmarks/probes/boot.asm. Its only
    # purpose is booting the hard disk if BIOS tries this data floppy first.
    boot = bytes.fromhex("fa31c08ed88ec08ed0bc007cfcbe007cbf0006b90001f3a5ea5b060000fbbd030031c0b280cd13b80102bb007cb90100ba8000cd1373054d75e7eb0f813efe7d55aa7507b280ea007c000031c0cd16cd19ebf8")
    image[62:62 + len(boot)] = boot
    image[510:512] = b"\x55\xaa"
    fat = bytearray(9 * 512)
    fat[:3] = b"\xf0\xff\xff"
    cluster = 2
    for index, (name, data) in enumerate(files):
        sectors = (len(data) + 511) // 512
        if cluster - 2 + sectors > 2880 - 33:
            raise SystemExit("driver package exceeds floppy capacity")
        first = cluster if sectors else 0
        for offset in range(0, len(data), 512):
            chunk = data[offset:offset + 512]
            address = (33 + cluster - 2) * 512
            image[address:address + len(chunk)] = chunk
            value = 0xfff if offset + 512 >= len(data) else cluster + 1
            entry = cluster * 3 // 2
            word = int.from_bytes(fat[entry:entry + 2], "little")
            word = ((word & 15) | value << 4) if cluster & 1 else ((word & 0xf000) | value)
            fat[entry:entry + 2] = word.to_bytes(2, "little")
            cluster += 1
        stem, extension = name.rsplit(".", 1)
        if len(stem) > 8 or len(extension) > 3:
            raise SystemExit(f"not an 8.3 filename: {name}")
        entry = 19 * 512 + index * 32
        image[entry:entry + 11] = (stem.ljust(8) + extension.ljust(3)).encode("ascii")
        image[entry + 11] = 0x20
        # Fixed valid FAT date, 2026-09-11, for reproducible media.
        struct.pack_into("<H", image, entry + 24, (2026 - 1980) << 9 | 9 << 5 | 11)
        struct.pack_into("<HI", image, entry + 26, first, len(data))
    image[512:5120] = fat
    image[5120:9728] = fat
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(image)
    print(args.output)


if __name__ == "__main__":
    main()
