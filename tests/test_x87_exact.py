#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Differential check of actual exact x87 guards against unchanged SoftFloat."""
import argparse
import json
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=ROOT / 'target/qemu-build')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    build, output = args.build.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    entries = json.loads((build / 'compile_commands.json').read_text())
    entry = next(row for row in entries if row['file'].endswith('/fpu/softfloat.c'))
    command = shlex.split(entry['command'])
    command = command[:command.index('-MD')]
    command += ['-ffp-contract=off', str(ROOT / 'tests/native_cpu/fpu_exact.c'),
                str(build / entry['output'])]
    command += shlex.split(subprocess.check_output(['pkg-config', '--libs', 'glib-2.0'], text=True))
    command += ['-lm', '-o', str(output / 'exact-test')]
    (output / 'command.json').write_text(json.dumps(command, indent=2) + '\n')
    subprocess.run(command, cwd=build, check=True)
    subprocess.run([str(output / 'exact-test')], check=True)


if __name__ == '__main__':
    main()
