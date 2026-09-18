"""Read-only controller-pose/cursor correlation beside the persistent VR fixes.

No calls into the game and no game memory writes. Guard unmodified field-reference
instructions, rather than weakening either existing sampler's entry-point checks.
"""
from __future__ import annotations
import argparse
from datetime import datetime
import json
import os
from pathlib import Path
import struct
import time
from inspect_game import PE
from runtime_state import ProcessReader, snapshot
from controller_trace import sample as controller_sample
from playability_trace import floats

# Check unchanged function bodies, not detoured entries, after verifying the executable hash.
SITES = ((0x37AA80, 0x20D), (0x37A5F2, 0xE5), (0x364210, 0xA8),
         (0x1C934A, 0x17), (0x1CCAF0, 0x12C), (0x1FC40A, 8),
         (0x24960E, 15), (0x249745, 0x50))


def sample(reader, base):
    """Read one unsynchronized sample from a verified image; reject observed pointer turnover."""
    cursor_header = reader.read(base + 0x62D4D0, 8)
    controller_header = reader.read(base + 0x469A570, 16)
    cursor = struct.unpack("<Q", cursor_header)[0]
    state = controller_sample(reader, base)
    head = reader.read(base + 0x469A540, 0x1D)
    state.update(
        camera_mode=struct.unpack("<i", reader.read(base + 0x62D5C4, 4))[0],
        menu_fade=floats(reader.read(base + 0x61E994, 4), 0, 1)[0],
        head=dict(orientation_components=floats(head, 0, 4),
                  position_components=floats(head, 0x10, 3), valid_byte=head[0x1C]),
        cursor_pointer=hex(cursor),
        camera_angles_by_rva={hex(rva):floats(reader.read(base+rva, 4),0,1)[0]
                              for rva in (0x6303EC, 0x6303F0, 0x630420)})
    if cursor:
        raw = reader.read(cursor, 0xCC)
        state["cursor"] = dict(screen_coordinates=floats(raw,0x18,2),
            alternate_coordinates=floats(raw,0x20,2), flag_39=raw[0x39], flag_3a=raw[0x3A],
            projection_scalars=floats(raw,0x94,2), ray_origin=floats(raw,0x9C,3),
            plane_origin=floats(raw,0xA8,3), plane_u=floats(raw,0xB4,3), plane_v=floats(raw,0xC0,3))
    if reader.read(base+0x62D4D0,8)!=cursor_header or reader.read(base+0x469A570,16)!=controller_header:
        raise ValueError("Cursor/controller metadata changed during aiming sample")
    return state


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument("--game-dir", type=Path, default=Path(r"D:\SteamLibrary\steamapps\common\The Witness"))
    parser.add_argument("--seconds", type=float, default=20)
    parser.add_argument("--hz", type=int, default=60)
    parser.add_argument("--label", default="unspecified")
    args = parser.parse_args()
    if os.name != "nt" or struct.calcsize("P") != 8:
        parser.error("Use 64-bit Python on Windows")
    if not 0 < args.pid <= 0xFFFFFFFF or not 0 < args.seconds <= 30 or not 1 <= args.hz <= 120 or len(args.label) > 200:
        parser.error("Use a positive PID, 0 < seconds <= 30, 1 <= hz <= 120, and label length <= 200")
    output = Path(__file__).resolve().parents[1] / "out/logs" / (
        f"puzzle-aim-{datetime.now().strftime('%Y%m%d-%H%M%S-%f')}-{args.pid}.jsonl")
    game = args.game_dir.resolve(strict=True)
    if game == output.resolve() or game in output.resolve().parents:
        parser.error("Capture must stay outside the game installation")
    reader = ProcessReader(args.pid)
    try:
        baseline = snapshot(reader, game / "witness64_d3d11.exe")
        pe = PE(Path(baseline["executable"]))
        base = int(baseline["image_base"], 16)
        for rva, size in SITES:
            offset = pe.offset(rva)
            if reader.read(base + rva, size) != pe.data[offset:offset + size]:
                raise ValueError(f"Live instruction mismatch at RVA 0x{rva:x}")
        output.parent.mkdir(parents=True, exist_ok=True)
        with output.open("x", encoding="utf-8") as stream:
            def emit(value):
                stream.write(json.dumps(value, allow_nan=False) + "\n")
            emit({"event": "capture.started", "label": args.label, "baseline": baseline,
                  "verified_extra_rvas": [hex(rva) for rva, _ in SITES], "hz": args.hz,
                  "limits": "Unsynchronized cached cursor, pose and input fields. Controller slots are not hand roles or connection counts. This capture does not hook or change input/rendering; it can run with the tested persistent sessions. Brief edges may be missed."})
            start = time.monotonic()
            count = 0
            try:
                while time.monotonic() - start < args.seconds:
                    state = sample(reader, base)
                    emit({"event": "sample", "elapsed_seconds": time.monotonic() - start, "state": state})
                    count += 1
                    time.sleep(max(0, start + count / args.hz - time.monotonic()))
            except Exception as error:
                emit({"event": "capture.failed", "samples": count, "error": str(error)})
                raise
            emit({"event": "capture.finished", "samples": count})
        print(f"Read-only puzzle aiming capture: {output}")
    finally:
        reader.close()


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        raise SystemExit(f"Error: {error}")
