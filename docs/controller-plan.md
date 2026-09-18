# Knuckles input work

Historical investigation notes; see [current state](current-state.md) for the latest working build and validation.

## Established baseline

The persistent cursor/pause build is promoted and enabled in the current game session. Its combined persistent behavior passed all three integration tests; the user accepted the earlier positive pause headset test and deferred extended pause validation.

On 2026-09-18, two read-only, 20-second captures (1,800 samples per hand) in PID 9508 found:

- Moving/using only the left controller changed slot 1's pose. Slot 0 axes stayed zero and native trigger/pad button values did not change.
- Moving/using only the right controller changed slot 0's pose. Both legacy axis-0 components reached about -1 to +1, and native trigger/pad button values changed.
- Renderer draw suppression stayed zero throughout both captures. The cursor/pause session remained enabled without submission faults.

The per-hand experiment identifies the slot order in this launch only. The native poll routine at RVA `0x37AA00` enumerates device indices and decodes buttons/axes only for slot 0. It does not choose controls using hand roles. Cached slots are neither connection counts nor proof of fresh tracking. Do not infer all input mapping defects from these snapshots.

The ignored report `out/reports/controller-baseline-summary.json` retains counts and source log names. The user confirmed that some right-controller buttons visibly worked, but movement and turning did not. No controller bindings or input behavior were changed.

## User-verified button behavior

With the persistent fixes running:

| Right-hand input | Observed behavior |
| --- | --- |
| B | Opens pause |
| Trigger | Enters puzzle-active view and clicks again within it (user confirmed); preserve this native behavior |
| Trackpad click | Cancels puzzle-active view |
| Thumbstick movement | Cancels puzzle-active view; does not move or turn |
| Thumbstick click | Cancels puzzle-active view |
| Left-hand controls | No visible response |

The user briefly suspected that pause had regressed, then explicitly confirmed it works. The session log also remained enabled without route faults. No additional pause comparison was performed or is required for that retracted report.

Preserve right B and trigger behavior. Before assigning right-stick turning, identify and gate the native puzzle-cancel path so turning cannot accidentally dismiss a puzzle. The mixed stick/trackpad capture does not identify the exact cancellation mechanism or prove which physical input supplies each legacy axis/button bit.

## Next implementation sequence

1. Read both hands from the game's existing historical OpenVR interface, selecting roles explicitly. Match `IVRSystem_012` and its controller-state ABI; do not initialize another scene application or replace the shipped OpenVR DLL. A role change, disconnection, or failed poll must release actions owned by the mod.
2. Bridge left-stick movement into the game's existing movement input. Verify the receiving action/key codes before sending anything; the unused cached axis alone is insufficient. Start with a configurable dead zone and neutral/release behavior. Preserve keyboard/mouse use and avoid duplicate ownership of native trigger inputs.
3. Add right-stick turning with a configurable snap angle, edge/rearm threshold, and one turn per deflection. Keep turning out of pause and puzzle modes until their semantics are validated.
4. Reuse native trigger/puzzle actions where they work; add tracked right-hand pointing only after the ray and activation path are verified. Preserve the already tested cursor presentation.
5. Verify pause/menu controls, controller reconnection, left/right role changes, focus loss, stopped session, and held-button release. Then return to fixed-view/mono puzzle modes with tracked presentation and visible mode controls.

Initial intended layout: left stick moves, right stick turns, right trigger interacts/draws, a non-system face button pauses/backs out. This is a proposed mapping, not implemented behavior.

## Diagnostic

`python tools/controller_trace.py --pid <PID> --seconds 20 --label left_controller_only` reads poses, slot-0 axis/pad data, and the native button bits written by the OpenVR decoder. It validates the exact executable and relevant live instructions and rejects pointer/allocation turnover. It can run with the persistent pause fix enabled because it does not depend on the detoured pause predicate. Four defensive-read tests live in `tests/test_controller_trace.py`.

