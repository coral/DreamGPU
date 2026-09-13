
# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import socket
import tempfile
import threading
import time
import unittest

spec = importlib.util.spec_from_file_location("dg_halflife", (Path(__file__).resolve().parents[2] / 'scripts/benchmarks/halflife.py'))
hl = importlib.util.module_from_spec(spec)
spec.loader.exec_module(hl)

ENGINE = b"Loading tram...\r\n379 frames 4.877 seconds 77.717 fps\r\n\xff"


class ResultTests(unittest.TestCase):
    def test_numeric_console_formats_and_raw_unknown(self):
        result = hl.parse_result(ENGINE)
        self.assertEqual((result["frames"], result["seconds"], result["fps"]), (379, 4.877, 77.717))
        self.assertEqual(hl.parse_result(b"1369 frames, 5.218 seconds, 2.62385e2 FPS")["fps"], 262.385)
        self.assertEqual(hl.parse_result(b"FPS: 77.717")["fps"], 77.717)
        for raw in [b"loading", b"77.717", b"NaN fps", b"-2.5 fps", b"1e999 fps", b"0 frames 5 seconds 2 fps"]:
            with self.subTest(raw=raw):
                self.assertIsNone(hl.parse_result(raw)["fps"])

    def test_renderer_evidence_remains_separate_from_summary_and_counter_proof(self):
        self.assertEqual(hl.parse_renderer_proof(ENGINE)["status"], "not_established")
        evidence = (b"\r\nRENDERER_MODULE C:\\SIERRA\\Half-Life\\hl.exe\r\n"
                    b"RENDERER_MODULE C:\\SIERRA\\Half-Life\\hw.dll\r\n"
                    b"RENDERER_MODULE C:\\WINNT\\system32\\opengl32.dll\r\n"
                    b"RENDERER_MODULE C:\\WINNT\\system32\\dgpuicd.dll\r\n"
                    b"RENDERER_MODULE C:\\WINNT\\system32\\dgpugl.dll\r\n"
                    b"ENGINE_GL_VENDOR: DreamGPU\r\n"
                    b"ENGINE_GL_RENDERER: DreamGPU (native host OpenGL)\r\n"
                    b"ENGINE_GL_VERSION: 1.1 DreamGPU\r\n"
                    b"RENDERER_PROOF system-icd-and-engine-strings\r\n")
        module_only = evidence.split(b"ENGINE_GL_VENDOR:")[0] + (
            b"ENGINE_GL_STRINGS unavailable\r\nRENDERER_PROOF system-icd-modules\r\n")
        self.assertEqual(hl.parse_renderer_proof(module_only)["status"], "verified_by_runner")
        self.assertFalse(hl.parse_renderer_proof(module_only)["engine_strings_observed"])
        proof = hl.parse_renderer_proof(ENGINE + evidence)
        self.assertEqual(proof["status"], "verified_by_runner")
        self.assertFalse(proof["gpu_command_execution_proven"])
        self.assertEqual(hl.parse_result(ENGINE + evidence), hl.parse_result(ENGINE))
        for invalid in (evidence.replace(b"DreamGPU (native host OpenGL)", b"GDI Generic"),
                        evidence + b"ENGINE_GL_VERSION: duplicate\r\n",
                        b"RENDERER_PROOF system-icd-and-engine-strings\r\n"):
            self.assertEqual(hl.parse_renderer_proof(invalid)["status"], "not_established")

    def test_command_is_bounded_and_rejects_injected_demo(self):
        self.assertIn("-toconsole -condebug", hl.launch_command("dgperf.dem"))
        for name in ["../x", "a b", "x;+quit", "a" * 65, "a\nquit"]:
            with self.subTest(name=name), self.assertRaises(argparse.ArgumentTypeError):
                hl.demo_name(name)


class ProtocolTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(dir="/tmp", prefix="hl-run-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)

    def run_server(self, behavior, **kwargs):
        endpoint, output = self.root / "serial", self.root / "output"
        server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.addCleanup(server.close)
        server.bind(str(endpoint))
        server.listen(1)
        errors = []

        def serve():
            try:
                connection, _ = server.accept()
                connection.settimeout(2)
                with connection, connection.makefile("rwb", buffering=0) as stream:
                    ping = stream.readline().decode().strip().split()
                    self.assertEqual(ping[0], "PING")
                    behavior(stream, ping[1])
            except (BrokenPipeError, ConnectionResetError):
                pass
            except BaseException as error:
                errors.append(error)
        thread = threading.Thread(target=serve, daemon=True)
        thread.start()
        report = hl.run_demo(endpoint, kwargs.pop('demo', 'dgperf'), output, **kwargs)
        thread.join(timeout=3)
        self.assertFalse(thread.is_alive())
        self.assertEqual(errors, [])
        self.assertEqual(json.loads((output / "run.json").read_text()), report)
        return report, output

    def started(self, stream, request_id):
        stream.write(f"READY {request_id}\r\n".encode())
        self.assertEqual(stream.readline(), f"RUN {request_id} dgperf\n".encode())
        stream.write(f"STARTED {request_id}\n".encode())

    def test_probe_uses_fixed_command_and_retains_result_without_fps(self):
        raw = b'PASS automated arrays: exact GPU pixels and lifecycle\r\n'
        def behavior(stream, request_id):
            stream.write(f'READY {request_id}\n'.encode())
            self.assertEqual(stream.readline(), f'PROBE {request_id} arrays\n'.encode())
            stream.write(f'STARTED {request_id}\nRESULT {request_id} {raw.hex()}\n'.encode())
        report, output = self.run_server(behavior, demo='arrays', probe=True)
        self.assertEqual(report['state'], 'completed')
        self.assertEqual(report['result'], {'probe': 'arrays', 'passed': True})
        self.assertEqual((output / 'engine-output.txt').read_bytes(), raw)

    def test_dual_work_ack_requires_successful_observation_callback(self):
        seen=[];raw=b'PASS automated dual: bounded real GPU process work\n'
        def behavior(stream,request_id):
            stream.write(f'READY {request_id}\n'.encode());stream.readline()
            stream.write(f'STARTED {request_id}\nMEASURING {request_id}\n'.encode())
            self.assertEqual(stream.readline(),f'MINIMIZED {request_id}\n'.encode())
            self.assertEqual(seen,['MEASURING'])
            stream.write(f'MEASURED {request_id}\n'.encode())
            self.assertEqual(stream.readline(),f'CAPTURED {request_id}\n'.encode())
            stream.write(f'RESULT {request_id} {raw.hex()}\n'.encode())
        report,_=self.run_server(behavior,demo='dual',probe=True,on_phase=seen.append)
        self.assertEqual(report['state'],'completed')
        self.assertEqual(seen,['MEASURING','MEASURED'])

    def test_dual_failed_observation_never_authorizes_guest_work(self):
        def behavior(stream,request_id):
            stream.write(f'READY {request_id}\n'.encode());stream.readline()
            stream.write(f'STARTED {request_id}\nMEASURING {request_id}\n'.encode())
            time.sleep(.03)
            stream.write(f'ERROR {request_id} minimize-ack\n'.encode())
            self.assertEqual(stream.readline(),b'')
        def failed(kind):raise RuntimeError('OS did not minimize')
        report,_=self.run_server(behavior,demo='dual',probe=True,on_phase=failed)
        self.assertEqual(report['state'],'error')
        self.assertIn('OS did not minimize',report['measurement_phases']['MEASURING']['callback_error'])

    def test_game_phase_callbacks_are_ordered_and_do_not_replace_result(self):
        seen=[]
        raw=b'PASS automated utd3d: owned game completed\n'
        def behavior(stream,request_id):
            stream.write(f'READY {request_id}\n'.encode());stream.readline()
            stream.write(f'STARTED {request_id}\nMEASURING {request_id}\nMEASURED {request_id}\n'.encode())
            self.assertEqual(stream.readline(),f'CAPTURED {request_id}\n'.encode())
            stream.write(f'RESULT {request_id} {raw.hex()}\n'.encode())
        report,_=self.run_server(behavior,demo='utd3d',probe=True,on_phase=seen.append)
        self.assertEqual(report['state'],'completed');self.assertEqual(seen,['MEASURING','MEASURED'])
        self.assertEqual(set(report['measurement_phases']),set(seen))
        boundaries=report['measurement_phases']
        self.assertLessEqual(boundaries['MEASURING']['host_monotonic_seconds'],boundaries['MEASURED']['host_monotonic_seconds'])

    def test_game_callback_failure_still_collects_owned_result(self):
        raw=b'PASS automated utglide: owned game completed\n'
        def behavior(stream,request_id):
            stream.write(f'READY {request_id}\n'.encode());stream.readline()
            stream.write(f'STARTED {request_id}\nMEASURING {request_id}\nRESULT {request_id} {raw.hex()}\n'.encode())
        def failed(phase):raise RuntimeError('screenshot failed')
        report,_=self.run_server(behavior,demo='utglide',probe=True,on_phase=failed)
        self.assertEqual(report['state'],'completed')
        self.assertEqual(report['measurement_phases']['MEASURING']['callback_error'],'screenshot failed')

    def test_end_capture_failure_still_acknowledges_owned_cleanup(self):
        raw=b'PASS automated utglide: owned game completed\n'
        def behavior(stream,request_id):
            stream.write(f'READY {request_id}\n'.encode());stream.readline()
            stream.write(f'STARTED {request_id}\nMEASURING {request_id}\nMEASURED {request_id}\n'.encode())
            self.assertEqual(stream.readline(),f'CAPTURED {request_id}\n'.encode())
            stream.write(f'RESULT {request_id} {raw.hex()}\n'.encode())
        def failed(phase):
            if phase=='MEASURED':raise RuntimeError('end capture failed')
        report,_=self.run_server(behavior,demo='utglide',probe=True,on_phase=failed)
        self.assertEqual(report['state'],'completed')
        self.assertEqual(report['measurement_phases']['MEASURED']['callback_error'],'end capture failed')

    def test_duplicate_game_phase_is_rejected(self):
        def behavior(stream,request_id):
            stream.write(f'READY {request_id}\n'.encode());stream.readline()
            stream.write(f'STARTED {request_id}\nMEASURING {request_id}\nMEASURING {request_id}\n'.encode())
        report,_=self.run_server(behavior,demo='utd3d',probe=True)
        self.assertIn('duplicate',report['error']['message'])

    def test_timedemo_observations_are_ordered_and_keep_engine_interval_unknown(self):
        seen=[]
        def behavior(stream,request_id):
            self.started(stream,request_id)
            stream.write(f'PROCESS_READY {request_id}\n'.encode())
            self.assertEqual(stream.readline(),f'CONTINUE {request_id}\n'.encode())
            stream.write(f'PROCESS_RESUMED {request_id}\nCONSOLE_OBSERVED {request_id}\nTIMEDEMO_RESULT {request_id}\n'.encode())
            self.assertEqual(stream.readline(),f'OBSERVED {request_id}\n'.encode())
            stream.write(f'RESULT {request_id} {ENGINE.hex()}\n'.encode())
        report,_=self.run_server(behavior,on_phase=seen.append)
        self.assertEqual(report['state'],'completed')
        self.assertEqual(seen,['PROCESS_READY','PROCESS_RESUMED','CONSOLE_OBSERVED','TIMEDEMO_RESULT'])
        self.assertEqual(report['engine_interval']['status'],'unknown')
        self.assertEqual(report['result']['fps'],77.717)
        bounds=report['timedemo_boundaries'];window=report['engine_interval']['enclosing_host_window']
        self.assertLessEqual(bounds['PROCESS_READY']['host_monotonic_seconds'],window['start'])
        self.assertLessEqual(window['start'],bounds['PROCESS_RESUMED']['host_monotonic_seconds'])
        self.assertLessEqual(window['end'],report['host_monotonic_seconds']['result_received'])

    def test_host_counter_observations_are_preserved_at_acknowledged_boundaries(self):
        def behavior(stream, request_id):
            self.started(stream, request_id)
            stream.write(f'PROCESS_READY {request_id}\n'.encode())
            self.assertEqual(stream.readline().decode().strip(), f'CONTINUE {request_id}')
            stream.write(f'PROCESS_RESUMED {request_id}\nCONSOLE_OBSERVED {request_id}\nTIMEDEMO_RESULT {request_id}\n'.encode())
            self.assertEqual(stream.readline().decode().strip(), f'OBSERVED {request_id}')
            stream.write(f'RESULT {request_id} {ENGINE.hex()}\n'.encode())
        def counter(kind):
            if kind == 'TIMEDEMO_RESULT':
                return {'source': 'fixture-owned-counter', 'submitted': 123, 'completed': 123}
            return None
        report, _ = self.run_server(behavior, on_phase=counter)
        self.assertEqual(report['state'], 'completed')
        self.assertEqual(report['timedemo_boundaries']['TIMEDEMO_RESULT']['host_observation']['completed'], 123)
        self.assertFalse(report['renderer_proof']['gpu_command_execution_proven'])

    def test_failed_sampler_arming_aborts_suspended_process(self):
        def behavior(stream,request_id):
            self.started(stream,request_id)
            stream.write(f'PROCESS_READY {request_id}\n'.encode())
            self.assertEqual(stream.readline(),f'ABORT {request_id}\n'.encode())
            stream.write(f'ERROR {request_id} launch-not-armed\n'.encode())
        def failed(kind):raise RuntimeError('perf enable acknowledgement missing')
        report,_=self.run_server(behavior,on_phase=failed)
        self.assertEqual(report['state'],'error')
        self.assertEqual(report['timedemo_boundaries']['PROCESS_READY']['ack'],'ABORT')

    def test_summary_sampler_failure_still_releases_cleanup(self):
        def behavior(stream,request_id):
            self.started(stream,request_id)
            stream.write(f'PROCESS_READY {request_id}\n'.encode());stream.readline()
            stream.write(f'PROCESS_RESUMED {request_id}\nCONSOLE_OBSERVED {request_id}\nTIMEDEMO_RESULT {request_id}\n'.encode())
            self.assertEqual(stream.readline(),f'OBSERVED {request_id}\n'.encode())
            stream.write(f'RESULT {request_id} {ENGINE.hex()}\n'.encode())
        def failed(kind):
            if kind=='TIMEDEMO_RESULT':raise RuntimeError('perf stop failed')
        report,_=self.run_server(behavior,on_phase=failed)
        self.assertEqual(report['state'],'completed')
        self.assertIn('callback_error',report['timedemo_boundaries']['TIMEDEMO_RESULT'])

    def test_timedemo_duplicate_and_out_of_order_boundaries_rejected(self):
        base = self.root
        for index, phases in enumerate([('PROCESS_RESUMED',),('PROCESS_READY','PROCESS_READY')]):
            self.root = base / str(index); self.root.mkdir()
            def behavior(stream,request_id):
                self.started(stream,request_id)
                for phase in phases:
                    stream.write(f'{phase} {request_id}\n'.encode())
                    if phase=='PROCESS_READY' and phase==phases[0]:
                        if stream.readline().startswith(b'ABORT'):return
            report,_=self.run_server(behavior)
            self.assertEqual(report['state'],'error')
            self.assertIn('timedemo boundary',report['error']['message'])

    def test_probe_requires_matching_api_pass(self):
        for name in ('sysgl', 'sysglide', 'setupcheck', 'win9xinstall', 'win9xdiag', 'd3d6', 'd3d7', 'd3d8', 'd3d9', 'glide', 'windows', 'modes', 'win98', 'utlogs', 'utdsetup', 'utd3d', 'ntupdate'):
            with self.subTest(name=name):
                raw = f'PASS automated {name}: real GPU pixels\n'.encode()
                self.assertEqual(hl.parse_probe(raw, name), {'probe': name, 'passed': True})
                with self.assertRaises(hl.ProtocolError):
                    hl.parse_probe(b'PASS automated arrays: unrelated API\n', name)

    def test_ui_requires_observed_dialog_and_matching_continuation(self):
        for code in (0, 11, 12, 13):
            receipt = {'schema': 1, 'operation': 'sysresume', 'installer_exit': code,
                       'terminal': code != 11}
            action = 'INSTALLER_RESTART_DECLINED' if code == 11 else 'INSTALLER_DIALOG_ACCEPTED'
            raw = (action + f'\nINSTALLER_EXIT {code}\n' + json.dumps(receipt, separators=(',', ':'))
                   + '\nPASS automated sysresume: complete installer operation receipt\n').encode()
            self.assertEqual(hl.parse_probe(raw, 'sysui'), {'probe': 'sysui', 'passed': True})
            for invalid in (raw.replace(action.encode() + b'\n', b''),
                            raw + action.encode() + b'\n',
                            raw.replace(f'INSTALLER_EXIT {code}'.encode(), b'INSTALLER_EXIT 28'),
                            raw.replace(b'"terminal":true', b'"terminal":false') if code != 11
                            else raw.replace(b'"terminal":false', b'"terminal":true')):
                with self.assertRaises(hl.ProtocolError):
                    hl.parse_probe(invalid, 'sysui')

    def test_probe_rejects_missing_or_contradictory_pass(self):
        for raw in (b'379 frames 4.877 seconds 77.717 fps',
                    b'PASS automated arrays: pixels\nFAIL delete context\n'):
            with self.subTest(raw=raw), self.assertRaises(hl.ProtocolError):
                hl.parse_probe(raw)

    def test_success_fragmented_lines_stale_ids_and_exact_manifest(self):
        manifest = self.root / "input.json"
        original = b'{ "native_sha256": "abcdef", "frontend_sha256": "123456" }\n'
        manifest.write_bytes(original)
        def behavior(stream, request_id):
            stream.write(b"READY stale\nERROR earlier failed\n")
            self.started(stream, request_id)
            stream.write(b"RESULT stale 00\n")
            response = f"RESULT {request_id} {ENGINE.hex()}\n".encode()
            for position in range(0, len(response), 7):
                stream.write(response[position:position + 7])
        report, output = self.run_server(behavior, manifest=manifest)
        self.assertEqual(report["state"], "completed")
        self.assertEqual(report["result"]["fps"], 77.717)
        self.assertEqual((output / "engine-output.txt").read_bytes(), ENGINE)
        self.assertEqual(report["raw_text"].encode("latin-1"), ENGINE)
        self.assertEqual((output / "manifest-input.json").read_bytes(), original)
        self.assertEqual(report["manifest"]["sha256"], hashlib.sha256(original).hexdigest())
        self.assertGreaterEqual(report["latency_seconds"]["completion_after_run"], report["latency_seconds"]["start_after_run"])

    def test_guest_error_preserves_console_tail(self):
        def behavior(stream, request_id):
            self.started(stream, request_id)
            stream.write(f"ERROR {request_id} process_exited {b'failed loading map'.hex()}\n".encode())
        report, output = self.run_server(behavior)
        self.assertEqual(report["state"], "error")
        self.assertEqual(report["failed_state"], "await_result")
        self.assertEqual((output / "engine-error.txt").read_bytes(), b"failed loading map")

    def test_timeout_retains_failure_state(self):
        def behavior(stream, request_id):
            self.started(stream, request_id)
            time.sleep(.08)
        report, _ = self.run_server(behavior, timeout=.02)
        self.assertEqual(report["state"], "error")
        self.assertEqual(report["failed_state"], "await_result")
        self.assertEqual(report["error"]["type"], "TimeoutError")

    def test_result_before_started_is_rejected(self):
        def behavior(stream, request_id):
            stream.write(f"READY {request_id}\n".encode())
            stream.readline()
            stream.write(f"RESULT {request_id} {ENGINE.hex()}\n".encode())
        report, _ = self.run_server(behavior)
        self.assertEqual(report["failed_state"], "await_started")

    def test_malformed_hex_is_rejected(self):
        def behavior(stream, request_id):
            self.started(stream, request_id)
            stream.write(f"RESULT {request_id} 0xyz\n".encode())
        report, output = self.run_server(behavior)
        self.assertEqual(report["state"], "error")
        self.assertFalse((output / "engine-output.txt").exists())

    def test_oversized_unterminated_line_is_bounded(self):
        def behavior(stream, request_id):
            self.started(stream, request_id)
            response = memoryview(f"RESULT {request_id} ".encode() + b"f" * (hl.MAX_LINE + 1))
            while response:
                written = stream.write(response)
                self.assertGreater(written, 0)
                response = response[written:]
        report, _ = self.run_server(behavior)
        self.assertIn("bounded", report["error"]["message"])

    def test_stale_messages_have_a_count_bound(self):
        def behavior(stream, request_id):
            stream.write(b"READY stale\n" * (hl.MAX_MESSAGES + 1))
        report, _ = self.run_server(behavior)
        self.assertIn("too many", report["error"]["message"])

    def test_output_cannot_replace_prior_run(self):
        output = self.root / "existing"
        output.mkdir()
        sentinel = output / "run.json"
        sentinel.write_text("old run")
        with self.assertRaises(FileExistsError):
            hl.run_demo(self.root / "unused", "dgperf", output)
        self.assertEqual(sentinel.read_text(), "old run")


if __name__ == "__main__":
    unittest.main()
