#!/usr/bin/env python3
"""Cold-boot a source-built Win98 probe on a disposable disk; report serial output.

No existing image is modified. Drivers are installed offline and WIN.INI launches
one probe after login. A diagnostic VxD event triggers the single initial logon
Escape; completion is serial, not screenshot-driven. Successful guests stop and
close, preserving the disk and logs for inspection.
"""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import struct
import subprocess
import sys
import time

from scripts.benchmarks.bench import WebSocket, qmp_execute


def checked(command):
    subprocess.run([str(arg) for arg in command], check=True)


def startup_ini(text, executable):
    """Replace only [windows] run; preserve all other section values."""
    lines = text.splitlines()
    start = next((i for i, line in enumerate(lines) if line.strip().lower() == '[windows]'), None)
    if start is None:
        return '[windows]\r\nrun=C:\\' + executable.upper() + '\r\n' + text
    end = next((i for i in range(start+1, len(lines)) if lines[i].lstrip().startswith('[')), len(lines))
    replacements = [i for i in range(start+1, end) if lines[i].split('=', 1)[0].strip().lower() == 'run']
    line = 'run=C:\\' + executable.upper()
    if replacements:
        lines[replacements[0]] = line
        for i in reversed(replacements[1:]):
            del lines[i]
    else:
        lines.insert(start+1, line)
    return '\r\n'.join(lines) + '\r\n'


def partition_offset(path):
    with path.open('rb') as stream:
        mbr = stream.read(512)
    if len(mbr) != 512 or mbr[510:] != b'\x55\xaa':
        raise ValueError('source image lacks an MBR')
    offset = struct.unpack_from('<I', mbr, 454)[0] * 512
    if not offset:
        raise ValueError('source image has no first partition')
    return offset


def prepare(args):
    if not args.completion or not 0 < args.timeout <= 300 or not 1024 <= args.port <= 65535:
        raise ValueError('require nonempty completion, bounded timeout and an unprivileged port')
    output = args.output.resolve()
    output.mkdir()  # An existing evidence directory is never overwritten.
    source = args.source.resolve(strict=True)
    package = args.package.resolve(strict=True)
    native = args.native.resolve(strict=True)
    juke = args.juke.resolve(strict=True)
    executable = args.executable.lower()
    if not re.fullmatch(r'[a-z0-9_]{1,8}\.exe', executable):
        raise ValueError('probe must have a DOS 8.3 executable name')
    if any(not re.fullmatch(r'[A-Za-z0-9_.-]{1,64}', name) or name in ('.','..') for name in args.payload):
        raise ValueError('payload names must be plain files inside the package')
    for name in ('dgpumini.vxd', 'dgpumini.drv', executable, *args.payload):
        if not (package/name).is_file():
            raise ValueError('package missing ' + name)
    frozen = output/'package'
    shutil.copytree(package, frozen)
    machine = output/'machines/win98'
    machine.mkdir(parents=True)
    raw = output/'prepare.raw'
    checked(['qemu-img', 'convert', '-O', 'raw', source, raw])
    volume = str(raw) + '@@' + str(partition_offset(raw))
    for name in ('dgpumini.vxd', 'dgpumini.drv'):
        checked(['mcopy', '-o', '-i', volume, frozen/name, '::WINDOWS/SYSTEM/'+name])
    checked(['mcopy', '-o', '-i', volume, frozen/executable, '::'+executable.upper()])
    for name in args.payload:
        checked(['mcopy','-o','-i',volume,frozen/name,'::'+name])
    ini = output/'WIN.INI'
    checked(['mcopy', '-i', volume, '::WINDOWS/WIN.INI', ini])
    ini.write_bytes(startup_ini(ini.read_text(encoding='latin1'), executable).encode('latin1'))
    checked(['mcopy', '-o', '-i', volume, ini, '::WINDOWS/WIN.INI'])
    checked(['qemu-img', 'convert', '-O', 'qcow2', raw, machine/'disk.qcow2'])
    raw.unlink()
    wrapper = output/'qemu-system-i386'
    wrapper.write_text('''#!/usr/bin/env python3
import sys,os,json
from pathlib import Path
p=Path(__file__).resolve().parent
args=sys.argv[1:];out=[];i=0
while i<len(args):
 if args[i]=='-device' and args[i+1].split(',')[0].lower() in ['ac97','sb16']:i+=2;continue
 if args[i] in ['-m','-smp','-cpu']:
  out.extend([args[i],{'-m':'256M','-smp':'1','-cpu':'pentium3'}[args[i]]]);i+=2;continue
 out.append(args[i]);i+=1
qmp=out[out.index('-qmp')+1].split(':',1)[1].split(',')[0]
(p/'launch.json').write_text(json.dumps({'qmp':qmp,'args':out},indent=2))
extra=['-S','-chardev','file,id=dgserial,path='+str(p/'serial0.log'),'-serial','chardev:dgserial','-chardev','file,id=dgdebug,path='+str(p/'debugcon.log'),'-device','isa-debugcon,iobase=0xe9,chardev=dgdebug']
native=''' + repr(str(native)) + '''
os.execv(native,[native,*extra,*out])
''')
    wrapper.chmod(0o755)
    (machine/'machine.toml').write_text('[vm]\nname="Win98 source probe"\nbackend="qemu"\nautostart=false\n'
        '[qemu]\nprofile="retro-win98"\nprofile_version=1\nqemu_binary='+json.dumps(str(wrapper))+
        '\ndisk="disk.qcow2"\n[render]\nshader="none"\n')
    (output/'config.toml').write_text('[general]\nwindow_width=1024\nwindow_height=768\ndefault_shader="none"\n'
        'fullscreen=false\nvsync=true\n[slots]\n"1"="win98"\n[control]\nlisten="127.0.0.1:'+str(args.port)+'"\n')
    (output/'resources').symlink_to(args.resources.resolve(strict=True))
    report = dict(schema=1, state='prepared', source=str(source), package=str(package), native=str(native),
                  juke=str(juke), ws='ws://127.0.0.1:'+str(args.port), executable=executable,
                  completion=args.completion, timeout=args.timeout,
                  files={name:hashlib.sha256((frozen/name).read_bytes()).hexdigest()
                         for name in ('dgpumini.vxd','dgpumini.drv',executable,*args.payload)})
    (output/'probe.json').write_text(json.dumps(report, indent=2)+'\n')
    return output


