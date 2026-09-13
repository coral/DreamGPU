#!/usr/bin/env python3
"""Dependency-free Juke capture/replay harness. Run --help for commands."""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import argparse
import base64
import collections
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import shutil
import socket
import struct
import subprocess
import sys
import time
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[2]
DESKTOP_SCENARIOS = {'desktop-fill': 1, 'desktop-copy': 2,
                     'desktop-scroll': 3, 'desktop-repaint': 4}


class WebSocket:
    """Small RFC6455 client: masking, fragmentation, ping/pong and bounded reads."""
    def __init__(self, url, timeout=15):
        uri = urlsplit(url)
        if uri.scheme != 'ws':
            raise ValueError('Use ws:// (control endpoint is local; tunnel remote ports with SSH)')
        self.sock = socket.create_connection((uri.hostname, uri.port or 80), timeout)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.buffer = bytearray()
        key = base64.b64encode(os.urandom(16)).decode()
        path = uri.path or '/'
        if uri.query:
            path += '?' + uri.query
        self.sock.sendall((f'GET {path} HTTP/1.1\r\nHost: {uri.netloc}\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n').encode())
        while b'\r\n\r\n' not in self.buffer:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise EOFError('Closed during WebSocket handshake')
            self.buffer.extend(chunk)
            if len(self.buffer) > 65536:
                raise ValueError('Oversized HTTP handshake')
        head, rest = bytes(self.buffer).split(b'\r\n\r\n', 1)
        self.buffer = bytearray(rest)
        lines = head.decode().split('\r\n')
        headers = {name.lower(): value.strip() for name, value in (line.split(':', 1) for line in lines[1:])}
        expected = base64.b64encode(hashlib.sha1((key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest()).decode()
        if lines[0].split()[1] != '101' or headers.get('sec-websocket-accept') != expected:
            raise ValueError('Invalid WebSocket upgrade')

    def read(self, size):
        while len(self.buffer) < size:
            chunk = self.sock.recv(max(4096, min(size - len(self.buffer), 65536)))
            if not chunk:
                raise EOFError('WebSocket closed')
            self.buffer.extend(chunk)
        result = bytes(self.buffer[:size])
        del self.buffer[:size]
        return result

    def send(self, payload, opcode=1):
        if isinstance(payload, dict):
            payload = json.dumps(payload).encode()
        mask = os.urandom(4)
        n = len(payload)
        header = bytes([0x80 | opcode, 0x80 | (n if n < 126 else 126 if n < 65536 else 127)])
        if n >= 126:
            header += struct.pack('!H' if n < 65536 else '!Q', n)
        self.sock.sendall(header + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(payload)))

    def receive(self):
        message = bytearray()
        started = False
        while True:
            a, b = self.read(2)
            opcode, n = a & 15, b & 127
            if n == 126:
                n = struct.unpack('!H', self.read(2))[0]
            elif n == 127:
                n = struct.unpack('!Q', self.read(8))[0]
            if b & 128 or a & 112 or n + len(message) > 512 * 1024 * 1024:
                raise ValueError('Unsupported/oversized server frame')
            payload = self.read(n)
            if opcode == 8:
                raise EOFError('Server closed WebSocket')
            if opcode == 9:
                self.send(payload, 10)
                continue
            if opcode == 10:
                continue
            if opcode == 1 and not started:
                started = True
            elif opcode != 0 or not started:
                raise ValueError('Unexpected WebSocket frame')
            message.extend(payload)
            if a & 128:
                return json.loads(message)

    def command(self, kind, expected=None, **fields):
        self.send(dict(type=kind, **fields))
        while True:
            response = self.receive()
            if response['type'] == 'error':
                raise RuntimeError(response['message'])
            if response['type'] == 'vm_switched' and expected != 'vm_switched':
                continue
            if expected is None or response['type'] == expected:
                return response
            raise RuntimeError(f'Expected {expected}, got {response["type"]}')

    def close(self):
        try:
            self.send(b'', 8)
        except OSError:
            pass
        self.sock.close()


def output(command):
    result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
    return result.stdout.strip() if result.returncode == 0 else None


def metadata(args):
    return dict(schema=1, created=time.strftime('%Y-%m-%dT%H:%M:%S%z'),
                host=platform.node(), platform=platform.platform(),
                revision=args.revision or output(['git', 'rev-parse', 'HEAD']),
                binary_sha256=hashlib.sha256(Path(args.binary).read_bytes()).hexdigest() if hasattr(args, 'binary') else None,
                instrumentation=not args.cpu_only,
                quiet_loop_trace=bool(os.environ.get('JUKE_PERF_QUIET_LOOPS')),
                render_qos=os.environ.get('JUKE_RENDER_QOS'),
                metal_shared_texture=os.environ.get('JUKE_METAL_SHARED_TEXTURE'),
                metal_direct_texture=os.environ.get('JUKE_METAL_DIRECT_TEXTURE'),
                qemu_input_refresh_ms=os.environ.get('JUKE_QEMU_INPUT_REFRESH_MS'),
                experiment_environment_note='Historical comparison-binary metadata only; current runtime has no performance-mode switches.',
                dirty=output(['git', 'status', '--porcelain']),
                submodules=output(['git', 'submodule', 'status']),
                rust=output(['rustc', '--version']),
                label=args.label, scenario=args.scenario, notes=args.notes,
                injection=args.injection, display=output(['kscreen-doctor', '-o']) if shutil.which('kscreen-doctor') else output(['system_profiler', 'SPDisplaysDataType']) if sys.platform == 'darwin' else None)


def distribution(values):
    values = sorted(values)
    if not values:
        return dict(count=0)
    def pct(p):
        index = (len(values) - 1) * p
        lo = int(index)
        return values[lo] + (values[min(lo + 1, len(values) - 1)] - values[lo]) * (index - lo)
    return dict(count=len(values), mean=sum(values)/len(values), p50=pct(.5), p95=pct(.95), p99=pct(.99), max=values[-1])


def samples_with_probe_submissions(capture):
    """Link old candidate telemetry by immutable guest generation, never next frame."""
    samples = list(capture['samples'])
    if any(sample['name']=='probe.submitted' for sample in samples):
        return samples
    submissions = collections.defaultdict(list)
    for sample in sorted(samples,key=lambda sample:sample['ts']):
        if sample['name']=='frame.submitted' and sample.get('value',0):
            submissions[sample['value']].append(sample)
    for ack in capture['samples']:
        if ack['name']!='probe.ack' or not ack.get('value',0):
            continue
        submitted = next((sample for sample in submissions[ack['value']] if sample['ts']>=ack['ts']),None)
        if submitted:
            samples.append(dict(name='probe.submitted',id=ack['id'],ts=submitted['ts'],
                                value=submitted['id'],dur=0,tid=submitted.get('tid',0),
                                derived_from_generation=ack['value']))
    return samples


def probe_submission_verification(capture, samples):
    expected = sorted({sample['id'] for sample in capture['samples']
                       if sample['name']=='input.received' and sample.get('value')==1 and sample['id']})
    submitted = sorted({sample['id'] for sample in samples if sample['name']=='probe.submitted' and sample['id']})
    verified = bool(capture.get('probe_sequence_ids',False) and not capture['dropped']
                    and expected and submitted==expected)
    return dict(verified=verified, expected=expected, submitted=submitted,
                reason=None if verified else 'GPU-submitted probe counters do not match the complete verified replay')


def summarize(capture):
    samples = samples_with_probe_submissions(capture)
    submission_verification = probe_submission_verification(capture, samples)
    durations, counters, events = collections.defaultdict(list), collections.defaultdict(list), collections.Counter()
    cumulative = {'86box.audio.submitted_buffers','86box.audio.source_restarts','86box.audio.discarded_buffers'}
    numeric = cumulative | {sample['name'] for sample in samples if sample.get('value',0)}
    numeric |= {sample['name'] for sample in samples if sample['name'].endswith(('.queued','.queue_depth','.pending'))}
    for sample in samples:
        events[sample['name']] += 1
        if sample.get('dur', 0):
            durations[sample['name']].append(sample['dur'])
        if sample['name'] in numeric:
            counters[sample['name']].append(sample.get('value',0))
    seconds = max((capture['end_us'] - capture['start_us'])/1e6, 1e-9)
    # Correlate stages with explicit IDs and edges; never an arbitrary next frame.
    by_id = collections.defaultdict(dict)
    for sample in sorted(samples, key=lambda sample: sample['ts']):
        if sample.get('id', 0):
            name = sample['name']
            if name in ('input.received','input.consumed'):
                name += {1:'.keydown',2:'.keyup'}.get(sample.get('value',0),'.other')
            by_id[sample['id']].setdefault(name, sample['ts'])
    latency = collections.defaultdict(list)
    pairs = [('input.received.keydown','input.consumed.keydown'),
             ('input.received.keyup','input.consumed.keyup'),
             ('input.received.other','input.consumed.other'),
             ('input.received.keydown','86box.input.consumed'),
             ('input.received.keydown','probe.ack'),
             ('input.received.keydown','probe.submitted'),
             ('frame.acquired','frame.uploaded'),
             ('frame.submitted','gpu.completion_observed')]
    for stages in by_id.values():
        for begin, end in pairs:
            if begin.startswith('input.') and capture['dropped']:
                continue
            if end in ('probe.ack','probe.submitted') and not capture.get('probe_sequence_ids',False):
                continue
            if end == 'probe.submitted' and not submission_verification['verified']:
                continue
            if begin in stages and end in stages and stages[end] >= stages[begin]:
                latency[f'{begin}_to_{end}'].append(stages[end]-stages[begin])
    return dict(seconds=seconds, dropped=capture['dropped'],
                probe_submission_verification=submission_verification,
                duration_us={name: distribution(values) for name, values in durations.items()},
                latency_us={name: distribution(values) for name, values in latency.items()},
                events=dict(events), rate_hz={name: n/seconds for name, n in events.items()},
                values={name: dict(**(dict(first=v[0],last=v[-1],delta=v[-1]-v[0]) if name in cumulative else dict(sum=sum(v))), **distribution(v)) for name, v in counters.items()})


def validate_probe(capture, replay_events, enabled):
    expected = [event.get('sequence', index) for index,event in enumerate(replay_events,1)
                if isinstance(event['event'],dict) and 'KeyDown' in event['event']]
    received = [sample['id'] for sample in sorted(capture['samples'],key=lambda sample:sample['ts'])
                if sample['name']=='input.received' and sample.get('value')==1]
    acknowledged = sorted({sample['id'] for sample in capture['samples'] if sample['name']=='probe.ack' and sample['id']})
    reason = None
    if not enabled: reason = 'No dispatch probe with verified reset was requested'
    elif capture['dropped']: reason = 'Trace dropped samples'
    elif expected != list(range(1,len(expected)+1)) or not expected: reason = 'Replay must number each keydown consecutively from 1'
    elif received != expected: reason = 'Received keydowns differ from replay (missing or extra host input)'
    elif acknowledged != expected: reason = 'Guest acknowledgement counts differ from replay; latency is unverified'
    return dict(verified=reason is None, reason=reason, expected=expected, received=received, acknowledged=acknowledged)


def perfetto(capture):
    events = []
    samples = sorted(samples_with_probe_submissions(capture), key=lambda sample: sample['ts'])
    submission_verification = probe_submission_verification(capture, samples)
    index = collections.defaultdict(list)
    for sample in samples:
        event = dict(name=sample['name'], ts=sample['ts'], pid=1, tid=sample.get('tid', 0),
                     args=dict(id=sample.get('id', 0), value=sample.get('value', 0)))
        if 'derived_from_generation' in sample:
            event['args']['derived_from_generation']=sample['derived_from_generation']
        if sample.get('dur', 0):
            event.update(ph='X', dur=sample['dur'])
        else:
            event.update(ph='i', s='t')
        events.append(event)
        index[(sample['name'],sample.get('id',0))].append(sample)
        if sample['name']=='frame.submitted':
            index[('submitted_generation',sample.get('value',0))].append(sample)
    flow_ids = {}
    def connect(name, identity, begin, end):
        if end['ts'] < begin['ts']:
            return
        flow_key = f'{name}:{identity}:{begin["ts"]}'
        flow_id = flow_ids.setdefault(flow_key, len(flow_ids)+1)
        for direction, sample in [('flow_out',begin),('flow_in',end)]:
            # Perfetto IDs must be integers/hex, not arbitrary strings. V2
            # flows bind directly to these zero-duration stage anchors rather
            # than whichever unrelated slice last occupied the same thread.
            event = dict(name=name,cat='pipeline',ph='X',dur=0,id=flow_id,
                         bind_id=flow_id,ts=sample['ts'],pid=1,
                         tid=sample.get('tid',0),args=dict(flow_key=flow_key))
            event[direction] = True
            events.append(event)
    def first_after(name, identity, timestamp, edge=None):
        return next((s for s in index[(name,identity)] if s['ts']>=timestamp and (edge is None or s.get('value',0)==edge)),None)
    for sample in samples:
        identity=sample.get('id',0)
        if not identity:
            continue
        name=sample['name']
        if name == 'input.received' and not capture['dropped']:
            edge=sample.get('value',0)
            consumed=first_after('input.consumed',identity,sample['ts'],edge)
            if consumed: connect('input.consume',f'{identity}:{edge}',sample,consumed)
            if edge==1 and capture.get('probe_sequence_ids',False):
                ack=first_after('probe.ack',identity,sample['ts'])
                if ack: connect('input.probe',identity,sample,ack)
                submitted=first_after('probe.submitted',identity,sample['ts'])
                if submitted and submission_verification['verified']: connect('input.probe_submit',identity,sample,submitted)
        elif name in ('frame.published','frame.acquired'):
            next_stage='frame.acquired' if name=='frame.published' else 'frame.uploaded'
            end=first_after(next_stage,identity,sample['ts'])
            if end: connect(name+'->'+next_stage,identity,sample,end)
        elif name == 'frame.uploaded':
            submitted=first_after('submitted_generation',identity,sample['ts'])
            if submitted: connect('frame.submit',identity,sample,submitted)
        elif name == 'frame.submitted':
            completed=first_after('gpu.completion_observed',identity,sample['ts'])
            if completed: connect('gpu.completion',identity,sample,completed)
        elif name == 'probe.ack' and sample.get('value',0):
            uploaded=first_after('frame.uploaded',sample.get('value'),sample['ts'])
            if uploaded: connect('probe.upload',identity,sample,uploaded)
    return dict(traceEvents=events, displayTimeUnit='ms', metadata=dict(dropped=capture['dropped']))


def scenario_events(name, duration):
    if name == 'idle':
        return []
    path = Path(name)
    if path.is_file():
        events = json.loads(path.read_text())
    elif name == 'keyboard':
        events = [dict(at=at + offset, sequence=i+1, event={kind: 'A'})
                  for i, at in enumerate([i*.25 for i in range(int(duration*4))])
                  for offset, kind in [(0, 'KeyDown'), (.04, 'KeyUp')]]
    elif name == 'keyboard-jitter' or name in DESKTOP_SCENARIOS:
        # Fixed integer PRNG and seed: spread input across guest/display timer
        # phases while retaining four presses per second and a fixed duration.
        state = 0x4A554B45
        events = []
        for i in range(int(duration*4)):
            state = (1664525*state + 1013904223) & 0xffffffff
            at = i*.25 + .125 + (state/(1 << 32)-.5)*.15
            events.extend(dict(at=at+offset, sequence=i+1, event={kind:'A'})
                          for offset,kind in [(0,'KeyDown'),(.04,'KeyUp')])
    elif name == 'mouse':
        events = [dict(at=i/240, event=dict(MouseMoveRel=dict(dx=3 if i//120 % 2 == 0 else -3, dy=0))) for i in range(int(duration*240))]
    else:
        raise ValueError('Scenario must be idle, keyboard, keyboard-jitter, mouse, '
                         'desktop-fill, desktop-copy, desktop-scroll, desktop-repaint, or a JSON replay path')
    last = -1.0
    for entry in events:
        if not isinstance(entry.get('at'), (int, float)) or not math.isfinite(entry['at']) or entry['at'] < last or entry['at'] < 0:
            raise ValueError('Replay times must be finite, nonnegative and sorted')
        last = entry['at']
    return [e for e in events if e['at'] < duration]


def replay(ws, events, duration, host=None):
    start = time.monotonic()
    timing = []
    for sequence, entry in enumerate(events, 1):
        deadline = start + entry['at']
        time.sleep(max(0, deadline - time.monotonic()))
        sent = time.monotonic()
        sent_us = time.clock_gettime_ns(time.CLOCK_MONOTONIC)//1000
        if host:
            host.send(entry['event'])
        else:
            ws.command('inject_input', 'ok', event=entry['event'], sequence=entry.get('sequence', sequence))
        timing.append(dict(sequence=entry.get('sequence', sequence), scheduled=entry['at'], sent=sent-start, sent_us=sent_us, roundtrip_ms=(time.monotonic()-sent)*1000))
    time.sleep(max(0, start + duration - time.monotonic()))
    return timing


def connect_until(url, deadline, process=None):
    while True:
        try:
            return WebSocket(url)
        except (OSError, EOFError):
            if time.monotonic() >= deadline or process and process.poll() is not None:
                raise
            time.sleep(.2)


def png_dimensions(png):
    if len(png) < 33 or png[:8] != b'\x89PNG\r\n\x1a\n' or png[8:16] != b'\0\0\0\rIHDR':
        raise ValueError('Readiness screenshot is not a PNG with an IHDR header')
    width, height = struct.unpack_from('!II', png, 16)
    if not width or not height:
        raise ValueError('Readiness screenshot has empty dimensions')
    return width, height


def observed_refresh_hz(capture):
    return sorted({sample['value'] / 1000 for sample in capture['samples']
                   if sample['name'] == 'display.refresh_millihz' and sample['value'] > 0})


def verify_refresh(capture, expected):
    observed = observed_refresh_hz(capture)
    verified = bool(observed) and all(abs(value - expected) <= .5 for value in observed)
    return dict(expected_hz=expected, observed_hz=observed, tolerance_hz=.5, verified=verified,
                reason=None if verified else 'Recorded display refresh differs from the requirement or is unavailable')


def probe_barcode(png):
    """Decode (acknowledgement, desktop workload) from an RGB/RGBA screenshot.

    The original input probe remains valid, with no desktop workload identity.
    An extended signature is mandatory when measuring desktop work: an old
    DGPUPROB executable must never masquerade as a fast desktop implementation.
    """
    import zlib
    if png[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('Invalid PNG')
    offset, compressed = 8, bytearray()
    width = height = channels = 0
    while offset < len(png):
        length = struct.unpack_from('!I', png, offset)[0]
        kind = png[offset+4:offset+8]
        data = png[offset+8:offset+8+length]
        if kind == b'IHDR':
            width, height, depth, color, compression, filtering, interlace = struct.unpack('!IIBBBBB',data)
            if depth != 8 or color not in (2,6) or interlace:
                raise ValueError('Probe decoder requires non-interlaced 8-bit RGB/RGBA PNG')
            channels = 3 if color == 2 else 4
        if kind == b'IDAT':
            compressed.extend(data)
        offset += length + 12
    if width < 152 or height < 8:
        return None
    rows = zlib.decompress(compressed)
    previous = bytearray(width*channels)
    for y in range(5):
        begin = y*(1+width*channels)
        filtering = rows[begin]
        row = bytearray(rows[begin+1:begin+1+width*channels])
        for x in range(len(row)):
            left = row[x-channels] if x >= channels else 0
            up = previous[x]
            corner = previous[x-channels] if x >= channels else 0
            if filtering == 1: predictor = left
            elif filtering == 2: predictor = up
            elif filtering == 3: predictor = (left+up)//2
            elif filtering == 4:
                p = left+up-corner
                distances = [abs(p-left),abs(p-up),abs(p-corner)]
                predictor = [left,up,corner][distances.index(min(distances))]
            elif filtering == 0: predictor = 0
            else: raise ValueError('Invalid PNG row filter')
            row[x] = (row[x]+predictor)&255
        previous = row
    def pixel(block): return row[(block*8+4)*channels:(block*8+4)*channels+3]
    for block in range(3):
        if any(value < 200 if c == block else value > 55 for c,value in enumerate(pixel(block))):
            return None
    sequence = 0
    for bit in range(16):
        values = pixel(bit+3)
        if min(values) > 200: sequence |= 1<<bit
        elif max(values) >= 55: return None
    mode = None
    if width >= 320 and height >= 240:
        signature = [(255, 0, 255), (0, 255, 255), (255, 255, 0)]
        if all(all(abs(value - expected) < 55 for value, expected in zip(pixel(block + 19), color))
               for block, color in enumerate(signature)):
            bits = [pixel(block) for block in range(22, 26)]
            if all(min(values) > 200 or max(values) < 55 for values in bits):
                mode = sum(1 << bit for bit, values in enumerate(bits) if min(values) > 200)
    return sequence, mode


def probe_sequence(png):
    barcode = probe_barcode(png)
    return barcode[0] if barcode is not None else None


def qmp_execute(endpoint, commands):
    connection=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM)
    connection.settimeout(15)
    connection.connect(endpoint)
    try:
        with connection.makefile('rwb',buffering=0) as stream:
            greeting=json.loads(stream.readline())
            if 'QMP' not in greeting: raise ValueError('Invalid QMP greeting')
            replies=[]
            for name,arguments in [('qmp_capabilities',{}),*commands]:
                stream.write(json.dumps(dict(execute=name,arguments=arguments)).encode()+b'\n')
                while True:
                    response=json.loads(stream.readline())
                    if 'error' in response: raise RuntimeError(str(response['error']))
                    if 'return' in response:
                        replies.append(response['return']); break
            return replies[1:]
    finally:
        connection.close()


def qmp_screenshot(endpoint):
    import tempfile
    with tempfile.TemporaryDirectory(prefix='dg-probe-ready-') as folder:
        filename=Path(folder)/'ready.png'
        qmp_execute(endpoint,[('screendump',dict(filename=str(filename),format='png'))])
        return filename.read_bytes()


def qmp_probe_media(endpoint, filename):
    blocks=qmp_execute(endpoint,[('query-block',{})])[0]
    device=next((b['device'] for b in blocks if b.get('device','').startswith('floppy')),None)
    if not device:
        raise RuntimeError('Probe floppy controller missing; attach a readonly floppy drive in the disposable config')
    qmp_execute(endpoint,[('blockdev-change-medium',{'device':device,'filename':str(Path(filename).resolve()),'format':'raw','read-only-mode':'read-only'})])


def probe_media_path(args):
    """Resolve media once; reinsert it after *every* RAM snapshot restore."""
    if args.probe_media:
        path = Path(args.probe_media).resolve()
        if not path.is_file():
            raise ValueError('Probe media does not exist: ' + str(path))
        return path
    if args.probe and args.command == 'run':
        path = Path(args.fixture).resolve() / 'machines' / args.vm / 'probes.img'
        if path.is_file():
            return path
    return None



def cpu_summary(processes, seconds):
    """Aggregate sibling VMs with the same executable without losing their CPU."""
    result = {}
    for entry in processes.values():
        key = Path(entry['executable']).name + '.core_percent'
        result[key] = result.get(key, 0) + entry['cpu_seconds'] / seconds * 100
    result['total.core_percent'] = sum(result.values())
    return result


def process_stats(root):
    if not root:
        return {}
    entries = {}
    if sys.platform == 'linux':
        ticks = os.sysconf('SC_CLK_TCK')
        page_kib = os.sysconf('SC_PAGE_SIZE')/1024
        for directory in Path('/proc').iterdir():
            if not directory.name.isdecimal():
                continue
            try:
                stat = (directory/'stat').read_text()
                _, delimiter, tail = stat.rpartition(')')
                if not delimiter:
                    continue
                fields = tail.split()
                # tail begins at field3 (state); utime/stime are14/15.
                executable = os.readlink(directory/'exe')
                entries[int(directory.name)] = dict(parent=int(fields[1]),
                    cpu_seconds=(int(fields[11])+int(fields[12]))/ticks,
                    rss_kib=int(fields[21])*page_kib, executable=executable)
            except (OSError, ValueError, IndexError):
                continue  # Process exited, is inaccessible, or is a kernel thread.
        selected = {root}
        while True:
            children = {pid for pid, entry in entries.items() if entry['parent'] in selected}
            if children <= selected:
                break
            selected |= children
        return {str(pid): entries[pid] for pid in selected if pid in entries}
    rows = output(['ps', '-axo', 'pid=,ppid=,time=,rss=,comm=']) or ''
    for row in rows.splitlines():
        fields = row.strip().split(None, 4)
        if len(fields) != 5:
            continue
        pid, parent, cpu, rss, name = fields
        days, _, rest = cpu.rpartition('-')
        seconds = 0.0
        for part in rest.split(':'):
            seconds = seconds * 60 + float(part)
        seconds += float(days or 0)*86400
        entries[int(pid)] = dict(parent=int(parent), cpu_seconds=seconds, rss_kib=int(rss), executable=name)
    selected = {root}
    while True:
        children = {pid for pid, entry in entries.items() if entry['parent'] in selected}
        if children <= selected:
            break
        selected |= children
    return {str(pid): entries[pid] for pid in selected if pid in entries}


def macos_window_bounds(pid):
    """Read public window geometry without requesting input-control privileges."""
    if sys.platform != 'darwin' or not pid:
        return None
    import ctypes, plistlib
    cg=ctypes.CDLL('/System/Library/Frameworks/CoreGraphics.framework/CoreGraphics')
    cf=ctypes.CDLL('/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation')
    cg.CGWindowListCopyWindowInfo.argtypes=[ctypes.c_uint32,ctypes.c_uint32]
    cg.CGWindowListCopyWindowInfo.restype=ctypes.c_void_p
    cf.CFPropertyListCreateData.argtypes=[ctypes.c_void_p,ctypes.c_void_p,ctypes.c_long,ctypes.c_ulong,ctypes.c_void_p]
    cf.CFPropertyListCreateData.restype=ctypes.c_void_p
    cf.CFDataGetLength.argtypes=[ctypes.c_void_p];cf.CFDataGetLength.restype=ctypes.c_long
    cf.CFDataGetBytePtr.argtypes=[ctypes.c_void_p];cf.CFDataGetBytePtr.restype=ctypes.c_void_p
    cf.CFRelease.argtypes=[ctypes.c_void_p]
    windows=cg.CGWindowListCopyWindowInfo(0,0);data=None
    try:
        if not windows:return None
        data=cf.CFPropertyListCreateData(None,windows,100,0,None)
        if not data:return None
        entries=plistlib.loads(ctypes.string_at(cf.CFDataGetBytePtr(data),cf.CFDataGetLength(data)))
        entries=[entry for entry in entries if entry.get('kCGWindowOwnerPID')==int(pid) and entry.get('kCGWindowLayer')==0]
        if not entries:return None
        window=max(entries,key=lambda entry:entry['kCGWindowBounds']['Width']*entry['kCGWindowBounds']['Height'])
        class Point(ctypes.Structure): _fields_=[('x',ctypes.c_double),('y',ctypes.c_double)]
        class Size(ctypes.Structure): _fields_=[('width',ctypes.c_double),('height',ctypes.c_double)]
        class Rect(ctypes.Structure): _fields_=[('origin',Point),('size',Size)]
        cg.CGMainDisplayID.restype=ctypes.c_uint32
        cg.CGDisplayBounds.argtypes=[ctypes.c_uint32];cg.CGDisplayBounds.restype=Rect
        main_id=cg.CGMainDisplayID();area=cg.CGDisplayBounds(main_id)
        bounds=window['kCGWindowBounds']
        inside=(bounds['X']>=area.origin.x and bounds['Y']>=area.origin.y and
                bounds['X']+bounds['Width']<=area.origin.x+area.size.width and
                bounds['Y']+bounds['Height']<=area.origin.y+area.size.height)
        return dict(bounds=bounds,onscreen=window.get('kCGWindowIsOnscreen'),
                    main_display=dict(id=main_id,x=area.origin.x,y=area.origin.y,width=area.size.width,height=area.size.height),
                    inside_main_display=inside,coordinate_space='CoreGraphics global logical screen points')
    finally:
        if data:cf.CFRelease(data)
        if windows:cf.CFRelease(windows)


def place_macos_window(pid, position):
    if sys.platform != 'darwin' or not pid:
        raise ValueError('--window-position requires macOS and a launched app or --pid')
    try:
        x,y = map(int,position.split(','))
    except (ValueError,AttributeError):
        raise ValueError('Window position must be X,Y in macOS logical screen points')
    script = f'''tell application "System Events"
        tell (first application process whose unix id is {int(pid)})
            set frontmost to true
            set position of window 1 to {{{x}, {y}}}
            return {{position of window 1, size of window 1}}
        end tell
    end tell'''
    result=subprocess.check_output(['osascript','-e',script],text=True).strip()
    import re
    values=[int(v) for v in re.findall(r'-?\d+',result)]
    if len(values)!=4 or values[:2]!=[x,y]:
        raise RuntimeError('Window placement verification failed: '+result)
    return dict(position=values[:2],size=values[2:],coordinate_space='macOS AX logical screen points',foreground=True)


def native_artifacts(root):
    if not root:
        return []
    paths={entry['executable'] for entry in process_stats(root).values()}
    if sys.platform=='linux':
        try:
            for line in Path(f'/proc/{root}/maps').read_text().splitlines():
                fields=line.split(None,5)
                if len(fields)==6 and 'lib86box' in fields[5]: paths.add(fields[5])
        except OSError:
            pass
    elif sys.platform=='darwin' and shutil.which('lsof'):
        listing=output(['lsof','-p',str(root),'-Fn']) or ''
        paths.update(line[1:] for line in listing.splitlines() if line.startswith('n') and 'lib86box' in line)
    result=[]
    for name in sorted(paths):
        try:
            path=Path(name).resolve(strict=True)
            result.append(dict(path=str(path),sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
        except OSError:
            result.append(dict(path=name,sha256=None))
    return result


def capture(args):
    desktop_mode = DESKTOP_SCENARIOS.get(args.scenario)
    if desktop_mode is not None:
        args.probe = True
    if args.expect_guest_size and not args.probe:
        raise ValueError('--expect-guest-size requires --probe or a desktop scenario')
    if args.expect_refresh_hz and args.cpu_only:
        raise ValueError('--expect-refresh-hz requires instrumentation; omit --cpu-only')
    target = Path(args.output)
    target.mkdir(parents=True, exist_ok=False)
    manifest = metadata(args)
    manifest['capture_conditions'] = {
        'duration_seconds': args.duration, 'warmup_seconds': args.warmup,
        'snapshot': args.snapshot, 'probe': args.probe,
        'setup_sha256': hashlib.sha256(Path(args.setup).read_bytes()).hexdigest() if args.setup else None,
    }
    if desktop_mode is not None:
        manifest['desktop_workload'] = dict(abi=1, mode=desktop_mode,
            operations_per_keydown=16, pacing='keyboard-jitter',
            endpoint='GDI flush and visible acknowledgement after each batch',
            note='Fixed-rate workload latency; not a measurement of maximum GDI throughput')
    events = scenario_events(args.scenario, args.duration)
    manifest['replay_definition'] = dict(
        sha256=hashlib.sha256(json.dumps(events,sort_keys=True,separators=(',',':')).encode()).hexdigest(),
        count=len(events), events=events if len(events)<=512 else None,
        seed=0x4A554B45 if args.scenario=='keyboard-jitter' or desktop_mode is not None else None)
    process = None
    log = None
    ws = None
    host = None
    try:
        if args.injection == 'host':
            from scripts.automation.host_input import HostInput
            host = HostInput()
        if args.command == 'run':
            uri = urlsplit(args.url)
            try:
                existing = socket.create_connection((uri.hostname, uri.port or 80), .2)
            except ConnectionRefusedError:
                pass
            else:
                existing.close()
                raise RuntimeError('Control port already in use; refusing to attach a launched run to an existing app')
            fixture = Path(args.fixture).resolve()
            if not (fixture / '.juke-benchmark-fixture.json').exists():
                raise ValueError('run requires a disposable directory created by bench.py fixture')
            manifest['fixture'] = json.loads((fixture / '.juke-benchmark-fixture.json').read_text())
            manifest['fixture_files'] = {str(path.relative_to(fixture)):hashlib.sha256(path.read_bytes()).hexdigest()
                for path in [fixture/'config.toml', fixture/'machines'/args.vm/'machine.toml', fixture/'machines'/args.vm/'86box.cfg', fixture/'machines'/args.vm/'probes.img'] if path.is_file()}
            log = (target/'juke.log').open('wb')
            environment=os.environ.copy()
            if args.window_position:
                environment['JUKE_WINDOW_POSITION']=args.window_position
            process = subprocess.Popen([args.binary, '--config-dir', str(fixture), '-c', '-m', args.vm], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT,env=environment)
        ws = connect_until(args.url, time.monotonic()+args.ready_timeout, process)
        manifest['active_vm'] = ws.command('get_active', 'active_vm')
        manifest['vm_state'] = ws.command('get_vm_state', 'vm_detailed_state', vm_id=None)
        # Readiness establishes a running backend. Probe readiness is separate.
        if args.command == 'run':
            deadline = time.monotonic()+args.ready_timeout
            while not any(v['id'] == args.vm and v['state'].lower() == 'running' for v in manifest['vm_state']['vms']):
                if time.monotonic() > deadline:
                    raise TimeoutError('Guest backend did not become running')
                time.sleep(.25)
                manifest['vm_state'] = ws.command('get_vm_state', 'vm_detailed_state', vm_id=None)
        manifest['native_artifacts'] = native_artifacts(process.pid if process else args.pid)
        qmp_endpoint = args.qmp
        if args.command == 'run' and not qmp_endpoint:
            import re
            match = re.search(r'-qmp unix:([^,\s]+)', (target/'juke.log').read_text())
            if match:
                qmp_endpoint = match.group(1)
        manifest['probe_readiness'] = 'qmp_screendump' if qmp_endpoint else 'juke_screenshot'
        probe_media = probe_media_path(args)
        if args.probe_media and not qmp_endpoint:
            raise ValueError('Reinserting probe media requires a QEMU monitor (--qmp for attach)')
        if probe_media and qmp_endpoint:
            manifest['probe_media'] = dict(path=str(probe_media),
                sha256=hashlib.sha256(probe_media.read_bytes()).hexdigest(),
                reinsert_after_snapshot=True)
        if args.window_position and not process:
            manifest['window'] = place_macos_window(args.pid,args.window_position)
        if process:
            manifest['window'] = macos_window_bounds(process.pid)
            manifest['requested_window_position'] = args.window_position
        (target/'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
        for repetition in range(1, args.repeat+1):
            guest_dimensions = None
            if args.snapshot:
                vm_id = args.vm if args.command == 'run' else manifest['active_vm']['vm_id']
                if qmp_endpoint:
                    response=qmp_execute(qmp_endpoint,[('human-monitor-command',{'command-line':'loadvm '+json.dumps(args.snapshot)})])[0]
                    diagnostics=[line for line in response.splitlines() if line.strip() and 'no host audio driver' not in line]
                    if diagnostics: raise RuntimeError('QEMU snapshot restore: '+str(diagnostics))
                else:
                    ws.command('load_snapshot', 'snapshot_loaded', vm_id=vm_id, name=args.snapshot)
            if args.probe and qmp_endpoint and probe_media:
                qmp_probe_media(qmp_endpoint,probe_media)
            if args.setup:
                setup = json.loads(Path(args.setup).read_text())
                replay(ws, setup, max((e['at'] for e in setup), default=0)+1)
            time.sleep(args.warmup)
            if args.command == 'run' and args.window_position:
                actual=macos_window_bounds(process.pid)
                expected=list(map(int,args.window_position.split(',')))
                # macOS can adjust the client request for titlebar/small frame insets.
                # Record exact bounds and require a visible window entirely on the primary display.
                if not actual or not actual['onscreen'] or not actual['inside_main_display'] or abs(actual['bounds']['X']-expected[0])>16 or not 0 <= expected[1]-actual['bounds']['Y'] <= 64:
                    raise RuntimeError('Own-app window placement was not honored: '+str(actual))
                manifest['window']=actual
                (target/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
            if args.expect_refresh_hz:
                # Capture-start metadata reports the monitor actually used by
                # this app. Check before input replay; still recheck the timed
                # capture because a remote session can change displays later.
                ws.command('start_capture', 'ok')
                time.sleep(.05)
                preflight = ws.command('stop_capture', 'performance_capture')['capture']
                verification = verify_refresh(preflight, args.expect_refresh_hz)
                (target/f'refresh-preflight-{repetition:02}.json').write_text(
                    json.dumps(dict(capture=preflight, verification=verification))+'\n')
                if not verification['verified']:
                    raise RuntimeError('Display refresh preflight failed before replay: ' + str(verification))
            if args.probe:
                if desktop_mode is not None:
                    deadline = time.monotonic() + args.ready_timeout
                    # A snapshot/slow floppy can leave the launch dialog active.
                    # Wait for the executable before selecting its workload.
                    while True:
                        if qmp_endpoint:
                            png = qmp_screenshot(qmp_endpoint)
                        else:
                            shot = ws.command('get_screenshot', 'screenshot', vm_id=None)
                            png = base64.b64decode(shot['png_base64'])
                        barcode = probe_barcode(png)
                        if barcode is not None and barcode[1] is not None:
                            break
                        if time.monotonic() >= deadline:
                            (target/f'not-ready-{repetition:02}.png').write_bytes(png)
                            raise TimeoutError('Desktop probe executable is not ready; screenshot preserved')
                        time.sleep(.1)
                    mode_key = 'F' + str(desktop_mode + 1)
                    ws.command('inject_input', 'ok', event={'KeyDown': mode_key}, sequence=0)
                    ws.command('inject_input', 'ok', event={'KeyUp': mode_key}, sequence=0)
                ws.command('inject_input', 'ok', event={'KeyDown': 'F12'}, sequence=0)
                ws.command('inject_input', 'ok', event={'KeyUp': 'F12'}, sequence=0)
                deadline = time.monotonic() + args.ready_timeout
                while True:
                    if qmp_endpoint:
                        png = qmp_screenshot(qmp_endpoint)
                    else:
                        shot = ws.command('get_screenshot', 'screenshot', vm_id=None)
                        png = base64.b64decode(shot['png_base64'])
                    barcode = probe_barcode(png)
                    if barcode is not None and barcode[0] == 0 and (desktop_mode is None or barcode[1] == desktop_mode):
                        (target/f'ready-{repetition:02}.png').write_bytes(png)
                        guest_dimensions = png_dimensions(png)
                        if args.expect_guest_size and args.expect_guest_size != f'{guest_dimensions[0]}x{guest_dimensions[1]}':
                            raise ValueError(f'Guest resolution {guest_dimensions[0]}x{guest_dimensions[1]} differs from expected {args.expect_guest_size}; screenshot preserved')
                        break
                    if time.monotonic() >= deadline:
                        raise TimeoutError('Probe did not acknowledge the requested workload; '
                                           'launch the current DGPUPROB executable first')
                    time.sleep(.1)
            cpu_before = process_stats(process.pid if process else args.pid)
            capture_start = time.monotonic_ns()//1000
            if not args.cpu_only:
                ws.command('start_capture', 'ok')
            try:
                replay_timing = replay(ws, events, args.duration, host)
                if args.probe:
                    time.sleep(.15)  # Drain the final acknowledged frame before ending trace.
                cpu_after = process_stats(process.pid if process else args.pid)
            finally:
                try:
                    if host:
                        host.send('ReleaseAll')
                    ws.command('inject_input', 'ok', event='ReleaseAll', sequence=0)
                except RuntimeError as error:
                    if 'unknown variant' not in str(error):
                        raise
                recorded = dict(start_us=capture_start, end_us=time.monotonic_ns()//1000, dropped=0, samples=[]) if args.cpu_only else ws.command('stop_capture', 'performance_capture')['capture']
            cpu = {pid: dict(cpu_seconds=after['cpu_seconds']-cpu_before[pid]['cpu_seconds'], rss_kib=after['rss_kib'], executable=after['executable']) for pid, after in cpu_after.items() if pid in cpu_before}
            recorded['probe_verification'] = validate_probe(recorded, events, args.probe and args.injection == 'dispatch')
            recorded['probe_sequence_ids'] = recorded['probe_verification']['verified']
            summary = summarize(recorded)
            summary['display_refresh_hz'] = observed_refresh_hz(recorded)
            if args.expect_refresh_hz:
                summary['refresh_verification'] = verify_refresh(recorded, args.expect_refresh_hz)
            if guest_dimensions:
                summary['guest_dimensions'] = list(guest_dimensions)
            summary['cpu'] = cpu_summary(cpu, summary['seconds'])
            stem = target/f'run-{repetition:02}'
            stem.with_suffix('.json').write_text(json.dumps(dict(capture=recorded, summary=summary, replay=replay_timing, processes=cpu), separators=(',', ':'))+'\n')
            stem.with_suffix('.perfetto.json').write_text(json.dumps(perfetto(recorded), separators=(',', ':'))+'\n')
            print(json.dumps(dict(run=repetition, **summary), indent=2))
            if args.expect_refresh_hz and not summary['refresh_verification']['verified']:
                raise RuntimeError(f"Display refresh validation failed; raw capture preserved at {stem}.json: " + str(summary['refresh_verification']))
            if args.probe and args.injection == 'dispatch' and not args.cpu_only:
                if not recorded['probe_verification']['verified']:
                    raise RuntimeError(f"Probe replay failed validation; raw capture preserved at {stem}.json: " + str(recorded['probe_verification']['reason']))
                if any(sample['name']=='probe.submitted' for sample in samples_with_probe_submissions(recorded)) and not summary['probe_submission_verification']['verified']:
                    raise RuntimeError(f"Probe submission endpoint incomplete; raw capture preserved at {stem}.json")
    finally:
        if host:
            host.close()
        if ws:
            if process and process.poll() is None:
                try:
                    ws.command('stop_vm', 'ok')
                except (OSError, EOFError, RuntimeError):
                    pass
            ws.close()
        if process:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        if log:
            log.close()


def fixture(args):
    source, target = Path(args.source).resolve(), Path(args.output).resolve()
    if target.exists():
        raise ValueError('Fixture output must not exist; never overwrite existing guests')
    machine = source/'machines'/args.vm
    if not machine.is_dir():
        raise ValueError(f'No machine {machine}')
    # Copy the entire selected machine: no writable disks/snapshots are symlinked.
    # clonefile/reflink avoids multi-gigabyte data copies when supported.
    target.mkdir(parents=True)
    (target/'machines').mkdir()
    try:
        cmd = ['cp', '-cR' if sys.platform == 'darwin' else '--reflink=auto', str(machine), str(target/'machines'/args.vm)]
        if sys.platform != 'darwin':
            cmd.insert(1, '-a')
        subprocess.run(cmd, check=True)
        shutil.copy2(source/'config.toml', target/'config.toml')
        (target/'resources').symlink_to(source/'resources', target_is_directory=True)
        # Absolute device paths and symlinks can escape the disposable fixture.
        for path in (target/'machines'/args.vm).rglob('*'):
            if path.is_symlink():
                raise ValueError(f'Fixture contains symlink {path}; materialize it before benchmarking')
            if path.suffix in ('.toml', '.cfg'):
                import re
                if re.search(r'(?:[=,]\s*["\']?/|file=/|/dev/)', path.read_text(errors='replace')):
                    raise ValueError(f'Absolute path in {path}; use fixture-relative disk paths')
        record = dict(source=str(source), vm=args.vm, created=time.time(), source_config_sha256=hashlib.sha256((machine/'machine.toml').read_bytes()).hexdigest())
        (target/'.juke-benchmark-fixture.json').write_text(json.dumps(record, indent=2)+'\n')
        print(target)
    except Exception:
        print(f'Incomplete fixture retained for inspection: {target}', file=sys.stderr)
        raise


def native(args):
    """Measure a native frontend command with tracing disabled."""
    command = json.loads(Path(args.command_json).read_text())
    if not isinstance(command, list) or not command or not all(isinstance(v, str) for v in command):
        raise ValueError('Native command JSON must be a nonempty array of arguments')
    target = Path(args.output)
    target.mkdir(parents=True, exist_ok=False)
    located = shutil.which(command[0])
    executable = Path(located) if located else Path(args.cwd or ROOT)/command[0]
    executable = executable.resolve()
    manifest = dict(schema=1, host=platform.node(), platform=platform.platform(),
                    executable=str(executable), binary_sha256=hashlib.sha256(executable.read_bytes()).hexdigest(),
                    revision=args.revision or output(['git','rev-parse','HEAD']),
                    submodules=output(['git','submodule','status']),
                    display=output(['kscreen-doctor','-o']) if shutil.which('kscreen-doctor') else output(['system_profiler','SPDisplaysDataType']) if sys.platform=='darwin' else None,
                    created=time.time(), label=args.label, command=command,
                    notes=args.notes, instrumentation=False, injection='none')
    (target/'manifest.json').write_text(json.dumps(manifest, indent=2))
    with (target/'native.log').open('wb') as log:
        process = subprocess.Popen(command, cwd=args.cwd or ROOT, stdout=log, stderr=subprocess.STDOUT)
        try:
            time.sleep(args.warmup)
            manifest['window'] = macos_window_bounds(process.pid)
            if args.require_main_display and (not manifest['window'] or not manifest['window']['onscreen'] or not manifest['window']['inside_main_display']):
                raise RuntimeError('Native window is not entirely on the main display: '+str(manifest['window']))
            (target/'manifest.json').write_text(json.dumps(manifest,indent=2))
            if args.window_position:
                manifest['window'] = place_macos_window(process.pid,args.window_position)
                (target/'manifest.json').write_text(json.dumps(manifest,indent=2))
                time.sleep(1)
            for repetition in range(1,args.repeat+1):
                if process.poll() is not None:
                    raise RuntimeError('Native frontend exited before measurement')
                before = process_stats(process.pid)
                start = time.monotonic_ns()//1000
                time.sleep(args.duration)
                end = time.monotonic_ns()//1000
                after = process_stats(process.pid)
                if process.poll() is not None:
                    raise RuntimeError('Native frontend exited during measurement')
                samples = dict(start_us=start,end_us=end,dropped=0,samples=[])
                summary=summarize(samples)
                cpu={pid:dict(cpu_seconds=entry['cpu_seconds']-before[pid]['cpu_seconds'],rss_kib=entry['rss_kib'],executable=entry['executable']) for pid,entry in after.items() if pid in before}
                summary['cpu'] = cpu_summary(cpu, summary['seconds'])
                (target/f'run-{repetition:02}.json').write_text(json.dumps(dict(capture=samples,summary=summary,processes=cpu)))
                print(json.dumps(dict(run=repetition,**summary),indent=2))
        finally:
            process.terminate()
            try: process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill(); process.wait()


def comparison_runs(path):
    path = Path(path)
    files = sorted(path.glob('run-*.json')) if path.is_dir() else [path]
    runs = []
    for file in files:
        if '.perfetto.' in file.name:
            continue
        data = json.loads(file.read_text())
        summary = data['summary']
        if 'capture' in data:
            summary['display_refresh_hz'] = observed_refresh_hz(data['capture'])
        # Older captures already have the exact guest screenshot, even though
        # their summaries predate explicit resolution metadata.
        screenshot = file.with_name(file.stem.replace('run-', 'ready-') + '.png')
        if screenshot.exists():
            summary['guest_dimensions'] = list(png_dimensions(screenshot.read_bytes()))
        runs.append(summary)
    return runs


def validate_comparison_dimensions(runs):
    dimensions = {tuple(run['guest_dimensions']) for run in runs if run.get('guest_dimensions')}
    if dimensions and any(not run.get('guest_dimensions') for run in runs):
        raise ValueError('Guest resolution is missing from some captures; equivalence is unverified')
    if len(dimensions) > 1:
        raise ValueError('Guest resolutions differ across captures: ' + ', '.join(f'{w}x{h}' for w, h in sorted(dimensions)))


def validate_comparison_refresh(runs):
    observed = [value for run in runs for value in run.get('display_refresh_hz', [])]
    if observed and any(not run.get('display_refresh_hz') for run in runs):
        raise ValueError('Display refresh is missing from some captures; equivalence is unverified')
    if observed and max(observed) - min(observed) > .5:
        raise ValueError('Display refresh differs across captures: ' + ', '.join(f'{value:g} Hz' for value in sorted(set(observed))))
    if any(run.get('refresh_verification', {}).get('verified') is False for run in runs):
        raise ValueError('A capture failed its required display refresh check')


def comparison_conditions(before_path, after_path):
    """Reject known experimental drift; never infer missing guest-state evidence."""
    manifests = []
    for value in (before_path, after_path):
        path = Path(value)
        path = (path if path.is_dir() else path.parent) / 'manifest.json'
        manifests.append(json.loads(path.read_text()) if path.is_file() else {})
    missing, checked = [], []
    fields = [('host',), ('platform',), ('scenario',), ('instrumentation',),
              ('injection',), ('replay_definition', 'sha256'),
              ('active_vm', 'vm_id'), ('capture_conditions',)]
    absent = object()
    for field in fields:
        values = []
        for manifest in manifests:
            value = manifest
            for key in field:
                value = value.get(key, absent) if isinstance(value, dict) else absent
            values.append(value)
        name = '.'.join(field)
        if any(value is absent or value is None for value in values):
            missing.append(name)
        elif values[0] != values[1]:
            raise ValueError('Benchmark conditions differ: ' + name)
        else:
            checked.append(name)
    return {'checked_equal': checked, 'missing': missing,
            'scope': 'Recorded host/workload conditions only. Guest disk, driver and boot-state '
                     'equivalence require the associated fixture/package receipts. '
                     'Metric deltas alone do not prove a regression or speedup.'}


def compare(args):
    conditions = comparison_conditions(args.before, args.after)
    before, after = comparison_runs(args.before), comparison_runs(args.after)
    if not before or not after:
        raise ValueError('Both comparisons need at least one capture')
    validate_comparison_dimensions(before + after)
    validate_comparison_refresh(before + after)
    if any(r.get('dropped', 0) for r in before + after):
        raise ValueError('Dropped samples prevent a complete performance comparison')
    rows = []
    for category in ['duration_us', 'latency_us', 'rate_hz', 'cpu']:
        keys = sorted(set().union(*(r.get(category, {}) for r in before+after)))
        for key in keys:
            for percentile in (['p50','p95','p99'] if category not in ('rate_hz','cpu') else [None]):
                def values(runs):
                    return [r[category][key][percentile] if percentile else r[category][key] for r in runs if key in r.get(category,{}) and (not percentile or percentile in r[category][key])]
                a, b = values(before), values(after)
                if not a or not b:
                    continue
                av, bv = sum(a)/len(a), sum(b)/len(b)
                rows.append(dict(metric=f'{category}.{key}'+(f'.{percentile}' if percentile else ''), before=av, after=bv, change_percent=(bv/av-1)*100 if av else None, before_range=[min(a),max(a)], after_range=[min(b),max(b)]))
    print(json.dumps(dict(before_runs=len(before), after_runs=len(after), conditions=conditions, metrics=rows, dropped_before=sum(r['dropped'] for r in before), dropped_after=sum(r['dropped'] for r in after)), indent=2))


def export_trace(args):
    source, destination = Path(args.capture), Path(args.output)
    if source.resolve() == destination.resolve():
        raise ValueError('Trace export must not overwrite its raw capture')
    raw = json.loads(source.read_text())
    capture_data = raw.get('capture', raw)
    if not isinstance(capture_data, dict) or 'samples' not in capture_data:
        raise ValueError('Expected a raw performance capture or benchmark run JSON')
    destination.write_text(json.dumps(perfetto(capture_data)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subs = parser.add_subparsers(dest='command', required=True)
    for mode in ['attach','run']:
        p = subs.add_parser(mode, help='Capture a running Juke' if mode == 'attach' else 'Launch a disposable benchmark fixture')
        p.add_argument('--url', default='ws://127.0.0.1:9999')
        p.add_argument('--duration', type=float, default=30)
        p.add_argument('--window-position', help='macOS only: place and activate own window at X,Y logical screen points')
        p.add_argument('--warmup', type=float, default=2)
        p.add_argument('--repeat', type=int, default=1)
        p.add_argument('--scenario', default='idle')
        p.add_argument('--snapshot', help='Restore this QEMU snapshot before every repetition')
        p.add_argument('--setup', help='JSON input replay performed before each repetition, outside capture')
        p.add_argument('--probe-media', help='Probe floppy to reinsert after each restore (run automatically uses the fixture machine\'s probes.img)')
        p.add_argument('--qmp', help='QEMU monitor socket for snapshot restoration/readiness; auto-discovered for launched QEMU')
        p.add_argument('--probe', action='store_true', help='Reset the visible input probe before each repetition')
        p.add_argument('--expect-guest-size', help='Require WIDTHxHEIGHT in probe readiness screenshots before capturing')
        p.add_argument('--expect-refresh-hz', type=float, help='Require the recorded host display refresh (within 0.5 Hz)')
        p.add_argument('--injection', choices=['dispatch','host'], default='dispatch')
        p.add_argument('--pid', type=int, help='Juke PID for CPU accounting in attach mode')
        p.add_argument('--ready-timeout', type=float, default=120)
        p.add_argument('--label', default='candidate')
        p.add_argument('--revision', help='Explicit binary revision for preserved baseline executables')
        p.add_argument('--cpu-only', action='store_true', help='Leave tracing disabled; measure process CPU cost separately')
        p.add_argument('--notes', default='')
        p.add_argument('--output', required=True)
        if mode == 'run':
            p.add_argument('--binary', default=str(ROOT/'target/profiling/juke'))
            p.add_argument('--fixture', required=True)
            p.add_argument('--vm', required=True)
        p.set_defaults(function=capture)
    p = subs.add_parser('fixture', help='Create disposable guest copy; preserves original disks')
    p.add_argument('--source', default=str(ROOT/'files'))
    p.add_argument('--vm', required=True)
    p.add_argument('--output', required=True)
    p.set_defaults(function=fixture)
    p = subs.add_parser('native', help='Measure a native frontend command on a prepared disposable fixture')
    p.add_argument('--command-json', required=True, help='JSON array of executable and arguments (no shell)')
    p.add_argument('--cwd')
    p.add_argument('--require-main-display',action='store_true',help='macOS: verify native window is entirely on main display before capture')
    p.add_argument('--window-position', help='macOS only: place and activate native window at X,Y logical screen points')
    p.add_argument('--warmup', type=float, default=10)
    p.add_argument('--duration', type=float, default=30)
    p.add_argument('--repeat', type=int, default=5)
    p.add_argument('--label', default='native')
    p.add_argument('--revision', help='Native executable source revision')
    p.add_argument('--notes', default='')
    p.add_argument('--output', required=True)
    p.set_defaults(function=native)
    p = subs.add_parser('compare', help='Compare distributions and across-run spread')
    p.add_argument('before'); p.add_argument('after'); p.set_defaults(function=compare)
    p = subs.add_parser('export', help='Regenerate Perfetto flow visualization from a saved raw capture')
    p.add_argument('capture'); p.add_argument('--output', required=True)
    p.set_defaults(function=export_trace)
    args = parser.parse_args()
    if hasattr(args, 'duration') and (not math.isfinite(args.duration) or args.duration <= 0 or args.repeat < 1 or not math.isfinite(args.warmup) or args.warmup < 0):
        parser.error('Duration must be positive, repeat >= 1, warmup >= 0')
    if getattr(args, 'expect_refresh_hz', None) is not None and (not math.isfinite(args.expect_refresh_hz) or args.expect_refresh_hz <= 0):
        parser.error('Expected refresh must be finite and positive')
    try:
        args.function(args)
    except (OSError, ValueError, RuntimeError, TimeoutError) as error:
        parser.exit(1, f'bench: {error}\n')

if __name__ == '__main__':
    main()
