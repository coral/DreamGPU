"""Validated deltas for the bounded native GPU diagnostic window."""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import argparse
import json
from pathlib import Path
import re


def integer(value):
    if type(value) is not int or value < 0:
        raise ValueError('Native counters must be unsigned integers')
    return value


def difference(before, after):
    delta = integer(after) - integer(before)
    if delta < 0:
        raise ValueError('Native counters reset during the measured interval')
    return delta


def histogram(before, after, keys, values):
    def index(rows):
        result = {}
        for row in rows:
            key = tuple(integer(row[field]) for field in keys)
            if key in result:
                raise ValueError('Duplicate native histogram key')
            result[key] = {field: integer(row[field]) for field in values}
        return result
    old, new = index(before), index(after)
    if old.keys() - new.keys():
        raise ValueError('Native histogram entries disappeared during measurement')
    rows = []
    for key, value in new.items():
        delta = {field: difference(old.get(key, {}).get(field, 0), value[field])
                 for field in values}
        if any(delta.values()):
            rows.append(dict(zip(keys, key), **delta))
    return sorted(rows, key=lambda row: (-sum(row[field] for field in values),
                                         tuple(row[field] for field in keys)))


def summarize(start, end):
    if start['schema'] != 1 or end['schema'] != 1:
        raise ValueError('Unknown native diagnostic schema')
    if start['enabled'] is not True or end['enabled'] is not True:
        raise ValueError('Both measured boundaries must have active counters')
    elapsed = difference(start['elapsed_us'], end['elapsed_us'])
    if not elapsed:
        raise ValueError('Native measurement interval is empty')
    first, last = start['gl'], end['gl']
    gl = {field: difference(first[field], last[field])
          for field in ('batches', 'bytes', 'records', 'query_overflow', 'function_overflow')}
    for field, keys, values in (
        ('operations', ('op',), ('count',)),
        ('functions', ('function',), ('count',)),
        ('desktop', ('op',), ('count', 'rectangle_bytes')),
        ('queries', ('function', 'arg0', 'arg1'), ('count',)),
    ):
        gl[field] = histogram(first[field], last[field], keys, values)
    header = Path(__file__).resolve().parents[2] / 'guest/include/gl-funcs.h'
    names = {int(number, 16): name for name, number in re.findall(
        r'FEnum_(\w+)\s*,\s*/\*\s*0x([0-9a-fA-F]+)\s*\*/', header.read_text())}
    for row in gl['functions'] + gl['queries']:
        row['name'] = names.get(row['function'], 'unknown')
    mmio = histogram(start['mmio'], end['mmio'], ('offset',), ('reads', 'writes'))
    return {
        'schema': 1, 'elapsed_us': elapsed,
        'mmio_reads': sum(row['reads'] for row in mmio),
        'mmio_writes': sum(row['writes'] for row in mmio),
        'mmio': mmio, 'gl': gl,
        'query_keys_complete': not last['query_overflow'],
        'function_totals_complete': not last['function_overflow'],
        'scope': 'Validated submissions between guest measurement boundaries; not execution success, game FPS or physical bandwidth. Query overflow loses key detail, not the separate function totals.',
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    boundaries = json.loads(args.run.read_text())['diagnostics']['boundaries']
    result = summarize(boundaries['MEASURING']['native'], boundaries['MEASURED']['native'])
    output = json.dumps(result, indent=2) + '\n'
    if args.output:
        with args.output.open('x') as destination:
            destination.write(output)
    else:
        print(output, end='')


if __name__ == '__main__':
    main()
