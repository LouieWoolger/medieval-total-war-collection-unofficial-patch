"""Exact four-state PE32 proof; requires a task-owned MTW_STOCK_EXE."""

import importlib.util
import os
from pathlib import Path
import struct
import unittest
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32, UC_HOOK_CODE
from unicorn.x86_const import UC_X86_REG_EAX, UC_X86_REG_EIP, UC_X86_REG_ESP


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "tools" / "build-sprite-exe-patch.py"
SPEC = importlib.util.spec_from_file_location("sprite_exe_patch", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class SpriteExePatchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        path = os.environ.get("MTW_STOCK_EXE")
        if not path:
            raise unittest.SkipTest("MTW_STOCK_EXE is required")
        cls.stock = Path(path).read_bytes()
        cls.variants = MODULE.build_variants(cls.stock)

    def test_all_states_and_reversibility(self):
        variants = self.variants
        self.assertEqual(set(variants), {(0, 0), (1, 0), (0, 1), (1, 1)})
        self.assertEqual(variants[(0, 0)], self.stock)
        self.assertEqual(MODULE.sha256(variants[(1, 0)]), MODULE.SCROLL_SHA256)
        self.assertEqual(len({MODULE.sha256(v) for v in variants.values()}), 4)
        for current in variants.values():
            for desired, want in variants.items():
                self.assertEqual(MODULE.transform(current, *desired), want)

    def test_sprite_section_and_entry_targets(self):
        for scroll, rva, raw, count in ((0, 0xB4C000, 0x4E7000, 7),
                                        (1, 0xB4E000, 0x4E9000, 9)):
            data = self.variants[(scroll, 1)]
            pe = struct.unpack_from("<I", data, 0x3C)[0]
            opt = pe + 24
            table = opt + struct.unpack_from("<H", data, pe + 20)[0]
            entry = 0x33D47E
            displacement = struct.unpack_from("<i", data, entry + 1)[0]
            self.assertEqual(data[entry], 0xE9)
            self.assertEqual(0x73D47E + 5 + displacement, 0x400000 + rva)
            self.assertEqual(struct.unpack_from("<H", data, pe + 6)[0], count)
            self.assertEqual(struct.unpack_from("<I", data, opt + 56)[0], rva + 0x1000)
            section = table + (count - 1) * 40
            self.assertEqual(data[section:section + 8].rstrip(b"\0"), b".mtwspr")
            self.assertEqual(struct.unpack_from("<I", data, section + 12)[0], rva)
            self.assertEqual(struct.unpack_from("<I", data, section + 20)[0], raw)
            self.assertEqual(struct.unpack_from("<I", data, section + 36)[0], 0x60000020)
            self.assertEqual(len(data), raw + 0x1000)
            self.assertEqual(struct.unpack_from("<I", data, opt + 64)[0], MODULE.pe_checksum(data, opt + 64))

    def test_foreign_and_partial_states_rejected(self):
        for data in (self.stock[:-1], self.stock + b"x",
                     self.variants[(1, 1)][:-1]):
            with self.assertRaises(ValueError):
                MODULE.transform(data, 0, 0)
        damaged = bytearray(self.variants[(0, 1)])
        damaged[0x33D47E] ^= 1
        with self.assertRaises(ValueError):
            MODULE.transform(bytes(damaged), 0, 0)

    def test_hook_skips_only_origins_beyond_bottom_or_right(self):
        cases = ((98, 622, False), (799, 599, True), (800, 599, False),
                 (799, 600, False), (0, 0, True), (-1, 10, True))
        for scroll in (0, 1):
            va = 0xF4E000 if scroll else 0xF4C000
            code = MODULE.assemble_hook(va)
            for x, y, enters_original in cases:
                with self.subTest(scroll=scroll, x=x, y=y):
                    machine = Uc(UC_ARCH_X86, UC_MODE_32)
                    machine.mem_map(0xF40000, 0x10000)
                    machine.mem_map(0x100000, 0x10000)
                    machine.mem_map(0x200000, 0x1000)
                    machine.mem_write(va, code)
                    machine.mem_write(0xF44100, struct.pack("<II", 800, 600))
                    stack = 0x108000
                    machine.mem_write(stack, struct.pack("<7I", 0x200000, 0x11111111,
                                                          x & 0xFFFFFFFF, y & 0xFFFFFFFF,
                                                          0, 0xFFFFFFFF, 0xFFFFFFFF))
                    machine.reg_write(UC_X86_REG_EAX, 0xABCDEF01)
                    machine.reg_write(UC_X86_REG_ESP, stack)
                    machine.reg_write(UC_X86_REG_EIP, va)
                    destinations = []

                    def stop_at_target(uc, address, size, unused):
                        if address in (MODULE.BODY, 0x200000):
                            destinations.append(address)
                            uc.emu_stop()

                    machine.hook_add(UC_HOOK_CODE, stop_at_target)
                    machine.emu_start(va, 0, count=20)
                    self.assertEqual(destinations, [MODULE.BODY if enters_original else 0x200000])
                    self.assertEqual(machine.reg_read(UC_X86_REG_EAX), 0xABCDEF01)
                    self.assertEqual(machine.reg_read(UC_X86_REG_ESP), stack + (0 if enters_original else 4))


if __name__ == "__main__":
    unittest.main()
