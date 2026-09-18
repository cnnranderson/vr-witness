# Testing plan

## Desktop stage (available now)

No VR headset, controller, or game modifications are required for the diagnostic test. Automated injection coverage uses a hidden process owned by this project. The first manual game attach has also succeeded, as recorded below.

Manual baseline:

1. Launch the 64-bit game normally through Steam; record whether it reaches the menu and loads normally.
2. Run `scripts/probe.ps1` while the game is open. Expected: no visual change, five-second capture, success message, DLL unloaded, game still responsive.
3. Note whether the game was at the menu, exploring, paused, or solving a panel. Keep this note with the log filename; the probe does not infer game state.
4. For later camera experiments, use a separate test save and back up existing saves before behavior-changing builds. No save manipulation is part of the diagnostic probe.

Failure reports should include the command output and log filename, launch arguments, game state, any other injectors/overlays, and what happened on screen. Logs can include local paths; inspect them before sharing publicly.

## VR stage (connected headset available)

- A PC VR headset with SteamVR working. For Steam Frame, establish ordinary PC VR streaming first, independent of this mod.
- Confirm a known-working SteamVR experience tracks both head position and orientation. This provides a baseline for problems unrelated to the mod.
- Prefer a gamepad for the first playable prototype. Motion-controller pointer interaction is a later milestone, not a requirement to begin development.
- Record GPU model, headset/connection, refresh rate, render resolution, executable hash, and runtime version for comparable measurements. No GPU performance claim has been made yet.

Test rotation on all three axes, leaning in each direction, crouching, recentering, world scale, eye separation, and movement while turning. Test a stationary scene before locomotion. Then test menus, pause, alt-tab, loading, tracking loss, and quitting.

For puzzle fallbacks, verify the notification is visible, toggling preserves puzzle progress, the fixed game view remains steady, the headset continues tracking, the mono image has no eye-to-eye solution mismatch, and returning to world VR does not jump the player unexpectedly. Assess comfort on real hardware; desktop or synthetic poses cannot establish it.

## Current validation

Completed on 2026-09-18 UTC (local evening of September 17):

- x64 GCC build with warnings treated as errors.
- CTest smoke test: exact-target refusal, cross-process DLL load/run/unload, valid JSONL inventory, repeated injection into the same process, target survival.
- DLL and loader dependencies: only Windows `KERNEL32.dll` and `msvcrt.dll`; no extra compiler runtime DLLs must be placed beside the game.
- Read-only PE inspection of the installed executable and bundled OpenVR DLL.
- Actual game attach confirmed by user output and full JSONL log: PID 6856, capture `2026-09-18T06:08:42.286Z` through `06:08:47.287Z` (5.001 seconds), 106 modules, `probe.finished` with `success`. The loader reported a complete unload, and a subsequent process check found the same game process responding.
- D3D11, DXGI, and the bundled OpenVR DLL were present. No SteamVR client, Oculus runtime DLL, or OpenXR loader was observed. Steam and NVIDIA overlay modules were present; no overlay conflict was established.
- External read-only snapshot of the same verified executable: stored OpenVR system/compositor pointers were null and the cached init token was zero. The sampler checks the executable hash, loaded image size, and relevant live instruction bytes before reading the globals.

Native VR session, September 17 evening / September 18 UTC:

- Launched through Steam with `-vr`, without changing compatibility settings or shipped DLLs. SteamVR identified a legacy app and created two D3D11 eye textures at 1852 x 2056.
- PID 61512: the injected module inventory saw `vrclient_x64.dll`; the later read-only state report saw non-null system and compositor pointers and init token 1. An earlier snapshot during startup had a system pointer but no compositor yet, illustrating why startup snapshots need context.
- User confirmed world/menu in VR, head rotation and leaning. The user clarified that keyboard/mouse walking works; Knuckles locomotion does not. Mouse activation and drawing work, but the puzzle cursor is visible only on the monitor.
- `playability-20260917-235424-013431-61512.jsonl`: 119 read-only samples while the user explicitly held puzzle mode open. Cursor scalars at RVAs `0x62ff14`, `0x62ff20`, `0x62ff24` remained 1; flags at cursor offsets `0x39/0x3a` were 0/1. These values argue against a simply inactive cursor but do not prove correct rendering in each eye.
- Three read-only decoder tests pass: unexpected array count rejected before dereference, pointer turnover rejected, nonfinite values encoded as null. Allocated controller slots are never reported as connected devices.
- Owned-process render smoke passes trace/defer/trace with independent host counters proving actual reordered calls, normal ordering restored, and clean process exit. First game hook attempt failed before enabling hooks (`MH_ERROR_MEMORY_ALLOC`); the game remained responsive. A memory-region query found no suitable aligned free region within the stock allocator range. The local MinHook allocator was widened with a corresponding RIP-displacement overflow guard; the owned-process tests passed again. The game was closed normally for rebuild.

