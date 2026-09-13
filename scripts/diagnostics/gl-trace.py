#!/usr/bin/env python3
"""Capture and summarize bounded QEMU GL batch diagnostics, without guest input.

Start the fixture with QEMU -D /absolute/qemu.trace (log trace backend), then:
  python3 scripts/diagnostics/gl-trace.py capture --qmp /tmp/fixture.qmp \
    --log /absolute/qemu.trace --seconds 5 --output /tmp/gl-batches.log
  python3 scripts/diagnostics/gl-trace.py summarize /tmp/gl-batches.log

Capture writes a new raw log and adjacent .json summary; neither is overwritten.
Use one retro GPU device per log. Capture is separate from tracing-disabled
timedemos: logging and clock reads perturb the workload. No commands start,
stop, resume or send input to the guest. Four trace events must be disabled
before capture, and all attempted enables are disabled again in finally.
SIGINT/SIGTERM also take this cleanup path; SIGKILL/host failure cannot do so.
"""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import argparse
from collections import Counter
import hashlib
import json
import math
import os
from pathlib import Path
import re
import signal
import socket
import stat
import sys
import time

EVENTS = tuple("dreamgpu_gl_" + kind for kind in ("submit", "work", "complete", "reject"))
EVENT = re.compile(r"\bdreamgpu_gl_(submit|work|complete|reject)\s+(.*)$")
FIELDS = {
    "submit": {"seq", "records", "bytes", "prepare_us"},
    "work": {"seq", "records", "bytes", "queue_us", "execute_us", "error"},
    "complete": {"seq", "error", "elapsed_us"},
}
MAX_BYTES = 32 * 1024 * 1024
MAX_LINE = 4096
MAX_EVENTS = 300_000
MAX_JOBS = 100_000
NOTES = [
    "Diagnostic logging perturbs execution; compare performance only in separate tracing-disabled timedemos.",
    "Scope the log to one retro GPU: events contain job sequences but no device identity.",
    "prepare_us spans DMA copy, validation and enqueue; queue_us starts at worker enqueue. These intervals overlap.",
    "execute_us covers the worker record loop, including any waits inside records; it is not GPU execution time.",
    "total_us spans device submit entry through completion before IRQ delivery; it excludes guest marshalling, later guest IRQ/DPC handling and presentation.",
    "Do not sum or subtract prepare/queue/execute to infer phases of total. Microsecond rounding and tracing boundaries differ.",
    "Validation rejections can precede submission; zero executed-work errors do not prove an error-free command stream.",
    "Only complete, consistent jobs enter duration distributions. Partial capture edges, pre-enqueue failures and reset cancellation remain visible in counts.",
]


class TraceError(Exception):
    pass


def distribution(values):
    values = sorted(values)
    return {"count": len(values), "mean": sum(values) / len(values) if values else None,
            "p50": values[math.ceil(len(values) * .50) - 1] if values else None,
            "p95": values[math.ceil(len(values) * .95) - 1] if values else None,
            "max": values[-1] if values else None}


