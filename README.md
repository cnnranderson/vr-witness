# Witness VR

An experimental Windows x64 mod project for 6DOF VR in The Witness, initially targeting PC VR streamed to Steam Frame through SteamVR.

**Current stage: native 6DOF, a puzzle cursor fix, and a first pause repair verified in the headset.** The game's `-vr` mode follows head rotation and leaning. Keyboard/mouse movement and puzzle drawing work. Our mod submits each eye after its cursor and VR menu, and keeps the paused scene rendering separately for each eye. In a 30-second headset test, the user confirmed that the pause menu appeared, head tracking worked, and the duplicated eye view was fixed. Persistent cursor behavior and pause in the combined persistent session have now been confirmed by the user. Left-stick Knuckles walking also passed a bounded headset test. Complete Knuckles controls, Steam Frame, gamepad input, calibrated world scale, extended stability, and fixed/mono puzzle modes remain unfinished or untested.

The module probe still only observes and unloads. The render DLL supports both bounded diagnostics and a combined cursor/pause session that runs until stopped or the game exits. This remains a development prototype.

## Start playing on this PC

Double-click **Start Witness VR.bat** in the repository. Turn on the headset and both controllers. The launcher starts SteamVR if needed, starts The Witness with `-vr`, waits up to five minutes for its native VR interfaces, then enables the cursor/pause and controller sessions from the existing tested builds. It uses 45-degree snaps, pointing/manual cursor, A recenter and left-B back; speeds come from `config/input.ini`.

An already-running game is reused; healthy sessions are left running, disabled ones are enabled, and stopped ones are started. A non-VR game must be closed normally first. An incompatible loaded build or fault produces an error instead of restarting/killing the game. The window shows success or the error and stays open until a keypress; closing it after success leaves the mod active. Launcher/session logs stay under `out/logs`. No administrator privileges or rebuild are needed. Python must remain installed for the read-only readiness check.

For a desktop shortcut, create a shortcut to the batch file; leave the batch file in this repository so it can find its scripts. The game installation is unchanged. The PowerShell equivalent is `scripts/start-vr.ps1`; it accepts `-GameDir` and `-WaitSeconds` (10..600) if needed.

## What you need now

- The installed 64-bit Steam version of The Witness and a monitor/keyboard/mouse.
- No headset is needed for binary inspection or the initial injection test.
- On this PC, CMake and Python are already installed. The portable x64 compiler has been downloaded and verified inside `.tools/`; Visual Studio's missing C++ workload is not a blocker.
- For VR testing: a working PC VR headset/SteamVR connection. An existing headset is connected on this PC; Steam Frame testing comes later. Keyboard/mouse works as the present input baseline. An Xbox-style gamepad is optional and has not been tested.

## First live test

1. Start The Witness normally through Steam, without `-vr` for this baseline test. Leave it at the main menu, or use a separate test save.
2. In PowerShell run:

   ```powershell
   Set-Location 'C:\Users\samic\Documents\GitHub\vr-witness'
   .\scripts\probe.ps1
   ```

3. Expect `Probe finished and unloaded`, followed by a graphics/VR module table. The game should remain responsive. No visual change is expected yet.
4. Logs are under `out/logs/`. Tell the developer whether the game stayed responsive and whether the command succeeded; the developer can read those logs directly on this PC.

The probe script automatically selects the sole running `witness64_d3d11.exe`. It refuses an ambiguous selection; use `-TargetPid 1234` if necessary. For another installation, pass `-GameDir 'E:\Games\The Witness'`. Use `-Seconds 15` for a longer capture (maximum 30 seconds).

If PowerShell blocks script execution, use this command for just this process:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File 'C:\Users\samic\Documents\GitHub\vr-witness\scripts\probe.ps1'
```

Do not run Steam or the game as administrator. The loader uses the same user session and needs no administrator privileges in the normal case. If a probe times out, close and restart the game before retrying or rebuilding the DLL; a timeout deliberately leaves possibly in-use memory/code alone.

## Build and verify

```powershell
# First setup on a new checkout; download stays inside the project.
.\scripts\setup-toolchain.ps1

# Build and exercise the loader against our own hidden test process.
.\scripts\build.ps1 -Test

