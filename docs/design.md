# Design and milestones

## User requirements

- Keep source, builds, tools, configuration, and logs in `C:\Users\samic\Documents\GitHub\vr-witness`.
- Keep the game installation clean; prefer loading the mod directly from the project. Any later deployment must be small, reversible, and documented.
- Target actual 6DOF: head rotation **and position**, with correct stereo views and world scale.
- Account for puzzles needing a fixed viewpoint or a mono view. The user's screenshot raised this concern; its explanation of the original developers' intentions is not independently established and is not treated as a technical fact.

## Puzzle fallback and comfort

Keep the **game camera** distinct from the **tracked headset view**. Freezing the camera presented directly across the headset would make the image move with the user's head, which is not our proposed fallback.

Planned modes:

1. **World VR:** head tracking controls the view relative to a locomotion origin. Initial movement uses a gamepad; full tracked-controller interaction comes later.
2. **Fixed-view puzzle screen:** capture the puzzle from a fixed game camera and show it on a virtual screen placed in the tracked VR environment. The user can move their head normally around that screen.
3. **Mono puzzle screen:** when two game-eye views disagree on a perspective-dependent solution, show one game-camera view on the virtual screen. The surrounding VR scene and screen placement remain head tracked.

A user-controlled toggle must always be available. Display an in-headset notification identifying the active mode and how to return to world VR. Later automatic suggestions require actual puzzle-state detection; do not pretend this exists now. Preserve locomotion position and puzzle input state across mode changes, and reset the tracking origin intentionally on return. Test each type of perspective puzzle without publishing solution spoilers in general docs.

Menus, pause, focus loss, loading, tracking loss, and runtime shutdown must be included in the presentation design. Do not leave a stale full-field image attached to the user's head. A future implementation should use runtime-supported presentation/fallback behavior appropriate to each state.

## Implementation strategy

First investigate and reuse the game's existing OpenVR path. Its versioned interfaces must be matched to the historical ABI; swapping in current C++ interface headers or a newer DLL without verifying compatibility is not sufficient. The diagnosis tool must not initialize another VR session behind the game's back.

If that path cannot be made reliable, investigate an OpenXR bridge using the game's D3D11 device and verified camera/render hooks. A final swapchain `Present` hook alone does not provide true stereo: each eye needs its correct view/projection, and rendering must not advance simulation independently for each eye. Any reused rendering path must handle culling, shadows, postprocessing, and HUD placement.

Initial target: Windows PC rendering and SteamVR streaming to Frame. Standalone execution on Frame is a separate compatibility/performance project and is outside the first prototype.

## Milestones and evidence required

| Stage | Deliverable | Exit condition |
| --- | --- | --- |
| 0 — diagnostics | x64 loader, bounded observation DLL, binary inventory | Build and owned-process injection test pass; then verify an attach in the actual game |
| 1 — trace legacy VR | Trace initialization/interface requests and identify render/camera sites | Logs distinguish code actually executed from imports merely present; bind findings to exact executable hash |
| 2 — desktop camera experiment | Controlled translation and rotation offsets with a restore control | Repeatable offset without destabilizing simulation, save/load, or puzzle state; no headset required |
| 3 — stereo/head tracking | Correct per-eye views, head pose, recenter, gamepad movement | Hardware confirms parallax, scale, low-latency movement, and stable frame pacing |
| 4 — playable puzzles | Correct cursor/ray and mode switching | Panels and perspective puzzles usable; fixed/mono fallback notification and manual toggle tested |
| 5 — polish | Motion controllers, comfort settings, reproducible installation | Regression coverage across menus, loading, play sessions, and target hardware |

Stage 0 has passed both owned-process testing and an actual-game attach/unload. The native `-vr` experiment subsequently reached active OpenVR system/compositor interfaces and stereo texture submission; the user confirmed head rotation and leaning in the headset. Keyboard/mouse walking and puzzle drawing also work. This changes the practical sequence: repair the missing headset cursor first, then controller input and puzzle fallback modes. A separate camera override or replacement stereo renderer is not currently required to obtain basic 6DOF.

The module probe still only observes module loading. External samplers record verified-build globals without hooks. The separate render probe traces actual eye-finalization, submission, and cursor routine calls; its optional deferred mode is a bounded experiment, not established playability. Do not advance a milestone based only on successful compilation or static strings.

## Injection lifetime

