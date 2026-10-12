"""Check diagnostic guards and transition races without a game or headset."""

import io
import itertools
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


class NativeReader:
    """Exact-size memory fixture with deterministic changes between repeated reads."""

    base = 0x140000000
    table = 0x10000
    entries = 0x20000
    player = 0x30000
    records = 0x40000
    renderer = 0x50000

    def __init__(self):
        self.reads = []
        self.memory = {
            self.base + 0x630470: struct.pack("<i", 103),
            self.base + 0x62D0A0: struct.pack("<Q", self.table),
            self.table + 8: struct.pack("<i4xi4xQ", 100, 4, self.entries),
            self.entries + 24: struct.pack("<Q", self.player),
            self.player + 0x24: struct.pack("<fff", 10, 20, 30),
            self.player + 8: struct.pack("<Q", self.base + 0x62E350),
            self.player + 0xD0: struct.pack("<iii20xi4xi", 1, 0, 1, 205, 1),
            self.base + 0x469A570: struct.pack("<QII", self.records, 2, 2),
            self.records + 0x10: struct.pack("<fff", 1, 2, 3),
            self.records + 0x48: struct.pack("<fff", 4, 5, 6),
            self.base + 0x469A5B0: struct.pack("<Q", self.renderer),
            self.renderer + 0x26: bytes([0, 0, 0, 1]),
        }

    def change_after_read(self, address, new_value):
        self.memory[address] = [self.memory[address], new_value]

    def read(self, address, size):
        self.reads.append((address, size))
        value = self.memory.get(address, OSError("Unmapped address"))
        if isinstance(value, list):
            value = value.pop(0) if len(value) > 1 else value[0]
        if isinstance(value, Exception):
            raise value
        if len(value) != size:
            raise AssertionError(f"Unexpected read size {size} at {address:x}")
        return value