## Bounded left-stick implementation (2026-09-18)

The new `witness-input-probe.dll` is independent of the persistent render DLL. The loader's `--controller-trace` records both hands from the existing IVRSystem_012; `--controller-move` additionally enables the experimental left stick for at most 30 seconds. `scripts/controller-test.ps1` defaults to passive capture and the `controller-experiment` build; use `-Move` explicitly for walking. Loading this DLL does not stop the cursor/pause session. Close the game before rebuilding the loaded input DLL, which remains pinned after hooks stop.

The native VR poll at RVA `0x37AA00` now has an optional after-call observer. It resolves left/right roles every poll, checks connection and state-call success, reads all five axis-type properties, and normally selects exactly one axis explicitly typed as a joystick. The compatibility option described below accepts only the exact legacy axis layout observed in the game. Historical Windows controller state is 64 bytes with axes at offset 24, and the state method takes no size argument. Slot indices are 17 (role), 20 (connected), 23 (integer property), 32 (state), and 39 (input focus); pointer-returning virtual methods must be counted when deriving the table.

The movement hook at RVA `0x242230` calls the original and changes its vector only for the known caller returning to `0x24A50E`. It follows the game's forward/left basis at `0x61FF98`/`0x61FFA8`; the supported build's live key bindings were W/S/A/D (119/115/97/100), confirming that positive native strafe means left. Stick-right therefore subtracts that basis. A radial 0.20 dead zone scales analog movement, and combined input is capped at normal movement magnitude (or preserves a larger existing native magnitude). No keyboard events or native button state are written.

This first experiment gates movement to camera mode 2, zero menu fade, focused game, stored HMD-valid flag and connected HMD, no draw suppression and no other application capturing VR input. It requires a neutral stick after focus, connection, role/axis change, invalid values, or a sampling interruption, and rejects stale or cross-thread samples. Mode 2 was subsequently observed throughout the successful normal-walking headset test. Other camera modes and transitions still require separate validation. F9 ends a test early; the existing render session also responds to F9.

Right-stick turning is not enabled. Its native puzzle-cancel behavior remains unresolved; the raw five-axis capture is intended to identify whether SteamVR exposes a separate joystick before selecting an appropriate interception point. Right B, trigger, trackpad, keyboard and mouse retain their native behavior. Controller drawing/aiming and fixed/mono puzzle modes remain future work.

### Actual-game passive capture and compatibility option

Five seconds in PID 9508 produced 440 native poll callbacks and 443 movement callbacks; movement was disabled and zero changes were applied. Both role queries and state reads succeeded. Left was device 4 and right device 3 in that capture, independently of the cached ordinal slots. SteamVR exposed `[trackpad, trigger, trigger, unavailable, unavailable]` for both hands. Left axis 0 reached about +/-1 in both directions; there was no axis typed as a joystick. All valid samples reported camera mode 2. This passive capture proved interface compatibility and available input. The later movement test below separately confirmed headset walking.

`-LegacyAxis0` / `--controller-legacy-axis0` explicitly accepts that exact layout and chooses its shared axis 0 when no joystick exists. This can make the left trackpad move the player too; separate physical stick/pad input still requires different SteamVR bindings or a newer action-input path. Without this option, the observed legacy layout remains neutral. The option is encoded separately from movement enable, so passive capture still never applies movement. Tests exercise both rejection without opt-in and movement with opt-in.

The passive game's cursor/pause session remained running, enabled and fault-free afterward (75,624 deferred/submitted, zero fallback). The initial input DLL remains pinned at `out/menu-experiment/bin` until game exit. The revised compatibility build is isolated at `out/controller-experiment/bin`; restart before loading it. The loader refuses to load a second input DLL from another path in the same process.

Next hardware test after restart, with the game focused and both sticks initially centered:

```powershell
.\scripts\controller-test.ps1 -Move -LegacyAxis0 -Seconds 30
```

