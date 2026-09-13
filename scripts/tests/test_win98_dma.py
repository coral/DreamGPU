"""Native DMA submission is not completion; preserve that distinction."""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch
import tempfile
import json
import hashlib
spec=importlib.util.spec_from_file_location('dma_check',(Path(__file__).resolve().parents[2] / 'scripts/diagnostics/win98-dma-check.py'))
dma=importlib.util.module_from_spec(spec);spec.loader.exec_module(dma)
COMMAND='ide_bus_exec_cmd IDE exec cmd: bus 0x1; state 0x2; cmd 0xca\n'
IDE='ide_dma_cb IDEState 0x2; sector_num=8 n=2 cmd=DMA WRITE\n'
START='dma_blk_io dbs=0x3 bs=0x4 offset=4096 to_dev=1\n'
END='dma_complete dbs=0x3 ret=0 cb=0x5\n'
class DmaTrace(unittest.TestCase):
 def test_matched_success(self):
  r=dma.summarize(COMMAND+IDE+START+END);self.assertTrue(r['passed']);self.assertEqual(r['matched_successful_sectors'],2)
 def test_submission_alone_not_success(self):
  r=dma.summarize(COMMAND+IDE+START);self.assertFalse(r['passed']);self.assertEqual(r['unfinished_dma_requests'],1)
 def test_failure_not_success(self):
  r=dma.summarize(COMMAND+IDE+START+END.replace('ret=0','ret=-5'));self.assertFalse(r['passed']);self.assertEqual(r['negative_dma_completions'],1)
 def test_unrelated_completion_not_ide(self):
  self.assertFalse(dma.summarize(COMMAND+IDE+START.replace('4096','8192')+END)['passed'])
  self.assertFalse(dma.summarize(COMMAND+IDE+START.replace('to_dev=1','to_dev=0')+END)['passed'])
 def test_partial_edge_visible(self):
  r=dma.summarize(END+COMMAND+IDE+START+END);self.assertTrue(r['passed']);self.assertEqual(r['unmatched_completions'],1)
 def test_reuse_requires_previous_completion(self):
  with self.assertRaises(ValueError):dma.summarize(COMMAND+IDE+START+START)
class DmaCaptureCleanup(unittest.TestCase):
 def test_failed_probe_restores_original_trace_states(self):
  with tempfile.TemporaryDirectory() as tmp:
   root=Path(tmp).resolve();fixture=root/'fixture';fixture.mkdir();package=root/'package';package.mkdir()
   (fixture/'native-trace.log').write_text('old trace\n')
   state={'state':'ready','pid':123,'qmp':'owned-qmp','serial':'owned-serial'}
   (fixture/'run.json').write_text(json.dumps(state))
   iso=package/'dma.iso';iso.write_bytes(b'readonly test media')
   manifest=package/'manifest.json';manifest.write_text(json.dumps({'readonly':True,'iso_sha256':hashlib.sha256(iso.read_bytes()).hexdigest()}))
   changed=[]
   def qmp(endpoint,commands):
    self.assertEqual(endpoint,'owned-qmp');name,args=commands[0]
    if name=='query-status':return [{'status':'running'}]
    if name=='query-block':return [[{'inserted':{'file':str(iso)}}]]
    if name=='trace-event-get-state':return [[{'state':'enabled' if args['name']=='dma_complete' else 'disabled'}]]
    if name=='trace-event-set-state':changed.append((args['name'],args['enable']));return [{}]
    self.fail('Unexpected command '+name)
   with patch.object(dma.subprocess,'check_output',return_value='juke --config-dir '+str(fixture)),patch.object(dma,'owned_qemu',return_value=456),patch.object(dma,'qmp_execute',side_effect=qmp),patch.object(dma.hl,'run_demo',side_effect=RuntimeError('guest disconnected')):
    result=dma.capture(fixture,manifest,root/'result')
   self.assertFalse(result['passed']);self.assertIn('guest disconnected',result['errors'])
   self.assertEqual(changed[:5],[(name,True) for name in dma.EVENTS])
   self.assertEqual(changed[5:],[(name,name=='dma_complete') for name in reversed(dma.EVENTS)])
if __name__=='__main__':unittest.main()
