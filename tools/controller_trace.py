"""Read-only cached controller and native button-state capture for the verified build.

Does not call game/VR functions, install hooks, change bindings, or generate input.
Works beside the render/pause session because it validates only code it relies on.
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
from playability_trace import floats

# Verify controller/input references outside the cursor and pause detours.
SITES = ((0x37AA00, 0x28D), (0x37A5F2, 0xE5), (0x0696B0, 0x12), (0x364210, 0xA8))
KEYS = {0x135: "legacy_trigger", 0x136: "legacy_pad_click", 0x13D: "legacy_application_menu"}


def sample(reader, base):
    """Read one unsynchronized sample from a verified image; reject observed pointer turnover."""
    checks = []

    def stable(address, size):
        value = reader.read(address, size)
        checks.append((address, value))
        return value

    def pointer(address):
        return struct.unpack("<Q", stable(address, 8))[0]

    controller_header = stable(base + 0x469A570, 16)
    array, capacity, count = struct.unpack("<QII", controller_header)
    if count > 2 or count > capacity or (count and not array):
        raise ValueError("Unexpected controller array; no array read attempted")
    renderer = pointer(base + 0x469A5B0)
    input_root = pointer(base + 0x469A060)
    app = pointer(input_root) if input_root else 0
    keyboard = pointer(app + 0x90) if app else 0
    result = {
        "allocated_slots": count,
        "controllers": [],
        "native_buttons": {},
        "controller_array": hex(array),
        "native_input_object": hex(keyboard),
    }
    if renderer:
        # Native foreground suppression; flags are raw bytes, not device status.
        flags = reader.read(renderer + 0x21, 0xB)
        result["renderer_flags"] = {hex(0x21 + i): value for i, value in enumerate(flags)}
    if count:
        raw = reader.read(array, count * 0x38)
        for i in range(count):
            offset = i * 0x38
            entry = {
                "slot": i,
                "orientation_components": floats(raw, offset, 4),
                "position_components": floats(raw, offset + 0x10, 3),
            }
            if i == 0:
                entry.update(
                    startup_controller_flag=raw[offset + 0x1C],
                    pad_press_edge=raw[offset + 0x1D],
                    pad_release_edge=raw[offset + 0x1E],
                    pad_pressed=raw[offset + 0x1F],
                    pad_touched=raw[offset + 0x20],
                    legacy_axis0=floats(raw, offset + 0x24, 2),
                )
            result["controllers"].append(entry)
    if keyboard:
        first, last = min(KEYS), max(KEYS)
        raw = reader.read(keyboard + 8 + first * 4, (last - first + 1) * 4)
        for key, label in KEYS.items():
            bits = struct.unpack_from("<I", raw, (key - first) * 4)[0]
            result["native_buttons"][label] = {
                "key_code": hex(key),
                "raw_bits": bits,
                "held": bool(bits & 1) and not bool(bits & 4),
            }
    # Reject any observed pointer/allocation turnover, including input chains.
    for address, expected in checks:
        if reader.read(address, len(expected)) != expected:
            raise ValueError("Pointer or allocation metadata changed during sample")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument(
        "--game-dir", type=Path, default=Path(r"D:\SteamLibrary\steamapps\common\The Witness")
    )
    parser.add_argument("--seconds", type=float, default=20)
    parser.add_argument("--hz", type=int, default=60)
    parser.add_argument("--label", default="unspecified")
    args = parser.parse_args()
    if os.name != "nt" or struct.calcsize("P") != 8:
        parser.error("Use 64-bit Python on Windows")
    if (
        not 0 < args.pid <= 0xFFFFFFFF
        or not 0 < args.seconds <= 30
        or not 1 <= args.hz <= 120
        or len(args.label) > 200
    ):
        parser.error("Use a positive PID, 0 < seconds <= 30, 1 <= hz <= 120, and label length <= 200")
    output = (
        Path(__file__).resolve().parents[1]
        / "out/logs"
        / (f"controller-{datetime.now().strftime('%Y%m%d-%H%M%S-%f')}-{args.pid}.jsonl")
    )
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
            if reader.read(base + rva, size) != pe.data[offset : offset + size]:
                raise ValueError(f"Live instruction mismatch at RVA 0x{rva:x}")
        output.parent.mkdir(parents=True, exist_ok=True)
        with output.open("x", encoding="utf-8") as stream:

            def emit(value):
                stream.write(json.dumps(value, allow_nan=False) + "\n")

            emit(
                {
                    "event": "capture.started",
                    "label": args.label,
                    "baseline": baseline,
                    "verified_extra_rvas": [hex(rva) for rva, _ in SITES],
                    "hz": args.hz,
                    "limits": "Unsynchronized cached fields; slots are not connection counts or hand roles. Only slot 0 contains decoded input. Brief button edges may be missed; poses may be stale.",
                }
            )
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
        print(f"Read-only controller capture: {output}")
    finally:
        reader.close()


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        raise SystemExit(f"Error: {error}")
