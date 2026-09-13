# SPDX-License-Identifier: GPL-2.0-or-later
import unittest
from scripts.fixtures import guest_tools


class ProbeDeployment(unittest.TestCase):
    def manifest(self, path='/SIERRA/Half-Life/DGUT.EXE'):
        return {'files': [{'destination': path, 'sha256': 'a' * 64}],
                'probe_helpers': {'utglide': 'a' * 64}}

    def test_actual_runner_paths(self):
        paths = guest_tools.registered()
        self.assertGreater(len(paths), 40)
        self.assertEqual(str(paths['sysui']), r'C:\DGSETUI.EXE')
        self.assertEqual(str(paths['utglide']), r'C:\SIERRA\Half-Life\DGUT.EXE')
        self.assertEqual(len(guest_tools.validate(self.manifest())), 1)

    def test_case_insensitive_windows_path(self):
        self.assertEqual(len(guest_tools.validate(self.manifest('/sierra/HALF-LIFE/dgut.exe'))), 1)

    def test_right_filename_wrong_directory_is_not_an_upgrade(self):
        with self.assertRaisesRegex(ValueError, 'must stage its exact helper'):
            guest_tools.validate(self.manifest('/DGUT.EXE'))

    def test_old_helper_is_not_an_upgrade(self):
        value = self.manifest()
        value['files'][0]['sha256'] = 'b' * 64
        with self.assertRaises(ValueError):
            guest_tools.validate(value)

    def test_unknown_and_optical_probes_rejected(self):
        for name in ('typo', 'win9xinstall'):
            value = self.manifest(); value['probe_helpers'] = {name: 'a' * 64}
            with self.assertRaises(ValueError):
                guest_tools.validate(value)

    def test_ambiguous_paths_rejected(self):
        value = self.manifest()
        value['files'].append({'destination': '/sierra/half-life/dgut.exe', 'sha256': 'a' * 64})
        with self.assertRaises(ValueError):
            guest_tools.validate(value)

    def test_unrelated_application_files_still_allowed(self):
        self.assertEqual(guest_tools.validate({'files': [{'destination': '/DGUT.EXE'}]}), [])


if __name__ == '__main__':
    unittest.main()