- Rebuilt trace in actual game PID 30928 succeeded: all 724 recorded submissions immediately preceded their corresponding cursor call. No rendering behavior was changed in this capture.
- **First successful headset cursor repair:** `render-20260918-001049-30928-55fb2801.jsonl`, 30-second deferred mode, 5,366 deferred submissions and 5,366 submissions after cursor return, no fallback events, and no dropped log events. Eight ordinary submissions occurred during transition into/out of the experiment. The user answered "Cursor appeared and aligned correctly" to the test covering in-headset visibility, both-eye alignment, mouse following, and instability. This is a successful first hardware test, not exhaustive coverage. All hooks were disabled at completion and stock behavior returned.
- A separate read-only check after the experiment matched all three live function prologues to the original executable bytes. Both OpenVR interfaces remained initialized and the same game process remained responsive. The native build and both CTest integration tests passed; the three Python sampler guard tests passed. Development files remain in the project, with no mod DLL copied into the installation.

Not yet validated: calibrated world scale/eye separation, startup call history, controller input, gamepad input, Steam Frame streaming, long-session cursor stability, menus/loading transitions with the fix, fixed/mono mode, or comfort/performance. Window responsiveness is not a substitute for playability.

## Cursor session follow-up (2026-09-18)

- Session controls now compile and pass three CTest integration tests. The session fixture runs beyond 30 seconds, exercises disable/enable/stop/restart, rejects duplicate starts and overlapping timed traces, tests an omitted cursor call, checks bounded log output, and exits cleanly. The three Python read guards also pass.
- The first expanded fixture exposed GCC interprocedural register assumptions across patched functions. Compiling the fixture targets separately from their caller fixed the test harness; the full suite then passed. This was found before the new DLL was loaded into The Witness.
- Real game PID 34040, log `cursor-session-20260918-003109-34040-12d892be.jsonl`: 16,150 deferred submissions and 16,150 submissions after cursor return, no fallback faults. The user confirmed the persistent cursor works and **F8 hides and restores it**. CLI stop completed and restored the hooks.
- **Known native pause defect confirmed by the user:** the pause menu is absent in the headset, the view appears doubled, and the displayed world stops following HMD motion. The same problem occurs with all mod hooks stopped; normal tracking returns after resuming. This is not considered a passed menu test. Fixed/mono presentation and a pause repair are not implemented.
- A three-second trace during investigation (`render-20260918-003520-34040-63ef7bda.jsonl`) had no eye/cursor events. The subsequent cached head-pose sample did change. Focus/menu state was not independently established for that capture, so absence of calls alone does not locate the pause defect. The sampler now records menu fade/state and the renderer's draw-suppression byte for the next controlled comparison.
- Follow-up sampling found menu fade 0 and renderer byte `+0x2b` set, which suppresses the world-render call. That explains why this unfocused capture cannot stand in for a focused pause trace. The expanded render diagnostic also hooks the menu draw routine, preserving its stereo/eye/mirror arguments. It passed all three integration tests in the separate `out/menu-investigation` build before deployment.

## Menu-completion experiment (2026-09-18)