def run_probe(output):
    output = output.resolve()
    report = json.loads((output/'probe.json').read_text())
    if report['state'] != 'prepared':
        raise ValueError('probe already attempted; prepare a new evidence directory')
    start = time.monotonic()
    deadline = start + report['timeout']
    qmp = None
    app = None
    ws = None
    report['state'] = 'starting'
    try:
        with (output/'juke.log').open('w') as log:
            app = subprocess.Popen([report['juke'], '--config-dir', str(output), '-c', '-m', 'win98'],
                                   stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        report['pid'] = app.pid
        while time.monotonic() < deadline:
            if app.poll() is not None:
                raise RuntimeError('Juke exited before probe startup; see juke.log')
            if not ws:
                try:
                    ws = WebSocket(report['ws'])
                    ws.command('start_vm', 'ok', vm_id='win98')
                except (OSError, RuntimeError):
                    if ws: ws.close()
                    ws = None
            if (output/'launch.json').exists():
                qmp = json.loads((output/'launch.json').read_text())['qmp']
                if Path(qmp).exists():
                    break
            time.sleep(.25)
        else:
            raise TimeoutError('no QEMU control endpoint before startup deadline')
        qmp_execute(qmp, [('set_link', {'name':'net0','up':False}), ('cont', {})])
        report['network_disconnected_before_execution'] = True
        dismissed = False
        while time.monotonic() < deadline:
            serial = (output/'serial0.log').read_text(errors='replace') if (output/'serial0.log').exists() else ''
            if 'DGACCESS DONE pass=0' in serial or '\nDONE FAIL' in serial:
                raise RuntimeError('guest probe reported failure; see serial0.log')
            if report['completion'] in serial:
                report['state'] = 'passed'
                report['seconds'] = time.monotonic() - start
                break
            debug = (output/'debugcon.log').read_text(errors='replace') if (output/'debugcon.log').exists() else ''
            app_log = (output/'juke.log').read_text(errors='replace')
            if 'graphics coherence fault' in app_log:
                raise RuntimeError('native graphics coherence fault stopped guest; see juke.log')
            if not dismissed and 'event=00000004' in debug:
                qmp_execute(qmp, [('send-key', {'keys':[{'type':'qcode','data':'esc'}], 'hold-time':80})])
                dismissed = True
                report['initial_login_dismissal'] = 'single Escape after first diagnostic process exit'
            if app.poll() is not None:
                raise RuntimeError('Juke exited during probe; see juke.log')
            time.sleep(.25)
        else:
            raise TimeoutError('serial probe did not complete before deadline')
    except Exception as error:
        report.update(state='failed', error=str(error))
        raise
    finally:
        if qmp:
            try: qmp_execute(qmp, [('stop', {})])
            except Exception: pass
        if ws:
            try: ws.command('stop_vm', 'ok', vm_id='win98')
            except Exception: pass
            ws.close()
        if app and app.poll() is None:
            app.send_signal(signal.SIGTERM)
            try: app.wait(timeout=5)
            except subprocess.TimeoutExpired:
                app.kill(); app.wait()
        for name in ('serial0.log','debugcon.log'):
            if (output/name).exists():
                report['files'][name] = hashlib.sha256((output/name).read_bytes()).hexdigest()
        (output/'probe.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    p = commands.add_parser('prepare')
    for name in ('source','package','native','juke','output'):
        p.add_argument('--'+name, type=Path, required=True)
    p.add_argument('--resources', type=Path, default=Path('files/resources'))
    p.add_argument('--executable', required=True)
    p.add_argument('--payload', action='append', default=[])
    p.add_argument('--completion', required=True)
    p.add_argument('--port', type=int, default=19997)
    p.add_argument('--timeout', type=float, default=90)
    p = commands.add_parser('run')
    p.add_argument('output', type=Path)
    args = parser.parse_args()
    if args.command == 'prepare': print(prepare(args))
    else: run_probe(args.output)

if __name__ == '__main__':
    main()