Test left-stick forward/back/strafe, return to neutral, then briefly pause and resume. Confirm direction, stopping, no movement while paused, and neutral rearm. Right-hand controls are not changed in this build. Keep the cursor/pause session started through its existing script.

### First headset movement result

After restarting into PID 29956 and restoring the cursor/pause session, a 30-second `-Move -LegacyAxis0` test logged 2,675 polls, 2,678 movement calls and 500 applied movement vectors. Both hands remained valid; all 1,447 sampled states were mode 2 with zero pause fade. The user responded, "Yes! it moved properly, completely" to the direction/release/pause question. This confirms the left-stick walking result; the log did not observe a pause interval, so pause-specific blocking/resume validation remains pending rather than inferred from that broad response. A focused pause check was requested before persistent input.

The input hooks disabled cleanly at the deadline, and the cursor/pause session remained enabled without faults or fallbacks (36,054 deferred/submitted). This controller feature is still bounded; walking is off between tests. Right-stick turning is not implemented.

### Headset pause/blocking confirmation and persistent input

A second 30-second capture in PID 29956 logged five pause intervals (fade > 0). Movement output stayed zero throughout those intervals despite left-stick input, and resumed afterward. The total was 2,673 polls, 1,709 movement callbacks and 947 applied vectors. The user confirmed that pause blocking, resumed walking, the menu, head tracking and eye views all worked. Controller navigation within the menu is missing; the user explicitly deferred that issue, and mouse navigation remains the current path.

The new persistent controller session uses a separate `InputSessionRequest`/`WitnessInputSessionControl` protocol so the working render session layout is unchanged. `scripts/controller-session.ps1 start -LegacyAxis0` starts the current compatibility layout; `status`, `disable`, `enable` and `stop` control it independently. Its default build is `out/input-session`, which must be loaded only after the earlier pinned input DLL exits with the game. The loader enforces that separation. Right-stick turning, pointer/drawing controls and menu navigation remain unimplemented.

F7 toggles movement while the game is focused; held hotkeys are disarmed across focus changes. F9 stops input and also stops any active cursor/pause session. F8 remains the render fix toggle. CLI enable and hotkey enable both reset the movement latch so a held stick cannot move until neutral is observed. Session logs contain state changes and 30-second counter heartbeats rather than every-frame axis samples. A worker closes the log, disables hooks, drains callbacks and leaves DLL/trampolines pinned when stopped. Timed diagnostics cannot overlap an input session. Persistent-session hardware validation is separate from the completed bounded tests.


## Pointing baseline and narrowed scope (2026-09-18)

The persistent input build is now loaded in a fresh game process together with the persistent cursor/pause repair. It has run beyond the former 30-second limit and applied movement. Both sessions reported enabled without faults, and rendering had zero fallback. This is actual-game execution evidence; the completed walking/pause headset checks used the bounded mode, and F7 still needs a hardware check.

The user prefers investigating right-controller pointing for puzzle cursor movement. They confirmed that the right trigger already clicks again inside puzzle mode. Preserve that native action and right B; the immediate missing feature is the connection between hand aiming and cursor movement. Right-stick snap turning remains planned for walking, with turning blocked in puzzles and pause. Stick-controlled cursor movement can be an optional fallback. Controller menu navigation remains deferred.

Two 20-second read-only pointing captures each collected 1,200 samples beside the active fixes. Camera mode was 0 and menu fade 0 throughout; native button bits and legacy axis 0 were zero. The right-hand-only instruction produced movement in cached slot 0 while slot 1 stayed nearly still, identifying this launch's order only. The user repeated the check and reported that the cursor stayed still. In the repeat, head orientation also changed considerably and raw screen cursor coordinates changed, so the raw coordinate ranges must not be described as a stationary world-space cursor. User feedback establishes the absent hand-driven behavior; the trace establishes that hand poses reach the game. Trigger clicking is separately user-confirmed and was intentionally not exercised in those captures.