- Focused native pause capture in PID 54836 showed both-eye rendering continuing while the menu was fully open, with submissions before cursor/menu passes. Cached head orientations continued changing. This narrows the defect to presentation; it does not establish that delayed submission alone repairs it.
- Added bounded `-DeferMenu` mode; persistent sessions still use the headset-tested cursor completion point. The fixture includes mono, wrong-eye, and mirror decoys before the correct menu pass, plus an omitted-menu event to test fallback and restart. Independent host counters distinguish submissions before cursor, after cursor, and after menu.
- The first alternate build compiled successfully and completed trace, cursor, and menu phases. The menu phase recorded 40 submissions after the correct menu call and no route fault or lost events. Defender then blocked the loader as `Behavior:Win32/DefenseEvasion.A!ml`; the incomplete suite was not counted as a pass. The user elected to review/allow that specific build. No security settings were changed by the project.
- The flagged loader was `out/menu-experiment/bin/witness-probe-loader.exe`, SHA-256 `dd5ea2c063eb24858c3c93c20d43ffde39d57b6acfddb72f4c95d3ffac6140cb`. Full tests were rerun against the same build after execution became available again: all three passed in 39.60 seconds, including the menu fault/recovery checks and the cursor session lasting over 30 seconds.
- Actual-game menu deferral and headset pause/resume validation remain pending. The cursor session was restored after the baseline diagnostic so that normal play retains the tested cursor fix.
- The normal `out/build/bin` loader copy was also blocked by Defender. After the user explicitly allowed that entry, all five tested binaries were promoted unchanged and verified by hash. The game restarted as PID 57148 with non-null OpenVR system/compositor pointers and token 1. Cursor session start succeeded; the menu experiment awaits the user's headset readiness. The project did not alter antivirus settings or the game installation.
- Actual-game 30-second menu experiment completed in PID 57148: `render-20260918-010310-57148-f2a400cf.jsonl` recorded 5,366 deferrals and 5,366 matching-eye submissions after menu return, zero fallback faults, and zero dropped events. The sampler captured four menu openings with draw suppression zero and 328 distinct cached head orientations in 328 fully open menu samples. This verifies execution/order through pause transitions, not visual correctness; headset feedback is pending. Cursor session `cursor-session-20260918-010342-57148-fed13e99.jsonl` was started automatically afterward and reported enabled with no route fault.
- Diagnostic counters are cumulative for the loaded DLL. After mixing a menu experiment and cursor sessions, `deferred` includes menu deferrals while session `submitted` counts cursor-completion submissions only. Their absolute difference alone does not indicate undrained work; use per-capture events and counter deltas. This reporting limitation does not change submission behavior.
- **Headset result: no improvement.** The user reported that waiting through menu drawing did not repair the pause display. Keep cursor completion as the default. The read-only sampler was extended with target addresses/dimensions and menu transform components for the next comparison. All four Python read-guard tests pass, including rejection of target dereferencing and nonfinite matrix JSON; a two-second live decoder check succeeded against the running build without stopping the cursor session.
- Follow-up external capture `playability-20260918-010950-259300-57148.jsonl` collected 3,351 samples across pause/resume cycles without changing the cursor fix. Among samples with draw suppression zero, 1,837 had a fully open menu and 47 distinct menu matrices. Target samples overwhelmingly landed on the monitor target between eye passes. This cannot establish the target used by a specific menu draw.
- Added render-thread snapshots to bounded trace events: current target, both eye-target identifiers, scene-source identifier, fade, and 16 menu matrix components. Only verified image globals are read; pointed-to target/GPU objects are not dereferenced. Persistent sessions skip snapshot collection. The fixture independently updates target identifiers between eyes and includes a nonfinite matrix element to check JSON handling. All three integration tests passed in 38.63 seconds in `out/menu-experiment`; this diagnostic build awaits restart/deployment. Loader bytes are unchanged from the specifically approved build.
- Deployed that diagnostic after the user's normal game exit; the DLL/fixture hashes matched the tested files and the approved loader stayed unchanged. PID 17288 initialized OpenVR. Capture `render-targets-20260918-011755-17288-9cb5c4b2.jsonl` used the established cursor behavior for 30 seconds and completed 5,366 cursor deferrals without faults or trace loss. All recorded stereo menu entry/exit snapshots had the correct eye target, including 3,355 fully paused menu exits with 3,355 distinct menu matrices. This rejects a simple incorrect target at those boundaries or a frozen menu matrix; it does not prove correct menu pixels, projection, depth, or shader state. Persistent cursor session `cursor-session-20260918-011826-17288-100df386.jsonl` was restored automatically with no route fault.

## Read-only state comparison

`scripts/runtime-state.ps1` records the stored OpenVR interface pointers and registered camera setting scalars under `out/reports/`. It does not inject a DLL, call game functions, or write game memory. It currently supports only the recorded executable SHA-256. Run it with 64-bit Python while the game is open; `-TargetPid` and `-GameDir` work as in the probe script.

Keep the default launch for the desktop baseline. A separate `-vr` run belongs to the legacy-startup experiment: the OpenVR initialization path checks headset presence first, so a run without a connected headset may not exercise initialization at all. The snapshot records current values only and cannot recover startup call history or distinguish a never-initialized session from all possible failed/shutdown paths.


## Pause redraw candidate (2026-09-18)

