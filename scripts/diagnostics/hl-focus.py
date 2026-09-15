#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""One retail fullscreen diagnostic, with visibility and composed-frame evidence.

Requires a ready private fixture with DGFOCUS.EXE staged as E:\\DGDRV.EXE.
The existing serial ntupdate command runs that helper and exports its log. This
does not install a driver despite the historical serial command name. No image
is used to choose input; the guest helper owns all game input and phase timing.
"""
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import argparse
import base64
from concurrent.futures import ThreadPoolExecutor, wait
import hashlib
import json
from pathlib import Path
import time

from scripts.benchmarks.bench import WebSocket
from scripts.benchmarks.game import ensure_host_visible
from scripts.benchmarks.halflife import run_demo


def run(fixture, output):
    state = json.loads((fixture / "run.json").read_text())
    if state["state"] != "ready":
        raise ValueError("A ready private fixture is required")
    output.mkdir(parents=True, exist_ok=False)
    record = {"fixture": str(fixture), "scope": "fullscreen recovery, not performance",
              "controller_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
              "host_visibility": {}, "samples": []}
    ws = WebSocket(state["ws"])
    try:
        ensure_host_visible(ws, state["pid"], record["host_visibility"])
        began = time.monotonic()
        with ThreadPoolExecutor(max_workers=1) as pool:
            work = pool.submit(run_demo, state["serial"], "ntupdate", output / "guest",
                               timeout=110, probe=True)
            while True:
                sample = {"seconds": time.monotonic() - began,
                          "host": ws.command("get_window_state", "window_state")}
                for kind, command in (("raw", "get_screenshot"),
                                      ("rendered", "get_rendered_screenshot")):
                    try:
                        image = ws.command(command, "screenshot", vm_id=state["machine"])
                    except RuntimeError as error:
                        # Raw CPU storage can be unavailable while the guest
                        # owns an accelerated drawable. Keep collecting the
                        # composed output and preserve the exact observation.
                        sample[kind] = {"error": str(error)}
                        continue
                    name = f"{len(record['samples']):03d}-{kind}.png"
                    (output / name).write_bytes(base64.b64decode(image["png_base64"]))
                    sample[kind] = {"width": image["width"], "height": image["height"],
                                    "path": name}
                record["samples"].append(sample)
                (output / "capture.json").write_text(json.dumps(record, indent=2) + "\n")
                if work.done():
                    record["guest"] = work.result()
                    error_log = record["guest"].get("error", {}).get("raw_text")
                    if error_log:
                        (output / "guest" / "engine-output.txt").write_text(error_log)
                    break
                # Sleep until either the helper completes or the next evidence
                # interval; no redundant capture after its completion.
                wait([work], timeout=5)
    finally:
        ws.close()
        (output / "capture.json").write_text(json.dumps(record, indent=2) + "\n")
    return record


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    result = run(args.fixture.resolve(), args.output.resolve())
    print(json.dumps({"samples": len(result["samples"]), "guest": result.get("guest")},
                     indent=2))
    # This status reports helper completion only; composed-frame evidence is
    # still required to establish fullscreen graphics recovery.
    raise SystemExit(0 if result.get("guest", {}).get("state") == "completed" else 1)