`tools/puzzle_aim_trace.py` checks the exact EXE hash and unmodified instruction bodies, then samples cached controller/head poses, cursor projection fields, native button bits, camera mode and menu fade. This permits a read-only capture while the input poll entry is detoured. Existing samplers retain their original stricter entry checks. Four tests cover pointer turnover, null cursor, invalid floats and inherited controller-array guards. A 120-sample live sanity check and both longer captures completed successfully. These snapshots are unsynchronized and cannot by themselves validate ray alignment or pose freshness.

Static investigation identified a narrower input candidate: RVA `0x1D0E10` returns a two-float cursor movement value through its first pointer argument, with a boolean second argument. The direct call at `0x1D46D8` (return `0x1D46DD`) adds/clamps that delta before native puzzle-path processing. Its ABI, timing and reachability must be verified before changing results. Supplying movement through this route could retain the game's path constraints; moving only the rendered cursor would not establish drawing support.

RVA `0x1CCAF0` constructs the cursor ray from its projection plane, and `0x1CC5B0` projects a world point into those cursor coordinates. The controller quaternion ray at `0x24D909` belongs to teleport targeting (`0x24D670`), not an existing puzzle pointer. A true hand-origin ray still needs a verified tracking-to-world transform and puzzle target depth; projecting onto the near-camera plane alone would give wrong parallax. Do not redirect the tracked headset pose or enable the teleport path to simulate puzzle pointing.

The next bounded prototype should apply only right-hand aiming through verified puzzle input, keep trigger handling native, and become inactive on pause/focus/tracking loss or stale input. Verify cursor alignment in both eyes, traced lines, entry/exit, and hand/head movement independently before enabling persistently. No pointing or right-turning hook is implemented yet. The native VR adjustment at `0x23E540` quantizes heading to 22.5 degrees; account for that when implementing snap turns rather than assuming an arbitrary angle works.


## Bounded angular-pointing implementation

`src/puzzle_aim.hpp` and the input DLL now implement a first angular pointer behind `--controller-aim`. The separate `--controller-aim-trace` observes the same pose/cursor route without replacing input. Both require a timed controller capture, capped at 30 seconds; persistent walking still defaults to no pointing. Build into `out/pointing-experiment` to keep the known loaded movement build intact.

The poll observer obtains the same-frame HMD and role-selected right-hand poses with `IVRCompositor_013::GetLastPoses` (slot 3). It does not wait for another frame, initialize VR, replace OpenVR, or change camera/headset poses. The historical 80-byte pose layout and method owner are validated. Both matrices must be finite, orthonormal, connected and normally tracked. The controller's local -Z direction is expressed in head-local coordinates and mapped into the native cursor projection basis.

This first implementation uses the eye's ray origin: rotating the hand controls the cursor, but translating the hand alone does not supply hand-origin parallax. It is not yet a controller-origin laser or full positional hand aiming. A verified target surface/depth is still needed for that refinement; no arbitrary-depth projection is substituted.

Only the native cursor-delta call returning to RVA `0x1D46DD` is replaced. The original runs first; trigger/button handling and the subsequent native line/path constraints remain intact. Modes 0 and 1 are eligible; walking, pause fade, focus loss, input capture, invalid tracking, stale samples and cross-thread calls retain native output. Enabling/reacquiring while the trigger is held waits for release. Device changes reset that latch. Targets behind or outside the view are rejected; movement is capped at 0.035 native coordinate units per update with a small jitter threshold. Stopping disables and drains all input hooks while retaining the DLL and trampolines until game exit.

Use `scripts/controller-test.ps1 -AimTrace -Seconds 3 -BuildName pointing-experiment` for initial route/ABI validation, then `-Aim -Seconds 30` for the bounded headset test. Stop a persistent controller session before either capture, and restore it afterward using this same build path. The cursor/pause session can remain active throughout. A different already-loaded input DLL requires normal game exit before testing. Actual-game cursor movement, headset alignment, drawing and transition behavior are pending validation.


