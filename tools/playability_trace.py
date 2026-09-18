"""Bounded, read-only samples of legacy VR input and cursor state in one known build.

Field names describe static uses, not a proven fix or a synchronized frame capture.
No game functions, hooks, breakpoints, or memory writes are used.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import struct
import time

from inspect_game import PE
from runtime_state import ProcessReader, snapshot


# Verify pointer, input and cursor references against the hash-checked image.
SITES = ((0x37AA00, 0x28D), (0x37A5F2, 0x67), (0x1C934A, 0x17),
         (0x1CCAF0, 0x12C), (0x1CDA90, 0x29), (0x1CEE99, 0x28),
         (0x1CF7DE, 0x87), (0x1CF3B3, 0x190), (0x1FE730, 0x1F),
         (0x0696B0, 0x12), (0x37A26D, 0x1A), (0x357794, 0x51),
         (0x1FC706, 0x36), (0x1FCA30, 0xB0), (0x1C8A41, 0x18),
         (0x1C8E7A, 9))


def floats(raw: bytes, offset: int, count: int) -> list:
    # Uninitialized/nonfinite fields are evidence to retain, not JSON NaN.
    return [value if math.isfinite(value) else None
            for value in struct.unpack_from("<" + "f" * count, raw, offset)]


def sample(reader: ProcessReader, base: int) -> dict:
    """Read cached state from a verified image; reject invalid bounds or observed pointer turnover."""
    pointer_rvas = (0x469A5B0, 0x62D4D0, 0x469A570)
    pointers = [reader.read(base + rva, 8) for rva in pointer_rvas]
    renderer, cursor, controllers = [struct.unpack("<Q", value)[0] for value in pointers]
    head = reader.read(base + 0x469A540, 0x1D)
    result = dict(
        head=dict(orientation_components=floats(head, 0, 4),
                  position_components=floats(head, 0x10, 3), valid_byte=head[0x1C]),
        menu=dict(fade_by_rva_61e994=floats(reader.read(base + 0x61E994, 4), 0, 1)[0],
                  state_by_rva_630010=struct.unpack("<i", reader.read(base + 0x630010, 4))[0]),
        pointers=dict(renderer=hex(renderer), cursor=hex(cursor), controllers=hex(controllers)),
        cursor_scalars_by_rva=dict(zip((hex(rva) for rva in range(0x62FF14, 0x62FF40, 4)),
                                    floats(reader.read(base + 0x62FF14, 0x2C), 0, 11))),
        cursor_depth_inputs=dict(
            orientation_components=floats(reader.read(base + 0x62D584, 16), 0, 4),
            reference_vector=floats(reader.read(base + 0x61E800, 12), 0, 3),
            subtracted_scalar=floats(reader.read(base + 0x62D1F4, 4), 0, 1)[0]),
        # Resource addresses are identifiers, not synchronized eye/draw snapshots.
        render_targets_by_rva={hex(rva): hex(struct.unpack("<Q", reader.read(base + rva, 8))[0])
                               for rva in (0x469A5D0, 0x469A5D8, 0x469AB58, 0x469AB70,
                                           0x630C78, 0x630BD0, 0x630BD8)},
        bound_target_dimensions=list(struct.unpack("<II", reader.read(base + 0x469A5F0, 8))),
        menu_matrix_components=floats(reader.read(base + 0x630B50, 64), 0, 16),
    )
    if renderer:
        raw = reader.read(renderer, 0x48)
        result["renderer"] = dict(
            width=struct.unpack_from("<I", raw, 8)[0], height=struct.unpack_from("<I", raw, 12)[0],
            flag_bytes={hex(offset): raw[offset] for offset in range(0x23, 0x2C)},
            target_pointers_by_offset={hex(offset): hex(struct.unpack_from("<Q", raw, offset)[0])
                                       for offset in (0x38, 0x40)})
    if cursor:
        raw = reader.read(cursor, 0xCC)
        result["cursor"] = dict(
            screen_coordinates=floats(raw, 0x18, 2), alternate_coordinates=floats(raw, 0x20, 2),
            flag_39=raw[0x39], flag_3a=raw[0x3A],
            projection_scalars=floats(raw, 0x94, 2), ray_origin=floats(raw, 0x9C, 3),
            plane_origin=floats(raw, 0xA8, 3), plane_u=floats(raw, 0xB4, 3), plane_v=floats(raw, 0xC0, 3))
    capacity, count = struct.unpack("<II", reader.read(base + 0x469A578, 8))
    # Two allocated slots can be stale; only slot 0 receives decoded buttons.
    if count > 2 or count > capacity or (count and not controllers):
        raise ValueError("Unexpected controller array; refusing to follow its pointer")
    result["controller_allocated_slots"] = count
    result["controllers"] = []
    if count:
        raw = reader.read(controllers, count * 0x38)
        for index in range(count):
            offset = index * 0x38
            entry = dict(slot=index, orientation_components=floats(raw, offset, 4),
                         position_components=floats(raw, offset + 0x10, 3))
            if index == 0:
                entry.update(startup_model_flag=raw[offset + 0x1C],
                             touchpad_press_edge=raw[offset + 0x1D],
                             touchpad_release_edge=raw[offset + 0x1E],
                             touchpad_pressed=raw[offset + 0x1F],
                             touchpad_touched=raw[offset + 0x20],
                             legacy_axis0=floats(raw, offset + 0x24, 2))
            result["controllers"].append(entry)
    # Reject observed pointer turnover rather than reuse metadata from another object.
    if any(reader.read(base + rva, 8) != before for rva, before in zip(pointer_rvas, pointers)):
        raise ValueError("Object pointer changed during capture; retry when loading has finished")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument("--game-dir", type=Path, default=Path(r"D:\SteamLibrary\steamapps\common\The Witness"))
    parser.add_argument("--seconds", type=float, default=5)
    parser.add_argument("--hz", type=int, default=30)
    parser.add_argument("--label", default="unspecified")
    args = parser.parse_args()
    if os.name != "nt" or struct.calcsize("P") != 8:
        parser.error("Use 64-bit Python on Windows")
    if not 0 < args.pid <= 0xFFFFFFFF or not 0 < args.seconds <= 30 or not 1 <= args.hz <= 120:
        parser.error("Use a positive PID, 0 < seconds <= 30, and 1 <= hz <= 120")
    if len(args.label) > 200:
        parser.error("Keep the capture label under 200 characters")
    output = Path(__file__).resolve().parents[1] / "out/reports" / (
        f"playability-{datetime.now().strftime('%Y%m%d-%H%M%S-%f')}-{args.pid}.jsonl")
    game_dir = args.game_dir.resolve(strict=True)
    if game_dir == output.resolve() or game_dir in output.resolve().parents:
        parser.error("Capture must be outside the game installation")
    reader = ProcessReader(args.pid)
    try:
        baseline = snapshot(reader, game_dir / "witness64_d3d11.exe")
        pe = PE(Path(baseline["executable"]))
        base = int(baseline["image_base"], 16)
        for rva, size in SITES:
            offset = pe.offset(rva)
            if reader.read(base + rva, size) != pe.data[offset:offset + size]:
                raise ValueError(f"Live instruction mismatch at RVA 0x{rva:x}")
        output.parent.mkdir(parents=True, exist_ok=True)
        with output.open("x", encoding="utf-8") as stream:
            def emit(event):
                stream.write(json.dumps(event, allow_nan=False) + "\n")
                stream.flush()
            emit(dict(event="capture.started", label=args.label, baseline=baseline,
                      verified_extra_rvas=[hex(rva) for rva, _ in SITES], hz=args.hz,
                      limits="Unsynchronized cached fields, not function-call tracing. Slots are not connected-device counts. Axes/units are not calibrated. Null floats mean nonfinite values."))
            start = time.monotonic()
            count = 0
            try:
                while time.monotonic() - start < args.seconds:
                    state = sample(reader, base)
                    emit(dict(event="sample", elapsed_seconds=time.monotonic() - start, state=state))
                    count += 1
                    time.sleep(max(0, min(1 / args.hz, args.seconds - (time.monotonic() - start))))
            except (OSError, ValueError, KeyboardInterrupt) as error:
                emit(dict(event="capture.failed", samples=count, error=str(error)))
                raise
            emit(dict(event="capture.finished", samples=count, generated_utc=datetime.now(timezone.utc).isoformat()))
    finally:
        reader.close()
    print(f"Read-only playability capture: {output}")
    print(f"{count} samples. No game memory was written; no functions were called in the game.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as error:
        raise SystemExit(f"Error: {error}") from error
