"""Exercise defensive reads without attaching to or modifying any process."""
import json
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from playability_trace import sample


class Memory:
    def __init__(self, count=2, turnover=False):
        self.count = count
        self.turnover = turnover
        self.pointer_reads = 0
        self.controller_reads = 0

    def read(self, address, size):
        if address == 0x469A570:
            self.pointer_reads += 1
            return struct.pack("<Q", 0x10000 if self.pointer_reads == 1 or not self.turnover else 0x20000)
        if address == 0x469A578:
            return struct.pack("<II", 2, self.count)
        if address == 0x10000:
            self.controller_reads += 1
            raw = bytearray(size)
            struct.pack_into("<f", raw, 0x24, float("nan"))
            raw[0x1F] = 1
            return bytes(raw)
        return bytes(size)


class ReadGuards(unittest.TestCase):
    def test_invalid_count_never_dereferences_array(self):
        memory = Memory(count=3)
        with self.assertRaisesRegex(ValueError, "Unexpected controller array"):
            sample(memory, 0)
        self.assertEqual(memory.controller_reads, 0)

    def test_pointer_turnover_rejects_sample(self):
        with self.assertRaisesRegex(ValueError, "pointer changed"):
            sample(Memory(turnover=True), 0)

    def test_nonfinite_data_preserved_as_null_without_claiming_second_slot_input(self):
        result = sample(Memory(), 0)
        first, second = result["controllers"]
        self.assertEqual(first["legacy_axis0"], [None, 0.0])
        self.assertEqual(first["touchpad_pressed"], 1)
        self.assertNotIn("legacy_axis0", second)
        json.dumps(result, allow_nan=False)

    def test_render_target_addresses_are_not_dereferenced(self):
        class Targets(Memory):
            def read(self, address, size):
                if address == 0x469AB58:
                    return struct.pack("<Q", 0xBAD00000)
                if address == 0xBAD00000:
                    raise AssertionError("Unsynchronized render target was dereferenced")
                if address == 0x630B50:
                    return struct.pack("<16f", *([float('inf')] * 16))
                return super().read(address, size)
        result = sample(Targets(), 0)
        self.assertEqual(result['render_targets_by_rva']['0x469ab58'], '0xbad00000')
        self.assertEqual(result['menu_matrix_components'], [None] * 16)
        json.dumps(result, allow_nan=False)


if __name__ == "__main__":
    unittest.main()
