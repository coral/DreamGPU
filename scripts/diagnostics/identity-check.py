#!/usr/bin/env python3
"""Correlate Win98 identity-probe requests with OS-derived VxD trace records.

This reports observed identities and lifecycle order. It does not assume that
an inherited/duplicated handle must work, or that a Windows PID equals a VMM
process token. A malformed trace or an unpaired successful request is rejected.
"""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import argparse
import json
from pathlib import Path
import re


def analyze(trace, logs):
    records = []
    previous = 0
    for line in trace.splitlines():
        if not line.startswith("DGID "):
            continue
        fields = dict(re.findall(r"\b(\w+)=([0-9a-f]{8})\b", line))
        record = {key: int(value, 16) for key, value in fields.items()}
        if not {"seq", "event", "vm", "context", "crit", "claims", "destroy"} <= record.keys():
            raise ValueError("incomplete VxD identity record")
        if record["seq"] <= previous or record["event"] not in (1, 2, 3, 4):
            raise ValueError("invalid trace sequence/event; do not interpret corrupted diagnostic output")
        previous = record["seq"]
        if record["event"] != 4:
            if not {"code", "tag", "handle", "param_vm", "in", "out"} <= record.keys():
                raise ValueError("incomplete VWIN32 request record")
            if record["event"] == 1 and record["code"] != 0:
                raise ValueError("OPEN record has unexpected control code")
            if record["event"] == 3 and record["code"] != 0xffffffff:
                raise ValueError("CLOSE record has unexpected control code")
        records.append(record)
    if not records:
        raise ValueError("no VxD identity records")
    requests = {}
    for record in records:
        if record["event"] != 2:
            continue
        if record["code"] & 0xffff0000 != 0x4a490000:
            raise ValueError("unexpected diagnostic control code")
        marker = record["code"] & 0xffff
        if marker in requests:
            raise ValueError(f"duplicate marker {marker:04x}; provide one isolated probe run")
        requests[marker] = record
    processes = []
    paired = set()
    markers = set()
    parent_text = None
    for path in logs:
        text = path.read_text()
        start = re.search(r"DG identity probe v\d+ pid=([0-9A-F]+) role=(\d+)", text)
        if not start:
            raise ValueError(f"missing process identity in {path}")
        pid, role = int(start[1], 16), int(start[2])
        if role == 0:
            parent_text = text
        process = {"role": role, "win32_pid": pid, "requests": []}
        pattern = r"REQUEST pid=([0-9A-F]+) handle=([0-9A-F]+) marker=([0-9A-F]+) ok=(\d+) error=(\d+) bytes=(\d+)"
        for match in re.finditer(pattern, text):
            marker, success = int(match[3], 16), bool(int(match[4]))
            if marker in markers:
                raise ValueError(f"duplicate user marker {marker:04x}; provide one isolated probe run")
            markers.add(marker)
            if int(match[1], 16) != pid:
                raise ValueError("request PID differs from its process log")
            if int(match[6]):
                raise ValueError("zero-payload identity request unexpectedly returned bytes")
            event = requests.get(marker)
            if success and event is None:
                raise ValueError(f"successful marker {marker:04x} has no driver record")
            if event:
                if event["in"] or event["out"]:
                    raise ValueError("identity probe unexpectedly supplied a data buffer")
                paired.add(marker)
            process["requests"].append({"marker": marker, "win32_handle": int(match[2], 16),
                                        "succeeded": success, "win32_error": int(match[5]),
                                        "driver": event})
        if role == 0 and "RESULT harness_failures=0;" not in text:
            raise ValueError("parent harness did not complete successfully")
        required = {0x101, 0x102, 0x120, 0x130, *range(0x300, 0x30c)} if role == 0 else {0x210 + role}
        if role in (1, 3):
            required.add(0x220 + role)
        successful = {request["marker"] for request in process["requests"] if request["succeeded"]}
        if not required <= successful:
            raise ValueError(f"role {role} did not complete its required independent-handle requests")
        processes.append(process)
    if sorted(process["role"] for process in processes) != [0, 1, 2, 3]:
        raise ValueError("one parent and all three child logs are required")
    if paired != set(requests):
        raise ValueError("driver requests are not fully paired with the supplied process logs")
    for process in processes:
        role = process["role"]
        if not role:
            continue
        created = re.search(rf"CREATE role={role} pid=([0-9A-F]+) inherited=([01])", parent_text)
        expected_exit = 77 if role == 2 else 0
        if (not created or int(created[1], 16) != process["win32_pid"] or
                int(created[2]) != (role != 3) or
                not re.search(rf"^EXIT role={role} code={expected_exit}$", parent_text, re.MULTILINE)):
            raise ValueError(f"role {role} creation/exit evidence does not match the intended scenario")
    identities = {}
    for process in processes:
        for request in process["requests"]:
            event = request["driver"]
            if not event:
                continue
            key = event["tag"], event["context"]
            identities.setdefault(key, set()).add(process["win32_pid"])
    return {
        "schema": 1,
        "trace_records": len(records),
        "paired_requests": len(paired),
        "processes": processes,
        "observed_process_keys": [{"tag": tag, "context": context, "win32_pids": sorted(pids)}
                                  for (tag, context), pids in sorted(identities.items())],
        "opens": [record for record in records if record["event"] == 1],
        "closes": [record for record in records if record["event"] == 3],
        "destroys": [record for record in records if record["event"] == 4],
        "claim_limit": "Observed VWIN32 identity/lifetime relationships only; no GL channel or window presentation claim",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trace", type=Path, required=True)
    parser.add_argument("--logs", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = analyze(args.trace.read_text(), [args.logs / f"DGID{i}.LOG" for i in range(4)])
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(f"Paired {result['paired_requests']} requests across {len(result['processes'])} processes")


if __name__ == "__main__":
    main()
