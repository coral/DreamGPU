
# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import importlib.util
import io
import json
from pathlib import Path
import socket
import tempfile
import threading
import unittest

spec = importlib.util.spec_from_file_location("dg_gl_trace", (Path(__file__).resolve().parents[2] / 'scripts/diagnostics/gl-trace.py'))
trace = importlib.util.module_from_spec(spec)
spec.loader.exec_module(trace)


def submit(seq=1, records=1024, size=49152, prepare=5):
    return f"dreamgpu_gl_submit seq={seq} records={records} bytes={size} prepare_us={prepare}\n"


def work(seq=1, records=1024, size=49152, queue=3, execute=7, error=0):
    return f"dreamgpu_gl_work seq={seq} records={records} bytes={size} queue_us={queue} execute_us={execute} error={error}\n"


def complete(seq=1, error=0, elapsed=12):
    return f"dreamgpu_gl_complete seq={seq} error={error} elapsed_us={elapsed}\n"


def summary(*lines, **limits):
    return trace.summarize(io.BytesIO("".join(lines).encode()), **limits)


class ParserTests(unittest.TestCase):
    def test_worker_can_log_before_submit_and_reused_sequence_stays_separate(self):
        report = summary(work(), "[QEMU] " + submit(), complete(),
                         submit(records=1, size=40), work(records=1, size=40), complete(elapsed=20))
        self.assertEqual(report["counts"]["paired"], 2)
        self.assertEqual(report["counts"]["partial"], 0)
        self.assertEqual(report["totals"], {"submitted_bytes": 49192, "submitted_records": 1025})
        self.assertEqual(report["total_us"], {"count": 2, "mean": 16, "p50": 12, "p95": 20, "max": 20})
        # prepare+queue+execute exceeds total: phases overlap, not corruption.
        self.assertEqual(report["prepare_us"]["p50"], 5)
        self.assertEqual(report["queue_us"]["p50"], 3)
        self.assertEqual(report["execute_us"]["p50"], 7)
        self.assertNotIn("non_work_us", report)

    def test_partial_error_and_inconsistent_jobs_do_not_invent_paired_time(self):
        report = summary(complete(2, error=3), submit(3), work(3),
                         submit(4), work(4, size=40), complete(4),
                         submit(5), work(5, error=4), complete(5, error=4))
        self.assertEqual(report["counts"]["partial"], 2)
        self.assertEqual(report["counts"]["completion_without_submit"], 1)
        self.assertEqual(report["counts"]["inconsistent"], 1)
        self.assertEqual(report["counts"]["paired"], 1)
        self.assertEqual(report["errors"], {3: 1, 4: 1})

    def test_duplicate_submit_and_work_are_ambiguous(self):
        report = summary(submit(), submit(), work(), complete())
        self.assertEqual(report["counts"]["paired"], 0)
        self.assertEqual(report["counts"]["ambiguous_jobs"], 2)
        report = summary(work(), work(), submit(), complete())
        self.assertEqual(report["counts"]["paired"], 0)

    def test_malformed_numbers_duplicates_and_partial_final_record(self):
        report = summary(submit().replace("seq=1", "seq=-1"),
                         work().replace("seq=1", "seq=4294967296"),
                         complete().replace("error=0", "error=0 error=1"),
                         submit().replace("prepare_us=5", "prepare_us=18446744073709551616"),
                         "dreamgpu_gl_complete seq=7 error=")
        self.assertEqual(report["malformed_event_lines"], [1, 2, 3, 4, 5])
        self.assertEqual(report["counts"]["paired"], 0)

    def test_validation_rejections_are_visible_without_executed_work_errors(self):
        reject = "dreamgpu_gl_reject seq=0 execution=0 op=9 fn=809 a0=0xcf0 a1=0x0 a2=0x0 a3=0x0 error=11"
        report = summary(submit(), work(), complete(), reject + "\n",
                         reject + " a4=0x0 a5=0x0 a6=0x0 a7=0x0\n")
        self.assertEqual(report["errors"], {})
        self.assertEqual(report["rejections"]["total"], 2)
        self.assertEqual(report["rejections"]["validation"], 2)
        self.assertEqual(report["rejections"]["by_command"][0]["argument0"], 0xcf0)
        self.assertEqual(report["counts"]["paired"], 1)
        self.assertEqual(report["counts"]["malformed"], 0)
        bad = summary(reject.replace("execution=0", "execution=2") + "\n",
                      reject.replace("error=11", "error=0") + "\n",
                      reject + " a0=0x1\n")
        self.assertEqual(bad["counts"]["malformed"], 3)
        self.assertEqual(bad["rejections"]["total"], 0)

    def test_byte_line_event_and_job_bounds(self):
        for data, limits in [(submit(), {"max_bytes": 10}),
                             ("x" * (trace.MAX_LINE + 1), {}),
                             (submit() + work(), {"max_events": 1}),
                             (submit(1) + submit(2), {"max_jobs": 1})]:
            with self.subTest(limits=limits), self.assertRaises(trace.TraceError):
                summary(data, **limits)


class FakeQmp:
    def __init__(self, on_enable=None, fail_enable=None, fail_disable=None, state="disabled"):
        self.calls = []
        self.on_enable, self.fail_enable, self.fail_disable = on_enable, fail_enable, fail_disable
        self.state = state

    def command(self, name, args):
        self.calls.append((name, dict(args)))
        if name == "trace-event-get-state":
            return [{"name": args["name"], "state": self.state}]
        if args["enable"]:
            if self.on_enable:
                self.on_enable(args["name"])
            if args["name"] == self.fail_enable:
                raise trace.TraceError("lost enable acknowledgement")
        elif args["name"] == self.fail_disable:
            raise trace.TraceError("lost disable acknowledgement")
        return {}

    def toggled(self, enabled):
        return [args["name"] for command, args in self.calls
                if command == "trace-event-set-state" and args["enable"] == enabled]


class CaptureTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.log = Path(self.directory.name) / "qemu.trace"
        self.log.write_text("earlier unrelated fixture log\n")
        self.now = 0

    def clock(self):
        return self.now

    def sleep(self, seconds):
        self.now += seconds

    def append(self, text):
        with self.log.open("a") as source:
            source.write(text)

    def capture(self, qmp, output=None, **kwargs):
        return trace.capture(qmp, self.log, output or io.BytesIO(), seconds=.1,
                             clock=self.clock, sleep=self.sleep, **kwargs)

    def test_exact_new_slice_and_only_trace_control_commands(self):
        def enable(name):
            if name == trace.EVENTS[-1]:
                self.append(submit() + work() + complete())
        qmp, output = FakeQmp(on_enable=enable), io.BytesIO()
        report = self.capture(qmp, output)
        self.assertEqual(output.getvalue(), (submit() + work() + complete()).encode())
        self.assertEqual(qmp.toggled(False), list(trace.EVENTS))
        self.assertTrue(report["events_disabled"])
        self.assertEqual(report["end_byte"] - report["start_byte"], len(output.getvalue()))
        self.assertTrue(all(name.startswith("trace-event-") for name, _ in qmp.calls))

    def test_partial_enable_failure_still_disables_unacknowledged_enable(self):
        qmp = FakeQmp(fail_enable=trace.EVENTS[1])
        with self.assertRaisesRegex(trace.TraceError, "enable acknowledgement"):
            self.capture(qmp)
        self.assertEqual(qmp.toggled(False), list(trace.EVENTS[:2]))

    def test_disable_failures_try_every_event_and_report_uncertain_state(self):
        qmp = FakeQmp(fail_disable=trace.EVENTS[0])
        with self.assertRaisesRegex(trace.TraceError, "Failed to disable"):
            self.capture(qmp)
        self.assertEqual(qmp.toggled(False), list(trace.EVENTS))

    def test_existing_capture_is_untouched(self):
        qmp = FakeQmp(state="enabled")
        with self.assertRaisesRegex(trace.TraceError, "left untouched"):
            self.capture(qmp)
        self.assertEqual(qmp.toggled(True) + qmp.toggled(False), [])

    def test_interrupt_and_log_limit_disable_all_events(self):
        def interrupt(_):
            raise KeyboardInterrupt
        qmp = FakeQmp()
        with self.assertRaises(KeyboardInterrupt):
            trace.capture(qmp, self.log, io.BytesIO(), seconds=.1, clock=self.clock, sleep=interrupt)
        self.assertEqual(qmp.toggled(False), list(trace.EVENTS))
        qmp = FakeQmp(on_enable=lambda _: self.append("too much data\n"))
        with self.assertRaisesRegex(trace.TraceError, "byte limit"):
            self.capture(qmp, max_bytes=1)
        self.assertEqual(qmp.toggled(False), list(trace.EVENTS))

    def test_rotation_or_truncation_is_rejected(self):
        for rotate in [False, True]:
            with self.subTest(rotate=rotate):
                self.log.write_text("preamble\n")
                def mutate(name):
                    if name == trace.EVENTS[-1]:
                        if rotate:
                            self.log.unlink()
                        self.log.write_text("")
                qmp = FakeQmp(on_enable=mutate)
                with self.assertRaisesRegex(trace.TraceError, "rotated or was truncated"):
                    self.capture(qmp)
                self.assertEqual(qmp.toggled(False), list(trace.EVENTS))

    def test_duration_bound_precedes_any_qmp_mutation(self):
        for seconds in [0, 31, float("inf"), float("nan")]:
            qmp = FakeQmp()
            with self.assertRaises(trace.TraceError):
                trace.capture(qmp, self.log, io.BytesIO(), seconds=seconds)
            self.assertEqual(qmp.calls, [])


class QmpTests(unittest.TestCase):
    def test_real_unix_socket_matches_ids_around_unsolicited_events(self):
        with tempfile.TemporaryDirectory(dir="/tmp") as directory:
            path = Path(directory) / "qmp"
            server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            self.addCleanup(server.close)
            server.bind(str(path))
            server.listen(1)
            errors = []

            def serve():
                try:
                    connection, _ = server.accept()
                    with connection, connection.makefile("rwb", buffering=0) as stream:
                        stream.write(b'{"QMP":{}}\r\n')
                        for expected in ["qmp_capabilities", "trace-event-get-state"]:
                            request = json.loads(stream.readline())
                            self.assertEqual(request["execute"], expected)
                            stream.write(b'{"event":"STOP"}\r\n{"id":999,"return":{}}\r\n')
                            stream.write(json.dumps({"id": request["id"], "return": [expected]}).encode() + b"\r\n")
                except BaseException as error:
                    errors.append(error)
            thread = threading.Thread(target=serve, daemon=True)
            thread.start()
            qmp = trace.Qmp(path)
            try:
                self.assertEqual(qmp.command("trace-event-get-state", {"name": trace.EVENTS[0]}), ["trace-event-get-state"])
            finally:
                qmp.close()
            thread.join(timeout=3)
            self.assertFalse(thread.is_alive())
            self.assertEqual(errors, [])


if __name__ == "__main__":
    unittest.main()