- User clarified that the monitor briefly shows a VR menu, then the normal desktop pause menu, while the headset shows neither and appears to repeat the right-eye view in both eyes. This supersedes interpreting the extra menu as an overlapping in-headset overlay.
- Read-only resource identity capture in PID 17288 (`render-resource-identities-20260918-012652-17288.json`) found separate left-eye, right-eye, and monitor resources. Monitor targets `+0x38` and `+0x40` share a resource, but neither aliases an eye. This is a snapshot, not a GPU pixel capture.
- Added bounded `--redraw-pause` mode, combining the existing menu-completion submission with an exact-caller override of the scene-draw pause check. The session default remains cursor completion. No game files or saves were modified.
- Alternate build `out/menu-experiment` passed all three CTests in 41.56 seconds. Fixture counters recorded 19 paused redraws, 215 preserved paused skips, zero changed results at unrelated callers, and zero incorrectly skipped unpaused draws. Missing-menu tests disable both submission deferral and the pause override, then recover in a new run. All normal modes leave the predicate hook disabled.
- This build has not yet run in the game. Hardware questions: does the world track normally while paused; are the menu and both eyes correct; does Escape resume; does the puzzle cursor work afterward? A positive hook trace alone cannot establish those results.

- Promoted the tested pause-redraw build after the user closed the game, with matching hashes for all five binaries. Restarted as PID 26464; native OpenVR system/compositor pointers are non-null and token is 1. Restored cursor session `cursor-session-20260918-013648-26464-3016a52b.jsonl`. The new loader executed successfully; no new security-setting changes were needed. The timed pause override is not active while awaiting headset readiness.

- **Positive hardware result:** 30-second `pause-redraw-20260918-013749-26464-27b6912d.jsonl` recorded 1,380 overridden render pause checks across four pause intervals, 5,364 deferred/completed submissions, zero fallback faults, and zero dropped events. All 2,778 fully paused stereo menu exits had correct eye targets and distinct matrices. The user reported **tracking and both eyes fixed, menu visible**. Resume and longer-session behavior need explicit follow-up.
- The parallel external sampler deliberately refused the new pause detour because its live-byte signature changed. No samples from that tool are claimed for this run; draw-aligned trace events provide the pause/call-order evidence. Normal cursor-only session was restored immediately after the timed experiment.
- Promoting the successful combination to persistent sessions: menu completion plus caller-scoped pause redraw; F8 controls both, F9 stops both. Session submission totals now include cursor and menu completion, fixing the earlier cumulative reporting discrepancy. New session tests check redraw beyond 30 seconds, unrelated pause results, missing-menu faults, restart, and summed completion counters. This persistent revision is undergoing the required integration suite before deployment.

- The persistent revision passed all three CTests in 41.81 seconds. Work resumed with the game closed; promoted DLL SHA-256 `1b4feb838e34d2aaa4fac2f13840828dadcab46531f997392dd2e84486307dd5` unchanged into `out/build/bin`, verified its hash, and left the installation untouched. The user accepts the pause result for now and requested the next workstream.
- After the user connected the headset and both Knuckles controllers, launched PID 9508 and started the combined session (`cursor-session-20260918-095810-9508-a2c93250.jsonl`). Native OpenVR system/compositor pointers are non-null with token 1. No extra VR session was initialized.
- Added `tools/controller_trace.py`, a bounded read-only capture using its own controller/input instruction windows. It runs beside the pause hook without relaxing the older sampler's checks or stopping the fixes. It records cached poses, slot-0 legacy axes/pad state, and native button states emitted by the game's OpenVR decoder. Four tests pass for bad counts/null arrays, allocation/input pointer turnover, button-bit interpretation, nonfinite data, and conservative slot labels. A two-second actual-game decoder check succeeded; per-hand input captures are next.

- Per-hand controller capture completed with persistent fixes active: `controller-20260918-095946-298150-9508.jsonl` (left) and `controller-20260918-100103-723217-9508.jsonl` (right), each 1,800 samples. Only the right capture changed slot-0 axes and native buttons; both axis components spanned about -1 to +1. Left motion updated slot 1, whose input the native routine ignores. This establishes live right-hand input delivery in this launch, not movement/puzzle compatibility. See `docs/controller-plan.md` and ignored `controller-baseline-summary.json`.

- User observation after the right-hand capture: some buttons worked, but no movement or turning. Prioritize locomotion/turning while preserving working native button actions.

