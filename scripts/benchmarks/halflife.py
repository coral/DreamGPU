#!/usr/bin/env python3
"""Run a Half-Life timedemo through the guest serial runner, without UI polling.

QEMU fixture: -chardev socket,id=bench,path=/tmp/hl-bench,server=on,wait=off
              -serial chardev:bench
Guest: start the source-built serial runner once, with its validated game flags.
Host: halflife.py run dgperf --socket /tmp/hl-bench --output /tmp/run-01

One request at a time. PING/READY then RUN/STARTED/RESULT or ERROR; no QMP,
screenshots or guest input during timing. STARTED means CreateProcess succeeded,
not that timedemo rendering began. Host latency is distinct from guest FPS.
Exact engine console bytes and optional input manifest are retained in the new output
directory. Unrecognized result text is reported without inventing an FPS value.
"""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import argparse
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path, PureWindowsPath
import re
import secrets
import socket
import subprocess
import sys
import time

MAX_RESULT_BYTES = 65536
MAX_LINE = MAX_RESULT_BYTES * 2 + 128
MAX_MESSAGES = 128
IDENTIFIER = re.compile(r"[A-Za-z0-9_-]{1,64}\Z")
NUMBER = r"[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?"


def demo_name(value):
    """Accept a demo basename, optionally ending in .dem, without commands."""
    name = value[:-4] if value.lower().endswith(".dem") else value
    if not re.fullmatch(r"[A-Za-z0-9_][A-Za-z0-9_-]{0,63}", name):
        raise argparse.ArgumentTypeError("use a demo basename such as dgperf or dgperf.dem")
    return name


def launch_command(demo, mode="timedemo", executable=r"C:\SIERRA\Half-Life\hl.exe"):
    demo = demo_name(demo)
    if mode not in ("timedemo", "playdemo"):
        raise ValueError("mode must be timedemo or playdemo")
    if (not PureWindowsPath(executable).is_absolute() or
            PureWindowsPath(executable).suffix.lower() != ".exe" or
            any(char in executable for char in '\r\n\0"')):
        raise ValueError("executable must be an absolute Windows .exe path")
    return subprocess.list2cmdline([
        executable, "-windowed", "-toconsole", "-condebug", "-nosound", "-dev", "-gl",
        "-gldrv", "dgpugl.dll", "+" + mode, demo,
    ])


class ProtocolError(Exception):
    pass


class GuestError(ProtocolError):
    def __init__(self, code, raw=None):
        super().__init__("guest error: " + code)
        self.raw = raw
        self.code = code


def decode_hex(payload):
    if not payload or len(payload) > MAX_RESULT_BYTES * 2 or len(payload) % 2 or not re.fullmatch(r"[0-9A-Fa-f]+", payload):
        raise ProtocolError("expected bounded hex engine output")
    return bytes.fromhex(payload)


def parse_result(raw):
    """Recognize labelled timedemo results; raw bytes remain authoritative."""
    text = raw.decode("latin-1")
    matches = list(re.finditer(rf"(?<![\w.+-])({NUMBER})\s+frames\s*[,;]?\s*({NUMBER})\s+seconds?\s*[,;]?\s*({NUMBER})\s+fps\b", text, re.I))
    if matches:
        fields = dict(zip(("frames", "seconds", "fps"), map(float, matches[-1].groups())))
        if all(math.isfinite(value) and value > 0 for value in fields.values()) and fields["frames"].is_integer():
            fields["frames"] = int(fields["frames"])
            return {"parse_status": "labelled_summary", **fields}
        return {"parse_status": "invalid_summary", "fps": None}
    matches = list(re.finditer(rf"(?<![\w.+-])({NUMBER})\s+fps\b|\bfps\s*[:=]\s*({NUMBER})\b", text, re.I))
    if matches:
        value = float(next(item for item in matches[-1].groups() if item is not None))
        if math.isfinite(value) and value > 0:
            return {"parse_status": "labelled_fps", "fps": value}
    return {"parse_status": "unrecognized", "fps": None}


