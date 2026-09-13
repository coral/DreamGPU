#!/usr/bin/env python3
"""Summarize one QEMU retro 2D device's scoped submit/work/complete trace log.

Enable dreamgpu_gpu_submit, dreamgpu_gpu_work and dreamgpu_gpu_complete
together. Add QEMU -msg timestamp=on for optional Perfetto export. Trace logging
adds overhead: these diagnostics are not benchmark or input-latency measurements.
"""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import argparse
from collections import Counter
from datetime import datetime
import hashlib
import json
import math
from pathlib import Path
import re

EVENT = re.compile(r"dreamgpu_gpu_(submit|work|complete)\s+(.*)")
FIELD = re.compile(r"([a-z_]+)=(\d+)")
STAMP = re.compile(r"\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d(?:\.\d+)?(?:Z|[+-]\d\d:\d\d)")
REQUIRED = {"submit": {"seq", "commands", "bytes", "inline_cap"},
            "work": {"seq", "bytes", "rows", "elapsed_us", "inline"},
            "complete": {"seq", "error", "chunks", "elapsed_us"}}


def distribution(values):
    values = sorted(values)
    if not values:
        return {"count": 0, "sum": 0, "mean": None, "p50": None, "p95": None, "p99": None, "max": None}
    return {"count": len(values), "sum": sum(values), "mean": sum(values) / len(values),
            **{f"p{q}": values[max(0, math.ceil(len(values) * q / 100) - 1)] for q in (50, 95, 99)},
            "max": values[-1]}


def parse(lines, start_line=1, end_line=None):
    batches, active, malformed = [], {}, []
    for number, line in enumerate(lines, 1):
        if number < start_line:
            continue
        if end_line is not None and number > end_line:
            break
        match = EVENT.search(line)
        if not match:
            continue
        kind, body = match.groups()
        fields = {key: int(value) for key, value in FIELD.findall(body)}
        if not REQUIRED[kind] <= fields.keys() or (kind == "work" and fields["inline"] not in (0, 1)):
            malformed.append(number)
            continue
        stamp = STAMP.search(line[:match.start()])
        timestamp = None
        if stamp:
            delta = datetime.fromisoformat(stamp[0].replace("Z", "+00:00")) - datetime.fromisoformat("1970-01-01T00:00:00+00:00")
            timestamp = (delta.days * 86400 + delta.seconds) * 1_000_000 + delta.microseconds
        event = {"kind": kind, "line": number, "timestamp_us": timestamp, **fields}
        sequence = fields["seq"]
        batch = active.get(sequence)
        # Sequence numbers can restart after a device reset. Never combine those
        # batches, or a repeated submit with an incomplete earlier capture.
        if batch is None or (kind == "submit" and batch["submit"] is not None):
            batch = {"sequence": sequence, "submit": None, "work": [], "complete": None}
            batches.append(batch)
            active[sequence] = batch
        if kind == "work":
            batch["work"].append(event)
        else:
            batch[kind] = event
        if kind == "complete":
            active.pop(sequence, None)
    return batches, malformed


