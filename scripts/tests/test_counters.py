"""A reset or lossy histogram must not become a plausible performance result."""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import copy
import unittest
from scripts.diagnostics.counters import summarize


def snapshot():
    return {'schema': 1, 'enabled': True, 'elapsed_us': 100,
            'mmio': [{'offset': 4, 'reads': 1, 'writes': 2}],
            'gl': {'batches': 1, 'bytes': 64, 'records': 2,
                   'query_overflow': 0, 'function_overflow': 0,
                   'functions': [{'function': 26, 'count': 2}],
                   'operations': [], 'desktop': [], 'queries': []}}


class CounterTests(unittest.TestCase):
    def test_exact_deltas_and_overflow_scope(self):
        first = snapshot(); last = copy.deepcopy(first)
        last['elapsed_us'] += 10_000_000
        last['mmio'][0]['reads'] += 90
        last['gl']['functions'][0]['count'] += 50
        last['gl']['query_overflow'] = 100
        result = summarize(first, last)
        self.assertEqual(result['elapsed_us'], 10_000_000)
        self.assertEqual(result['mmio_reads'], 90)
        self.assertEqual(result['gl']['functions'][0]['count'], 50)
        self.assertEqual(result['gl']['functions'][0]['name'], 'glBegin')
        self.assertFalse(result['query_keys_complete'])
        self.assertTrue(result['function_totals_complete'])

    def test_resets_disappearing_keys_and_duplicate_keys_rejected(self):
        for mutate in (
            lambda last: last.update(elapsed_us=0),
            lambda last: last['gl'].update(bytes=0),
            lambda last: last.update(mmio=[]),
            lambda last: last['mmio'].append(dict(last['mmio'][0])),
            lambda last: last['mmio'][0].update(reads=-1),
            lambda last: last.update(enabled=False),
        ):
            first = snapshot(); last = copy.deepcopy(first)
            last['elapsed_us'] += 1
            mutate(last)
            with self.assertRaises(ValueError):
                summarize(first, last)

    def test_prior_overflow_stays_visible_even_without_interval_overflow(self):
        first = snapshot(); first['gl']['query_overflow'] = 3
        last = copy.deepcopy(first); last['elapsed_us'] += 1
        result = summarize(first, last)
        self.assertEqual(result['gl']['query_overflow'], 0)
        self.assertFalse(result['query_keys_complete'])


if __name__ == '__main__':
    unittest.main()
