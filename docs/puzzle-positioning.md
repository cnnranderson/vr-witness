# Puzzle-entry positioning investigation

Read-only diagnostics for excessive forward motion when entering some puzzles.
No puzzle-entry camera correction is implemented.

## Capture

Use the current working VR build. Stand near an affected puzzle with both sticks
centered and puzzle mode closed, then run:

```powershell
.\scripts\puzzle-transition.ps1 -GameDir 'E:\Games\The Witness'
```

The script announces when recording starts. Keep your head approximately still,
enter and leave the puzzle twice, then wait for the 30-second capture to finish.
Repeat once at an unaffected puzzle for comparison. Record the locations and
whether the visible jump happens on entry or exit; no puzzle solutions are needed.

This external reader verifies the supported executable and live instruction
references. It does not inject, call game functions, write game memory, or
restart existing hooks. It records at most 3000 samples/8 MiB under `out/logs`.
Python 3.10+ x64 is required. A game exit/read failure ends the capture and leaves
the partial log available.

## Evidence and limitations

Addresses use the executable fingerprint in [native integration](native-integration.md).

- Existing input mode: RVA `62D5C4`; native/VR yaw: `6303EC` / `630420`.
- Existing movement view-forward basis: `61FF98`.
- Existing cursor object: pointer `62D4D0`, projection origin at offset `9C`.
- The native VR update `23E540` copies the three floats at `469A550`,
  rotates them using the VR heading, and stores the result at `630520`.
- Its helper `249FE0` either copies that input or combines it with two
  additional records, then stores three floats at `630530`.
- The update subtracts previous values at `630540` and writes a derived vector
  at `630550`, before updating the previous values.

These are statically identified native intermediates, not a confirmed final
eye position or raw OpenVR tracking-space pose. Their coordinate conventions
and units still need runtime validation. The additional records' meaning and
connection to puzzle entry are unproven.

External reads are not synchronized with rendering. A mode/pointer check
detects some transitions but cannot guarantee a coherent frame. Missing or
nonfinite vectors are null, never substituted with zero. The projection origin
may be stale outside puzzle mode. Requested sampling is roughly 100 Hz and can
miss brief changes.

## Correction criteria

Compare affected and unaffected entries before selecting a hook. Distinguish
the puzzle's intended camera approach, native tracking-origin adjustments,
and physical headset motion. Preserve both eye views and real head rotation
and translation. Entry/exit must not accumulate a position offset.

A transition correction must preserve any viewpoint needed by the puzzle.
Use a tracked fixed-view screen when that cannot be achieved in world VR, as
specified in [presentation design](design.md). Do not apply a global movement
scale or smooth the final tracked camera as a shortcut.

Check the diagnostic guards with:

```powershell
python -m unittest discover -s tests/tools -p 'test_puzzle_transition.py'
```
