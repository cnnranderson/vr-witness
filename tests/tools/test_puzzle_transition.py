"""Check diagnostic guards and transition races without a game or headset."""

import io
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import puzzle_transition as trace


class Reader:
    def __init__(self):
        self.mode_reads = 0
        self.pointer_reads = 0
        self.change_mode = False
        self.change_pointer = False
        self.fail_cursor = False

    def read(self, address, size):
        if address == 0x62D5C4:
            self.mode_reads += 1
            return struct.pack("<i", 2 if self.change_mode and self.mode_reads == 2 else 0)
        if address == 0x62D4D0:
            self.pointer_reads += 1
            return struct.pack("<Q", 0 if self.change_pointer and self.pointer_reads == 2 else 0x10000)
        if address == 0x1009C:
            if self.fail_cursor:
                raise OSError("Object removed")
            return struct.pack("<fff", 1, 2, 3)
        return bytes(size)


class CaptureTests(unittest.TestCase):
    def test_changed_mode_is_marked_unstable(self):
        reader = Reader()
        reader.change_mode = True
        row = trace.sample(reader, 0)
        self.assertFalse(row["mode_stable"])
        self.assertEqual((row["mode"], row["mode_after"]), (0, 2))

    def test_cursor_swap_does_not_report_old_origin(self):
        reader = Reader()
        reader.change_pointer = True
        row = trace.sample(reader, 0)
        self.assertIsNone(row["projection_origin"])
        self.assertFalse(row["projection_readable"])

    def test_disappearing_cursor_keeps_other_state(self):
        reader = Reader()
        reader.fail_cursor = True
        row = trace.sample(reader, 0)
        self.assertIsNone(row["projection_origin"])
        self.assertTrue(row["mode_stable"])

    def test_nan_is_unavailable_not_zero(self):
        self.assertIsNone(trace.floats(struct.pack("<fff", 1, float("nan"), 2)))
        self.assertEqual(trace.floats(bytes(12)), [0, 0, 0])

    def test_signature_mismatch_fails_before_capture(self):
        class Image:
            data = b"\x01" * 100

            def offset(self, rva):
                return 0

        with self.assertRaisesRegex(ValueError, "Live camera reference mismatch"):
            trace.verify_references(Reader(), 0, Image())

    def test_duration_limit_is_enforced_without_reads(self):
        for seconds in (0, 31, -1):
            with self.assertRaisesRegex(ValueError, "duration"):
                trace.capture(None, 0, io.StringIO(), seconds, {})

    def test_stalled_clock_cannot_produce_unbounded_samples(self):
        stream = io.StringIO()
        with patch.object(trace.time, "monotonic", return_value=0), patch.object(trace.time, "sleep"):
            report = trace.capture(Reader(), 0, stream, 1, {})
        self.assertEqual(report["samples"], 100)
        self.assertEqual(len(stream.getvalue().splitlines()), 102)

    def test_byte_limit_rejects_oversized_output(self):
        with patch.object(trace, "MAX_BYTES", 5):
            with self.assertRaisesRegex(ValueError, "8 MiB limit"):
                trace.capture(None, 0, io.StringIO(), 1, {})


if __name__ == "__main__":
    unittest.main()
