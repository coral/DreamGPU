#!/usr/bin/env python3
"""Extract bounded FAT32 diagnostic files from a paused disposable guest.

The explicit QMP endpoint/device is exported read-only over a temporary local
NBD socket. The prior run state is restored, including on extraction failure.
Only 8.3 guest paths are supported; this never mounts or writes a guest disk.
"""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import argparse
from pathlib import Path
import socket
import struct
import tempfile

from scripts.benchmarks.bench import qmp_execute


class Reader:
    def __init__(self, endpoint):
        self.socket = socket.socket(socket.AF_UNIX)
        self.socket.settimeout(10)
        self.socket.connect(endpoint)
        self.handle = 0
        if self.receive(16) != b"NBDMAGICIHAVEOPT":
            raise ValueError("NBD fixed-newstyle handshake required")
        flags = struct.unpack(">H", self.receive(2))[0]
        if flags & 3 != 3:
            raise ValueError("NBD fixed-newstyle/no-zeroes flags required")
        self.socket.sendall(struct.pack(">I", 3))
        self.socket.sendall(b"IHAVEOPT" + struct.pack(">II", 1, 5) + b"guest")
        self.size, flags = struct.unpack(">QH", self.receive(10))
        if not flags & 2:
            raise ValueError("diagnostic export must be read-only")

    def receive(self, size):
        data = bytearray()
        while len(data) < size:
            chunk = self.socket.recv(size - len(data))
            if not chunk:
                raise EOFError("short NBD reply")
            data.extend(chunk)
        return bytes(data)

    def read(self, offset, size):
        if offset < 0 or size < 0 or size > 1024 * 1024 or offset + size > self.size:
            raise ValueError("NBD read out of bounds")
        self.handle += 1
        self.socket.sendall(struct.pack(">IHHQQI", 0x25609513, 0, 0, self.handle, offset, size))
        magic, error, handle = struct.unpack(">IIQ", self.receive(16))
        if magic != 0x67446698 or handle != self.handle or error:
            raise OSError(error, "invalid NBD read response")
        return self.receive(size)

    def close(self):
        self.socket.close()


def u16(data, offset):
    return struct.unpack_from("<H", data, offset)[0]


def u32(data, offset):
    return struct.unpack_from("<I", data, offset)[0]


class Fat32:
    def __init__(self, reader):
        self.reader = reader
        mbr = reader.read(0, 512)
        if mbr[510:512] != b"\x55\xaa":
            raise ValueError("invalid MBR")
        partitions = [mbr[n:n + 16] for n in range(446, 510, 16)]
        matches = [p for p in partitions if p[4] in (0x0b, 0x0c, 0x1b, 0x1c)]
        if len(matches) != 1:
            raise ValueError("exactly one FAT32 MBR partition required")
        base = u32(matches[0], 8) * 512
        boot = reader.read(base, 512)
        sector, clusters = u16(boot, 11), boot[13]
        if sector not in (512, 1024, 2048, 4096) or not clusters or clusters & (clusters - 1):
            raise ValueError("invalid FAT32 geometry")
        self.cluster_size = sector * clusters
        if self.cluster_size > 65536 or boot[16] not in (1, 2) or u16(boot, 17):
            raise ValueError("unsupported FAT32 geometry")
        self.fat = base + sector * u16(boot, 14)
        self.data = self.fat + sector * boot[16] * u32(boot, 36)
        self.root = u32(boot, 44)

    def chain(self, cluster):
        seen = set()
        while 2 <= cluster < 0x0ffffff8:
            if cluster in seen or len(seen) >= 32768:
                raise ValueError("invalid or oversized FAT32 chain")
            seen.add(cluster)
            yield self.reader.read(self.data + (cluster - 2) * self.cluster_size, self.cluster_size)
            cluster = u32(self.reader.read(self.fat + cluster * 4, 4), 0) & 0x0fffffff

    def entries(self, cluster):
        for block in self.chain(cluster):
            for offset in range(0, len(block), 32):
                entry = block[offset:offset + 32]
                if not entry[0]:
                    return
                if entry[0] == 0xe5 or entry[11] == 15 or entry[11] & 8:
                    continue
                name, extension = entry[:8].decode("cp437").strip(), entry[8:11].decode("cp437").strip()
                yield (name + ("." + extension if extension else ""),
                       u16(entry, 26) | u16(entry, 20) << 16, u32(entry, 28), entry[11])

    def file(self, path):
        cluster = self.root
        parts = path.replace("\\", "/").strip("/").split("/")
        for index, part in enumerate(parts):
            if not part or part in (".", ".."):
                raise ValueError("invalid guest path")
            match = next((e for e in self.entries(cluster) if e[0] == part.upper()), None)
            if match is None:
                raise FileNotFoundError(path)
            _, cluster, size, flags = match
            if index < len(parts) - 1 and not flags & 16:
                raise NotADirectoryError(path)
        if flags & 16 or size > 16 * 1024 * 1024:
            raise ValueError("expected diagnostic file no larger than 16 MiB")
        result = bytearray()
        for block in self.chain(cluster):
            result.extend(block[:size - len(result)])
            if len(result) == size:
                break
        if len(result) != size:
            raise ValueError("short FAT32 file chain")
        return bytes(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qmp", required=True)
    parser.add_argument("--device", required=True, help="explicit QEMU block device, e.g. ide0-hd0")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("files", nargs="+", help="guest 8.3 paths, e.g. BOOTLOG.TXT WINDOWS/SYSTEM/DGPUMINI.VXD")
    args = parser.parse_args()
    def execute(name, params=None):
        return qmp_execute(args.qmp, [(name, params or {})])[0]
    running = execute("query-status")["running"]
    started, reader = False, None
    with tempfile.TemporaryDirectory(prefix="dreamgpu-fat-") as temporary:
        endpoint = str(Path(temporary) / "read.sock")
        try:
            if running:
                execute("stop")
            execute("nbd-server-start", {"addr": {"type": "unix", "data": {"path": endpoint}}})
            started = True
            execute("nbd-server-add", {"device": args.device, "name": "guest", "writable": False})
            reader = Reader(endpoint)
            fat = Fat32(reader)
            args.output.mkdir(parents=True, exist_ok=True)
            for filename in args.files:
                destination = args.output / filename.replace("\\", "/").split("/")[-1]
                destination.write_bytes(fat.file(filename))
                print(destination)
        finally:
            if reader:
                reader.close()
            try:
                if started:
                    execute("nbd-server-stop")
            finally:
                if running:
                    execute("cont")


if __name__ == "__main__":
    main()
