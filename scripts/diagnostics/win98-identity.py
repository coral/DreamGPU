#!/usr/bin/env python3
"""Correlate Win98 VWIN32 identity callbacks with source-built probe output.

This reads bounded text logs, not a guest screen or memory dump. The output is
an observation of this OS build; it does not promote caller-supplied IDs into
security authority or silently infer an unobserved process-exit relationship.
"""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import argparse
import hashlib
import json
import re
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def analyze(trace, logs):
    events = []
    for line in trace.splitlines():
        if not line.startswith("DGID "):
            continue
        fields = dict(re.findall(r"([a-z_]+)=([0-9a-fA-F]{8})\b", line))
        event = {key: int(value, 16) for key, value in fields.items()}
        if not {"seq", "event", "context", "destroy", "crit", "claims"} <= event.keys():
            raise ValueError("incomplete VxD record")
        if event["event"] not in (1, 2, 3, 4):
            raise ValueError("invalid event kind (possible callback ABI corruption)")
        if events and event["seq"] != events[-1]["seq"] + 1:
            raise ValueError("trace has missing, reordered or duplicate records")
        events.append(event)
    requests = []
    for log in logs:
        for line in log.splitlines():
            match = re.match(r"REQUEST pid=([0-9A-F]+) handle=([0-9A-F]+) marker=([0-9A-F]+) ok=(\d+)", line)
            if match:
                pid, handle, marker, ok = match.groups()
                requests.append(dict(pid=int(pid, 16), handle=int(handle, 16),
                                     marker=int(marker, 16), ok=bool(int(ok))))
    observed = []
    for request in requests:
        matched = [e for e in events if e["event"] == 2 and e.get("code") == 0x4a490000 | request["marker"]]
        if request["ok"] and len(matched) != 1:
            raise ValueError("successful request does not have exactly one VxD callback")
        observed.append(dict(request=request, callbacks=matched))
    failures = [line for log in logs for line in log.splitlines()
                if line.startswith("RESULT") and "harness_failures=0;" not in line]
    complete = any("RESULT harness_failures=0;" in log for log in logs)
    success = [entry for entry in observed if entry["request"]["ok"]]
    pids = {entry["request"]["pid"] for entry in success}
    destroyed = {e["destroy"] for e in events if e["event"] == 4}
    return dict(schema=1, status="observed" if complete and not failures else "incomplete",
                complete=complete, failures=failures, events=events, paired_requests=observed,
                successful_processes=len(pids),
                observed_exit_pids=sorted(pids & destroyed),
                missing_exit_pids=sorted(pids - destroyed),
                notes=["Process and handle fields are recorded separately; numeric equality alone does not prove lifetime.",
                       "No GL submission, clipping or CPU coherence acceptance is implied."])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trace", type=Path, required=True)
    parser.add_argument("--log", type=Path, action="append", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    for path in [args.trace, *args.log]:
        if path.stat().st_size > 4 * 1024 * 1024:
            parser.error(f"log exceeds 4 MiB bound: {path}")
    result = analyze(args.trace.read_text(), [p.read_text() for p in args.log])
    result["inputs"] = [{"path": str(p), "sha256": digest(p)} for p in [args.trace, *args.log]]
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({key: result[key] for key in ("status", "complete", "successful_processes", "failures")}))
    return 0 if result["complete"] and not result["failures"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
