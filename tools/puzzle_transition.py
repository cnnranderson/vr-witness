"""Capture puzzle-entry state without injection, game calls, or memory writes."""

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

# References in the fingerprinted executable, outside the existing hook prologues.
REFERENCE_RANGES = (
    (0x2497E7, 7),  # Input mode.
    (0x249745, 16),  # Native and VR headings.
    (0x242396, 67),  # Movement view basis.
    (0x1CCAF0, 18),  # Puzzle cursor object.
    (0x23E563, 22),  # Native VR translation input.
    (0x23E759, 71),  # Filtered/previous translation subtraction.
    (0x23E7C8, 66),  # Delta and previous translation stores.
    (0x249FE7, 20),  # Optional translation filter branch.
    (0x24A175, 28),  # Filter output.
)
TRANSLATION_FIELDS = {
    "vr_rotated_translation": 0x00,
    "vr_filtered_translation": 0x10,
    "vr_previous_translation": 0x20,
    "vr_translation_delta": 0x30,
}
MAX_BYTES = 8 * 1024 * 1024


def verify_references(reader, base: int, pe: PE) -> None:
    """Reject live code changes before sampling; executable identity is checked by snapshot."""
    for rva, size in REFERENCE_RANGES:
        offset = pe.offset(rva)
        if reader.read(base + rva, size) != pe.data[offset : offset + size]:
            raise ValueError(f"Live camera reference mismatch at RVA 0x{rva:x}")


def floats(raw: bytes, offset: int = 0, count: int = 3) -> list[float] | None:
    """Decode finite native floats; null marks unavailable/invalid values, never zero motion."""
    values = struct.unpack_from("<" + "f" * count, raw, offset)
    return list(values) if all(math.isfinite(value) for value in values) else None


def sample(reader, base: int) -> dict:
    """Read native intermediates, not a synchronized rendered camera or a physical HMD pose."""
    mode = struct.unpack("<i", reader.read(base + 0x62D5C4, 4))[0]
    headings = reader.read(base + 0x6303EC, 0x38)
    translations = reader.read(base + 0x630520, 0x3C)
    result = {
        "mode": mode,
        "native_yaw": floats(headings, 0, 1),
        "vr_yaw": floats(headings, 0x34, 1),
        "view_forward": floats(reader.read(base + 0x61FF98, 12)),
        "vr_translation_input": floats(reader.read(base + 0x469A550, 12)),
        "tracking": bool(reader.read(base + 0x469A55C, 1)[0]),
        **{name: floats(translations, offset) for name, offset in TRANSLATION_FIELDS.items()},
    }
    pointer = struct.unpack("<Q", reader.read(base + 0x62D4D0, 8))[0]
    result["projection_origin"] = None
    result["projection_readable"] = False
    if pointer:
        try:
            origin = floats(reader.read(pointer + 0x9C, 12))
            same_pointer = struct.unpack("<Q", reader.read(base + 0x62D4D0, 8))[0] == pointer
            result["projection_readable"] = same_pointer and origin is not None
            if result["projection_readable"]:
                result["projection_origin"] = origin
        except (OSError, ValueError):
            pass  # The cursor object can disappear during a transition.
    result["mode_after"] = struct.unpack("<i", reader.read(base + 0x62D5C4, 4))[0]
    result["mode_stable"] = result["mode_after"] == mode
    return result


def capture(reader, base: int, stream, seconds: int, metadata: dict) -> dict:
    """Record at most 30 seconds/3000 samples/8 MiB; never overwrite an existing capture."""
    if not 1 <= seconds <= 30:
        raise ValueError("Capture duration must be between 1 and 30 seconds")
    written = 0

    def emit(record):
        nonlocal written
        line = json.dumps(record, separators=(",", ":"), allow_nan=False) + "\n"
        written += len(line.encode("utf-8"))
        if written > MAX_BYTES:
            raise ValueError("Capture reached the 8 MiB limit")
        stream.write(line)

    emit({"event": "puzzle_capture.started", **metadata})
    print("Capture running: enter and leave the affected puzzle twice; keep sticks centered.", flush=True)
    start = time.monotonic()
    count = 0
    changes = []
    previous_mode = None
    while count < seconds * 100 and time.monotonic() - start < seconds:
        before = time.monotonic()
        row = sample(reader, base)
        row["elapsed_ms"] = round((before - start) * 1000, 3)
        row["read_ms"] = round((time.monotonic() - before) * 1000, 3)
        if row["mode_stable"] and row["mode"] != previous_mode:
            changes.append({"elapsed_ms": row["elapsed_ms"], "mode": row["mode"]})
            previous_mode = row["mode"]
        emit({"event": "puzzle_capture.sample", **row})
        count += 1
        time.sleep(min(0.01, max(0, seconds - (time.monotonic() - start))))
    report = {"event": "puzzle_capture.finished", "samples": count, "mode_changes": changes}
    emit(report)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument("--game-dir", type=Path, required=True)
    parser.add_argument("--seconds", type=int, default=30)
    args = parser.parse_args()
    if os.name != "nt" or struct.calcsize("P") != 8:
        parser.error("Use 64-bit Python on Windows")
    if not 0 < args.pid <= 0xFFFFFFFF or not 1 <= args.seconds <= 30:
        parser.error("Use a positive 32-bit PID and 1..30 seconds")
    expected = args.game_dir.resolve(strict=True) / "witness64_d3d11.exe"
    output_dir = Path(__file__).resolve().parents[1] / "out/logs"
    if output_dir == expected.parent or expected.parent in output_dir.parents:
        parser.error("Run development tools outside the game installation")
    output_dir.mkdir(parents=True, exist_ok=True)
    output = output_dir / f"puzzle-entry-{datetime.now():%Y%m%d-%H%M%S-%f}-{args.pid}.jsonl"
    reader = ProcessReader(args.pid)
    try:
        state = snapshot(reader, expected)
        base = int(state["image_base"], 16)
        pe = PE(expected)
        verify_references(reader, base, pe)
        metadata = {
            "generated_utc": datetime.now(timezone.utc).isoformat(),
            "sha256": state["sha256"],
            "pid": args.pid,
            "reference_rvas": [hex(rva) for rva, _ in REFERENCE_RANGES],
            "limits": "External, unsynchronized samples. Mode stability does not ensure frame coherence. "
            "VR fields are native intermediates with unverified units, not raw OpenVR poses. "
            "Projection origin can be stale outside puzzles; this is not a verified final eye position.",
        }
        print(f"Read-only capture: {output}", flush=True)
        with output.open("x", encoding="utf-8") as stream:
            report = capture(reader, base, stream, args.seconds, metadata)
        print(json.dumps(report))
    finally:
        reader.close()


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as error:
        raise SystemExit(f"Error: {error}") from error
