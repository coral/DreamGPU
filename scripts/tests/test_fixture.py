"""Contract checks for disposable fixture ownership and paused network startup."""

# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import importlib.util
import json
from pathlib import Path
import socket
import subprocess
import tempfile
import threading
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('dg_fixture', (Path(__file__).resolve().parents[2] / 'scripts/fixtures/fixture.py'))
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)


class FixtureTests(unittest.TestCase):
    def test_paused_cleanup_still_requires_owned_process(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            endpoint = root / 'qmp'
            endpoint.touch()
            report = {'state': 'paused', 'pid': 123, 'qmp': str(endpoint)}
            (root / 'run.json').write_text(json.dumps(report))
            with patch.object(fixture.subprocess, 'run') as process, \
                 patch.object(fixture, 'qmp_execute') as qmp, \
                 patch.object(fixture.os, 'killpg') as kill:
                process.return_value.returncode = 0
                process.return_value.stdout = 'another application'
                with self.assertRaisesRegex(ValueError, 'no longer owns'):
                    fixture.stop(root)
                qmp.assert_not_called()
                kill.assert_not_called()
                process.return_value.stdout = 'juke --config-dir ' + str(root)
                result = fixture.stop(root)
                self.assertEqual(result['state'], 'stopped')
                qmp.assert_called_once_with(str(endpoint), [('quit', {})])
                kill.assert_called_once_with(123, fixture.signal.SIGTERM)

    def test_fixed_install_launch_is_sent_once_without_probe_or_path(self):
        with tempfile.TemporaryDirectory(dir='/tmp') as directory:
            endpoint = str(Path(directory)/'serial')
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as server:
                server.bind(endpoint); server.listen(1); messages=[]
                def serve():
                    connection, _ = server.accept()
                    with connection, connection.makefile('rwb', buffering=0) as stream:
                        command = stream.readline().decode().strip(); messages.append(command)
                        stream.write(f'INSTALLING {command.split()[1]}\n'.encode())
                        self.assertEqual(stream.read(1), b'')
                thread=threading.Thread(target=serve); thread.start()
                request=fixture.launch_installer(endpoint,2)
                thread.join(2); self.assertFalse(thread.is_alive())
                self.assertEqual(messages,[f'INSTALL {request}'])

    def test_install_preserves_uncertain_attempt_and_never_retries(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory); package=path/'package.json'
            package.write_text(json.dumps({'runner_identity':'a'*64}))
            (path/'run.json').write_text(json.dumps({'serial':'unused'}))
            old={'identity':'a'*64,'instance':'1'*32}
            with patch.object(fixture,'inspect_runner',return_value=old), \
                 patch.object(fixture,'launch_installer',side_effect=TimeoutError('lost ack')) as launch:
                with self.assertRaises(TimeoutError):fixture.install_package(path,package,2)
                launch.assert_called_once()
            report=json.loads((path/'run.json').read_text())
            self.assertEqual(report['runner_before_install'],old)
            self.assertEqual(report['runner_install_attempt']['state'],'failed')
            with self.assertRaisesRegex(ValueError,'already pending'):
                fixture.install_package(path,package,2)

    def test_install_requires_new_instance_even_for_same_build(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory); package=path/'package.json'
            package.write_text(json.dumps({'runner_identity':'a'*64}))
            (path/'run.json').write_text(json.dumps({'serial':'unused'}))
            old={'identity':'a'*64,'instance':'1'*32}
            with patch.object(fixture,'inspect_runner',return_value=old), \
                 patch.object(fixture,'launch_installer',return_value='request'), \
                 patch.object(fixture,'ready',return_value='ready') as ready:
                report=fixture.install_package(path,package,2)
                ready.assert_called_once_with('unused',2,'a'*64,'1'*32)
            self.assertNotIn('runner_before_install',report)
            self.assertEqual(report['runner_last_install_from'],old)
            self.assertEqual(report['runner_install_attempt']['state'],'replacement_ready')

    def test_explicit_prelaunch_rejection_allows_only_a_deliberate_new_attempt(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory); package=path/'package.json'
            package.write_text(json.dumps({'runner_identity':'a'*64}))
            (path/'run.json').write_text(json.dumps({'serial':'unused'}))
            old={'identity':'a'*64,'instance':'1'*32}
            with patch.object(fixture,'inspect_runner',return_value=old), \
                 patch.object(fixture,'launch_installer',side_effect=fixture.hl.GuestError('install-media-unavailable')) as launch:
                with self.assertRaises(fixture.hl.GuestError):fixture.install_package(path,package,2)
                launch.assert_called_once()
            rejected=json.loads((path/'run.json').read_text())
            self.assertNotIn('runner_before_install',rejected)
            self.assertEqual(rejected['runner_install_attempt']['state'],'rejected_before_launch')
            with patch.object(fixture,'inspect_runner',return_value=old), \
                 patch.object(fixture,'launch_installer',return_value='second') as launch, \
                 patch.object(fixture,'ready',return_value='ready'):
                accepted=fixture.install_package(path,package,2)
                launch.assert_called_once()
            self.assertEqual(accepted['runner_install_history'],[rejected['runner_install_attempt']])

    def test_recorded_prelaunch_failure_migrates_without_losing_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory); package=path/'package.json'
            package.write_text(json.dumps({'runner_identity':'a'*64}))
            old={'identity':'a'*64,'instance':'1'*32}
            (path/'run.json').write_text(json.dumps({'serial':'unused','runner_before_install':old,
                'runner_install_attempt':{'state':'failed','error':'guest error: install-media-unavailable'}}))
            with patch.object(fixture,'inspect_runner',return_value=old), \
                 patch.object(fixture,'launch_installer',return_value='second'), \
                 patch.object(fixture,'ready',return_value='ready'):
                report=fixture.install_package(path,package,2)
            self.assertEqual(report['runner_install_history'][0]['error'],'guest error: install-media-unavailable')
            self.assertEqual(report['runner_install_history'][0]['before'],old)

    def test_same_build_reinstall_waits_for_a_different_process(self):
        with tempfile.TemporaryDirectory(dir='/tmp') as directory:
            endpoint=str(Path(directory)/'serial');expected='a'*64;old='1'*32;new='2'*32
            with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as server:
                server.bind(endpoint);server.listen(1);messages=[]
                def serve():
                    connection,_=server.accept()
                    with connection,connection.makefile('rwb',buffering=0) as stream:
                        for identity,instance in ((expected,old),('b'*64,new),(expected,new)):
                            command=stream.readline().decode().strip();messages.append(command)
                            stream.write(f'INSTANCE {command.split()[1]} {identity}-{instance}\n'.encode())
                thread=threading.Thread(target=serve);thread.start()
                with patch.object(fixture.time,'sleep'):
                    request=fixture.ready(endpoint,3,expected,old)
                thread.join(2);self.assertFalse(thread.is_alive())
                self.assertEqual(messages,[f'INSPECT {request}']*3)

    def test_process_identity_is_bounded_and_strict(self):
        for value in ('',None,'a'*64+'-'+('z'*32),'a'*64+'-'+('1'*33)):
            with self.assertRaises(fixture.hl.ProtocolError):fixture.decode_instance(value)
        self.assertEqual(fixture.decode_instance('a'*64+'-'+('1'*32)),{'identity':'a'*64,'instance':'1'*32})

    def test_install_inspection_supports_atomic_instance_and_legacy_build(self):
        for legacy in (False,True):
            with tempfile.TemporaryDirectory(dir='/tmp') as directory:
                endpoint=str(Path(directory)/'serial');identity='a'*64;instance='1'*32
                with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as server:
                    server.bind(endpoint);server.listen(1);messages=[]
                    def serve():
                        connection,_=server.accept()
                        with connection,connection.makefile('rwb',buffering=0) as stream:
                            command=stream.readline().decode().strip();messages.append(command.split()[0]);request=command.split()[1]
                            if legacy:
                                stream.write(f'ERROR {request} bad-command\n'.encode())
                                command=stream.readline().decode().strip();messages.append(command.split()[0]);request=command.split()[1]
                                stream.write(f'IDENTITY {request} {identity}\n'.encode())
                            else:stream.write(f'INSTANCE {request} {identity}-{instance}\n'.encode())
                    thread=threading.Thread(target=serve);thread.start()
                    result=fixture.inspect_runner(endpoint,2)
                    thread.join(2);self.assertFalse(thread.is_alive())
                    self.assertEqual(result,{'identity':identity,'instance':None if legacy else instance})
                    self.assertEqual(messages,['INSPECT','IDENTIFY'] if legacy else ['INSPECT'])

    def test_install_readiness_requires_new_runner_identity(self):
        with tempfile.TemporaryDirectory(dir='/tmp') as directory:
            endpoint=str(Path(directory)/'serial');expected='a'*64
            with socket.socket(socket.AF_UNIX,socket.SOCK_STREAM) as server:
                server.bind(endpoint);server.listen(1);messages=[]
                def serve():
                    connection,_=server.accept()
                    with connection,connection.makefile('rwb',buffering=0) as stream:
                        for reply in ('ERROR {} bad-command','IDENTITY {} '+('b'*64),'IDENTITY {} '+expected):
                            command=stream.readline().decode().strip();messages.append(command)
                            stream.write((reply.format(command.split()[1])+'\n').encode())
                thread=threading.Thread(target=serve);thread.start()
                with patch.object(fixture.time,'sleep'):
                    request=fixture.ready(endpoint,3,expected)
                thread.join(2);self.assertFalse(thread.is_alive())
                self.assertEqual(messages,[f'IDENTIFY {request}']*3)

    def test_network_is_disabled_before_cont(self):
        with patch.object(fixture, 'qmp_execute', side_effect=[[{'status': 'paused'}], [{}, {}]]) as qmp:
            self.assertEqual(fixture.resume_guest('/tmp/test')['status'], 'paused')
        self.assertEqual(qmp.call_args_list[1].args[1], [
            ('set_link', {'name': 'net0', 'up': False}), ('cont', {})])

    def test_running_guest_is_rejected_without_touching_link_or_resuming(self):
        with patch.object(fixture, 'qmp_execute', return_value=[{'status': 'running'}]) as qmp:
            with self.assertRaisesRegex(RuntimeError, 'not paused'):
                fixture.resume_guest('/tmp/test')
        qmp.assert_called_once()

    def test_setting_update_preserves_hardware_and_other_tables(self):
        text = '[vm]\nname = "x"\n[qemu]\nprofile = "retro-win2000"\nqemu_binary = "old"\n[render]\nshader = "none"\n'
        changed = fixture.replace_setting(text, 'qemu', 'qemu_binary', '/a/b"c')
        self.assertEqual(fixture.tomllib.loads(changed)['qemu'], {
            'profile': 'retro-win2000', 'qemu_binary': '/a/b"c'})
        self.assertEqual(changed.replace('qemu_binary = "/a/b\\"c"', 'qemu_binary = "old"'), text)

    def test_single_ready_handshake_skips_stale_id(self):
        with tempfile.TemporaryDirectory(dir='/tmp') as directory:
            endpoint = str(Path(directory) / 'serial')
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as server:
                server.bind(endpoint)
                server.listen(1)
                messages = []
                def serve():
                    connection, _ = server.accept()
                    with connection, connection.makefile('rwb', buffering=0) as stream:
                        ping = stream.readline().decode().strip()
                        messages.append(ping)
                        request = ping.split()[1]
                        stream.write(f'READY stale\nREADY {request}\n'.encode())
                        messages.append(stream.read(1))
                thread = threading.Thread(target=serve)
                thread.start()
                request = fixture.ready(endpoint, 2)
                thread.join(2)
                self.assertFalse(thread.is_alive())
                self.assertEqual(messages, [f'PING {request}', b''])

    def test_cold_runner_discovery_survives_discarded_first_ping(self):
        with tempfile.TemporaryDirectory(dir='/tmp') as directory:
            endpoint = str(Path(directory) / 'serial')
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as server:
                server.bind(endpoint)
                server.listen(1)
                messages = []
                def serve():
                    connection, _ = server.accept()
                    with connection, connection.makefile('rwb', buffering=0) as stream:
                        messages.append(stream.readline().decode().strip())
                        messages.append(stream.readline().decode().strip())
                        request = messages[1].split()[1]
                        stream.write(f'READY {request}\n'.encode())
                thread = threading.Thread(target=serve)
                thread.start()
                request = fixture.ready(endpoint, 3)
                thread.join(2)
                self.assertFalse(thread.is_alive())
                self.assertEqual(messages, [f'PING {request}', f'PING {request}'])

    def test_diagnostic_failure_does_not_replace_primary_error(self):
        with tempfile.TemporaryDirectory() as directory:
            report = {'qmp': '/gone', 'error': 'runner deadline expired'}
            with patch.object(fixture, 'qmp_screenshot', side_effect=OSError('QMP unavailable')):
                fixture.failure_diagnostics(Path(directory), report)
            self.assertEqual(report['error'], 'runner deadline expired')
            self.assertEqual(report['failure_screenshot_error'], 'QMP unavailable')

    def test_bootstrap_program_rejects_arguments_and_shell_input(self):
        self.assertEqual(fixture.validate_guest_program('C:\\DGPUBEN.EXE'), 'C:\\DGPUBEN.EXE')
        for value in ['DGPUBEN.EXE', 'C:\\runner.exe -arg', 'C:\\x&bad.exe', 'C:\\x\n.exe']:
            with self.subTest(value=value), self.assertRaises(ValueError):
                fixture.validate_guest_program(value)

    def test_package_provenance_preserves_previous_installation_and_rejects_symlinks(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            members = root / 'members'; members.mkdir()
            (members / 'runner.exe').write_bytes(b'runner')
            media = root / 'package.iso'; media.write_bytes(b'media')
            (root / 'run.json').write_text(json.dumps({'state': 'ready', 'installed_guest': 'original'}))
            record = fixture.record_package(root, media, members)
            report = json.loads((root / 'run.json').read_text())
            self.assertEqual(report['installed_guest'], 'original')
            self.assertEqual(record['files'], {'runner.exe': fixture.digest(members / 'runner.exe')})
            self.assertIn('host inputs only', record['verification'])
            (members / 'external').symlink_to(media)
            with self.assertRaisesRegex(ValueError, 'regular member files'):
                fixture.record_package(root, media, members)
            self.assertEqual(json.loads((root / 'run.json').read_text()), report)

    def test_login_key_rejects_unbounded_or_unknown_bootstrap(self):
        with tempfile.TemporaryDirectory() as directory:
            manifest = Path(directory) / 'manifest.json'
            for extra in ({'login_key': 'esc'},
                          {'login_key': 'space', 'login_return_after_seconds': 25}):
                manifest.write_text(json.dumps({'schema_version': 1, 'machine': 'win98', **extra}))
                with self.subTest(extra=extra), self.assertRaisesRegex(ValueError, 'login_key'):
                    fixture.prepare(manifest, Path(directory) / 'must-not-exist')
                self.assertFalse((Path(directory) / 'must-not-exist').exists())

    def test_login_event_requires_one_bounded_cold_trigger(self):
        with tempfile.TemporaryDirectory() as directory:
            manifest = Path(directory) / 'manifest.json'
            for extra in ({'login_debugcon_marker': ''},
                          {'login_debugcon_marker': 'event\nnext'},
                          {'login_debugcon_marker': 3},
                          {'login_debugcon_marker': 'event', 'login_return_after_seconds': 25},
                          {'login_debugcon_marker': 'event', 'snapshot': 'ready'}):
                manifest.write_text(json.dumps({'schema_version': 1, 'machine': 'win98', **extra}))
                with self.subTest(extra=extra), self.assertRaisesRegex(ValueError, 'login_debugcon_marker'):
                    fixture.prepare(manifest, Path(directory) / 'must-not-exist')
                self.assertFalse((Path(directory) / 'must-not-exist').exists())

    def test_artifact_hash_mismatch_stops_before_launch(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / 'qemu'
            binary.write_bytes(b'executable')
            with self.assertRaisesRegex(ValueError, 'hash mismatch'):
                fixture.checked_artifact({'path': str(binary), 'sha256': '0' * 64})

    def test_missing_app_resources_fail_before_disk_copy(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            manifest = root / 'manifest.json'
            manifest.write_text(json.dumps({'schema_version': 1, 'machine': 'winxp',
                                            'source_fixture': str(root)}))
            with self.assertRaisesRegex(ValueError, 'resources/fonts'):
                fixture.prepare(manifest, root / 'output')
            self.assertFalse((root / 'output').exists())

    def test_private_chain_keeps_snapshot_and_does_not_mutate_sources(self):
        image_tool = fixture.ROOT / 'target/qemu-build/qemu-img'
        if not image_tool.is_file():
            self.skipTest('requires built qemu-img for real image-format contract')
        with tempfile.TemporaryDirectory(dir='/tmp') as directory:
            root = Path(directory)
            source = root / 'source'
            machine = source / 'machines/win2000'
            machine.mkdir(parents=True)
            (source / 'resources/fonts').mkdir(parents=True)
            (source / 'config.toml').write_text('[general]\nvsync = true\n[control]\nlisten = "127.0.0.1:9000"\n')
            (machine / 'machine.toml').write_text('[vm]\nname = "test"\n[qemu]\nprofile = "retro-win2000"\nqemu_binary = "old"\ndisk = "disk.qcow2"\n')
            base, disk = machine / 'base.qcow2', machine / 'disk.qcow2'
            subprocess.run([str(image_tool), 'create', '-q', '-f', 'qcow2', str(base), '2M'], check=True)
            subprocess.run([str(image_tool), 'create', '-q', '-f', 'qcow2', '-F', 'qcow2', '-b', str(base), str(disk)], check=True)
            subprocess.run([str(image_tool), 'snapshot', '-c', 'ready', str(disk)], check=True)
            base.chmod(0o444)
            before = {str(path): fixture.digest(path) for path in (base, disk)}
            artifact = {'path': str(image_tool), 'sha256': fixture.digest(image_tool)}
            data = {'schema_version': 1, 'source_fixture': str(source), 'machine': 'win2000',
                    'disk': str(disk), 'snapshot': 'ready', 'qemu_img': str(image_tool),
                    'native': artifact, 'app': artifact, 'launcher': artifact}
            manifest = root / 'manifest.json'
            manifest.write_text(json.dumps(data))
            output = root / 'copy'
            result = fixture.prepare(manifest, output)
            self.assertEqual(result['state'], 'prepared')
            self.assertEqual(before, {str(path): fixture.digest(path) for path in (base, disk)})
            chain = json.loads(subprocess.check_output([str(image_tool), 'info', '--output=json', '--backing-chain', result['disk']]))
            self.assertEqual([item['name'] for item in chain[0]['snapshots']], ['ready'])
            self.assertTrue(all(Path(item['filename']).resolve().is_relative_to(output.resolve()) for item in chain))
            self.assertIn('-S -loadvm ready', (output / 'qemu-system-i386').read_text())
            self.assertNotEqual(str(disk), result['disk'])
            with self.assertRaises(FileExistsError):
                fixture.prepare(manifest, output)


if __name__ == '__main__':
    unittest.main()
