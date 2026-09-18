# Current working state

Updated 2026-09-18. Read this before historical experiment notes.

- Repository: `C:/Users/samic/Documents/GitHub/vr-witness`; keep development/builds/logs here. Game install stays clean.
- Supported EXE SHA-256: `8d672d444df6a6df7130f25a517bdab1af92731fe2138dc5b324d519561d55a5`.
- Running game PID46688, launcher11284, SteamVR29512. Native VR startup eventually completed; system and compositor pointers valid. Input build `input-settings` and render build `build` now loaded and enabled. Verify process/status before acting.
- Input log: `out/logs/input-session-20260918-131832-46688-c8004b86.jsonl`; render log: `out/logs/cursor-session-20260918-131822-46688-2083dcae.jsonl`.
- Actual-game check: input1138 polls,966 movement applications,5 snaps, no fault; render4136 deferred/submitted,0 fallback/no fault. Active speeds stick40/motion80, smoothing80ms, snap45.
- User-confirmed before this update: native stereo/6DOF, cursor/pause repair, walking/pause gating, pointing/trigger drawing, smoothing/A recenter, snap and manual/motion cursor switching.
- New slower stick speed and left-B puzzle back await headset feedback. All8 automated checks passed in38.10s; no binary changes since. Evidence/hashes: `out/reports/input-settings-status.json`.

## Controls and settings

- `config/input.ini`: independent StickCursorSpeedPercent=40 and MotionCursorSpeedPercent=80; each10..300. Worker reloads once/sec; invalid/missing field retains last value. Reload verified in fixture, not separately exercised in actual game yet. Settings edits need no build/restart. Motion smoothing remains session option.
- Right stick: snap only in walking mode2; manual cursor in puzzle modes0/1. Center between turns and after transitions. Right A (trigger released) recenters and returns to pointing. Right trigger clicks/draws natively; right B pauses. Left B sends native puzzle back (may cancel the drawn line first, then leave puzzle on another press).
- F7 toggles configured input; F8 toggles render repair; F9 stops both sessions. Center sticks and release buttons after enabling/resuming.
- Left B mapping confirmed in `input-20260918-125258-44936-3f0ddea0.jsonl`: leftdevice4 masks0/2,14 edges; rightdevice3 no presses. Native back setter RVA364210/key0x136, captured keyboard object at native pad caller return37AC55. Fresh puzzle/focus/tracking-gated press only; original next poll resumes ownership. No new native addresses.
- Role-selected input is essential: cached slot0 missed right-stick motion. Current legacy layout [1,3,3,-1,-1] uses axis0; right A bit2, B bit1, trigger bit33, stick deflection also presses bit32. Do not guess physical labels from standard names. Puzzle stick/pad native cancel is selectively suppressed to allow manual cursor.

## Launcher

`Start Witness VR.bat` now calls `scripts/start-vr.ps1`: starts SteamVR if needed, launches/reuses the game, waits for verified native VR pointers, then starts/enables both current mod sessions. Healthy sessions are preserved; faults/different input builds are refused. Transcript in out/logs; no install-folder changes. Script syntax/Windows PowerShell 5.1 Python preflight, missing-install handling, and deterministic readiness/timeout checks passed; full launcher launch/injection path has not yet been tried. Existing individual session scripts were validated previously. Game PID46688 was no longer running at the last process check; recheck before using prior status.

## Commands

```powershell
.\scripts\controller-session.ps1 status -BuildName input-settings
.\scripts\cursor-session.ps1 status
# After a later normal exit and VR relaunch, restore both:
.\scripts\cursor-session.ps1 start
.\scripts\controller-session.ps1 start -BuildName input-settings -LegacyAxis0 -Aim -Snap
# Passive capture only if needed; leaves current controls running:
.\scripts\controller-test.ps1 -BuildName input-settings -Seconds 30
python tools/summarize_input.py out/logs/<capture>.jsonl
```

Do not rebuild the loaded input-settings or build DLLs while PID46688 runs. For source changes use a new output directory or normal exit. Select focused test groups during iteration; All once for completed cross-feature/ABI/lifetime changes. No need to repeat the passed suite for unchanged binaries or config/doc edits.

Next: let user test slower cursor and left B at their own pace. No further readiness capture required.
Deferred: controller menu navigation; hand-position parallax; fixed/mono puzzle fallback. Do not claim these work. Camera/fallback constraints: `docs/design.md`; capture/testing workflow: `docs/development-workflow.md`.
