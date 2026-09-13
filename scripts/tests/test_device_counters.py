#!/usr/bin/env python3
"""Diskless QEMU diagnostic counters: exact MMIO, validated queries, reset/freeze."""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import argparse
import importlib.util
import json
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import time

spec=importlib.util.spec_from_file_location('dg_trace',(Path(__file__).resolve().parents[2] / 'scripts/diagnostics/gl-trace.py'))
trace=importlib.util.module_from_spec(spec);spec.loader.exec_module(trace)


def check(binary):
    with tempfile.TemporaryDirectory(prefix='dg-count-') as temporary:
        directory=Path(temporary);qmp_path=directory/'qmp';qtest_path=directory/'qtest'
        process=subprocess.Popen([str(binary),'-machine','pc','-accel','qtest','-m','64M','-nodefaults',
            '-vga','none','-device','dreamgpu,addr=04.0,id=retro','-display','none',
            '-qmp',f'unix:{qmp_path},server=on,wait=off','-qtest',f'unix:{qtest_path},server=on,wait=off',
            '-qtest-log','/dev/null'],stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.PIPE)
        qmp=None;channel=None;reader=None
        try:
            deadline=time.monotonic()+10
            while not qmp_path.exists() or not qtest_path.exists():
                if process.poll() is not None:raise RuntimeError(process.stderr.read().decode())
                if time.monotonic()>deadline:raise TimeoutError('QEMU endpoints unavailable')
                time.sleep(.01)
            qmp=trace.Qmp(qmp_path);channel=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM)
            channel.settimeout(5);channel.connect(str(qtest_path));reader=channel.makefile('rb')
            def command(text):
                channel.sendall(text.encode()+b'\n')
                while True:
                    reply=reader.readline().decode().strip()
                    if reply.startswith('IRQ '):continue
                    assert reply.startswith('OK'),(text,reply)
                    return reply
            def write(reg,value):command(f'writel {0xf0000000+reg:#x} {value:#x}')
            def read(reg):return int(command(f'readl {0xf0000000+reg:#x}').split()[1],16)
            def enabled(value):qmp.command('qom-set',{'path':'/machine/peripheral/retro','property':'diagnostic-counters','value':value})
            def stats():return json.loads(qmp.command('qom-get',{'path':'/machine/peripheral/retro','property':'diagnostic-stats'}))
            for offset,value in [(0x10,0xe0000000),(0x18,0xf0000000),(4,7)]:
                command(f'outl 0xcf8 {0x80002000+offset:#x}');command(f'outl 0xcfc {value:#x}')
            assert read(0x1000)==0x47524a51
            assert stats()['mmio']==[] # Disabled by default, including no hidden accumulation.
            enabled(True)
            read(0x1000);read(0x1000);write(0x1130,809)
            assert stats()['mmio']==[{'offset':0x1000,'reads':2,'writes':0},{'offset':0x1130,'reads':0,'writes':1}]
            enabled(False);frozen=stats();read(0x1000);assert stats()==frozen
            enabled(True);assert stats()['mmio']==[]
            generation=read(0x103c)
            # No engine/socket is needed: counters describe validated submission,
            # not successful native execution. Transport then correctly rejects it.
            packet=struct.pack('<12I',11,48,1,1,1,0,0,generation,809,0xcf0,0,0)
            command(f'write 0x10000 {len(packet)} 0x{packet.hex()}')
            for reg,value in [(0x1104,0x10000),(0x1108,0),(0x110c,48),(0x1110,1),
                              (0x1114,generation),(0x115c,0x20000),(0x1160,0),(0x1164,4),(0x1118,1)]:write(reg,value)
            enabled(False);gl=stats()['gl']
            assert gl['batches']==1 and gl['records']==1 and gl['bytes']==48,gl
            assert gl['operations']==[{'op':11,'count':1}],gl
            assert gl['functions']==[{'function':809,'count':1}],gl
            assert gl['queries']==[{'function':809,'arg0':0xcf0,'arg1':0,'count':1}],gl
            assert not gl['query_overflow'] and not gl['function_overflow']
            print('PASS device counters: default disabled, exact MMIO/query counts, reset and frozen snapshots')
        finally:
            if reader:reader.close()
            if channel:channel.close()
            if qmp:qmp.close()
            process.terminate()
            try:process.wait(timeout=5)
            except subprocess.TimeoutExpired:process.kill();process.wait()
            process.stderr.close()

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--qemu',type=Path,default=Path('target/qemu-build/qemu-system-i386'))
    check(parser.parse_args().qemu.resolve())
