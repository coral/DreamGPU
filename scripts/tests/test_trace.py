
# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("dg_trace", (Path(__file__).resolve().parents[2] / 'scripts/diagnostics/trace.py'))
trace = importlib.util.module_from_spec(spec)
spec.loader.exec_module(trace)


class TraceTests(unittest.TestCase):
    def test_continuation_and_reused_sequence_are_distinct(self):
        lines = [
            "[QEMU] dreamgpu_gpu_submit seq=1 commands=2 bytes=8192 inline_cap=4096",
            "[QEMU] dreamgpu_gpu_work seq=1 bytes=4096 rows=8 elapsed_us=3 inline=1",
            "[QEMU] dreamgpu_gpu_work seq=1 bytes=4096 rows=8 elapsed_us=4 inline=0",
            "[QEMU] dreamgpu_gpu_complete seq=1 error=0 chunks=2 elapsed_us=20",
            "dreamgpu_gpu_submit seq=1 commands=1 bytes=16 inline_cap=4096",
            "dreamgpu_gpu_work seq=1 bytes=16 rows=1 elapsed_us=0 inline=1",
            "dreamgpu_gpu_complete seq=1 error=0 chunks=1 elapsed_us=1",
        ]
        batches, malformed = trace.parse(lines)
        summary = trace.summarize(batches, malformed)
        self.assertEqual(summary["counts"]["paired"], 2)
        self.assertEqual(summary["counts"]["continued_batches"], 1)
        self.assertEqual(summary["non_work_elapsed_us"]["sum"], 14)
        self.assertEqual(summary["tiny_batches"]["submission_share"], 0.5)
        self.assertEqual(summary["bytes"]["processed"], 8208)
        with self.assertRaises(ValueError):
            trace.perfetto(batches)

    def test_incomplete_invalid_and_error_groups_do_not_invent_timings(self):
        batches, malformed = trace.parse([
            "dreamgpu_gpu_complete seq=1 error=2 chunks=0 elapsed_us=2",
            "dreamgpu_gpu_submit seq=2 commands=1 bytes=64 inline_cap=4096",
            "dreamgpu_gpu_work seq=2 bytes=64 rows=4 elapsed_us=1 inline=1",
            "dreamgpu_gpu_submit seq=3 commands=1 bytes=64 inline_cap=4096",
            "dreamgpu_gpu_complete seq=3 error=0 chunks=1 elapsed_us=8",
            "dreamgpu_gpu_work seq=4 bytes=oops",
        ])
        summary = trace.summarize(batches, malformed)
        self.assertEqual(summary["counts"]["paired"], 0)
        self.assertEqual(summary["counts"]["incomplete"], 1)
        self.assertEqual(summary["errors"], {2: 1})
        self.assertEqual(summary["inconsistent_sequences"], [3])
        self.assertEqual(malformed, [6])

    def test_timestamp_scope_and_perfetto_use_real_durations(self):
        batches, _ = trace.parse([
            "unrelated startup",
            "[QEMU] 2026-09-11T12:00:00.000010Z dreamgpu_gpu_submit seq=7 commands=1 bytes=32 inline_cap=4096",
            "[QEMU] 2026-09-11T12:00:00.000014Z dreamgpu_gpu_work seq=7 bytes=32 rows=1 elapsed_us=3 inline=1",
            "[QEMU] 2026-09-11T12:00:00.000020Z dreamgpu_gpu_complete seq=7 error=0 chunks=1 elapsed_us=12",
            "dreamgpu_gpu_complete seq=8 error=2 chunks=0 elapsed_us=1",
        ], 2, 4)
        events = trace.perfetto(batches)["traceEvents"]
        self.assertEqual(len(events), 2)
        self.assertEqual((events[0]["ts"], events[0]["dur"]), (0, 12))
        self.assertEqual((events[1]["ts"], events[1]["dur"]), (3, 3))


if __name__ == "__main__":
    unittest.main()
