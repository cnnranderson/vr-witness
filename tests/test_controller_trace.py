"""Test bounded reads and misleading controller data without game access."""

import json
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from controller_trace import sample


class Memory:
    def __init__(self, count=2, capacity=2, array=0x10000, turnover=None):
        self.count, self.capacity, self.array = count, capacity, array
        self.turnover = turnover
        self.calls = {}
        self.array_reads = 0

    def read(self, address, size):
        self.calls[address] = self.calls.get(address, 0) + 1
        if address == 0x469A570:
            count = self.count + (1 if self.turnover == "count" and self.calls[address] > 1 else 0)
            return struct.pack("<QII", self.array, self.capacity, count)
        pointers = {0x469A5B0: 0x20000, 0x469A060: 0x30000, 0x30000: 0x40000, 0x40090: 0x50000}
        if address in pointers:
            value = pointers[address]
            if self.turnover == "input" and address == 0x40090 and self.calls[address] > 1:
                value += 0x1000
            return struct.pack("<Q", value)
        if address == 0x10000:
            self.array_reads += 1
            raw = bytearray(size)
            struct.pack_into("<2f", raw, 0x24, float("nan"), -0.5)
            raw[0x1F] = 1
            return bytes(raw)
        if address == 0x50000 + 8 + 0x135 * 4:
            raw = bytearray(size)
            struct.pack_into("<I", raw, 0, 3)
            struct.pack_into("<I", raw, 4, 5)
            return bytes(raw)
        return bytes(size)


class Guards(unittest.TestCase):
    def test_bad_header_cannot_trigger_array_read(self):
        for kwargs in ({"count": 3}, {"capacity": 1}, {"array": 0}):
            with self.subTest(kwargs=kwargs):
                memory = Memory(**kwargs)
                with self.assertRaisesRegex(ValueError, "Unexpected controller array"):
                    sample(memory, 0)
                self.assertEqual(memory.array_reads, 0)

    def test_turnover_rejected_for_allocation_and_input_chain(self):
        for kind in ("count", "input"):
            with self.subTest(kind=kind), self.assertRaisesRegex(ValueError, "metadata changed"):
                sample(Memory(turnover=kind), 0)

    def test_raw_buttons_and_stale_slot_labels(self):
        result = sample(Memory(), 0)
        self.assertEqual(result["allocated_slots"], 2)
        self.assertNotIn("connected_count", result)
        first, second = result["controllers"]
        self.assertEqual(first["legacy_axis0"], [None, -0.5])
        self.assertNotIn("legacy_axis0", second)
        self.assertNotIn("hand", first)
        self.assertTrue(result["native_buttons"]["legacy_trigger"]["held"])
        self.assertFalse(result["native_buttons"]["legacy_pad_click"]["held"])
        json.dumps(result, allow_nan=False)

    def test_empty_array_not_dereferenced(self):
        memory = Memory(count=0, array=0)
        self.assertEqual(sample(memory, 0)["controllers"], [])
        self.assertEqual(memory.array_reads, 0)


if __name__ == "__main__":
    unittest.main()
