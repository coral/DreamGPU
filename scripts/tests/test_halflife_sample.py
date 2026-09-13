"""Real FIFO/child-process control tests; no guest or system profiler is launched."""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch
from scripts.benchmarks import halflife_sample as sampling

FAKE = r'''
import os,select,sys,time
control,ack,events,mode=sys.argv[1:]
c=os.open(control,os.O_RDWR);a=os.open(ack,os.O_RDWR)
with open(events,'w') as log:
 while True:
  ready=select.select([c,sys.stdin.fileno()],[],[],5)[0]
  if not ready:sys.exit(7)
  if sys.stdin.fileno() in ready:
   if not os.read(sys.stdin.fileno(),32):break
  if c in ready:
   command=os.read(c,32)
   log.write(command.decode());log.flush()
   os.write(a,b'wrong\n' if mode=='malformed' else b'ack\n')
   if mode=='early':break
os.close(c);os.close(a)
'''

class SamplerTests(unittest.TestCase):
    def setUp(self):
        self.folder=tempfile.TemporaryDirectory(prefix='dg-perf-control-')
        self.addCleanup(self.folder.cleanup)
        self.root=Path(self.folder.name)
        self.program=self.root/'fake.py';self.program.write_text(FAKE)
        self.real_popen=subprocess.Popen
        self.children=[]
        self.mode='normal'
        self.state={'pid':999,'native':'/frozen/qemu','qmp':'/owned/qmp','serial':'/owned/serial'}
        self.patchers=[patch.object(sampling.sys,'platform','linux'),
                       patch.object(sampling,'owned_qemu',return_value=1234),
                       patch.object(sampling.shutil,'which',side_effect=lambda n:'/usr/bin/'+n),
                       patch.object(sampling.subprocess,'Popen',side_effect=self.spawn)]
        for p in self.patchers:p.start();self.addCleanup(p.stop)

    def spawn(self,command,**kwargs):
        self.assertEqual(command[:2],['sudo','-n'])
        self.assertIn('130s',command)
        self.assertEqual(command[command.index('-p')+1],'1234,999')
        self.assertEqual(command[command.index('--clockid')+1],'mono')
        self.assertEqual(command[command.index('-D')+1],'-1')
        self.assertEqual(command[-2:],['--','/bin/cat'])
        control,ack=command[command.index('--control')+1][5:].split(',')
        p=self.real_popen([sys.executable,str(self.program),control,ack,str(self.root/'events'),self.mode],**kwargs)
        self.children.append(p);self.addCleanup(lambda:self.cleanup(p))
        return p

    @staticmethod
    def cleanup(p):
        if p.poll() is None:
            p.kill();p.wait(timeout=1)

    def test_acknowledged_enable_precedes_resume_and_disable_precedes_cleanup(self):
        sample=sampling.LaunchSample(self.state,self.root/'output')
        ready=time.monotonic();sample.phase('PROCESS_READY',ready)
        self.assertEqual((self.root/'events').read_text(),'enable\n')
        acknowledged=time.monotonic()
        summary=time.monotonic();sample.phase('TIMEDEMO_RESULT',summary)
        report=sample.finish({'PROCESS_READY':{'ack':'CONTINUE','ack_sent_monotonic_seconds':acknowledged},
                              'TIMEDEMO_RESULT':{'host_monotonic_seconds':summary}})
        self.assertTrue(report['coverage_proven'])
        self.assertEqual(report['exact_engine_interval'],'unknown')
        self.assertEqual((self.root/'events').read_text(),'enable\ndisable\n')
        self.assertEqual(report['exit_code'],0)
        self.assertLessEqual(report['enable']['ack_observed_monotonic_seconds'],acknowledged)
        self.assertGreaterEqual(report['disable']['command_sent_monotonic_seconds'],summary)
        self.assertFalse(list((self.root/'output').glob('*.fifo')))

    def test_malformed_ack_cleans_up_owned_child_without_coverage_claim(self):
        self.mode='malformed';sample=sampling.LaunchSample(self.state,self.root/'output')
        with self.assertRaisesRegex(RuntimeError,'acknowledgement'):sample.phase('PROCESS_READY',time.monotonic())
        self.assertFalse(sample.finish({})['coverage_proven'])
        self.assertIsNotNone(self.children[0].poll())
        self.assertFalse(list((self.root/'output').glob('*.fifo')))

    def test_no_boundary_legacy_runner_never_claims_sample(self):
        sample=sampling.LaunchSample(self.state,self.root/'output')
        self.assertFalse(sample.finish({})['coverage_proven']);self.assertEqual(self.children,[])

    def test_cleanup_without_summary_closes_sampler_and_marks_unproven(self):
        sample=sampling.LaunchSample(self.state,self.root/'output');sample.start()
        report=sample.finish({'PROCESS_READY':{'ack':'CONTINUE','ack_sent_monotonic_seconds':time.monotonic()}})
        self.assertFalse(report['coverage_proven']);self.assertEqual(report['exit_code'],0)

    def test_wrong_fixture_fails_before_profiler_or_game_can_be_resumed(self):
        with patch.object(sampling,'owned_qemu',side_effect=ValueError('wrong frozen child')):
            sample=sampling.LaunchSample(self.state,self.root/'output')
            with self.assertRaisesRegex(ValueError,'wrong frozen'):sample.start()
        self.assertFalse(sample.finish({})['coverage_proven']);self.assertFalse(self.children)

if __name__=='__main__':unittest.main()