class Serial:
    def __init__(self, connection):
        self.connection, self.buffer, self.messages = connection, bytearray(), 0

    def send(self, text, timeout):
        self.connection.settimeout(timeout)
        self.connection.sendall(text.encode("ascii") + b"\n")

    def receive(self, request_id, deadline):
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("serial response deadline expired")
            if b"\n" not in self.buffer:
                if len(self.buffer) > MAX_LINE:
                    raise ProtocolError("serial line exceeds bounded result size")
                self.connection.settimeout(remaining)
                chunk = self.connection.recv(min(4096, MAX_LINE + 1 - len(self.buffer)))
                if not chunk:
                    raise ProtocolError("serial connection closed before result")
                self.buffer.extend(chunk)
                continue
            line, _, self.buffer = self.buffer.partition(b"\n")
            if len(line) > MAX_LINE:
                raise ProtocolError("serial line exceeds bounded result size")
            self.messages += 1
            if self.messages > MAX_MESSAGES:
                raise ProtocolError("too many serial messages; stale IDs cannot extend the deadline")
            try:
                parts = line.rstrip(b"\r").decode("ascii").split(" ", 3)
            except UnicodeDecodeError as error:
                raise ProtocolError("non-ASCII serial protocol") from error
            if (len(parts) < 2 or parts[0] not in ("READY", "IDENTITY", "INSTANCE", "INSTALLING", "STARTED", "PROCESS_READY", "PROCESS_RESUMED", "CONSOLE_OBSERVED", "TIMEDEMO_RESULT", "MEASURING", "MEASURED", "RESULT", "ERROR") or
                    not IDENTIFIER.fullmatch(parts[1]) or
                    len(parts) not in ({"RESULT": (3,), "IDENTITY": (3,), "INSTANCE": (3,), "ERROR": (3, 4)}.get(parts[0], (2,)))):
                raise ProtocolError("malformed serial response")
            if parts[1] != request_id:
                continue
            if parts[0] == "ERROR":
                if not IDENTIFIER.fullmatch(parts[2]):
                    raise ProtocolError("malformed guest error code")
                raise GuestError(parts[2], decode_hex(parts[3]) if len(parts) == 4 else None)
            return parts[0], parts[2] if len(parts) == 3 else None


PROBES = ("hldebug", "win9xinstall", "win9xdiag", "dual", "lifecycle", "arrays", "windows", "modes", "win98", "loader", "setup98", "update98", "d3d6", "d3d7", "d3d8", "d3d9", "glide", "utsetup", "utglide", "utlogs", "utdsetup", "utd3d", "ntupdate", "ntdiag")


def parse_probe(raw, name="arrays"):
    lines = raw.decode('latin-1').splitlines()
    if any(line.startswith('FAIL') for line in lines) or not any(
            line.startswith(f'PASS automated {name}:') for line in lines):
        raise ProtocolError('probe did not return its required pixel/lifecycle PASS record')
    return {'probe': name, 'passed': True}