def summarize(source, max_bytes=MAX_BYTES, max_events=MAX_EVENTS, max_jobs=MAX_JOBS):
    """Read binary lines, bounding input, event count, line size and join memory."""
    active, jobs, malformed = {}, [], []
    rejections = Counter()
    read_bytes = event_count = line_number = 0
    digest = hashlib.sha256()
    while True:
        line = source.readline(MAX_LINE + 1)
        if not line:
            break
        line_number += 1
        read_bytes += len(line)
        if read_bytes > max_bytes:
            raise TraceError("Log exceeds byte limit")
        if len(line) > MAX_LINE:
            raise TraceError(f"Log line {line_number} exceeds {MAX_LINE} bytes")
        digest.update(line)
        match = EVENT.search(line.decode("utf-8", errors="replace").rstrip("\r\n"))
        if not match:
            continue
        event_count += 1
        if event_count > max_events:
            raise TraceError("Log exceeds event limit")
        kind, body = match.groups()
        if kind == "reject":
            fields = {}
            for item in body.split():
                pair = re.fullmatch(r"([a-z_0-9]+)=(0x[0-9a-fA-F]{1,8}|[0-9]{1,10})", item)
                if not pair or pair[1] in fields:
                    break
                fields[pair[1]] = int(pair[2], 16 if pair[2].startswith("0x") else 10)
            else:
                required = {"seq", "execution", "op", "fn", "a0", "a1", "a2", "a3", "error"}
                if (fields.keys() in (required, required | {"a4", "a5", "a6", "a7"}) and
                        all(value <= 0xffffffff for value in fields.values()) and
                        fields["execution"] in (0, 1) and fields["error"]):
                    rejections[(fields["execution"], fields["op"], fields["fn"], fields["a0"], fields["error"])] += 1
                    continue
            malformed.append(line_number)
            continue
        fields = {}
        for item in body.split():
            pair = re.fullmatch(r"([a-z_]+)=([0-9]{1,20})", item)
            if not pair or pair[1] in fields:
                break
            fields[pair[1]] = int(pair[2])
        else:
            if (fields.keys() == FIELDS[kind] and
                    all(value <= (0xffffffff if key in ("seq", "records", "error") else 0xffffffffffffffff)
                        for key, value in fields.items()) and
                    (kind == "complete" or fields["records"] > 0)):
                sequence = fields["seq"]
                job = active.get(sequence)
                # The worker can finish before the BQL thread logs submit.
                # A repeated event cannot belong to the same immutable job.
                if job is not None and kind in job:
                    job["ambiguous"] = True
                    active.pop(sequence)
                    job = None
                    ambiguous = True
                else:
                    ambiguous = False
                if job is None:
                    if len(jobs) >= max_jobs:
                        raise TraceError("Log exceeds job limit")
                    job = {"sequence": sequence, "first_line": line_number,
                           "ambiguous": ambiguous}
                    jobs.append(job)
                    active[sequence] = job
                job[kind] = fields
                if kind == "complete":
                    active.pop(sequence)
                continue
        malformed.append(line_number)

    paired, inconsistent = [], []
    for job in jobs:
        if not all(kind in job for kind in FIELDS):
            continue
        submit, work, complete = (job[kind] for kind in FIELDS)
        if (job["ambiguous"] or submit["records"] != work["records"] or
                submit["bytes"] != work["bytes"] or work["error"] != complete["error"]):
            inconsistent.append({"sequence": job["sequence"], "first_line": job["first_line"]})
        else:
            paired.append(job)
    submits = [job["submit"] for job in jobs if "submit" in job]
    completes = [job["complete"] for job in jobs if "complete" in job]
    return {
        "schema_version": 1, "notes": NOTES,
        "source": {"bytes": read_bytes, "sha256": digest.hexdigest()},
        "counts": {"events": event_count, "jobs": len(jobs), "submissions": len(submits),
                   "completions": len(completes), "paired": len(paired),
                   "partial": sum(not all(kind in job for kind in FIELDS) for job in jobs),
                   "completion_without_submit": sum("complete" in job and "submit" not in job for job in jobs),
                   "inconsistent": len(inconsistent), "ambiguous_jobs": sum(job["ambiguous"] for job in jobs),
                   "malformed": len(malformed)},
        "totals": {"submitted_bytes": sum(event["bytes"] for event in submits),
                   "submitted_records": sum(event["records"] for event in submits)},
        "errors": dict(sorted(Counter(event["error"] for event in completes if event["error"]).items())),
        "rejections": {"total": sum(rejections.values()),
                       "validation": sum(count for key, count in rejections.items() if not key[0]),
                       "execution": sum(count for key, count in rejections.items() if key[0]),
                       "by_command": [{"execution": bool(execution), "op": op, "function": function,
                           "argument0": argument0, "error": error, "count": count}
                           for (execution, op, function, argument0, error), count in sorted(rejections.items())]},
        "batch_bytes": distribution([event["bytes"] for event in submits]),
        "batch_records": distribution([event["records"] for event in submits]),
        "prepare_us": distribution([job["submit"]["prepare_us"] for job in paired]),
        "queue_us": distribution([job["work"]["queue_us"] for job in paired]),
        "execute_us": distribution([job["work"]["execute_us"] for job in paired]),
        "total_us": distribution([job["complete"]["elapsed_us"] for job in paired]),
        "malformed_event_lines": malformed, "inconsistent_jobs": inconsistent,
    }


class Qmp:
    """Small bounded QMP client; unsolicited events never count as replies."""
    def __init__(self, path, timeout=2.0):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.timeout, self.buffer, self.sequence = timeout, bytearray(), 0
        try:
            self.sock.settimeout(timeout)
            self.sock.connect(str(path))
            if "QMP" not in self.receive(time.monotonic() + timeout):
                raise TraceError("Missing QMP greeting")
            self.command("qmp_capabilities")
        except BaseException:
            self.close()
            raise

    def close(self):
        self.sock.close()

    def receive(self, deadline):
        while True:
            if time.monotonic() >= deadline:
                raise TraceError("QMP response timed out")
            if b"\n" in self.buffer:
                line, _, self.buffer = self.buffer.partition(b"\n")
                response = json.loads(line)
                if not isinstance(response, dict):
                    raise TraceError("Invalid QMP response")
                return response
            if len(self.buffer) >= 1024 * 1024:
                raise TraceError("QMP response exceeds 1 MiB")
            self.sock.settimeout(max(.001, deadline - time.monotonic()))
            chunk = self.sock.recv(4096)
            if not chunk:
                raise TraceError("QMP disconnected")
            self.buffer.extend(chunk)

    def command(self, name, arguments=None):
        self.sequence += 1
        request = {"execute": name, "arguments": arguments or {}, "id": self.sequence}
        self.sock.settimeout(self.timeout)
        self.sock.sendall(json.dumps(request).encode() + b"\n")
        deadline = time.monotonic() + self.timeout
        while True:
            response = self.receive(deadline)
            if response.get("id") != self.sequence:
                continue
            if "error" in response:
                raise TraceError(f"QMP {name}: {response['error']}")
            if "return" not in response:
                raise TraceError(f"QMP {name}: missing result")
            return response["return"]