## Headset pointing result and tunable persistent revision

The 30-second actual-game pointing test produced 2,131 applied cursor updates from 2,437 cursor calls, with 2,458 valid pose samples and clean teardown. The user confirmed that pointing and trigger drawing both worked, then reported excessive sensitivity and requested speed/smoothing controls and right A for recentering. Walking was restored; the render session stayed active.

The separate `pointing-session` revision adds opt-in persistent `-Aim`, `-AimSpeed` (10-300 percent of view width/second, default 80), and `-AimSmoothingMs` (0-250 ms, default 80). It replaces the per-frame cap with elapsed-time speed limiting and an exponential response. Zero smoothing leaves the speed cap active. Settings are chosen at session start; stopping and restarting the same loaded session can change them without restarting the game. Input-session protocol version 2 and its size guard reject mismatched loaders/DLLs; render and timed-probe protocols remain unchanged.

The recenter reference is a fixed tracking-space rotation mapping the current right-hand direction to the current HMD forward direction. Subsequent head movement still changes the projection normally. It never writes camera or headset transforms. The intended button is historical OpenVR A (bit 7), pending physical-button capture. Only a fresh press in mode 0 with the trigger released is accepted; drawing mode 1 and held-on-entry presses cannot recenter. Calibration survives ordinary puzzle/menu transitions but is reset by a device change, session restart, or F7 toggle. Poll-time invalidation disarms button and drawing latches even if the native cursor routine is skipped outside puzzle mode.

F7 now toggles all configured input, including pointing. This revision requires build/tests and a game restart before headset validation; the running older `pointing-experiment` remains walking-only between bounded tests. Right-stick turning and controller menu navigation remain pending.


## Physical A mapping verified

The right-A-only capture `input-20260918-115531-59488-c9b86064.jsonl` recorded eight press/release cycles, alternating only right pressed masks 0 and 4 (legacy Grip, bit 2). All 479 initialized samples stayed focused in puzzle mode 0 with zero menu fade. The right role was device 3 with the previously observed `[1,3,3,-1,-1]` axis layout. The left role was unavailable during this capture. No movement or aiming changes were enabled; hooks stopped cleanly and walking was restored. Cursor/pause remained running, enabled and fault-free, with 125,940 matching submissions and zero fallback.

The recenter input now uses bit 2 only when the explicit legacy-axis option accepted that exact hand layout. Other layouts retain standard OpenVR A (bit 7). Unit checks reject the wrong identifier for each layout and invalid hands; the session fixture exercises both legacy and standard button identifiers. This establishes the physical A mapping for the current bindings, not the uniqueness of that legacy signal to A or validation with another controller profile. Smoothing/recentering in the headset still requires the new build to be loaded after game exit.


## Walking-only snap turning (2026-09-18, prepared build)

The user confirmed the persistent pointing/smoothing/A-recenter build works well and requested snap turning while walking. A fresh role-specific right-stick capture in PID 15172 (`input-20260918-121512-15172-71ac06d4.jsonl`) recorded 477 valid samples: right device 3, left device 4, right axis 0 x range -0.990661..0.999970. Deflections without physical clicking also set button bit 32 (82 samples). This associates stick deflection with the legacy pad-click action; this capture remained in mode 2, so it does not itself demonstrate puzzle cancellation. The earlier cached-slot capture saw no axis motion and cannot be used to map the right stick.

For this increment, preserve native stick-to-cancel, trigger and B handling. The earlier proposal to suppress puzzle cancellation is deferred: a separate native-action filter would change existing interaction behavior. Snap is sampled only in walking mode and a held deflection cannot survive puzzle/pause/focus transitions. Center must be observed while walking before a later deflection turns. The compatibility axis is shared with the trackpad, which may consequently also turn.

### Verified native path

