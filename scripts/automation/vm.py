#!/usr/bin/env python3
"""Control a disposable retro GPU guest directly over its explicit QMP socket."""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import argparse
import json
from pathlib import Path
import re
import time

from scripts.benchmarks.bench import qmp_execute, qmp_screenshot


NT_DIAGNOSTICS = (
    "magic", "version", "interrupts", "dpcs", "irq_level", "irq_vector",
    "irq_mode", "gdi_sequence", "gdi_timeouts", "gdi_wait_status", "gdi_device_status",
    "assigned_irq_level", "assigned_irq_vector", "isr_entries", "device_extension",
    "transport", "isr_handler", "unload_hook", "process_notify_registered", "last_pid",
    "live_clients", "process_exit_closes", "explicit_closes", "gl_sequence", "gl_timeouts",
    "gl_host_error", "kernel_requestor_mode", "kernel_previous_mode", "rejected_user_requests",
    "desktop_fault_status", "desktop_seeds", "desktop_returns", "desktop_presents",
    "cohere_synchronize_surface", "cohere_cpu_destination", "cohere_cpu_source",
    "cohere_surface_teardown", "cohere_mode_change",
    "cursor_shapes", "cursor_moves", "cursor_errors", "cursor_capability",
    "gl_signature_hits", "gl_signature_misses", "gl_signature_invalidations",
)


def dg_device(buses):
    """Require one assigned retro GPU, including devices behind PCI bridges."""
    matches = []

    def visit(devices):
        for device in devices:
            if device.get("id", {}).get("vendor") == 0x1234 and device["id"].get("device") == 0x1113:
                matches.append(device)
            visit(device.get("pci_bridge", {}).get("devices", []))

    for bus in buses:
        visit(bus["devices"])
    if len(matches) != 1:
        raise ValueError(f"Expected exactly one retro GPU, found {len(matches)}")
    bars = [bar for bar in matches[0]["regions"] if bar["bar"] == 2 and
            bar["type"] == "memory" and bar["size"] >= 8192 and bar["address"] > 0]
    if len(bars) != 1:
        raise ValueError("Retro GPU BAR2 is not assigned")
    return matches[0], bars[0]["address"]


def parse_physical_words(output, address, count):
    """Reject monitor errors, truncated reads, and unexpected address strides."""
    words = []
    for line in output.splitlines():
        match = re.fullmatch(r"([0-9a-fA-F]+):\s*((?:0x[0-9a-fA-F]{8}\s*)+)", line.strip())
        if not match or int(match[1], 16) != address + len(words) * 4:
            raise ValueError(f"Invalid physical-memory response: {line!r}")
        words.extend(int(word, 16) for word in match[2].split())
    if len(words) != count:
        raise ValueError(f"Expected {count} physical words, received {len(words)}")
    return words


def nt_diagnostics(endpoint):
    """Read the driver's DMA diagnostic page without a fixed guest address.

    This is a live observation, not an atomic snapshot or a pass/fail verdict.
    It never pauses the guest, changes registers, or runs during timed captures.
    """
    device, bar = dg_device(qmp_execute(endpoint, [("query-pci", {})])[0])

    def read(address, count):
        output = qmp_execute(endpoint, [("human-monitor-command", {
            "command-line": f"xp /{count}wx 0x{address:x}"})])[0]
        return parse_physical_words(output, address, count)

    registers = read(bar + 0x1000, 18)
    if registers[0] != 0x47524a51:
        raise ValueError(f"Unexpected retro GPU register magic: {registers[0]:08x}")
    batch_address = registers[4] | registers[5] << 32
    if not batch_address or batch_address & 4095:
        raise ValueError("NT driver DMA page is not initialized or aligned")
    words = read(batch_address + 0xc00, len(NT_DIAGNOSTICS))
    if words[:2] != [0x4744494a, 1]:
        raise ValueError("NT driver diagnostic page has unknown magic/version")
    return {
        "schema": 1, "observed_unix_ns": time.time_ns(), "atomic": False,
        "pci": {key: device[key] for key in ("bus", "slot", "function")},
        "bar2": bar, "dma_page": batch_address,
        "diagnostics": dict(zip(NT_DIAGNOSTICS, words)),
        "device_registers": registers,
    }


def key(endpoint, value):
    codes = value.split("+")
    qmp_execute(endpoint, [("send-key", {
        "keys": [{"type": "qcode", "data": code} for code in codes], "hold-time": 20})])
    # This helper drives legacy setup dialogs; latency benchmarks use their
    # own timestamped injection. Keep makes/breaks visible to Win9x's keyboard.
    time.sleep(0.06)


def type_text(endpoint, value):
    punctuation = {" ": "spc", "\\": "backslash", "/": "slash", ".": "dot", ",": "comma",
                   ":": "shift+semicolon", ";": "semicolon", "-": "minus", "_": "shift+minus",
                   "=": "equal", "+": "shift+equal", '"': "shift+apostrophe", "'": "apostrophe",
                   ">": "shift+dot", "<": "shift+comma", "|": "shift+backslash",
                   "(": "shift+9", ")": "shift+0", "\n": "ret"}
    for char in value:
        if char in punctuation:
            code = punctuation[char]
        elif char.isascii() and char.isalnum():
            code = ("shift+" if char.isupper() else "") + char.lower()
        else:
            raise ValueError(f"Unsupported character: {char!r}")
        key(endpoint, code)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--qmp", required=True, help="Socket of the disposable guest")
    actions = parser.add_subparsers(dest="action", required=True)
    actions.add_parser("status")
    actions.add_parser("screen").add_argument("path", type=Path)
    actions.add_parser("key").add_argument("keys", nargs="+")
    actions.add_parser("type").add_argument("text")
    actions.add_parser("run").add_argument("command")
    actions.add_parser("monitor").add_argument("command")
    diagnostics = actions.add_parser("nt-diagnostics", help="Read NT driver IRQ/GL counters via QMP")
    diagnostics.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.action == "status":
        print(json.dumps(qmp_execute(args.qmp, [("query-status", {})]), indent=2))
    elif args.action == "screen":
        args.path.write_bytes(qmp_screenshot(args.qmp))
    elif args.action == "key":
        for value in args.keys:
            key(args.qmp, value)
    elif args.action == "type":
        type_text(args.qmp, args.text)
    elif args.action == "run":
        key(args.qmp, "ctrl+esc")
        time.sleep(0.5)
        key(args.qmp, "r")
        time.sleep(0.5)
        type_text(args.qmp, args.command)
        key(args.qmp, "ret")
    elif args.action == "monitor":
        print(qmp_execute(args.qmp, [("human-monitor-command", {"command-line": args.command})])[0])
    elif args.action == "nt-diagnostics":
        result = json.dumps(nt_diagnostics(args.qmp), indent=2) + "\n"
        if args.output:
            args.output.write_text(result)
        else:
            print(result, end="")


if __name__ == "__main__":
    main()
