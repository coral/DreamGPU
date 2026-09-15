#!/usr/bin/env python3
"""Format maintained C/C++ and analyze actual compilation-database commands.

Run on the build host (or inside its toolchain container), so database paths and
headers name the same sources used by the real compiler. No upstream tree walk.
"""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import argparse
import concurrent.futures
import difflib
import json
from pathlib import Path
import shlex
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
SUFFIXES = {'.c', '.cpp', '.h', '.hpp', '.inc'}
QEMU_PATTERNS = ('hw/display/dreamgpu*.c', 'hw/display/dreamgpu*.h',
                 'ui/dreamgpu*.c', 'ui/dreamgpu*.h',
                 'include/standard-headers/dreamgpu/*.h',
                 'tests/unit/test-dreamgpu*.c', 'tests/qtest/dreamgpu-test.c',
                 'include/ui/dreamgpu-shmem.h', 'audio/jukeaudio.c')


def maintained_qemu(root):
    return [p for pattern in QEMU_PATTERNS for p in (root / 'vendor/qemu').glob(pattern)
            if p.is_file()]


def maintained_files(root):
    tracked = subprocess.run(
        ['git', '-C', str(root), 'ls-files', '-z', '--cached', '--others', '--exclude-standard'],
        stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    if tracked.returncode:
        # Cargo exports source snapshots without Git metadata. These explicit
        # maintained roots cannot descend into prepared/vendor source trees.
        return sorted(set(maintained_qemu(root)) | {p for name in ('guest', 'tools', 'tests', 'include', 'crates', 'scripts', 'support', 'src')
                      for p in (root / name).rglob('*')
                      if p.is_file() and p.suffix in SUFFIXES
                      and not {'target', 'vendor', '.git'}.intersection(p.relative_to(root).parts)})
    result = tracked.stdout
    return sorted(set(maintained_qemu(root)) | {root / name.decode() for name in result.split(b'\0') if name
                   and Path(name.decode()).parts[0] not in {'vendor', 'target'}
                   and Path(name.decode()).suffix in SUFFIXES
                   and (root / name.decode()).is_file()})


def compiler_arguments(entry, extra, drop):
    arguments = entry.get('arguments') or shlex.split(entry['command'])
    if not arguments:
        raise ValueError('Empty compiler command')
    result = []
    index = 1
    while index < len(arguments):
        arg = arguments[index]
        if arg in ('-o', '-MF', '-MT', '-MQ'):
            if index + 1 == len(arguments):
                raise ValueError('Missing operand for ' + arg)
            index += 2
            continue
        if arg in ('-c', '-MD', '-MMD', *drop):
            index += 1
            continue
        path = Path(entry['directory']) / arg
        if not arg.startswith('-') and path.resolve() == Path(entry['file']).resolve():
            index += 1
            continue
        result.append(arg)
        index += 1
    return [*result, *extra]


def compile_entries(databases, owned, borrowed, extra, drop):
    owned = {Path(p).resolve() for p in owned}
    borrowed = {Path(p).resolve() for p in borrowed}
    selected, seen = [], set()
    for database in databases:
        path = Path(database).resolve()
        if path.is_dir() and not (path / 'compile_commands.json').exists():
            records = [json.loads(item.read_text()) for item in sorted(path.glob('*.json'))]
        else:
            if path.is_dir():
                path /= 'compile_commands.json'
            records = json.loads(path.read_text())
        for entry in records:
            directory = Path(entry['directory']).resolve()
            source = (directory / entry['file']).resolve()
            if source not in owned and source not in borrowed:
                continue
            entry = {**entry, 'directory': str(directory), 'file': str(source)}
            arguments = compiler_arguments(entry, extra, drop)
            identity = (str(source), tuple(arguments))
            if identity in seen:
                continue
            seen.add(identity)
            selected.append((entry, arguments))
    return selected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT)
    sub = parser.add_subparsers(dest='action', required=True)
    fmt = sub.add_parser('format')
    mode = fmt.add_mutually_exclusive_group(required=True)
    mode.add_argument('--check', action='store_true')
    mode.add_argument('--fix', action='store_true')
    fmt.add_argument('--clang-format', default='clang-format')
    tidy = sub.add_parser('tidy')
    tidy.add_argument('--compile-db', action='append', required=True)
    tidy.add_argument('--clang-tidy', default='clang-tidy')
    tidy.add_argument('--config-file', type=Path, help='Default: ROOT/.clang-tidy')
    tidy.add_argument('--extra-arg', action='append', default=[],
                      help='Explicit target/sysroot/resource-dir adjustment, recorded in report')
    tidy.add_argument('--drop-arg', action='append', default=[],
                      help='Explicit GCC-only analysis/codegen flag Clang cannot accept')
    tidy.add_argument('--borrowed-tu', type=Path, action='append', default=[],
                      help='Exact prepared upstream TU including a maintained fragment')
    tidy.add_argument('--borrowed-base', action='append', default=[], metavar='PREPARED=PINNED',
                      help='Limit borrowed main-file diagnostics to lines changed from pinned upstream')
    tidy.add_argument('--borrowed-header', type=Path, action='append', default=[],
                      help='Full maintained fragment included by a borrowed TU')
    tidy.add_argument('--borrowed-line-filter', type=Path,
                      help='clang-tidy JSON line filter for owned patch ranges/fragments in borrowed TUs')
    tidy.add_argument('--jobs', type=int, default=1)
    tidy.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    files = maintained_files(root)
    if args.action == 'format':
        command = [args.clang_format, '--style=file:' + str(root / '.clang-format')]
        command += ['-i'] if args.fix else ['--dry-run', '--Werror']
        subprocess.run([*command, *map(str, files)], check=True)
        print(f'PASS format: {len(files)} maintained files')
        return 0
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    selected = compile_entries(args.compile_db, {p.resolve() for p in files},
                               {p.resolve() for p in args.borrowed_tu},
                               args.extra_arg, args.drop_arg)
    if not selected:
        parser.error('No maintained translation units matched the compilation databases')
    args.output.mkdir(parents=True, exist_ok=True)
    version = subprocess.check_output([args.clang_tidy, '--version'], text=True)
    borrowed_filter = json.loads(args.borrowed_line_filter.read_text()) if args.borrowed_line_filter else []
    for pair in args.borrowed_base:
        prepared, separator, pinned = pair.partition('=')
        if not separator:
            parser.error('--borrowed-base requires PREPARED=PINNED')
        before = Path(pinned).read_text().splitlines()
        after = Path(prepared).read_text().splitlines()
        ranges = [[start + 1, end] for tag, _, _, start, end in
                  difflib.SequenceMatcher(None, before, after, autojunk=False).get_opcodes()
                  if tag != 'equal' and end > start]
        # An empty list selects every line, and LLVM rejects line zero. Use a
        # positive range beyond EOF when a donor has no maintained changes.
        borrowed_filter.append({'name': str(Path(prepared).resolve()),
                                'lines': ranges or [[len(after) + 2, len(after) + 2]]})
    borrowed_filter.extend({'name': str(path.resolve())} for path in args.borrowed_header)

    def analyze(item):
        index, (entry, arguments) = item
        command = [args.clang_tidy, entry['file'], '--config-file=' + str(args.config_file or root / '.clang-tidy'),
                   '--warnings-as-errors=*', '--', *arguments]
        if borrowed_filter and Path(entry['file']) in {p.resolve() for p in args.borrowed_tu}:
            command.insert(2, '--line-filter=' + json.dumps(borrowed_filter))
        result = subprocess.run(command, cwd=entry['directory'], stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True)
        log = args.output / f'{index:03}.log'
        log.write_text(result.stdout)
        print(('PASS' if result.returncode == 0 else 'FAIL') + ' ' + entry['file'], flush=True)
        return {'source_command': entry, 'analysis_command': command,
                'exit_code': result.returncode, 'log': str(log)}

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = list(pool.map(analyze, enumerate(selected)))
    report = {'clang_tidy': version.strip(), 'extra_arguments': args.extra_arg,
              'dropped_arguments': args.drop_arg, 'borrowed_diagnostic_filter': borrowed_filter,
              'translation_units': results}
    (args.output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    return int(any(row['exit_code'] for row in results))


if __name__ == '__main__':
    sys.exit(main())