def run_demo(endpoint, demo, output, manifest=None, ready_timeout=30, start_timeout=30, timeout=180, *, probe=False, on_phase=None, sampler=None):
    demo = demo_name(demo)
    if probe and demo not in PROBES:
        raise ValueError('Unknown fixed public API probe')
    for value, limit in ((ready_timeout, 120), (start_timeout, 120), (timeout, 900)):
        if not math.isfinite(value) or not 0 < value <= limit:
            raise ValueError("timeouts must be positive and bounded (ready/start≤120s, result≤900s)")
    provenance = None
    if manifest:
        with Path(manifest).open("rb") as source:
            manifest_bytes = source.read(1024 * 1024 + 1)
        if len(manifest_bytes) > 1024 * 1024:
            raise ValueError("input manifest exceeds 1 MiB")
        provenance = {"path": str(Path(manifest).resolve()),
                      "sha256": hashlib.sha256(manifest_bytes).hexdigest(),
                      "data": json.loads(manifest_bytes)}
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    if manifest:
        (output / "manifest-input.json").write_bytes(manifest_bytes)
    report = {"schema_version": 1, "request_id": secrets.token_hex(16), "demo": demo,
              "workload_kind": "public_api_probe" if probe else "timedemo",
              "socket": str(endpoint), "started_utc": datetime.now(timezone.utc).isoformat(),
              "state": "connecting", "manifest": provenance, "latency_seconds": {},
              "timeouts_seconds": {"ready": ready_timeout, "start": start_timeout, "result": timeout},
              "notes": ["STARTED acknowledges process creation, not the first timedemo frame.",
                        "Host wall latency is not the guest's timedemo FPS.",
                        "A host timeout does not cancel a running guest job; wait for its bounded cleanup before another run."]}
    began = time.monotonic()
    report["host_monotonic_seconds"] = {"request_begin": began}
    if not probe:
        report["engine_interval"] = {"status": "unknown", "reason": "Retail console exposes elapsed timedemo duration but no timestamped start/end frame events."}
    try:
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
            connection.settimeout(ready_timeout)
            connection.connect(str(endpoint))
            serial = Serial(connection)
            request_id = report["request_id"]
            report["state"] = "await_ready"
            serial.send(f"PING {request_id}", ready_timeout)
            if serial.receive(request_id, time.monotonic() + ready_timeout)[0] != "READY":
                raise ProtocolError("expected READY for this request")
            ready = time.monotonic()
            report["host_monotonic_seconds"]["ready_observed"] = ready
            report["latency_seconds"]["ready"] = ready - began
            report["state"] = "await_started"
            submitted = time.monotonic()
            report["host_monotonic_seconds"]["request_sent"] = submitted
            serial.send(f"{'PROBE' if probe else 'RUN'} {request_id} {demo}", start_timeout)
            if serial.receive(request_id, submitted + start_timeout)[0] != "STARTED":
                raise ProtocolError("expected STARTED before a result")
            started = time.monotonic()
            report["host_monotonic_seconds"]["process_created_observed"] = started
            report["latency_seconds"]["start_after_run"] = started - submitted
            report["state"] = "await_result"
            phases=[]
            launch_phases=[]
            while True:
                kind, payload = serial.receive(request_id, started + timeout)
                if kind in ("PROCESS_READY", "PROCESS_RESUMED", "CONSOLE_OBSERVED", "TIMEDEMO_RESULT"):
                    expected = ("PROCESS_READY", "PROCESS_RESUMED", "CONSOLE_OBSERVED", "TIMEDEMO_RESULT")
                    if probe or len(launch_phases) >= len(expected) or kind != expected[len(launch_phases)]:
                        raise ProtocolError("unexpected or duplicate timedemo boundary")
                    observed = time.monotonic()
                    launch_phases.append(kind)
                    event = {"host_monotonic_seconds": observed, "after_started_seconds": observed-started}
                    report.setdefault("timedemo_boundaries", {})[kind] = event
                    try:
                        if sampler: sampler.phase(kind, observed)
                        if on_phase: on_phase(kind)
                    except Exception as error:
                        event["callback_error"] = str(error)
                    if kind == "PROCESS_READY":
                        command = "ABORT" if "callback_error" in event else "CONTINUE"
                        event["ack_sent_monotonic_seconds"] = time.monotonic()
                        serial.send(f"{command} {request_id}", 5)
                        event["ack"] = command
                    elif kind == "TIMEDEMO_RESULT":
                        # Cleanup is released even if sampler shutdown failed.
                        event["ack_sent_monotonic_seconds"] = time.monotonic()
                        serial.send(f"OBSERVED {request_id}", 5)
                    continue
                if kind not in ("MEASURING","MEASURED"):break
                if not probe or demo not in ("utd3d","utglide","lifecycle","dual") or kind != ("MEASURING" if not phases else "MEASURED") or len(phases)>=2:
                    raise ProtocolError("unexpected or duplicate measurement phase")
                phases.append(kind)
                report.setdefault("measurement_phases",{})[kind]={"after_started_seconds":time.monotonic()-started}
                if on_phase:
                    try:on_phase(kind)
                    except Exception as error:report["measurement_phases"][kind]["callback_error"]=str(error)
                if kind=="MEASURING" and demo=="dual" and on_phase and "callback_error" not in report["measurement_phases"][kind]:
                    serial.send(f"MINIMIZED {request_id}",5)
                if kind=="MEASURED":serial.send(f"CAPTURED {request_id}",5)
            if kind != "RESULT":
                raise ProtocolError("expected bounded hex RESULT after STARTED")
            if launch_phases and launch_phases[-1] != "TIMEDEMO_RESULT":
                raise ProtocolError("result preceded timedemo summary observation")
            completed = time.monotonic()
            report["host_monotonic_seconds"]["result_received"] = completed
            if not probe:
                report["engine_interval"]["observed_boundaries_available"] = bool(launch_phases)
                if launch_phases:
                    report["engine_interval"]["enclosing_host_window"] = {
                        "start": report["timedemo_boundaries"]["PROCESS_READY"]["ack_sent_monotonic_seconds"],
                        "end": report["timedemo_boundaries"]["TIMEDEMO_RESULT"]["host_monotonic_seconds"],
                        "meaning": "CONTINUE sent through completed engine summary observed; includes launch, loading and console visibility delay."}
            raw = decode_hex(payload)
            (output / "engine-output.txt").write_bytes(raw)
            report.update(state="completed", result=parse_probe(raw, demo) if probe else parse_result(raw),
                          raw_text=raw.decode("latin-1"), result_bytes=len(raw),
                          result_sha256=hashlib.sha256(raw).hexdigest())
            report["latency_seconds"].update(completion_after_run=completed - submitted,
                                            completion_after_started=completed - started)
    except (OSError, ProtocolError, KeyboardInterrupt) as error:
        report.update(failed_state=report["state"], state="error",
                      error={"type": type(error).__name__, "message": str(error) or "interrupted"})
        if isinstance(error, GuestError) and error.raw is not None:
            (output / "engine-error.txt").write_bytes(error.raw)
            report["error"].update(raw_text=error.raw.decode("latin-1"),
                                   sha256=hashlib.sha256(error.raw).hexdigest())
    finally:
        if sampler:
            try: report["cpu_sample"] = sampler.finish(report.get("timedemo_boundaries", {}))
            except Exception as error: report["cpu_sample"] = {"error": str(error), "coverage_proven": False}
        report["latency_seconds"]["host_total"] = time.monotonic() - began
        (output / "run.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    commands = parser.add_subparsers(dest="command", required=True)
    command = commands.add_parser("command", help="Print a manual launch command only")
    command.add_argument("demo", type=demo_name)
    command.add_argument("--mode", choices=("timedemo", "playdemo"), default="timedemo")
    command.add_argument("--exe", default=r"C:\SIERRA\Half-Life\hl.exe")
    run = commands.add_parser("run", help="One serial-controlled timedemo; no UI/QMP polling")
    run.add_argument("demo", type=demo_name)
    probe = commands.add_parser("probe", help="Run a fixed public API correctness probe over serial")
    probe.add_argument("demo", choices=PROBES)
    for command in (run, probe):
        command.add_argument("--socket", type=Path, required=True)
        command.add_argument("--output", type=Path, help="New result directory; default: unique directory under target/runs")
        command.add_argument("--manifest", type=Path, help="Optional exact artifact manifest, copied and hashed before timing")
        command.add_argument("--ready-timeout", type=float, default=30)
        command.add_argument("--start-timeout", type=float, default=30)
        command.add_argument("--timeout", type=float, default=180, help="Seconds after STARTED; guest also has a bounded deadline and cleanup")
    run.add_argument("--sample-fixture", type=Path, help="Linux: arm bounded perf recording of this fixture's exact QEMU/Juke before resuming the owned game; input is fixture/run.json")
    args = parser.parse_args()
    try:
        if args.command == "command":
            print(launch_command(args.demo, args.mode, args.exe))
            return 0
        if args.output is None:
            stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
            args.output = Path("target/runs") / f"{args.demo}-{stamp}-{secrets.token_hex(3)}"
        sampler = None
        if args.command == 'run' and args.sample_fixture:
            from scripts.benchmarks.halflife_sample import LaunchSample
            state = json.loads(args.sample_fixture.read_text())
            if str(state['serial']) != str(args.socket):
                raise ValueError("sampling fixture serial endpoint differs from benchmark endpoint")
            sampler = LaunchSample(state, args.output)
        report = run_demo(args.socket, args.demo, args.output, args.manifest,
                          args.ready_timeout, args.start_timeout, args.timeout,
                          probe=args.command == 'probe', sampler=sampler)
        if report["state"] == "error":
            print(f"{report['failed_state']}: {report['error']['message']} ({args.output / 'run.json'})", file=sys.stderr)
            return 1
        if sampler and not report['cpu_sample'].get('coverage_proven'):
            print(f"Sample did not establish launch-to-summary coverage ({args.output / 'run.json'})", file=sys.stderr)
            return 1
        if args.command == 'probe':
            print(f"PASS {args.demo} ({args.output / 'run.json'})")
            return 0
        fps = report["result"]["fps"]
        print(f"{fps:.3f} FPS ({args.output / 'run.json'})" if fps is not None else
              f"Completed; unrecognized engine output retained ({args.output / 'run.json'})")
        return 0
    except (OSError, ValueError, argparse.ArgumentTypeError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    sys.exit(main())
