#!/usr/bin/env python3
"""Protocol and accounting tests, with no guest or GPU required."""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import json
import collections
import socket
import struct
import unittest
from scripts.benchmarks.bench import WebSocket, distribution, scenario_events, summarize, probe_sequence, probe_barcode, probe_media_path, perfetto, validate_probe, DESKTOP_SCENARIOS, png_dimensions, comparison_runs, validate_comparison_dimensions, verify_refresh, validate_comparison_refresh


class BenchTests(unittest.TestCase):
    def test_probe_frame_stages_use_ack_generation_not_next_frame(self):
        from scripts.benchmarks.bench import probe_frame_stages
        def event(name, ts, identity, value=0):
            return dict(name=name, ts=ts, id=identity, value=value)
        samples = [event('input.received', 10, 1, 1),
                   event('frame.published', 11, 7), event('frame.acquired', 12, 7),
                   event('frame.published', 20, 8), event('frame.acquired', 25, 8),
                   event('probe.ack', 30, 1, 8)]
        capture = dict(samples=samples, dropped=0, probe_sequence_ids=True)
        self.assertEqual(probe_frame_stages(capture), {'verified': True, 'rows': [
            {'sequence': 1, 'generation': 8, 'input_to_publication_us': 10,
             'publication_to_acquisition_us': 5, 'acquisition_to_ack_us': 5}]})
        for invalid in (dict(capture, dropped=1), dict(capture, probe_sequence_ids=False),
                        dict(capture, samples=samples[:3] + samples[4:]),
                        dict(capture, samples=samples + [samples[0]]),
                        dict(capture, samples=[dict(item, ts=31) if item['name'] == 'frame.acquired'
                                               else item for item in samples])):
            self.assertEqual(probe_frame_stages(invalid), {'verified': False, 'rows': []})

    def test_sibling_vm_cpu_is_summed_not_overwritten_by_paused_peer(self):
        from scripts.benchmarks.bench import cpu_summary
        processes = {1: {'executable': '/bundle/qemu-system-i386', 'cpu_seconds': .4},
                     2: {'executable': '/bundle/qemu-system-i386', 'cpu_seconds': 0},
                     3: {'executable': '/bundle/juke', 'cpu_seconds': .2}}
        self.assertEqual(cpu_summary(processes, 4), {'qemu-system-i386.core_percent': 10,
                                                   'juke.core_percent': 5, 'total.core_percent': 15})

    def test_refresh_guard_rejects_virtual_display_and_mid_capture_change(self):
        def capture(values):
            return {'samples': [dict(name='display.refresh_millihz', value=value) for value in values]}
        self.assertTrue(verify_refresh(capture([239760, 240000]), 240)['verified'])
        for values in ([], [0], [60000], [240000, 60000]):
            self.assertFalse(verify_refresh(capture(values), 240)['verified'])
        validate_comparison_refresh([{'display_refresh_hz': [239.76]}, {'display_refresh_hz': [240]}])
        for runs in ([{'display_refresh_hz': [240]}, {'display_refresh_hz': [60]}],
                     [{'display_refresh_hz': [240]}, {}],
                     [{'display_refresh_hz': [60], 'refresh_verification': {'verified': False}}]):
            with self.assertRaises(ValueError):
                validate_comparison_refresh(runs)

    def test_comparison_rejects_mismatched_guest_resolutions_in_old_captures(self):
        from pathlib import Path
        from tempfile import TemporaryDirectory
        def header(width, height):
            return b'\x89PNG\r\n\x1a\n\0\0\0\rIHDR' + struct.pack('!II', width, height) + bytes(9)
        with TemporaryDirectory() as directory:
            root = Path(directory)
            for index, dimensions in enumerate([(1280, 960), (1024, 768)], 1):
                (root / f'run-{index:02}.json').write_text(json.dumps({'summary': {}}))
                (root / f'ready-{index:02}.png').write_bytes(header(*dimensions))
            (root / 'run-01.perfetto.json').write_text('{}')
            runs = comparison_runs(root)
            self.assertEqual(len(runs), 2)
            with self.assertRaisesRegex(ValueError, 'resolutions differ'):
                validate_comparison_dimensions(runs)
            (root / 'ready-01.png').write_bytes(header(1024, 768))
            validate_comparison_dimensions(comparison_runs(root))

    def test_png_dimensions_require_valid_nonempty_header(self):
        for png in [b'', b'\x89PNG\r\n\x1a\n', bytes(33),
                    b'\x89PNG\r\n\x1a\n\0\0\0\rIHDR' + bytes(17)]:
            with self.assertRaises(ValueError): png_dimensions(png)

    def test_fixture_probe_media_is_automatically_reinserted(self):
        from pathlib import Path
        from tempfile import TemporaryDirectory
        from types import SimpleNamespace
        with TemporaryDirectory() as directory:
            args = SimpleNamespace(probe=True, probe_media=None, command='run', fixture=directory, vm='win2000')
            self.assertIsNone(probe_media_path(args))
            media = Path(directory) / 'machines' / 'win2000' / 'probes.img'
            media.parent.mkdir(parents=True)
            media.write_bytes(b'probe image')
            self.assertEqual(probe_media_path(args), media.resolve())
            args.command = 'attach'
            self.assertIsNone(probe_media_path(args))
            args.probe_media = str(media)
            self.assertEqual(probe_media_path(args), media.resolve())
            args.probe_media = str(media) + '.missing'
            with self.assertRaises(ValueError): probe_media_path(args)

    def test_desktop_scenarios_share_input_pacing_and_acknowledgement_ids(self):
        expected = scenario_events('keyboard-jitter', 10)
        for scenario in DESKTOP_SCENARIOS:
            self.assertEqual(scenario_events(scenario, 10), expected)

    def test_desktop_signature_distinguishes_old_probe_and_selected_workload(self):
        import zlib
        def png(mode, signature=True, width=320, height=240):
            sequence = 19
            colors = [(255, 0, 0), (0, 255, 0), (0, 0, 255)]
            colors += [(255,)*3 if sequence & (1 << bit) else (0,)*3 for bit in range(16)]
            colors += [(255, 0, 255), (0, 255, 255), (255, 255, 0)] if signature else [(0,)*3]*3
            colors += [(255,)*3 if mode & (1 << bit) else (0,)*3 for bit in range(4)]
            row = b''.join(bytes(color)*8 for color in colors)
            row += bytes(width*3-len(row))
            def chunk(kind, data):
                return struct.pack('!I', len(data))+kind+data+struct.pack('!I', zlib.crc32(kind+data))
            return (b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR', struct.pack('!IIBBBBB',width,height,8,2,0,0,0))
                    +chunk(b'IDAT',zlib.compress((b'\0'+row)*height))+chunk(b'IEND',b''))
        for mode in range(5):
            self.assertEqual(probe_barcode(png(mode)), (19, mode))
            self.assertEqual(probe_sequence(png(mode)), 19)
        self.assertEqual(probe_barcode(png(0, signature=False)), (19, None))
        self.assertEqual(probe_barcode(png(1, width=256)), (19, None))
        self.assertEqual(probe_barcode(png(1, height=200)), (19, None))

    def test_jittered_replay_is_complete_deterministic_and_spreads_timer_phases(self):
        events = scenario_events('keyboard-jitter', 10)
        self.assertEqual(events, scenario_events('keyboard-jitter', 10))
        self.assertEqual(len(events), 80)
        self.assertEqual([e['sequence'] for e in events[::2]], list(range(1,41)))
        self.assertEqual([e['at'] for e in events], sorted(e['at'] for e in events))
        self.assertGreater(events[0]['at'], 0)
        self.assertLess(events[-1]['at'], 10)
        self.assertGreater(len({round(e['at'] % .004, 5) for e in events[::2]}), 30)
        for down,up in zip(events[::2],events[1::2]):
            self.assertEqual(up['sequence'],down['sequence'])
            self.assertAlmostEqual(up['at']-down['at'],.04)
            self.assertEqual(down['event'],{'KeyDown':'A'})
            self.assertEqual(up['event'],{'KeyUp':'A'})

    def pair(self):
        client, peer = socket.socketpair()
        ws = WebSocket.__new__(WebSocket)
        ws.sock, ws.buffer = client, bytearray()
        self.addCleanup(client.close)
        self.addCleanup(peer.close)
        return ws, peer

    def test_fragmented_message_and_ping(self):
        ws, peer = self.pair()
        peer.sendall(b'\x01\x08{"type":' + b'\x89\x01x' + b'\x80\x05"ok"}')
        self.assertEqual(ws.receive(), {'type':'ok'})
        pong = peer.recv(100)
        self.assertEqual(pong[0], 0x8a)
        self.assertEqual(pong[-1] ^ pong[2], ord('x'))

    def test_client_mask_and_extended_length(self):
        ws, peer = self.pair()
        ws.send(b'x'*1000)
        wire = peer.recv(2000)
        self.assertEqual(wire[:4], b'\x81\xfe\x03\xe8')
        mask = wire[4:8]
        self.assertEqual(bytes(b ^ mask[i%4] for i,b in enumerate(wire[8:])), b'x'*1000)

    def test_unmatched_input_never_correlates_next_frame(self):
        samples = [dict(name='input.received',id=7,ts=100,dur=0,value=1,tid=1),
                   dict(name='probe.ack',id=8,ts=120,dur=0,value=3,tid=2)]
        result=summarize(dict(start_us=0,end_us=1000,dropped=0,samples=samples,probe_sequence_ids=True))
        self.assertEqual(result['latency_us'], {})
        samples.append(dict(name='probe.ack',id=7,ts=130,dur=0,value=4,tid=2))
        self.assertEqual(summarize(dict(start_us=0,end_us=1000,dropped=0,samples=samples,probe_sequence_ids=True))['latency_us']['input.received.keydown_to_probe.ack']['p50'],30)

    def test_sequence_shared_only_by_press_release_pair(self):
        events=scenario_events('keyboard',1)
        self.assertEqual([e['sequence'] for e in events],[1,1,2,2,3,3,4,4])
        self.assertEqual(distribution([0,10])['p95'],9.5)

    def test_probe_png_with_vertical_filter(self):
        import zlib
        pixels = bytearray()
        sequence = 0x8105
        for block in range(19):
            if block < 3:
                color = bytes(255 if channel == block else 0 for channel in range(3))
            else:
                color = bytes([255 if sequence & (1 << (block-3)) else 0])*3
            pixels.extend(color*8)
        raw = b'\x00'+pixels + (b'\x02'+bytes(len(pixels)))*7
        def chunk(kind, data):
            return struct.pack('!I',len(data))+kind+data+struct.pack('!I',zlib.crc32(kind+data))
        png = b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('!IIBBBBB',152,8,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b'')
        self.assertEqual(probe_sequence(png),sequence)

    def test_perfetto_edges_do_not_mix_input_or_submission_ids(self):
        def sample(name, identity, ts, value):
            return dict(name=name,id=identity,ts=ts,value=value,dur=0,tid=1)
        samples=[sample('input.received',1,10,1),sample('input.received',1,20,2),
                 sample('input.consumed',1,30,2),sample('input.consumed',1,40,1),
                 sample('frame.uploaded',1,50,100),sample('frame.submitted',8,60,1),
                 sample('gpu.completion_observed',1,65,99),sample('gpu.completion_observed',8,70,1)]
        trace=perfetto(dict(start_us=0,end_us=100,dropped=0,samples=samples))['traceEvents']
        flows=collections.defaultdict(list)
        for event in trace:
            if event.get('cat')=='pipeline': flows[event['args']['flow_key']].append(event['ts'])
            if event.get('cat')=='pipeline':
                self.assertIsInstance(event['bind_id'],int)
                self.assertEqual(event['ph'],'X')
                self.assertEqual(event['dur'],0)
                self.assertNotEqual(event.get('flow_in',False),event.get('flow_out',False))
        self.assertEqual(flows['input.consume:1:1:10'],[10,40])
        self.assertEqual(flows['input.consume:1:2:20'],[20,30])
        self.assertEqual(flows['frame.submit:1:50'],[50,60])
        self.assertEqual(flows['gpu.completion:8:60'],[60,70])
        self.assertNotIn('gpu.completion:1:50',flows)

    def test_host_probe_ids_and_missing_samples_are_not_correlated(self):
        samples=[dict(name='input.received',id=1,ts=10,dur=0,value=1,tid=1),
                 dict(name='probe.ack',id=1,ts=30,dur=0,value=3,tid=2)]
        capture=dict(start_us=0,end_us=100,dropped=0,samples=samples,probe_sequence_ids=False)
        self.assertEqual(summarize(capture)['latency_us'],{})
        self.assertFalse(any(e.get('name')=='input.probe' for e in perfetto(capture)['traceEvents']))
        capture.update(probe_sequence_ids=True,dropped=1)
        self.assertEqual(summarize(capture)['latency_us'],{})

    def test_probe_submission_uses_counter_not_gpu_sequence(self):
        samples=[dict(name=n,id=i,ts=t,value=v,dur=0,tid=1) for n,i,t,v in [
            ('input.received',1,10,1),('probe.ack',1,20,8),
            ('frame.submitted',1,25,99),('probe.submitted',1,40,123)]]
        capture=dict(start_us=0,end_us=100,dropped=0,samples=samples,probe_sequence_ids=True)
        self.assertEqual(summarize(capture)['latency_us']['input.received.keydown_to_probe.submitted']['p50'],30)
        edges=[e['ts'] for e in perfetto(capture)['traceEvents'] if e.get('args',{}).get('flow_key')=='input.probe_submit:1:10']
        self.assertEqual(edges,[10,40])
        capture['probe_sequence_ids']=False
        self.assertNotIn('input.received.keydown_to_probe.submitted',summarize(capture)['latency_us'])

    def test_probe_submission_derivation_requires_matching_generation(self):
        samples=[dict(name=n,id=i,ts=t,value=v,dur=0,tid=1) for n,i,t,v in [
            ('input.received',1,10,1),('probe.ack',1,20,8),
            ('frame.submitted',1,25,99),('frame.submitted',42,40,8)]]
        capture=dict(start_us=0,end_us=100,dropped=0,samples=samples,probe_sequence_ids=True)
        self.assertEqual(summarize(capture)['latency_us']['input.received.keydown_to_probe.submitted']['p50'],30)
        self.assertEqual(len(capture['samples']),4)
        samples.pop()
        self.assertNotIn('input.received.keydown_to_probe.submitted',summarize(capture)['latency_us'])

    def test_zero_frame_generation_cannot_create_probe_upload_flow(self):
        samples=[dict(name=n,id=i,ts=t,value=v,dur=0,tid=1) for n,i,t,v in [
            ('probe.ack',1,10,0),('frame.uploaded',0,20,100)]]
        capture=dict(start_us=0,end_us=100,dropped=0,samples=samples,probe_sequence_ids=True)
        self.assertFalse(any(e.get('name')=='probe.upload' for e in perfetto(capture)['traceEvents']))

    def test_missing_submission_invalidates_only_submission_endpoint(self):
        samples=[dict(name=n,id=i,ts=t,value=v,dur=0,tid=1) for n,i,t,v in [
            ('input.received',1,10,1),('input.received',2,20,1),
            ('probe.ack',1,30,0),('probe.ack',2,40,0),('probe.submitted',1,50,5)]]
        capture=dict(start_us=0,end_us=100,dropped=0,samples=samples,probe_sequence_ids=True)
        result=summarize(capture)
        self.assertIn('input.received.keydown_to_probe.ack',result['latency_us'])
        self.assertNotIn('input.received.keydown_to_probe.submitted',result['latency_us'])
        self.assertFalse(result['probe_submission_verification']['verified'])
        self.assertFalse(any(e.get('name')=='input.probe_submit' for e in perfetto(capture)['traceEvents']))

    def test_zero_queue_depth_is_counted(self):
        samples=[dict(name='input.queued',id=1,ts=i,dur=0,value=v,tid=1) for i,v in enumerate([0,2])]
        result=summarize(dict(start_us=0,end_us=100,dropped=0,samples=samples))
        self.assertEqual(result['values']['input.queued']['mean'],1)

    def test_cumulative_audio_counter_uses_delta_not_sum(self):
        samples=[dict(name='86box.audio.source_restarts',id=0,ts=i,dur=0,value=v,tid=1) for i,v in enumerate([10,15])]
        value=summarize(dict(start_us=0,end_us=100,dropped=0,samples=samples))['values']['86box.audio.source_restarts']
        self.assertEqual(value['delta'],5)
        self.assertNotIn('sum',value)

    def test_probe_rejects_lost_or_extra_guest_keys(self):
        replay=[dict(at=0,event={'KeyDown':'A'},sequence=1),dict(at=.25,event={'KeyDown':'A'},sequence=2)]
        samples=[dict(name='input.received',id=i,ts=i,value=1,dur=0,tid=1) for i in [1,2]]
        samples += [dict(name='probe.ack',id=1,ts=10,value=4,dur=0,tid=2)]
        capture=dict(start_us=0,end_us=100,dropped=0,samples=samples)
        self.assertFalse(validate_probe(capture,replay,True)['verified'])
        samples.append(dict(name='probe.ack',id=2,ts=20,value=5,dur=0,tid=2))
        self.assertTrue(validate_probe(capture,replay,True)['verified'])
        samples.append(dict(name='input.received',id=99,ts=30,value=1,dur=0,tid=1))
        self.assertFalse(validate_probe(capture,replay,True)['verified'])
        samples.append(dict(name='probe.ack',id=3,ts=40,value=6,dur=0,tid=2))
        self.assertFalse(validate_probe(capture,replay,True)['verified'])

    def test_close_rejected(self):
        ws, peer=self.pair()
        peer.sendall(b'\x88\x00')
        with self.assertRaises(EOFError): ws.receive()

if __name__ == '__main__': unittest.main()