- Persistent-session user follow-up: right B opens pause, right trigger enters puzzle-active view, and right trackpad click/stick movement/stick click cancel puzzle view. Left controls do nothing; no locomotion/turning observed. The user retracted a suspected pause regression and confirmed the pause fix works. Preserve B/trigger behavior and investigate the stick cancellation conflict before mapping turning. No additional pause comparison was run.


## 2026-09-18: controller bridge preparation

A separate pinned input DLL now supports role-based passive reads and bounded left-stick movement. The five-test suite passes (56.95 s): prior injection/render/session regressions, input math, and controller cross-process lifecycle/gating checks. The final input fixture reported {"moved": 77, "right": 24, "neutral": 924, "leaks": 0, "decoy_leaks": 0, "polls": 1001}. A negative actual-game check also confirmed that the loader refuses a second input build from another path.

The five-second passive game capture logged 440 polls, 443 movement calls and zero applied changes. Both hands expose the legacy axis-type layout [1,3,3,-1,-1], requiring the explicit LegacyAxis0 option for movement; this may also move from the left trackpad. Cursor/pause remained running and fault-free. Headset walking and mode transitions have not yet been validated. The old passive DLL is pinned in PID 9508; requested normal game exit before loading the tested compatibility build in out/controller-experiment. No game installation files were changed.


## 2026-09-18: first controller walking headset test

The explicit legacy-axis movement experiment ran for 30 seconds in PID 29956: 2,675 polls, 2,678 movement callbacks, 500 applied vectors, and clean hook teardown. The user confirmed correct movement. Both controller roles were valid throughout the sampled interval. Cursor/pause remained enabled with zero fallback. All sampled camera modes were 2 and all pause-fade values zero, so a dedicated pause/blocking/resume check is pending. See ignored report out/reports/controller-first-movement-test.json and capture out/logs/input-20260918-104718-29956-8c3a7e75.jsonl.


## 2026-09-18: controller pause test confirmed

The follow-up 30-second headset test covered five actual pause intervals, with stick deflection but zero movement output throughout each. Walking resumed afterward. Totals: 2,673 polls, 1,709 movement calls, 947 applied vectors. The user confirmed pause blocking/resume and correct stereo/menu/tracking. The persistent cursor/pause session remained enabled and fault-free. The user noted missing controller menu navigation and agreed to defer it; mouse navigation is still required. See out/reports/controller-pause-movement-test.json for the ignored diagnostic summary.


## 2026-09-18: persistent controller build checks

The out/input-session build passed all six CTests in 90.81 seconds. The new cross-process session check lasts beyond 30 seconds, verifies status/start/disable/enable/stop and restart, rejects overlapping captures/starts, checks that held input cannot rearm without neutral, verifies stable counters after hook teardown, and checks bounded heartbeat logs. Independent fixture metrics: {"moved": 485, "right": 150, "neutral": 1621, "leaks": 0, "decoy_leaks": 0, "polls": 2106}. The game is still running its older pinned bounded input DLL; normal exit was requested before loading this persistent build. F7 and persistent-session headset behavior remain manual follow-up checks.


## 2026-09-18: persistent launch and puzzle pointing baseline

Launched a fresh native VR game and started both persistent sessions from their tested project paths. The input session exceeded 30 seconds, reported 37,704 polls, 37,079 movement calls and 7,815 applied vectors; render reported 74,808 matching deferred/submitted eyes and zero fallback. Both were enabled and fault-free. F7 remains an explicit hardware follow-up.

The new read-only puzzle-aim sampler passed four defensive-read tests (12 Python trace tests total), a two-second live sanity check and two 20-second captures beside the active sessions. Each longer capture collected 1,200 samples with camera mode 0, fade 0, no native button activity and no legacy stick axis input. The user reported no hand-driven cursor movement in the repeated test, then independently confirmed that trigger clicking in puzzle mode already behaves correctly. Raw cursor coordinates varied while the head moved in the repeat; this is not evidence of working hand pointing. The ignored `out/reports/puzzle-pointing-baseline.json` records the captures, numeric ranges, static input candidates and limitations. No new game-input hook or cursor write was enabled.


## 2026-09-18: bounded pointing build

The isolated `out/pointing-experiment` build passed all six CTests in 100.35 seconds. The cursor geometry checks cover center/right aiming, common head/hand rotation, invalid and reflected matrices, degenerate projection, targets behind the head, bounded deltas, and trigger-release/device rearming. The input cross-process sequence exercises passive/movement/passive-aim/aim/passive-aim/normal capture, observes the historical compositor pose ABI, and independently checks caller isolation and transition gates. Fixture totals: 78 changed cursor outputs, zero aiming leaks, zero unrelated-caller changes. Movement regression also had zero leaks. All executable byte guards independently match the installed binary on disk.