def summarize(batches, malformed, tiny_bytes=4096):
    submissions = [batch["submit"] for batch in batches if batch["submit"]]
    completed = [batch for batch in batches if batch["complete"]]
    works = [event for batch in batches for event in batch["work"]]
    paired, inconsistent = [], []
    for batch in completed:
        if batch["submit"] is None:
            continue
        complete = batch["complete"]
        if complete["chunks"] != len(batch["work"]) or (not complete["error"] and sum(work["bytes"] for work in batch["work"]) != batch["submit"]["bytes"]):
            inconsistent.append(batch["sequence"])
            continue
        work_us = sum(work["elapsed_us"] for work in batch["work"])
        if complete["elapsed_us"] < work_us:
            inconsistent.append(batch["sequence"])
            continue
        paired.append((batch, work_us, complete["elapsed_us"] - work_us))
    tiny = [event for event in submissions if event["bytes"] <= tiny_bytes]
    total_bytes = sum(event["bytes"] for event in submissions)
    return {
        "schema_version": 1,
        "notes": ["Trace logging perturbs these measurements; do not compare them to tracing-disabled benchmarks.",
                  "Native elapsed spans device submission through completion before IRQ delivery; it excludes guest waiting after IRQ and presentation.",
                  "Non-work elapsed = complete elapsed minus work durations. It includes DMA, validation, logging and scheduling; these tracepoints cannot isolate scheduling or guest completion latency.",
                  "Only complete, internally consistent submit/work/complete groups contribute to paired durations.",
                  "Scope this file to one QEMU device; these tracepoints contain sequence numbers but no device identity."],
        "counts": {"submissions": len(submissions), "commands": sum(event["commands"] for event in submissions),
                   "completed": len(completed), "paired": len(paired),
                   "incomplete": sum(batch["complete"] is None for batch in batches),
                   "completion_without_submit": sum(batch["submit"] is None for batch in completed),
                   "work_chunks": len(works), "inline_chunks": sum(event["inline"] for event in works),
                   "continuation_chunks": sum(not event["inline"] for event in works),
                   "inline_only_batches": sum(all(work["inline"] for work in batch["work"]) for batch, _, _ in paired),
                   "continued_batches": sum(any(not work["inline"] for work in batch["work"]) for batch, _, _ in paired)},
        "bytes": {"submitted": total_bytes, "processed": sum(event["bytes"] for event in works),
                  "inline": sum(event["bytes"] for event in works if event["inline"]),
                  "continuation": sum(event["bytes"] for event in works if not event["inline"])},
        "rows": sum(event["rows"] for event in works),
        "inline_caps": dict(Counter(event["inline_cap"] for event in submissions)),
        "errors": dict(Counter(batch["complete"]["error"] for batch in completed if batch["complete"]["error"])),
        "tiny_batches": {"threshold_bytes": tiny_bytes, "count": len(tiny),
                         "submission_share": len(tiny) / len(submissions) if submissions else None,
                         "byte_share": sum(event["bytes"] for event in tiny) / total_bytes if total_bytes else None},
        "batch_bytes": distribution([event["bytes"] for event in submissions]),
        "native_elapsed_us": distribution([batch["complete"]["elapsed_us"] for batch, _, _ in paired]),
        "native_work_us": distribution([work_us for _, work_us, _ in paired]),
        "non_work_elapsed_us": distribution([gap for _, _, gap in paired]),
        "inline_chunk_us": distribution([event["elapsed_us"] for event in works if event["inline"]]),
        "continuation_chunk_us": distribution([event["elapsed_us"] for event in works if not event["inline"]]),
        "malformed_event_lines": malformed, "inconsistent_sequences": inconsistent,
    }


def perfetto(batches):
    events = []
    for batch in batches:
        for record in ([batch["complete"]] if batch["complete"] else []) + batch["work"]:
            if record["timestamp_us"] is None:
                raise ValueError("Perfetto export requires timestamps on every event; capture with QEMU -msg timestamp=on")
            events.append({"name": "native batch" if record["kind"] == "complete" else ("inline work" if record["inline"] else "continuation work"),
                           "cat": "qemu.retro.diagnostic", "ph": "X", "pid": 1,
                           "tid": 1 if record["kind"] == "complete" else 2,
                           "ts": record["timestamp_us"] - record["elapsed_us"],
                           "dur": record["elapsed_us"], "args": record})
    if events:
        origin = min(event["ts"] for event in events)
        for event in events:
            event["ts"] -= origin
    return {"displayTimeUnit": "us", "traceEvents": events,
            "metadata": {"warning": "Diagnostic trace with logging overhead; starts reconstructed from logged end time minus native elapsed."}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--start-line", type=int, default=1)
    parser.add_argument("--end-line", type=int)
    parser.add_argument("--tiny-bytes", type=int, default=4096)
    parser.add_argument("--json", type=Path, help="Write the machine-readable summary")
    parser.add_argument("--perfetto", type=Path, help="Export actual timestamped native events; requires QEMU -msg timestamp=on")
    args = parser.parse_args()
    if args.start_line < 1 or (args.end_line is not None and args.end_line < args.start_line) or args.tiny_bytes < 0:
        parser.error("Invalid line scope or tiny-batch threshold")
    with args.log.open(errors="replace") as source:
        batches, malformed = parse(source, args.start_line, args.end_line)
    summary = summarize(batches, malformed, args.tiny_bytes)
    with args.log.open("rb") as source:
        checksum = hashlib.file_digest(source, "sha256").hexdigest()
    summary["source"] = {"path": str(args.log.resolve()), "sha256": checksum,
                         "start_line": args.start_line, "end_line": args.end_line}
    if args.perfetto:
        try:
            trace = perfetto(batches)
        except ValueError as error:
            parser.error(str(error))
        args.perfetto.write_text(json.dumps(trace) + "\n")
    output = json.dumps(summary, indent=2) + "\n"
    if args.json:
        args.json.write_text(output)
    print(output, end="")


if __name__ == "__main__":
    main()