The loader validates the exact target process path and x64 architecture, resolves system functions relative to their actual owning module in the target, then calls `LoadLibraryW` with the project's DLL path. It finds the full 64-bit remote module base through a module snapshot, not the truncated thread exit code. It explicitly invokes `WitnessProbeRun` outside loader lock. The export writes a bounded module inventory and returns; only then does the loader call `FreeLibrary`.

`DllMain` is empty. There are no hooks or background threads to race during normal unload. If a remote call times out, the loader avoids freeing memory still potentially used by that thread. Restart the target to recover. Later hooked builds will need a different lifetime policy; do not assume this probe's unload procedure is safe for active detours or VR sessions.

The render probe uses that different policy: pin the DLL before installing any hooks and retain its trampolines until process exit. All five originals are resolved before enabling detours. Trace-only and cursor/menu timing diagnostics enable only the four render hooks. The combined session and pause-redraw experiment also enable the caller-scoped pause predicate hook. A bounded event array records from the render thread; log file I/O occurs in the exported diagnostic worker. Stopping deferred mode first prevents new deferrals, disables the submit hook, lets pending work drain through the render thread, then disables the remaining hooks. A failure makes the instance unavailable for retry and requires game restart. No callback or trampoline memory is freed during capture teardown. Repeated captures append to unused event slots rather than recycling slots a late callback might access.

The first deferred-submission headset test restored a correctly aligned puzzle cursor. A session-lifetime mode now has CLI start/stop/enable/disable/status and foreground-only F8/F9 controls. Its worker is created outside loader lock, owns the log, and returns independently of the loader. The worker records counter summaries and state changes; render callbacks only increment counters. Stopping joins the worker after draining/disabling hooks. A skipped completion point latches deferral off for that run. The combined session uses the matching stereo menu completion point and disables paused redraw on the same fault. Repeated start and overlapping timed diagnostics are refused without disrupting the active session. Owned-process integration covers these behaviors, including a run exceeding the old 30-second limit.

Transition coverage (pause/menu/loading/tracking loss) and extended hardware validation are still required before treating this as a playable release. Knuckles input follows as a separate workstream; the existing keyboard/mouse baseline is usable for cursor validation. Fixed/mono puzzle presentation remains an explicit requirement, not a feature implemented by this fix.

A focused pause trace found continuing head updates and stereo eye rendering, with submission still preceding cursor and menu drawing. A separate bounded `-DeferMenu` experiment waits for the matching eye's non-mirror stereo menu pass before submitting. Its first headset test produced no improvement despite verified execution through pause transitions, so timing alone is not the pause repair. The session default remained cursor completion until the subsequent combined pause-redraw test succeeded. Missing menu passes trigger the same next-eye fallback and fault latch; teardown retains the selected completion point until pending work has drained. The subsequent render-only pause override plus menu completion repaired the observed headset defect.


## Pause redraw repair

The paused renderer skips the per-eye world draw and later copies one retained scene source into each eye. The combined redraw/submission experiment subsequently fixed the reported duplicate-eye view and tracking in a headset test. This supports the stale-scene diagnosis; no GPU pixel capture was made. The monitor also draws its own menu after mirroring an eye; seeing a brief VR menu followed by the desktop menu does not alone prove that the desktop pass damages headset presentation. Read-only resource inspection found distinct eye and monitor GPU resources.

`--redraw-pause` / `scripts/render-trace.ps1 -RedrawPause` is a separate timed mode (maximum 30 seconds). For the supported executable hash it intercepts the native pause predicate at RVA `0x1FE730`, calls the original, and changes a true result to false only for the return address `0x1C8A46`. That call determines the render routine's scene-drawing decision. The earlier query in the same routine, all other callers, the stored pause state, and the native menu/input passes keep their behavior. Both the predicate and draw call/branch bytes are validated before enabling hooks. Eye submission waits for its matching stereo menu pass.

The override is disabled during teardown or after a submission route fault. The persistent session now enables the same verified combination; F8 disables/enables both behaviors and F9 stops them. Cursor-only/trace timed modes do not enable the predicate hook. Owned-process tests alternate paused and unpaused states and independently check the render caller, an unrelated caller, missing-menu recovery, subsequent normal modes, and process survival. This verifies call-site isolation and hook lifetime in the fixture; a separate 30-second game test confirmed visible menu, corrected eye views, and tracking. Extended playability and the persistent combination still require hardware follow-up. Fixed/mono puzzle fallback is unchanged and remains unimplemented.