Bound to EXE SHA-256 `8d672d444df6a6df7130f25a517bdab1af92731fe2138dc5b324d519561d55a5` and image size 74457088. Function RVA `0x23E540`, called from `0x24A462` (return `0x24A467`), rebuilds native VR transforms using the heading at `0x630420`. It quantizes to 22.5-degree units, rotates the HMD transform, updates room-scale data and marks view state dirty. Native horizontal mouse/control input at `0x249745` subtracts the same delta from `0x6303EC` and `0x630420`; pitch is separate at `0x6303F0`.

The hook validates the exact executable, entry/caller and both heading-reference/store sequences. Both floats must reside in writable committed data in the main image. On the native game thread, before the original VR update, it changes the paired heading floats by one signed snap increment and calls the original exactly once. It never edits HMD pose or pitch. All callbacks/trampolines remain pinned until process exit. The original heading is not restored on stop: turns are accumulated player orientation, not a temporary view offset.

### Input and controls

`src/snap_turn.hpp` resolves the right hand through the existing poll, uses the detected joystick or explicitly accepted exact legacy layout, and requires radius <=0.20 to arm. A horizontal deflection of at least 0.70, dominating vertical magnitude, yields one edge. Large vertical deflections consume the edge without turning. A 250 ms minimum interval additionally rejects fast repeated flicks without delaying them. Role/axis change, invalid values/state, a poll gap over 100 ms and context loss disarm. At application, recheck both sampled and current mode 2, focus, HMD-valid/connected state, zero menu fade, draw suppression, VR input capture, native VR active state, matching thread and sample age <=50 ms.

`-Snap` enables 45-degree turning; `-SnapAngle` accepts 22.5, 45 or 90, matching the native quantization. `-SnapTrace` hooks the same route with heading writes disabled. Input session protocol is now version 3; use matching loader and DLL from `out/snap-experiment`, and retain the separate render session. Existing `pointing-session` binaries remain intact until normal game exit.

Unit/process fixtures cover all angles/signs, neutral/repeat/cooldown logic, mode/focus/pause/state failure gates, right-role replacement, native update ordering, decoy callers, a second same-frame call, passive captures, enable/disable and session restart/teardown. Compilation, test results, actual-game route tracing and headset results are recorded separately in `out/reports/snap-implementation-status.json`. Hardware verification is still pending.

The prepared `snap-experiment` build passed all seven CTests (124.44 seconds), including the separate snap process test. All 19 input byte guards also match the installed executable on disk. The game still runs the earlier pointing DLL; normal exit/relaunch and actual-game/headset snap verification remain pending.


### Combined manual cursor and faster capture build

At the user's request, the prepared build now includes stick cursor ownership whenever -Aim is enabled. Neutral in a puzzle arms it; deflection applies time-scaled, dead-zoned cursor deltas through the existing caller-scoped cursor hook. Rest preserves the cursor and allows mouse input; A with released trigger recenters and returns to pointing. Context/device loss clears manual ownership. The same AimSpeed controls maximum hand motion and full-stick motion; smoothing is for hand pointing.

The native setter `0x364210` is hooked only to consume key `0x136` from the VR pad call returning to `0x37AC55`, while Aim is active in a focused/tracked puzzle and a fresh valid right-hand legacy-layout sample exists. Original setter still runs with false. Trigger, B and other callers are unchanged. This deliberately replaces the legacy stick/pad cancellation that conflicts with manual cursor input; the earlier preservation plan above is superseded by this user-requested feature. Cached diagnostic tools that validate the setter entry cannot coexist with this detour; use role-selected passive observation for input mapping.

An unflagged bounded capture now copies existing session samples without owning/stopping hooks. Observer failure does not poison or change the input session. Process tests verify active settings survive observation and continue rejecting overlapping behavior-changing diagnostics. Protocol version 4 adds manual/cancel counters. All eight tests passed with four independent jobs in 37.92 s, compared with 124.44 s for the prior serial seven-test run. The new snap/manual build has not yet run in the game/headset.