class NativeCameraTests(unittest.TestCase):
    def test_current_player_uses_validated_table_entry(self):
        reader = NativeReader()
        self.assertEqual(trace.player_state(reader, reader.base)[0], [10, 20, 30])
        self.assertEqual(reader.reads.count((reader.entries + 24, 8)), 2)

    def test_player_bad_indices_never_read_an_entry(self):
        for first, count in ((104, 4), (100, 3), (100, 0), (100, -1)):
            with self.subTest(first=first, count=count):
                reader = NativeReader()
                reader.memory[reader.table + 8] = struct.pack("<i4xi4xQ", first, count, reader.entries)
                self.assertEqual(trace.player_state(reader, reader.base), (None, None))
                self.assertFalse(
                    any(reader.entries <= address < reader.player for address, _ in reader.reads)
                )

    def test_null_player_lookup_components_are_unavailable(self):
        reader = NativeReader()
        missing = {
            reader.base + 0x630470: struct.pack("<i", 0),
            reader.base + 0x62D0A0: bytes(8),
            reader.table + 8: struct.pack("<i4xi4xQ", 100, 4, 0),
            reader.entries + 24: bytes(8),
        }
        for address, value in missing.items():
            with self.subTest(address=address):
                reader = NativeReader()
                reader.memory[address] = value
                self.assertEqual(trace.player_state(reader, reader.base), (None, None))

    def test_player_lookup_changes_discard_old_position(self):
        reader = NativeReader()
        changes = (
            (reader.base + 0x630470, struct.pack("<i", 102)),
            (reader.base + 0x62D0A0, struct.pack("<Q", reader.table + 100)),
            (reader.table + 8, struct.pack("<i4xi4xQ", 101, 4, reader.entries)),
            (reader.table + 8, struct.pack("<i4xi4xQ", 100, 5, reader.entries)),
            (reader.table + 8, struct.pack("<i4xi4xQ", 100, 4, reader.entries + 100)),
            (reader.entries + 24, struct.pack("<Q", reader.player + 100)),
            (reader.entries + 24, bytes(8)),
        )
        for address, value in changes:
            with self.subTest(address=address, value=value):
                reader = NativeReader()
                reader.change_after_read(address, value)
                self.assertEqual(trace.player_state(reader, reader.base), (None, None))

    def test_player_disappearing_and_nonfinite_position_are_unavailable(self):
        for value in (OSError("Object removed"), struct.pack("<fff", 10, float("inf"), 30)):
            with self.subTest(value=value):
                reader = NativeReader()
                reader.memory[reader.player + 0x24] = value
                self.assertIsNone(trace.player_state(reader, reader.base)[0])
        reader = NativeReader()
        reader.change_after_read(reader.table + 8, OSError("Table removed"))
        self.assertEqual(trace.player_state(reader, reader.base), (None, None))

    def test_approach_decisions_come_from_the_same_player(self):
        reader = NativeReader()
        position, approach = trace.player_state(reader, reader.base)
        self.assertEqual(position, [10, 20, 30])
        self.assertEqual(
            approach,
            {
                "mode": 1,
                "translation_done": 0,
                "turn_done": 1,
                "target_entity_id": 205,
                "view_origin_selector": 1,
            },
        )

    def test_wrong_player_type_does_not_read_approach_fields(self):
        reader = NativeReader()
        reader.memory[reader.player + 8] = bytes(8)
        self.assertEqual(trace.player_state(reader, reader.base), ([10, 20, 30], None))
        self.assertNotIn((reader.player + 0xD0, 0x2C), reader.reads)

    def test_changing_approach_or_type_discards_decisions_but_keeps_position(self):
        reader = NativeReader()
        for address, value in (
            (reader.player + 0xD0, struct.pack("<iii20xi4xi", 0, 1, 1, 205, 1)),
            (reader.player + 8, bytes(8)),
            (reader.player + 0xD0, OSError("Approach unavailable")),
        ):
            with self.subTest(address=address, value=value):
                reader = NativeReader()
                reader.change_after_read(address, value)
                self.assertEqual(trace.player_state(reader, reader.base), ([10, 20, 30], None))

    def test_unknown_approach_mode_and_stale_target_remain_raw(self):
        reader = NativeReader()
        reader.memory[reader.player + 0xD0] = struct.pack("<iii20xi4xi", 99, 2, -1, 205, 7)
        approach = trace.player_state(reader, reader.base)[1]
        self.assertEqual((approach["mode"], approach["translation_done"], approach["turn_done"]), (99, 2, -1))
        reader = NativeReader()
        reader.memory[reader.player + 0xD0] = struct.pack("<iii20xi4xi", 0, 1, 1, 205, 1)
        approach = trace.player_state(reader, reader.base)[1]
        self.assertEqual((approach["mode"], approach["target_entity_id"]), (0, 205))

    def test_missing_approach_keeps_readable_position(self):
        reader = NativeReader()
        reader.memory[reader.player + 0xD0] = OSError("Approach unavailable")
        self.assertEqual(trace.player_state(reader, reader.base), ([10, 20, 30], None))

    def test_controller_record_positions_use_native_stride(self):
        reader = NativeReader()
        self.assertEqual(trace.controller_filter_records(reader, reader.base), (2, [[1, 2, 3], [4, 5, 6]]))

    def test_controller_count_below_two_does_not_read_record_positions(self):
        for count in (0, 1):
            with self.subTest(count=count):
                reader = NativeReader()
                reader.memory[reader.base + 0x469A570] = struct.pack("<QII", reader.records, 2, count)
                self.assertEqual(trace.controller_filter_records(reader, reader.base), (count, None))
                self.assertEqual(len(reader.reads), 2)

    def test_unsigned_controller_count_does_not_expand_reads(self):
        reader = NativeReader()
        reader.memory[reader.base + 0x469A570] = struct.pack("<QII", reader.records, 0xFFFFFFFF, 0xFFFFFFFF)
        self.assertEqual(
            trace.controller_filter_records(reader, reader.base), (0xFFFFFFFF, [[1, 2, 3], [4, 5, 6]])
        )
        self.assertEqual(len(reader.reads), 4)

    def test_controller_metadata_changes_discard_old_records(self):
        reader = NativeReader()
        for pointer, count in ((0, 2), (reader.records + 100, 2), (reader.records, 1)):
            with self.subTest(pointer=pointer, count=count):
                reader = NativeReader()
                reader.change_after_read(reader.base + 0x469A570, struct.pack("<QII", pointer, 2, count))
                self.assertEqual(trace.controller_filter_records(reader, reader.base), (None, None))

    def test_null_and_disappearing_controller_records_are_unavailable(self):
        reader = NativeReader()
        reader.memory[reader.base + 0x469A570] = struct.pack("<QII", 0, 2, 2)
        self.assertEqual(trace.controller_filter_records(reader, reader.base), (None, None))
        reader = NativeReader()
        reader.memory[reader.records + 0x48] = OSError("Records removed")
        self.assertEqual(trace.controller_filter_records(reader, reader.base), (None, None))

    def test_nonfinite_controller_position_does_not_fabricate_zero(self):
        for offset in (0x10, 0x48):
            with self.subTest(offset=offset):
                reader = NativeReader()
                reader.memory[reader.records + offset] = struct.pack("<fff", 1, float("nan"), 3)
                self.assertEqual(trace.controller_filter_records(reader, reader.base), (2, None))

    def test_renderer_predicate_truth_table(self):
        for flag26, flag27, flag28, flag29 in itertools.product((0, 7), repeat=4):
            with self.subTest(flag26=flag26, flag27=flag27, flag28=flag28, flag29=flag29):
                reader = NativeReader()
                reader.memory[reader.renderer + 0x26] = bytes([flag26, flag27, flag28, flag29])
                flags, enabled = trace.renderer_filter_state(reader, reader.base)
                self.assertEqual(flags, {"0x26": flag26, "0x27": flag27, "0x29": flag29})
                self.assertEqual(enabled, bool(flag26 or (not flag27 and flag29)))

    def test_renderer_removal_and_pointer_change_are_unavailable(self):
        reader = NativeReader()
        reader.memory[reader.base + 0x469A5B0] = bytes(8)
        self.assertEqual(trace.renderer_filter_state(reader, reader.base), (None, None))
        for value in (bytes(8), struct.pack("<Q", reader.renderer + 100), OSError("Renderer removed")):
            with self.subTest(value=value):
                reader = NativeReader()
                reader.change_after_read(reader.base + 0x469A5B0, value)
                self.assertEqual(trace.renderer_filter_state(reader, reader.base), (None, None))
        reader = NativeReader()
        reader.memory[reader.renderer + 0x26] = OSError("Renderer removed")
        self.assertEqual(trace.renderer_filter_state(reader, reader.base), (None, None))


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

    def test_native_eye_position_is_a_nullable_sample_field(self):
        reader = Reader()
        original_read = reader.read
        for position in ((11, 12, 13), (11, float("inf"), 13)):
            with self.subTest(position=position):
                reader.read = lambda address, size: (
                    struct.pack("<fff", *position) if address == 0x62D5AC else original_read(address, size)
                )
                row = trace.sample(reader, 0)
                self.assertEqual(row["native_eye_position"], [11, 12, 13] if position[1] == 12 else None)
                self.assertIsNone(row["player_position"])
                self.assertIsNone(row["native_approach"])
                self.assertIsNone(row["native_controller_record_count"])
                self.assertIsNone(row["native_controller_record_positions"])
                self.assertIsNone(row["renderer_flags"])
                self.assertIsNone(row["native_xy_filter_predicate"])

    def test_signature_mismatch_fails_before_capture(self):
        class Image:
            data = b"\x01" * 100

            def offset(self, rva):
                return 0

        with self.assertRaisesRegex(ValueError, "Live camera reference mismatch"):
            trace.verify_references(Reader(), 0, Image())

    def test_new_camera_reference_mismatch_is_rejected(self):
        class Image:
            data = b"\x01" * 100

            def offset(self, rva):
                return 0

        class CodeReader:
            def read(self, address, size):
                return bytes(size) if address == 0x34C440 else b"\x01" * size

        with self.assertRaisesRegex(ValueError, "RVA 0x34c440"):
            trace.verify_references(CodeReader(), 0, Image())

    def test_duration_limit_is_enforced_without_reads(self):
        for seconds in (0, 31, -1):
            with self.assertRaisesRegex(ValueError, "duration"):
                trace.capture(None, 0, io.StringIO(), seconds, {})

    def test_approach_reference_mismatch_is_rejected(self):
        class Image:
            data = b"\x01" * 100

            def offset(self, rva):
                return 0

        class CodeReader:
            def read(self, address, size):
                return bytes(size) if address == 0xA856E else b"\x01" * size

        with self.assertRaisesRegex(ValueError, "RVA 0xa856e"):
            trace.verify_references(CodeReader(), 0, Image())

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
