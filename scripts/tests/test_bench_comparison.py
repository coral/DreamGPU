"""A numeric delta must not silently combine different capture conditions."""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import contextlib
import io
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest

from scripts.benchmarks.bench import compare, comparison_conditions, validate_comparison_dimensions


class ComparisonTests(unittest.TestCase):
    def captures(self, root):
        paths = [root / 'before', root / 'after']
        metadata = dict(host='one-host', platform='Linux', scenario='idle',
                        instrumentation=False, injection='dispatch',
                        active_vm={'vm_id': 'win2000'},
                        replay_definition={'sha256': 'fixed-input'},
                        capture_conditions={'duration_seconds': 10, 'warmup_seconds': 3,
                                            'probe': False, 'snapshot': None, 'setup_sha256': None})
        for path in paths:
            path.mkdir()
            (path / 'manifest.json').write_text(json.dumps(metadata))
            (path / 'run-01.json').write_text(json.dumps({'summary': {
                'dropped': 0, 'cpu': {'juke.core_percent': .3}}}))
        return paths

    def test_instrumented_and_uninstrumented_cpu_cannot_be_compared(self):
        with tempfile.TemporaryDirectory() as directory:
            before, after = self.captures(Path(directory))
            path = after / 'manifest.json'
            data = json.loads(path.read_text())
            data['instrumentation'] = True
            path.write_text(json.dumps(data))
            with self.assertRaisesRegex(ValueError, 'instrumentation'):
                compare(SimpleNamespace(before=before, after=after))

    def test_changed_warmup_rejected_even_when_capture_metrics_match(self):
        with tempfile.TemporaryDirectory() as directory:
            before, after = self.captures(Path(directory))
            path = after / 'manifest.json'
            data = json.loads(path.read_text())
            data['capture_conditions']['warmup_seconds'] = 0
            path.write_text(json.dumps(data))
            with self.assertRaisesRegex(ValueError, 'capture_conditions'):
                comparison_conditions(before / 'run-01.json', after)

    def test_old_missing_metadata_is_explicit_and_never_backfilled(self):
        with tempfile.TemporaryDirectory() as directory:
            before, after = self.captures(Path(directory))
            (after / 'manifest.json').unlink()
            with contextlib.redirect_stdout(io.StringIO()) as output:
                compare(SimpleNamespace(before=before, after=after))
            result = json.loads(output.getvalue())
            self.assertIn('capture_conditions', result['conditions']['missing'])
            self.assertEqual(result['conditions']['checked_equal'], [])
            self.assertFalse((after / 'manifest.json').exists())

    def test_dropped_samples_cannot_produce_a_speedup_report(self):
        with tempfile.TemporaryDirectory() as directory:
            before, after = self.captures(Path(directory))
            path = after / 'run-01.json'
            data = json.loads(path.read_text())
            data['summary']['dropped'] = 1
            data['summary']['cpu']['juke.core_percent'] = .1
            path.write_text(json.dumps(data))
            with self.assertRaisesRegex(ValueError, 'Dropped samples'):
                compare(SimpleNamespace(before=before, after=after))

    def test_partial_dimensions_are_not_assumed_equal(self):
        with self.assertRaisesRegex(ValueError, 'missing'):
            validate_comparison_dimensions([{'guest_dimensions': [640, 480]}, {}])


if __name__ == '__main__':
    unittest.main()
