"""Identity, reversibility and PE layout gates for the direct camera patch.

Set MTW_STOCK_EXE to a locally owned stock Medieval_TW.exe for these tests.
No game executable or save is committed to this repository.
"""

import importlib.util
import os
from pathlib import Path
import struct
import unittest


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools" / "build-scroll-exe-patch.py"
SPEC = importlib.util.spec_from_file_location("scroll_exe_patch", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class ScrollExePatchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = os.environ.get("MTW_STOCK_EXE")
        if not path:
            raise unittest.SkipTest("MTW_STOCK_EXE is required for exact-binary tests")
        cls.stock = Path(path).read_bytes()

    def setUp(self):
        self.hook = MODULE.assemble_hook(ROOT / "vendor" / "scroll_exe" / "scroll_hook.asm")

    def test_exact_roundtrip_and_section_permissions(self):
        patched = MODULE.patch_exe(self.stock, self.hook)
        self.assertEqual(MODULE.restore_exe(patched, self.hook), self.stock)
        self.assertTrue(MODULE.verify_patched(patched, self.hook))
        self.assertEqual(len(patched), len(self.stock) + 0x2000)
        pe = struct.unpack_from("<I", patched, 0x3C)[0]
        self.assertEqual(struct.unpack_from("<H", patched, pe + 6)[0], 8)
        opt = pe + 24
        self.assertEqual(struct.unpack_from("<I", patched, opt + 56)[0], 0xB4E000)
        section_table = opt + struct.unpack_from("<H", patched, pe + 20)[0]
        code = section_table + 6 * 40
        data = section_table + 7 * 40
        self.assertEqual(patched[code:code+8].rstrip(b"\0"), b".mtwpan")
        self.assertEqual(struct.unpack_from("<I", patched, code + 36)[0], 0x60000020)
        self.assertEqual(struct.unpack_from("<I", patched, data + 36)[0], 0xC0000040)
        for va in MODULE.CALLSITES:
            raw = va - 0x400000
            self.assertEqual(patched[raw:raw + 5], self.stock[raw:raw + 5])

    def test_wrong_stock_and_twice_applied_rejected(self):
        altered = bytearray(self.stock)
        altered[0x1000] ^= 1
        with self.assertRaises(ValueError):
            MODULE.patch_exe(bytes(altered), self.hook)
        patched = MODULE.patch_exe(self.stock, self.hook)
        with self.assertRaises(ValueError):
            MODULE.patch_exe(patched, self.hook)

    def test_partial_or_foreign_patch_rejected(self):
        patched = bytearray(MODULE.patch_exe(self.stock, self.hook))
        patched[0x1000] ^= 1
        with self.assertRaises(ValueError):
            MODULE.restore_exe(bytes(patched), self.hook)
        truncated = MODULE.patch_exe(self.stock, self.hook)[:-1]
        with self.assertRaises(ValueError):
            MODULE.restore_exe(truncated, self.hook)


if __name__ == "__main__":
    unittest.main()