def capture(qmp, log, output, seconds=5.0, max_bytes=MAX_BYTES,
            clock=time.monotonic, sleep=time.sleep):
    if not math.isfinite(seconds) or not .1 <= seconds <= 30:
        raise TraceError("Capture duration must be between 0.1 and 30 seconds")
    if not 1 <= max_bytes <= MAX_BYTES:
        raise TraceError(f"Capture byte bound must be between 1 and {MAX_BYTES}")
    for name in EVENTS:
        states = qmp.command("trace-event-get-state", {"name": name})
        if not isinstance(states, list) or len(states) != 1 or states[0].get("name") != name or states[0].get("state") != "disabled":
            raise TraceError(f"{name} must exist and be disabled; an existing trace is left untouched")
    enabled, copied = [], 0
    with Path(log).open("rb") as source:
        initial = os.fstat(source.fileno())
        if not stat.S_ISREG(initial.st_mode):
            raise TraceError("QEMU -D log must be a regular file")
        source.seek(0, 2)
        start = source.tell()

        def drain():
            nonlocal copied
            current = Path(log).stat()
            if (current.st_dev, current.st_ino) != (initial.st_dev, initial.st_ino) or current.st_size < source.tell():
                raise TraceError("QEMU trace log rotated or was truncated during capture")
            available = current.st_size - source.tell()
            if copied + available > max_bytes:
                raise TraceError("Capture reached its byte limit; trace disabled, partial raw log retained")
            while available:
                chunk = source.read(min(65536, available))
                if not chunk:
                    raise TraceError("QEMU log was truncated while reading")
                output.write(chunk)
                copied += len(chunk)
                available -= len(chunk)

        started = clock()
        try:
            for name in EVENTS:
                # Include an enable whose acknowledgement fails: it may have
                # taken effect before a broken connection or interruption.
                enabled.append(name)
                qmp.command("trace-event-set-state", {"name": name, "enable": True})
            deadline = clock() + seconds
            while clock() < deadline:
                drain()
                sleep(min(.05, max(0, deadline - clock())))
        finally:
            failures = []
            for name in enabled:
                try:
                    qmp.command("trace-event-set-state", {"name": name, "enable": False})
                except BaseException as error:
                    failures.append(f"{name}: {error}")
            if failures:
                raise TraceError("Failed to disable tracing; check QMP event state: " + "; ".join(failures))
        drain()
        output.flush()
        return {"requested_seconds": seconds, "elapsed_seconds_including_control": clock() - started,
                "source_log": str(Path(log).resolve()), "start_byte": start,
                "end_byte": source.tell(), "events_disabled": True}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    take = commands.add_parser("capture", help="Capture from an already running fixture with explicit -D logfile")
    take.add_argument("--qmp", type=Path, required=True)
    take.add_argument("--log", type=Path, required=True)
    take.add_argument("--output", type=Path, required=True, help="New raw capture file; adjacent .json summary is also created")
    take.add_argument("--seconds", type=float, default=5)
    take.add_argument("--max-bytes", type=int, default=MAX_BYTES)
    read = commands.add_parser("summarize")
    read.add_argument("log", type=Path)
    read.add_argument("--json", type=Path, help="Write new summary file")
    args = parser.parse_args()
    try:
        metadata = None
        if args.command == "capture":
            output_json = Path(str(args.output) + ".json")
            if args.output.exists() or output_json.exists():
                raise TraceError("Capture output or adjacent .json already exists")
            if not args.log.is_file():
                raise TraceError("Pass the existing regular logfile configured with QEMU -D")
            def interrupted(*_):
                raise KeyboardInterrupt
            previous_term = signal.signal(signal.SIGTERM, interrupted)
            try:
                qmp = Qmp(args.qmp)
                try:
                    with args.output.open("xb") as output:
                        metadata = capture(qmp, args.log, output, args.seconds, args.max_bytes)
                finally:
                    qmp.close()
            finally:
                signal.signal(signal.SIGTERM, previous_term)
            source_path = args.output
        else:
            source_path, output_json = args.log, args.json
        with source_path.open("rb") as source:
            report = summarize(source)
        report["source"]["path"] = str(source_path.resolve())
        if metadata is not None:
            report["capture"] = metadata
        text = json.dumps(report, indent=2) + "\n"
        if output_json:
            with output_json.open("x") as destination:
                destination.write(text)
        print(text, end="")
        return 0
    except (TraceError, OSError, ValueError, KeyboardInterrupt) as error:
        print(f"gl-trace: {error or 'interrupted; trace cleanup attempted'}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