# Read-only inspection of the installed game; writes a JSON report here.
python .\tools\inspect_game.py
```

Requirements on a new Windows machine: CMake 3.25+ on PATH, Python 3.10+ for inspection, and the portable toolchain installed by the setup script. Windows 10/11 x64 is the initial target. The setup script uses Windows `tar.exe` to extract the pinned official w64devkit archive and checks its SHA-256 before extraction. No system compiler installation or persistent PATH change is made. The third-party toolchain's licenses are included in `.tools/w64devkit/`.

The smoke test verifies wrong-target rejection, real x64 cross-process injection, valid JSONL logging, two attaches to the same process, clean DLL unload, and target survival. It does not establish game compatibility or VR functionality.

The first manual attach to the actual game also succeeded: diagnostics completed, the loader reported a clean unload, and the same game process remained responsive. This validates the observation probe in that session, not a playable VR conversion.

For a read-only snapshot of the verified build's stored OpenVR state, run `scripts/runtime-state.ps1` with the game open. It writes to `out/reports/` and does not inject or modify the game. Null interface pointers do not establish why initialization was absent or whether it was attempted earlier.

## Cursor and pause fixes

With SteamVR running and a headset connected, launch through Steam with `-vr`. No compatibility-mode changes or replacement OpenVR DLL were needed in the observed session.

For an ongoing cursor and pause session, from this repository:

```powershell
.\scripts\cursor-session.ps1 start
.\scripts\cursor-session.ps1 status
# Optional commands: disable, enable, stop
```

**F8** toggles both fixes and **F9** stops the session while The Witness is focused. After F9, use `start` to attach again. Focus changes disarm held hotkeys. There is currently no headset status overlay; `status` reports the active state. Disabling the fixes restores native submission and pause rendering, including their original defects. These controls do not implement puzzle perspective modes.

The user has verified persistent cursor visibility and the F8 toggle in the headset. The new pause repair passed a bounded headset test, and the user subsequently confirmed pause works in the combined persistent session. Extended pause/resume and puzzle-drawing regression checks remain.

Sessions write state changes and a 30-second heartbeat, without per-frame file writes or an ever-growing trace buffer. An unexpected render path that omits the matching stereo menu call latches a fault and disables deferral; inspect the log, then stop and restart the session to retry. Hook setup/teardown failures instead require restarting the game. The DLL and trampolines remain pinned after stopping, so **close the game before rebuilding**. Timed render diagnostics and sessions cannot run together.

```powershell
# Read-only cached cursor/controller/head state, maximum 30 seconds.
# Stop the session before this sampler; restart it afterward.
# Its live-code guards deliberately reject the active pause detour.
.\scripts\playability-trace.ps1 -Seconds 5 -Label 'puzzle-mode-open'

# Temporarily hook four rendering functions to record their actual order.
.\scripts\render-trace.ps1 -Seconds 5

# Experimental behavior change, automatically restored after the interval.
# Cursor-only mode passed the first headset test; it does not repair pause.
.\scripts\render-trace.ps1 -Seconds 20 -DeferCursor

# Pause investigation only: submit after the matching stereo menu pass.
# First headset test did not repair pause; retained for investigation.
.\scripts\render-trace.ps1 -Seconds 20 -DeferMenu

