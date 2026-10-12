# Puzzle-entry positioning investigation

Read-only diagnostics for forward motion when entering some puzzles, alongside
an experimental setting for suppressing the verified native panel approach.

In Settings, turn off **Automatic puzzle positioning** and save to test subsequent
entries without that approach. Turn it on and re-enter to restore native behavior.
The default is on. The option does not undo a completed movement, suppress all
scripted motion, or replace viewpoints required by perspective puzzles. Compare
entry, drawing, canceling, leaving, head movement, and linked-panel navigation
in both settings; restore it if alignment or interaction becomes difficult.

The reported movement depends on player position, viewing direction, and the
puzzle's available viewpoints. The same puzzle can produce different movement
or none, including when approached from farther away. Treat these as runtime
conditions, not a permanent affected/unaffected classification by puzzle ID.
The exact distance and angle gates still need tracing.

## Capture

Use the current working VR build. Stand near an affected puzzle with both sticks
centered and puzzle mode closed, then run:

```powershell
.\scripts\puzzle-transition.ps1 -GameDir 'E:\Games\The Witness'
```

The script announces when recording starts. Keep your head approximately still,
enter and leave the puzzle twice, then wait for the 30-second capture to finish.
Repeat at the same puzzle from a different distance and viewing angle, changing
one condition at a time. Include an entry that visibly repositions the player
and one that does not. Record starting conditions, any movement-stick use between
attempts, and whether visible motion happens on entry or exit; no puzzle
solutions are needed. A different puzzle is useful for broader coverage, not
for deriving a location-specific correction. After those comparisons, repeat
with ordinary controller pointing motion to separate native controller position
contributions from the puzzle's intended approach.

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
- Its helper `249FE0` either copies that input or combines it with the first two
  native controller records, then stores three floats at `630530`.
- The update subtracts previous values at `630540` and writes a derived vector
  at `630550`, before updating the previous values.

REA additionally traced the filter output into native body movement and the
eye-position helper. The capture now includes:

| Field | Native source and interpretation |
| --- | --- |
| `player_position` | Current player ID `630470` resolved through entity table `62D0A0`, position at `+24`. Lookup identity is checked again after reading. |
| `native_approach` | Same player entry, additionally checked against native player type `62E350`. Raw `mode` at `+D0`, `translation_done` at `+D4`, `turn_done` at `+D8`, `target_entity_id` at `+F0`, and `view_origin_selector` at `+F8`. The state block is read twice and discarded on a mismatch. |
| `native_eye_position` | Global `62D5AC`, populated by native camera callers from the eye helper. It can include the mod's height offset. |
| `native_controller_record_count` | Record count at `469A57C`, initialized to two by the OpenVR setup. It is not a count of connected or valid controllers. |
| `native_controller_record_positions` | First two positions at pointer `469A570` plus `10` and `48`; enumeration order, not hand roles. Null when unavailable or inconsistent. |
| `renderer_flags` | Raw renderer bytes `26`, `27`, and `29`; higher-level flag names remain unknown. |
| `native_xy_filter_predicate` | The native predicate `flag26 || (!flag27 && flag29)`. It does not alone establish an active VR eye calculation. |

These are statically identified native intermediates, not a synchronized final
per-eye view or raw OpenVR tracking-space pose. Controller records can be stale;
the native poll does not validate controller poses as it does the HMD. Readable,
finite records therefore do not establish current tracking validity. Their
contribution to the reported puzzle-entry jump still needs runtime comparison.
The shared approach update recomputes a destination and writes the player
position, independently of the controller filter. Approach mode `1` is used by
the traced panel-entry call; mode `0` returns from the common approach update.
Other modes have additional paths. Target IDs and completion fields may persist
outside their active mode. `view_origin_selector` selects an origin for target
angle calculation; it is not a flag to enable movement. A null approach record
does not invalidate a separately readable player position.
See the [REA findings and evidence index](rea-wiring.md) for the data flow and
remaining unknowns.

External reads are not synchronized with rendering. A mode/pointer check
detects some transitions but cannot guarantee a coherent frame. Player, controller
record and renderer pointers are also checked again, without guaranteeing that
their contents stayed unchanged between reads. Missing or
nonfinite vectors are null, never substituted with zero. The projection origin
may be stale outside puzzle mode. Requested sampling is roughly 100 Hz and can
miss brief changes.

## Further correction criteria

Compare repositioning and non-repositioning entries before selecting a hook. Distinguish
the puzzle's intended camera approach, native tracking-origin adjustments,
and physical headset motion. Preserve both eye views and real head rotation
and translation. Entry/exit must not accumulate a position offset.

A transition correction must preserve any viewpoint needed by the puzzle.
Use the shared native destination/approach decision and its current runtime
inputs. Do not build a table of puzzle offsets, infer a correction from one
recorded displacement, or treat all entries as requiring the same transform.
Use a tracked fixed-view screen when that cannot be achieved in world VR, as
specified in [presentation design](design.md). Do not apply a global movement
scale or smooth the final tracked camera as a shortcut.

Check the diagnostic guards with:

```powershell
python -m unittest discover -s tests/tools -p 'test_puzzle_transition.py'
```
