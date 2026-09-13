"""Read-only validation of legacy Windows NE/LE executable structure."""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import struct

def audit_vxd(path):
    """Reject LE packages Windows will refuse before the mini-VDD entry runs."""
    data = path.read_bytes()
    try:
        header = struct.unpack_from("<I", data, 0x3c)[0]
        if data[:2] != b"MZ" or data[header:header + 2] != b"LE":
            raise ValueError("not an MZ/LE image")
        def field(offset):
            return struct.unpack_from("<I", data, header + offset)[0]
        entry = header + field(0x5c)
        # Ordinal 1 is the device descriptor, never an OS/2 16-bit call gate.
        if data[entry] != 1 or data[entry + 1] != 3:
            raise ValueError("ordinal 1 DDB must be a 32-bit LE export (type 3)")
        obj = struct.unpack_from("<H", data, entry + 2)[0]
        offset = struct.unpack_from("<I", data, entry + 5)[0]
        if not 1 <= obj <= field(0x44):
            raise ValueError("DDB object out of bounds")
        size, base, flags, first_page, pages, _ = struct.unpack_from(
            "<6I", data, header + field(0x40) + (obj - 1) * 24)
        page_size = field(0x28)
        if not page_size or offset + 80 > size or not flags & 0x2000:
            raise ValueError("invalid DDB extent or non-32-bit object")
        page = first_page - 1 + offset // page_size
        page_entry = header + field(0x48) + page * 4
        file_page = int.from_bytes(data[page_entry:page_entry + 3], "big")
        physical = field(0x80) + (file_page - 1) * page_size + offset % page_size
        ddb = data[physical:physical + 80]
        if len(ddb) != 80 or ddb[:4] != bytes(4) or ddb[12:20] != b"DREAMGPU":
            raise ValueError("ordinal 1 does not identify the QEMU device descriptor")
        resident = header + field(0x58)
        if data[resident:resident + 10] != b"\x08DREAMGPU\0":
            raise ValueError("LE module name must match the DREAMGPU device descriptor")
        if struct.unpack_from("<H", ddb, 4)[0] != 0x400:
            raise ValueError("unexpected mini-VDD DDK version")
    except (ValueError, IndexError, struct.error) as error:
        raise SystemExit(f"invalid Win9x VxD {path}: {error}")


def audit_display_drv(path):
    """Catch unresolved NE selectors before Windows silently uses VGA."""
    data = path.read_bytes()
    try:
        header = struct.unpack_from("<I", data, 0x3c)[0]
        if data[:2] != b"MZ" or data[header:header + 2] != b"NE":
            raise ValueError("not an MZ/NE image")
        segments, modules = struct.unpack_from("<HH", data, header + 0x1c)
        table = header + struct.unpack_from("<H", data, header + 0x22)[0]
        shift = struct.unpack_from("<H", data, header + 0x32)[0]
        if shift > 16 or not segments:
            raise ValueError("invalid segment alignment or count")
        for index in range(segments):
            sector, size, flags, _ = struct.unpack_from("<4H", data, table + index * 8)
            if not flags & 0x100:
                continue
            relocation = (sector << shift) + (size or 65536)
            count = struct.unpack_from("<H", data, relocation)[0]
            for entry in range(count):
                _, kind, offset, target, value = struct.unpack_from(
                    "<BBHHH", data, relocation + 2 + entry * 8)
                if offset >= (size or 65536):
                    raise ValueError("relocation source outside its segment")
                if kind & 3 == 0 and target != 255 and not 1 <= target <= segments:
                    raise ValueError(f"relocation targets missing segment {target} (image has {segments})")
                if kind & 3 in (1, 2) and not 1 <= target <= modules:
                    raise ValueError("relocation targets missing import module")
    except (ValueError, IndexError, struct.error) as error:
        raise SystemExit(f"invalid Win9x display driver {path}: {error}")
