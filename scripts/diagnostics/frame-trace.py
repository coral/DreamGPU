#!/usr/bin/env python3
"""Capture the GL records QEMU executes for the next presented frames, then summarize.

  python3 scripts/diagnostics/frame-trace.py /path/qmp.sock /abs/out.trace [frames]
      [--vertices] [--limit MiB] [--device QOM-PATH] [--timeout SECONDS]
  python3 scripts/diagnostics/frame-trace.py --summarize /abs/out.trace

The device's QOM diagnostic-trace property arms a host-side capture: QEMU's
render worker waits for the next PRESENT record, writes one line per executed
record of the following frames to the file and stops by itself (or at the byte
limit, default 64 MiB). --vertices also dumps the client vertex data and inline
indices of every array draw. The GL engine exists only after the guest's first
GL submission. The output file must not exist. No guest input is sent; outside
the capture window the device behaves exactly as without tracing.

Line format: `frame seq= stream= ctx=client:context:drawable OP function
arguments [data=bytes [values]|hex=prefix] [err=code]`; indented lines are
vertices; `#` lines are the header and the end line.
"""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import argparse
from collections import Counter
import json
from pathlib import Path
import re
import sys
import time

from scripts.benchmarks.bench import qmp_execute

PROPERTY = 'diagnostic-trace'
END = re.compile(r'^# end reason=(\w+) frames=(\d+) records=(\d+) bytes=(\d+)$')
DATA = re.compile(r' data=(\d+)')
ERROR = re.compile(r' err=(\d+)$')


def trace_device(endpoint):
    """The one DreamGPU device that offers the trace property."""
    found = []
    for container in ('/machine/peripheral', '/machine/peripheral-anon'):
        entries = qmp_execute(endpoint, [('qom-list', {'path': container})])[0]
        for entry in entries[:512]:
            if entry.get('type') != 'child<dreamgpu>':
                continue
            path = f"{container}/{entry['name']}"
            properties = qmp_execute(endpoint, [('qom-list', {'path': path})])[0]
            if any(prop['name'] == PROPERTY for prop in properties):
                found.append(path)
    if len(found) != 1:
        raise SystemExit(f'Expected one DreamGPU device with {PROPERTY}, found {found}')
    return found[0]


def status(endpoint, device):
    value = qmp_execute(endpoint, [('qom-get', {'path': device, 'property': PROPERTY})])[0]
    result = json.loads(value)
    if result.get('schema') != 1:
        raise SystemExit(f'Unknown trace status: {value}')
    return result


def request(path, frames, vertices=False, limit=None):
    if ',' in str(path):
        raise SystemExit('The trace path must not contain a comma')
    parts = [str(path), str(frames)]
    if vertices:
        parts.append('vertices')
    if limit:
        parts.append(f'limit={limit}')
    return ','.join(parts)


def capture(endpoint, output, frames, vertices=False, limit=None, device=None, timeout=60.0,
            poll=0.2):
    device = device or trace_device(endpoint)
    qmp_execute(endpoint, [('qom-set', {'path': device, 'property': PROPERTY,
                                        'value': request(output, frames, vertices, limit)})])
    deadline = time.monotonic() + timeout
    while True:
        current = status(endpoint, device)
        if current['state'] == 'done' and current['path'] == str(output):
            return current
        if time.monotonic() >= deadline:
            # Ends the capture at the next GL batch; a stopped guest leaves it running.
            qmp_execute(endpoint, [('qom-set', {'path': device, 'property': PROPERTY,
                                                'value': 'stop'})])
            raise SystemExit(f'Trace did not finish within {timeout}s (state {current["state"]}, '
                             f'{current["completed"]}/{current["frames"]} frames); requested stop')
        time.sleep(poll)


def summarize(lines):
    """Per (op, function) record counts, payload bytes and errors."""
    rows = Counter()
    data = Counter()
    errors = Counter()
    frames = set()
    vertices = 0
    end = None
    for line in lines:
        line = line.rstrip('\n')
        if line.startswith('  '):
            vertices += line.lstrip().startswith('v')
            continue
        if line.startswith('#') or not line:
            match = END.match(line)
            if match:
                end = {'reason': match[1], 'frames': int(match[2]),
                       'records': int(match[3]), 'bytes': int(match[4])}
            continue
        fields = line.split(' ', 6)
        if len(fields) < 6:
            continue
        frames.add(fields[0])
        key = (fields[4], fields[5])
        rows[key] += 1
        size = DATA.search(line)
        data[key] += int(size[1]) if size else 0
        errors[key] += bool(ERROR.search(line))
    table = [{'op': op, 'function': function, 'count': count, 'data_bytes': data[op, function],
              'errors': errors[op, function]}
             for (op, function), count in rows.most_common()]
    return {'frames': len(frames), 'records': sum(rows.values()), 'vertex_lines': vertices,
            'end': end, 'functions': table}


def report(summary, path):
    end = summary['end'] or {'reason': 'missing end line (capture still running?)'}
    frames = max(summary['frames'], 1)
    print(f"{path}: {summary['frames']} frames, {summary['records']} records, "
          f"ended: {end['reason']}")
    print(f"{'count':>8} {'/frame':>9} {'data bytes':>12} {'errors':>6}  op         function")
    for row in summary['functions']:
        print(f"{row['count']:>8} {row['count'] / frames:>9.1f} {row['data_bytes']:>12} "
              f"{row['errors']:>6}  {row['op']:<10} {row['function']}")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('qmp', nargs='?', type=Path, help='QEMU QMP unix socket')
    parser.add_argument('output', type=Path, help='new absolute trace file path')
    parser.add_argument('frames', nargs='?', type=int, default=2)
    parser.add_argument('--vertices', action='store_true',
                        help='also dump client vertex data and inline indices of draws')
    parser.add_argument('--limit', type=int, metavar='MiB', help='file size limit (default 64)')
    parser.add_argument('--device', help='QOM path of the DreamGPU device (default: discover)')
    parser.add_argument('--timeout', type=float, default=60.0)
    parser.add_argument('--summarize', action='store_true',
                        help='only summarize an existing trace file')
    parser.add_argument('--json', action='store_true', help='print the summary as JSON')
    args = parser.parse_args(argv)
    output = args.output.resolve()
    if args.summarize:
        if args.qmp is not None:
            parser.error('--summarize takes only the trace file')
    else:
        if args.qmp is None:
            parser.error('capture needs the QMP socket and an output path')
        if output.exists():
            parser.error(f'{output} exists; traces are never overwritten')
        result = capture(str(args.qmp), output, args.frames, args.vertices, args.limit,
                         args.device, args.timeout)
        if result['reason'] != 'frames':
            print(f"warning: capture ended by {result['reason']}", file=sys.stderr)
    with output.open(encoding='utf-8', errors='replace') as lines:
        summary = summarize(lines)
    if args.json:
        print(json.dumps(summary, indent=2))
    else:
        report(summary, output)


if __name__ == '__main__':
    main()
