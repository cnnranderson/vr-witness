# Faster development loop

## What the evidence says

- Native -vr reuse, exact executable/caller checks, owned-process fixtures and short headset checks have produced the working cursor, pause, walking and pointing features. Keep these.
- Cached controller slot 0 is not a hand role. It missed right-stick motion; the role-selected poll then captured -0.99..1 immediately. Prefer role-selected input logs for mapping; use old cached samplers only for their specific render/native-state questions.
- The serial full suite took 124.44 s; about 70 s came from two >30 s lifetime tests. Do not run them for every math/settings edit. Independent fixture processes use unique events/files and can run in parallel.
- Diagnostic output and historical notes consumed excessive conversation space. Start from current-state.md; use rg, bounded source reads, automatic summaries and stored raw evidence.

## Test selection

`build.ps1 -Test -TestSuite Math|Input|Snap|Render|Lifecycle|All -BuildName snap-experiment`

- Math: input transformations/latches; normally under a second after incremental compilation.
- Input/Snap/Render: affected feature and its process checks.
- Lifecycle: long-session survival/control/teardown when these paths change.
- All: once for a finished combined build, new hooks, shared ABI/loader or cross-feature changes. Default four independent jobs. `-TestJobs 1` remains available for diagnosing contention.
- A test-only expectation fix needs only the affected test rerun; a documentation-only edit needs no game/test pass.
- Settings already supported by the loaded binary use session stop/start, no rebuild/restart. Bundle ABI/hook changes for one game restart. Never unload executing callbacks to avoid a restart.

## Controller capture

The new input build can observe an active session without stopping its hooks or reconfiguring controls:

```powershell
.\scripts\controller-test.ps1 -BuildName snap-experiment -Seconds 30
python .\tools\summarize_input.py out/logs/<capture>.jsonl
```

Do not add -Move/-Aim/-Snap/-LegacyAxis0 to a live observation: an unflagged capture observes the session's existing role/axis configuration. Behavior-changing captures still reject overlap. This requires the new build; old DLLs retain their earlier refusal behavior. The observer does not suppress native button side effects or the player's normal active controls.

Every sample already records BOTH hand roles, all five axes/types, full pressed/touched masks, focus/mode/pause state and feature counters. The summary reports validity/device identity, axis ranges, observed bits and timestamped press/release/context changes (`--json`). Raw files remain available. Poll snapshots at ~50 Hz can miss brief edges; hold/release each button deliberately for at least 200 ms.

For a broad 30-second inventory, begin with 2 seconds neutral, then use 2-second labeled intervals for each hand: stick circle, stick click, trigger, A, B, squeeze grip, trackpad touch/click. Keep the other hand's controls untouched. An operator should announce/record the intervals; this is a protocol, not an implemented automatic voice guide. If an interval overlaps two controls or a native menu transition, mark it ambiguous and repeat only that interval. A longer follow-up may be needed; 30 seconds is an inventory, not proof of every gameplay mapping.

Do not infer physical labels from legacy bit names. Confirmed current compatibility profile: right A -> bit2; right B -> bit1; right trigger -> bit33; stick/pad -> shared axis0, with right-stick deflection also setting bit32. Left walking and right axis direction are confirmed. A modern action-binding layer would be needed if the legacy stream cannot distinguish desired controls.

## One headset pass per combined feature

After process tests and a passive native-route check, coordinate one session covering snap direction/hold/neutral, stick cursor plus trigger drawing, A return to pointing, pause blocking and recovery. Use logs for state/counter questions; ask the user for visual alignment, comfort and behavior not visible in telemetry. Preserve the working render session throughout input tests.

## Code and context size

Keep verified native addresses/ABI guards and lifetime tests. They prevented bad assumptions and isolate faults. Avoid a broad hook-framework rewrite during feature work. Keep history in existing experiment documents but read it only by topic; current-state.md is the short handoff. Generated builds/logs stay ignored under out; retaining the last working binary is useful rollback, not production code.

Compaction is tied to context size; Codex documents a token threshold with model defaults. Smaller output and a compact project handoff reduce repeated work, but cannot guarantee zero compactions. No Codex settings were changed. Reference: https://learn.chatgpt.com/docs/config-file/config-reference

## Style and formatting

Use four-space indentation, descriptive names, and one statement per line. Keep comments to the reason, units, ownership or invariants that are not clear from the code. Document public helpers and scripts with their inputs, outputs and failure/lifetime behavior; do not narrate each statement. Use ASCII punctuation in project-owned files and preserve third-party sources/licenses.

C++ uses `.clang-format`; Python uses Black with `pyproject.toml`. The optional formatter versions are pinned in `config/formatters.txt`:

```powershell
python -m pip install --target .tools/formatters -r config/formatters.txt
.\scripts\format.ps1
.\scripts\format.ps1 -Check
```

Formatting touches only `src`, C++ fixtures and Python tools/tests. It skips vendored code, generated output and the game installation. The command restores its temporary Python path afterward. PowerShell commands expose comment-based help; use `Get-Help .\scripts\start-vr.ps1 -Detailed` as an example.

See [the code guide](code-guide.md) for shared protocol and input contracts. Preserve live-byte guards, calling conventions and callback lifetime rules when refactoring.
