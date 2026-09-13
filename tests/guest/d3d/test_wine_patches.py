#!/usr/bin/env python3
"""Apply reviewed patches to actual pinned Wine source and inspect provenance."""
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
HERE = ROOT / 'tests/guest/support'
SUPPORT = ROOT / 'support/guest'
spec = importlib.util.spec_from_file_location('dreamgpu_wine_prepare', HERE / 'wine_prepare.py')
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)
source = ROOT / 'vendor/wine9x'
lock = json.loads((SUPPORT / 'sources.lock.json').read_text())['sources']['wine9x']
assert subprocess.check_output(['git', '-C', str(source), 'rev-parse', 'HEAD'], text=True).strip() == lock['commit']
with tempfile.TemporaryDirectory(prefix='dreamgpu-wine-patches-') as temporary:
    for diagnostic in (False, True):
        work = Path(temporary) / str(diagnostic)
        output = work / 'output'
        shutil.copytree(source, work, ignore=shutil.ignore_patterns('.git'))
        output.mkdir()
        evidence = prepare.prepare(work, output, diagnostic)
        assert ('diagnostic' in evidence['patches']) == diagnostic
        assert evidence['inputs']['tests/guest/support/wine_prepare.py'] == prepare.patches.digest(HERE / 'wine_prepare.py')
        assert (output / 'surface-diagnostic.c').read_bytes() == (work / 'ddraw/surface.c').read_bytes()
        assert (work / 'ddraw/dg-wine-diagnostics.h').read_bytes() == (ROOT / 'guest/d3d/wine-diagnostics.h').read_bytes()
        for name in ('row-copy', 'map-policy'):
            assert (work / ('wined3d/dg-wine-' + name + '.h')).read_bytes() == (ROOT / ('guest/d3d/wine-' + name + '.h')).read_bytes()
        if diagnostic:
            source_ids = {str(index): path.name for index, path in enumerate(sorted((source / 'wined3d').glob('*.c')), 1)}
            assert evidence['copy_sources'] == source_ids
            assert (work / 'wined3d/dg-wine-copy-diagnostics.h').read_bytes() == (ROOT / 'guest/d3d/wine-copy-diagnostics.h').read_bytes()
            for path in (output).glob('copy-diagnostic-*.c'):
                assert path.read_bytes() == (work / 'wined3d' / path.name.removeprefix('copy-diagnostic-')).read_bytes()
        else:
            assert evidence['copy_sources'] == {}
            assert not (work / 'wined3d/dg-wine-copy-diagnostics.h').exists()
        # Base patches cannot silently be reapplied to an already prepared tree.
        before = (work / 'Makefile').read_bytes()
        try:
            prepare.prepare(work, output, diagnostic)
        except ValueError as error:
            assert 'Upstream source identity mismatch' in str(error)
        else:
            raise AssertionError('Already patched source unexpectedly accepted')
        assert (work / 'Makefile').read_bytes() == before
print('PASS actual pinned Wine source: base/diagnostic patches, source-ID provenance, helper/output identities and reapplication rejection')