# Headset-verified pause combination, active only for this interval.
.\scripts\render-trace.ps1 -Seconds 20 -RedrawPause
```

The render DLL checks the executable hash and live code, then hooks eye setup, submission, cursor drawing, and menu drawing. `-DeferCursor` delays only submissions made inside the main scene's eye-finalization function until the cursor routine returns. `-DeferMenu` waits through the cursor and matching stereo menu pass; unrelated eyes and mirror passes cannot release the pending eye. `-RedrawPause` adds a caller-specific pause predicate hook, keeping per-eye scene drawing active while other pause checks retain the native result. These options are mutually exclusive. Other submission paths retain their normal behavior. On successful completion hooks are disabled and the stock behavior returns, including the missing headset cursor; start a cursor session again afterward to restore the tested fix. The DLL and trampolines deliberately stay in memory until game exit; **close the game before rebuilding this DLL**. After any failed render experiment, restart the game before retrying. Logs distinguish trace-only and deferred modes. A save backup was made under `out/backups/` before the first game hook attempt.

Bounded traces also record target identifiers and the menu transform beside each render callback. These snapshots read verified globals without following target/GPU pointers. They help locate rendering faults but do not capture the displayed image. Persistent cursor sessions omit these detailed snapshots.

Run the read-only sampler's defensive tests with `python -m unittest discover -s tests -p test_playability_trace.py`. `scripts/build.ps1 -Test` exercises real render hooks in our own process, actual submission reordering, restoration, session controls, duration beyond 30 seconds, overlapping-command refusal, skipped-menu faults, pause caller isolation, restart, bounded logging, and clean process exit. These checks do not establish headset correctness. The hookable fixture functions compile separately from their caller to preserve standard ABI boundaries under optimization.

CMake also supports an installed x64 MSVC toolchain; that build path has not yet been tested. The provided PowerShell build command uses the portable GCC toolchain. `-Fresh` resets CMake's generated configuration if switching toolchains.

## Knuckles input work

Read-only per-hand captures now show that the game receives right-controller axes/buttons but ignores the left controller's inputs in the observed launch. Both controller poses update. The separate input DLL now adds role-selected left-hand walking, as described below. Right-hand angular pointing and native trigger drawing passed a bounded headset test. The user confirmed the persistent smoothing/recenter build works well. Walking-only snap turning is prepared in the separate experiment below; its headset test is pending. See [controller findings and plan](docs/controller-plan.md).

The focused input diagnostic works with the cursor/pause session active:

```powershell
python .\tools\controller_trace.py --pid <PID> --seconds 20 --label left_controller_only
python -m unittest discover -s tests -p test_controller_trace.py
```

## Files and installation policy

| Location | Purpose |
| --- | --- |
| `src/` | Loader and diagnostic DLL source |
| `scripts/` | Setup, build, and live-test commands |
| `tools/` | Read-only binary inspection |
| `tests/` | Our own test process and smoke test |
| `docs/` | Research, requirements, and development milestones |
| `.tools/` | Portable compiler/downloads; ignored by Git |
| `out/build/bin/` | Compiled DLL/loader; ignored by Git |
| `out/logs/`, `out/reports/` | Runtime logs and local binary analysis; ignored by Git |

**All development files stay in this project.** The loader loads the selected DLL by its absolute project path. Nothing is copied into the game directory, and the shipped `openvr_api.dll` is not replaced. Closing the game removes retained experiment code. Build output, game binaries, disassemblies, saves, personal logs, and the toolchain must not be committed. The render experiment includes the licensed [MinHook source subset and local changes](third_party/minhook/PROVENANCE.md).

See [design and milestones](docs/design.md), [testing plan](docs/testing.md), and [initial binary findings](docs/research.md).

## Bounded Knuckles movement experiment

A separate input DLL now reads both controller roles and can add left-stick movement through the native movement-vector routine. It is not part of the persistent cursor/pause session yet. In the actual-game passive test, SteamVR reported a Vive-style shared axis rather than a distinct joystick; the explicit compatibility option may therefore also respond to the left trackpad. Right-stick turning remains unfinished. Pointing/drawing evidence and the current controller session are described below.

After building/testing `controller-experiment` and restarting the game to clear any older pinned input DLL, start the existing cursor/pause session. From the repository, run:

```powershell
# Passive controller read, no movement applied:
.\scripts\controller-test.ps1 -Seconds 5
# Bounded walking test for the observed legacy layout:
.\scripts\controller-test.ps1 -Move -LegacyAxis0 -Seconds 30
```

Keep the game focused and begin with the stick centered. The experiment is designed to stop movement outside normal walking mode, after focus/input loss, and at the deadline; a neutral stick is required before rearming. The user confirmed correct left-stick movement and then separately confirmed blocking while paused, walking after resume, and correct headset presentation. Controller navigation inside the pause menu is still missing; use the mouse. F9 ends the experiment and also stops an active cursor/pause session. The input DLL stays pinned until game exit. Do not rebuild a loaded copy. See `docs/controller-plan.md` for evidence, limits and next checks.

## Persistent walking and puzzle pointing

The `pointing-session` build supports left-controller walking plus optional right-hand angular pointing until stopped or game exit. The user confirmed pointing and native trigger drawing in the earlier 30-second test, but found it too sensitive. This revision adds time-based speed limiting, smoothing and an A-button recenter reference. The user confirmed the persistent smoothing and A-recenter build works well in the headset.

```powershell
# Build while this output directory is NOT loaded in the game:
.\scripts\build.ps1 -Test -BuildName pointing-session
# After normal game exit/relaunch in VR and restoring the cursor/pause session:
.\scripts\controller-session.ps1 start -BuildName pointing-session -LegacyAxis0 -Aim -AimSpeed 80 -AimSmoothingMs 80
.\scripts\controller-session.ps1 status -BuildName pointing-session
# Optional: disable, enable, stop
```

`AimSpeed` is the maximum cursor movement in percent of view width per second (10â€"300, default 80). Lower is slower. `AimSmoothingMs` controls the exponential response (0â€"250 ms, default 80); higher smooths more and adds lag, while 0 disables smoothing. Restart just the input session to change settings; the game need not restart when reusing the same loaded build. Omitting `-Aim` keeps walking only.

Right A recenters aiming to the center of your view using your current hand direction. Release A after entering puzzle mode, then press it before drawing. Recenter is blocked while drawing/holding the trigger, in menus, or when focus/tracking is unavailable. It changes an aim reference only; it does not recenter the headset or move the game camera. The right-A-only capture confirmed legacy bit 2 on the observed compatibility layout; `-LegacyAxis0` selects that mapping. Other layouts use OpenVR A (bit 7). Pointing uses hand orientation and an eye-origin ray; hand translation parallax is not implemented.

F7 toggles walking and configured pointing together. Center the stick and release the trigger to rearm. F9 stops controller input and an active cursor/pause session; F8 toggles the render fixes only. Right-stick turning is now prepared in the separate build below; controller menu navigation remains deferred.

Builds are pinned until game exit: the old `pointing-experiment` DLL cannot be replaced by `pointing-session` in a running game. Use the matching `-BuildName` when controlling an older loaded session. All builds and logs remain in this project.

Read-only pointing diagnostics can run alongside the persistent sessions:

```powershell
python .\tools\puzzle_aim_trace.py --pid <PID> --seconds 20 --label right_hand_pointing_only
```


## Right-stick snap-turn experiment

The `snap-experiment` build adds optional walking-only snap turning. Default angle is 45 degrees; `-SnapAngle 22.5` and `-SnapAngle 90` are also supported. Center the right stick before the first turn, between turns, and after leaving a puzzle/menu or restoring focus. Holding it does not repeat turns. The legacy compatibility layout shares stick/trackpad input, so the right trackpad may also turn.

```powershell
# Compile only while this build directory is not loaded:
.\scripts\build.ps1 -Test -BuildName snap-experiment
# After normal game exit and VR relaunch, first observe the native update route:
.\scripts\controller-test.ps1 -BuildName snap-experiment -LegacyAxis0 -SnapTrace -Seconds 5
# Bounded test (stop any persistent input session first):
.\scripts\controller-test.ps1 -BuildName snap-experiment -LegacyAxis0 -Move -Aim -Snap -Seconds 30
# Persistent mode, after the headset test passes:
.\scripts\controller-session.ps1 start -BuildName snap-experiment -LegacyAxis0 -Aim -Snap
.\scripts\controller-session.ps1 status -BuildName snap-experiment
```

The input DLL stays pinned until game exit; changing from `pointing-session` requires a normal restart. Keep the separate cursor/pause session running. F7 toggles configured walking, pointing and turning together; F8 controls the render fixes; F9 stops both sessions. Right trigger/B retain native behavior. With -Aim enabled, the right stick also controls the puzzle cursor; the legacy stick/pad cancel is consumed in puzzle mode. Turning is blocked in puzzle modes, including the held deflection that exits a puzzle; center in walking mode before turning. Headset snap direction, presentation and comfort still require the first hardware test.


The combined `snap-experiment` build also includes manual puzzle cursor control with `-Aim`: center the right stick once in puzzle mode, then move it to take over. Release it to leave the cursor where it is. Press right A with the trigger released to recenter and return to hand pointing. `AimSpeed` controls both maximum hand-cursor speed and full-stick cursor speed; stick input has a radial dead zone. Mouse input remains available at stick rest. On the shared legacy layout, stick/pad click no longer cancels puzzles while this feature is active. Native trigger and B are preserved. These new controls still need headset validation.

For the shorter development/test/capture loop, see [development workflow](docs/development-workflow.md). Begin continuation work from [current state](docs/current-state.md); use historical reports only when needed. Passive captures in the new build can run beside the persistent input session without disabling walking or pointing.


## Live cursor speed settings and puzzle back

The session script now defaults to `input-settings`. This build keeps the tested walking, snap-turn and cursor controls, adds left B for the native puzzle cancel/back action, and reads `config/input.ini` once per second from its worker thread:

```ini
[Input]
StickCursorSpeedPercent=40
MotionCursorSpeedPercent=80
```

Both values are independently adjustable from 10 to 300, in percent of cursor view width per second. The stick default is half its previous speed. Save the file and the running input session picks it up within about one second; no game/session restart is needed for later tuning. Invalid or missing values preserve the last valid value for that field. Motion smoothing remains `AimSmoothingMs` at session start. `AimSpeed` is the initial motion-speed fallback when that INI key is absent; a valid INI value takes precedence.

Changing to this DLL from an older loaded build requires one normal game exit/relaunch. Then restore the cursor/pause session and start input with:

```powershell
.\scripts\controller-session.ps1 start -BuildName input-settings -LegacyAxis0 -Aim -Snap
.\scripts\controller-session.ps1 status -BuildName input-settings
```

Status reports the actual active `stick_cursor_speed_percent` and `aim_speed_percent`. Left B sends one native back press in puzzle mode, with release required after enabling/entering a puzzle; right B retains pause. Native back may first cancel a line while drawing, then leave puzzle mode on another press. Headset verification of left B and the adjusted speed is pending.
