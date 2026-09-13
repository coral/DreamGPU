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
control,ack,events,mode,data=sys.argv[1:]
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
   if mode=='nul':os.write(a,b'ack\n\0')
   elif mode=='fragmented-nul':
    os.write(a,b'ack\n');time.sleep(.02);os.write(a,b'\0')
   elif mode=='extra-nul':os.write(a,b'ack\n\0\0')
   else:os.write(a,b'wrong\n' if mode=='malformed' else b'ack\n')
   if mode=='early':break
os.close(c);os.close(a)
if mode!='empty':
 with open(data,'wb') as result:result.write(b'profile records')
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
                       patch.object(sampling.subprocess,'Popen',side_effect=self.spawn),
                       patch.object(sampling,'event_summary',return_value={'events':2})]
        for p in self.patchers:p.start();self.addCleanup(p.stop)

    def spawn(self,command,**kwargs):
        self.assertEqual(command[:2],['sudo','-n'])
        self.assertIn('130s',command)
        self.assertEqual(command[command.index('-p')+1],'1234,999')
        self.assertEqual(command[command.index('--clockid')+1],'mono')
        self.assertEqual(command[command.index('-D')+1],'-1')
        self.assertEqual(command[command.index('-e')+1],'cpu-clock')
        self.assertEqual(command[-2:],['--','/bin/cat'])
        control,ack=command[command.index('--control')+1][5:].split(',')
        p=self.real_popen([sys.executable,str(self.program),control,ack,str(self.root/'events'),self.mode,command[command.index('-o')+1]],**kwargs)
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

    def test_real_perf_nul_terminator_and_fragmented_trailer(self):
        for mode in ['nul','fragmented-nul']:
            with self.subTest(mode=mode):
                self.mode=mode
                sample=sampling.LaunchSample(self.state,self.root/mode)
                sample.start();resume=time.monotonic();summary=time.monotonic()
                sample.phase('TIMEDEMO_RESULT',summary)
                report=sample.finish({'PROCESS_READY':{'ack':'CONTINUE','ack_sent_monotonic_seconds':resume},
                                      'TIMEDEMO_RESULT':{'host_monotonic_seconds':summary}})
                self.assertTrue(report['coverage_proven'])
                if mode=='nul':self.assertEqual(report['enable']['ack_bytes_hex'],'61636b0a00')
                self.assertIsNotNone(self.children[-1].poll())
        self.mode='extra-nul'
        sample=sampling.LaunchSample(self.state,self.root/'extra-nul')
        with self.assertRaisesRegex(RuntimeError,'acknowledgement'):sample.start()
        self.assertFalse(sample.finish({})['coverage_proven'])

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

    def test_partial_fifo_creation_preserves_preexisting_output(self):
        output=self.root/'output';output.mkdir()
        existing=output/'perf-ack.fifo';existing.write_text('owned by someone else')
        sample=sampling.LaunchSample(self.state,output)
        with self.assertRaises(FileExistsError):sample.start()
        self.assertEqual(existing.read_text(),'owned by someone else')
        self.assertFalse((output/'perf-control.fifo').exists())
        self.assertFalse(self.children)
        self.assertFalse(sample.finish({})['coverage_proven'])

    def test_empty_recording_and_second_start_cannot_claim_coverage(self):
        self.mode='empty';sample=sampling.LaunchSample(self.state,self.root/'output')
        sample.start();resume=time.monotonic()
        with self.assertRaisesRegex(RuntimeError,'single-use'):sample.start()
        summary=time.monotonic();sample.phase('TIMEDEMO_RESULT',summary)
        report=sample.finish({'PROCESS_READY':{'ack':'CONTINUE','ack_sent_monotonic_seconds':resume},
                              'TIMEDEMO_RESULT':{'host_monotonic_seconds':summary}})
        self.assertFalse(report['coverage_proven']);self.assertEqual(report['recording_bytes'],0)

class RecordedEventTests(unittest.TestCase):
    def inspect(self, text, exit_code=0):
        def execute(command, stdout, stderr, timeout):
            self.assertIn('-G',command);self.assertIn('--ns',command)
            self.assertEqual(command[command.index('-F')+1],'time')
            self.assertEqual(timeout,12)
            stdout.write(text)
            return subprocess.CompletedProcess(command,exit_code)
        with patch.object(sampling.shutil,'which',side_effect=lambda n:'/usr/bin/'+n),\
                patch.object(sampling.subprocess,'run',side_effect=execute):
            return sampling.event_summary(Path('/owned/perf.data'))

    def test_actual_event_range_handles_interleaved_threads_and_retains_empty(self):
        result=self.inspect(b' 12.000000123:\n\n 11.999999999:\n 12.100000001:\n')
        self.assertEqual(result['events'],3)
        self.assertEqual(result['first_event_monotonic_seconds'],11.999999999)
        self.assertEqual(result['last_event_monotonic_seconds'],12.100000001)
        self.assertEqual(self.inspect(b'')['events'],0)

    def test_bad_profiler_output_and_failed_inspection_are_not_sample_events(self):
        with self.assertRaisesRegex(ValueError,'timestamp record'):self.inspect(b'perf error\n')
        with self.assertRaisesRegex(RuntimeError,'exited'):self.inspect(b'',1)

if __name__=='__main__':unittest.main()
