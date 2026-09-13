
# Keep this CLI runnable by its file path as well as through the scripts package.
if __package__ in (None, ""):
    import sys as _sys
    from pathlib import Path as _Path
    _sys.path.insert(0, str(_Path(__file__).resolve().parents[2]))

import importlib.util
from pathlib import Path
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("dg_vm", (Path(__file__).resolve().parents[2] / 'scripts/automation/vm.py'))
vm = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vm)


class DiagnosticsTests(unittest.TestCase):
    def test_pci_discovery_requires_one_assigned_device(self):
        gpu = {"id": {"vendor": 0x1234, "device": 0x1113}, "regions": [
            {"bar": 2, "type": "memory", "size": 8192, "address": 0xfebf0000}]}
        nested = [{"devices": [{"pci_bridge": {"devices": [gpu]}}]}]
        self.assertEqual(vm.dg_device(nested), (gpu, 0xfebf0000))
        for devices in ([], [gpu, gpu]):
            with self.assertRaises(ValueError):
                vm.dg_device([{"devices": devices}])
        gpu["regions"][0]["address"] = -1
        with self.assertRaises(ValueError):
            vm.dg_device(nested)

    def test_memory_parser_rejects_errors_and_wrong_offsets(self):
        self.assertEqual(vm.parse_physical_words(
            "00001000: 0x00000001 0x00000002\r\n00001008: 0xffffffff\r\n", 0x1000, 3),
            [1, 2, 0xffffffff])
        for response in ("Cannot access memory", "1000: 0x00000001",
                         "1004: 0x00000001 0x00000002", "1000: 0x123456789 0x00000002"):
            with self.assertRaises(ValueError):
                vm.parse_physical_words(response, 0x1000, 2)

    def test_diagnostic_read_uses_actual_dma_address_and_validates_identity(self):
        gpu = {"bus": 0, "slot": 2, "function": 0, "id": {"vendor": 0x1234, "device": 0x1113},
               "regions": [{"bar": 2, "type": "memory", "size": 8192, "address": 0xfebf0000}]}
        regs = [0] * 18
        regs[0] = 0x47524a51
        regs[4] = 0x12345000
        diag = [0] * len(vm.NT_DIAGNOSTICS)
        diag[:2] = [0x4744494a, 1]
        diag[28] = 2

        def dump(address, words):
            return f"{address:x}: " + " ".join(f"0x{word:08x}" for word in words)

        def respond(endpoint, commands):
            self.assertEqual(endpoint, "fixture.sock")
            name, args = commands[0]
            if name == "query-pci":
                return [[{"devices": [gpu]}]]
            if args["command-line"] == "xp /18wx 0xfebf1000":
                return [dump(0xfebf1000, regs)]
            self.assertEqual(args["command-line"], f"xp /{len(diag)}wx 0x12345c00")
            return [dump(0x12345c00, diag)]

        with patch.object(vm, "qmp_execute", side_effect=respond):
            result = vm.nt_diagnostics("fixture.sock")
            self.assertFalse(result["atomic"])
            self.assertEqual(result["dma_page"], 0x12345000)
            self.assertEqual(result["diagnostics"]["rejected_user_requests"], 2)
            diag[0] = 0
            with self.assertRaises(ValueError):
                vm.nt_diagnostics("fixture.sock")


if __name__ == "__main__":
    unittest.main()
