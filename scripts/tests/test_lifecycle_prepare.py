"""Focused monitor selection and preparation ownership gates."""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import importlib.util
from pathlib import Path
import tempfile
import json
import unittest

spec=importlib.util.spec_from_file_location('prepare',(Path(__file__).resolve().parents[2] / 'scripts/fixtures/lifecycle-prepare.py'))
prepare=importlib.util.module_from_spec(spec);spec.loader.exec_module(prepare)

class PeerPreparationTests(unittest.TestCase):
    def test_control_monitor_excludes_consumed_events_monitor(self):
        log='native -qmp unix:/tmp/first-qmp.sock,server,nowait -qmp unix:/tmp/first-events-qmp.sock,server,nowait\n'
        log+='native -qmp unix:/tmp/peer-qmp.sock,server,nowait -qmp unix:/tmp/peer-events-qmp.sock,server,nowait\n'
        self.assertEqual(prepare.control_qmps(log),['/tmp/first-qmp.sock','/tmp/peer-qmp.sock'])
    def test_live_primary_cannot_be_reconfigured(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory);(path/'run.json').write_text(json.dumps({'state':'ready'}))
            with self.assertRaisesRegex(ValueError,'unstarted'):prepare.prepare_peer(path)
            self.assertEqual(list(path.iterdir()),[path/'run.json'])
    def test_existing_peer_cannot_be_silently_restarted(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory);(path/'run.json').write_text(json.dumps({'state':'ready','lifecycle_peer':{'state':'starting'}}))
            with self.assertRaisesRegex(ValueError,'fresh'):prepare.prewarm(path,90)

if __name__=='__main__':unittest.main()
