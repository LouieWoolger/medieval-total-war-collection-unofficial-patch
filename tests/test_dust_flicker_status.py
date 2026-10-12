"""Contract checks for the external R187 status sampler."""
from __future__ import annotations

import importlib.util
from pathlib import Path
import struct
import unittest


SCRIPT = Path(__file__).parent / "diagnostics" / "dust_flicker_status.py"
SPEC = importlib.util.spec_from_file_location("dust_flicker_status", SCRIPT)
assert SPEC and SPEC.loader
STATUS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(STATUS)


class StatusMappingTests(unittest.TestCase):
    def test_v2_simulation_mapping(self) -> None:
        values = [STATUS.SIM_MAGIC, STATUS.SIM_VERSION, 1234]
        values += [0] * (len(STATUS.SIM_FIELDS) - len(values))
        values[9] = 456
        row = STATUS.decode_status(struct.pack("<24I", *values),
                                   STATUS.SIM_FIELDS, STATUS.SIM_MAGIC,
                                   STATUS.SIM_VERSION, 1234)
        self.assertEqual(row["normalized_child_updates"], 456)

    def test_v8_render_mapping(self) -> None:
        values = [STATUS.RENDER_MAGIC, STATUS.RENDER_VERSION, 1234]
        values += [0] * (len(STATUS.RENDER_FIELDS) - len(values))
        values[26] = 17
        row = STATUS.decode_status(struct.pack("<29I", *values),
                                   STATUS.RENDER_FIELDS, STATUS.RENDER_MAGIC,
                                   STATUS.RENDER_VERSION, 1234)
        self.assertEqual(row["tail_corrected"], 17)

    def test_unknown_version_and_pid_rejected(self) -> None:
        values = [STATUS.SIM_MAGIC, 3, 1234]
        values += [0] * (len(STATUS.SIM_FIELDS) - len(values))
        packet = struct.pack("<24I", *values)
        with self.assertRaises(ValueError):
            STATUS.decode_status(packet, STATUS.SIM_FIELDS,
                                 STATUS.SIM_MAGIC, STATUS.SIM_VERSION, 1234)
        values[1] = STATUS.SIM_VERSION
        packet = struct.pack("<24I", *values)
        with self.assertRaises(ValueError):
            STATUS.decode_status(packet, STATUS.SIM_FIELDS,
                                 STATUS.SIM_MAGIC, STATUS.SIM_VERSION, 9999)

    def test_truncated_mapping_rejected(self) -> None:
        with self.assertRaises(ValueError):
            STATUS.decode_status(b"\0" * 92, STATUS.SIM_FIELDS,
                                 STATUS.SIM_MAGIC, STATUS.SIM_VERSION, 1234)


if __name__ == "__main__":
    unittest.main()