This is compiled/owned-process evidence. The first actual-game pointing test and headset alignment/drawing checks remain pending. This build implements eye-origin angular pointing; positional hand-ray parallax is not implemented.


The new DLL then completed a three-second passive trace in the actual game: 257 polls with valid head/right-hand poses, 230 cursor hook calls, and zero applied cursor changes. Hooks disabled cleanly. The compositor method and cursor entry therefore execute compatibly in the supported game, beyond static/fixture evidence. Cursor/pause remained enabled with zero fallback, and persistent walking was restored using the same new input DLL. Headset pointing/drawing validation is the next check.


## 2026-09-18: pointing and drawing confirmed in headset

`input-20260918-113603-59488-4bb18162.jsonl` records a 30-second pointing test: 2,458 polls/valid pose samples, 2,437 cursor calls, 2,131 applied cursor updates, no emergency exit, and clean hook teardown. The user confirmed pointing and trigger drawing worked, then reported that aiming was very sensitive. Persistent walking was restored from the same loaded `pointing-experiment` build; cursor/pause stayed enabled. This validates the native cursor-input connection, not extended playability, positional hand-ray parallax, or the subsequent smoothing/recenter revision.


## 2026-09-18: smoothing/recenter persistent build passed

The final `out/pointing-session` revision passed all six CTests in 103.78 seconds. Math checks compare equal elapsed-time smoothing at 50 and 100 Hz, enforce the configured speed cap, reject invalid settings/stale updates, verify quaternion aim recentering, and reject held-on-entry/drawing-time A presses. The cross-process persistent session runs pointing beyond 30 seconds with non-default speed/smoothing settings, checks disable/enable and held-trigger rearming, exercises A press/hold/drawing rejection, then restarts walking-only without accidentally enabling pointing. Independent session metrics: 933 changed cursor outputs, 47 movement outputs, zero mode/caller leaks for either. Timed input regression recorded 84 cursor changes and 78 movement outputs, also zero leaks. Existing module, render and cursor-session tests pass. Changed PowerShell scripts parse successfully.

This build has not been loaded into the game. The physical A-button capture and headset assessment of speed, smoothing, recentering and transitions are pending. PID 59488 still runs the earlier `pointing-experiment` walking-only session plus the normal cursor/pause session; both report enabled and fault-free. Render counters match at 58,932, with zero fallback. Loading the new DLL requires normal game exit; development/install separation is unchanged.


## 2026-09-18: right-A capture and compatibility correction

A ten-second actual-game passive capture recorded 885 controller polls and 888 movement callbacks, with zero applied input. Its 479 initialized samples contained eight right press/release cycles on bit 2, no other pressed bits, and unchanged puzzle mode 0/menu fade 0. The prior prepared recenter code expected standard A bit 7; the compatibility path was corrected before deployment. The left role was unavailable during this capture. Both existing sessions resumed without fault; render submission counts matched with zero fallback. Corrected-build tests and headset deployment follow this entry.


The corrected compatibility build passed all six CTests in 104.73 seconds. The persistent fixture exercises both legacy bit 2 and standard bit 7 for recentering, as well as held-button and drawing rejection. Session fixture metrics: {"moved": 49, "right": 12, "neutral": 2333, "leaks": 0, "decoy_leaks": 0, "aim_changed": 968, "aim_leaks": 0, "aim_decoy_leaks": 0, "polls": 2382}. A normal game exit was requested before loading this build; new headset validation remains pending.


## 2026-09-18: corrected pointing session loaded

After the user closed the previous game, launched native `-vr` as PID 15172. The exact tested `pointing-session` input DLL hash was verified before loading; the loader hash also matches the passing build. A read-only snapshot found initialized native OpenVR system/compositor pointers and init token 1. The normal cursor/pause session and the new persistent input session both started successfully and reported enabled without faults. Input settings are legacy-axis compatibility, pointing enabled, speed 80 percent of view width/second and smoothing 80 ms. Initial counters show the poll/cursor hooks executing; initial render deferred/submitted counts match with zero fallback. These are startup checks, not headset confirmation of the revised feel or recenter behavior. User feedback was requested for smoothing, A before drawing, and trigger drawing. No development files were installed into the game directory.
