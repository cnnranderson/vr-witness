# Code guide

## Modules

| Location | Responsibility |
| --- | --- |
| `src/launcher/main.cpp` | Native Status/Settings UI and its own-window smoke check |
| `src/launcher/backend.*` | Steam discovery, settings, verified readiness and serialized loader control |
| `src/config_path.hpp` | Resolve portable or development settings relative to the DLL |
| `scripts/release.ps1`, `scripts/package.ps1` | Versioned build/test/package workflow and clean file allowlist |
| `src/loader.cpp` | Validate the target, load a repo DLL, and call its diagnostic/session export |
| `src/protocol.hpp` | Fixed x64 request/response layouts shared by loader and DLLs |
| `src/build_validation.hpp` | Shared executable hash and exact live-byte guards |
| `src/render_probe.cpp` | Defer each eye's submission until cursor/menu completion; repair paused redraw |
| `src/input_probe.cpp` | Read native hand roles and route movement, cursor, back and snap actions |
| `src/controller_input.hpp` | Historical OpenVR state layout, axis selection and movement math |
| `src/puzzle_aim.hpp` | Pose validation, aim projection, recenter calibration and smoothing |
| `src/stick_cursor.hpp`, `src/snap_turn.hpp`, `src/controller_buttons.hpp`, `src/menu_navigation.hpp` | Stateful input latches with no game-memory access |
| `src/input_settings.hpp` | Strict cursor-speed parsing; preserve the output on invalid input |
| `src/win_util.hpp` | Windows handle ownership, paths, module snapshots and error conversion |
| `tools/runtime_state.py` | Read-only process access, verified snapshots and the startup readiness wait |
| `scripts/start-vr.ps1` | Start/reuse VR and enable the two current sessions |
| `tests/*_targets.cpp` | Separately compiled native-call fixtures used by hook integration tests |

## DLL entry points

All exports use the Windows x64 ABI and return a `DWORD`: zero is success; nonzero values use `ProbeResult`. `DllMain` does no initialization. Export argument buffers live in the target process and remain valid until the remote call completes.

| Export | Request | Behavior |
| --- | --- | --- |
| `WitnessProbeRun` | `ProbeRequest` | Bounded diagnostic; input can passively observe an active session with flags zero |
| `WitnessSessionControl` | `SessionRequest` | Start, stop, enable, disable or query the render session |
| `WitnessInputSessionControl` | `InputSessionRequest` | Control the input session and return its active settings/counters |

Requests include their exact byte size and protocol version. `ProbeRequest` is 2064 bytes, `SessionRequest` is 2104, and `InputSessionRequest` is 2192. Static assertions enforce these layouts. Diagnostics use protocol 1; render sessions use protocol 2; input sessions use protocol 6. Field order, widths, exported names and calling conventions are compatibility boundaries.

Zero-initialize requests, then populate required fields. Diagnostic duration is capped at 30,000 ms. Diagnostics require an absolute, null-terminated UTF-16 log path within the 1024-character buffer. Sessions also accept an empty path to disable logging (`--no-log`); `logging` reports the requested session setting. The loader creates a unique path and refuses overwriting an existing log.

Session calls update their request buffer in place with state, enabled/fault flags and counters. Input calls require valid `aim_speed_percent` (10..300) and `aim_smoothing_ms` (0..250) even for status. `stick_speed_percent` must be zero on input and reports the active value on return. Input start consumes `legacy_axis0`, `pointing` and `snap_steps`; 0/1/2/4 snap steps mean off/22.5/45/90 degrees.

`ProbeRequest.reserved` is a DLL-specific diagnostic selector:

- Module probe: zero.
- Render: 0 trace, 1 cursor completion, 2 menu completion, 3 menu completion plus paused redraw.
- Input flags: bit 0 movement, bit 1 legacy axis, bit 2 aim, bit 3 aim trace, bit 4 snap, bit 5 snap trace. Bits 6/7 select 22.5/90 degrees; neither selects 45. The two angle bits are mutually exclusive and require snap. Aim/aim-trace and snap/snap-trace are mutually exclusive.

Use the loader/session scripts to construct these requests rather than writing a second protocol implementation.

## Input helper contracts

Input math headers do not call the game or Windows. `Hand` represents a role-selected device; `valid` and a supported `stick` index must be checked before using its axes. The legacy layout is enabled explicitly because OpenVR bit labels do not necessarily match physical Knuckles buttons.

- `Movement::update` returns normalized motion after a neutral rearm; context/device changes disarm it.
- `StickCursor::update` returns whether manual cursor owns this frame, including a zero delta while resting. A recenter hands control back to pointing.
- `Pointing::update` returns whether its delta may replace native cursor input; the output is not usable on failure.
- `AimCalibration::update` returns true when a fresh, non-drawing A press recalibrates; `disarm` retains the rotation while requiring a new release.
- `SnapTurn::update` returns -1, 0 or +1 and requires neutral between turns.
- `BButton::update` returns one allowed press edge after release. Left B toggles pause; right B cancels puzzles or goes back in menus.
- `MenuStick::update` produces native D-pad pulses only in an open menu, after neutral. Hold repeat starts at 350 ms and repeats every 150 ms. Each event uses a complete native press/release pair on the game input thread.

Time arguments are monotonic milliseconds. Cursor positions/deltas use native cursor view units; motion/stick speeds use view widths per second. INI values are percentages and are divided by 100 at the caller. Yaw headings use radians. Projection uses the eye origin and hand orientation; it does not implement hand-position parallax.

Session and launcher logs use `src/session_log.hpp`: empty paths disable file creation; complete records stop at 2 MiB per file. Disk-write failures and reaching the cap do not stop gameplay. Bounded developer captures explicitly requested with `--log` remain diagnostic output.

## Threads and lifetime

Hook callbacks execute on game threads. The input poll publishes a sample under `sample_lock`; consumers require a fresh sample from the same thread and recheck focus, context and tracking. Session commands are serialized separately. Worker threads own log output and periodic INI reads; hooks do not read settings files.

Create all trampolines before enabling hooks. Stop accepting new deferred eyes before draining pending submissions. Pinned DLLs and trampolines remain allocated until process exit because callbacks or return addresses can still reference them. A timed-out remote call preserves its argument allocation for the same reason. Do not replace a loaded build.

Native-call fixtures stay in separate translation units so compiler optimization does not assume calls bypass detours. Fixture evidence verifies routing and lifetime; the headset remains necessary for visual alignment and comfort.

## Python and scripts

`ProcessReader` owns a read-only handle; callers close it in `finally`. `read` returns exactly the requested bytes or raises. `snapshot` verifies executable identity and live instruction references before reading known fields. `wait_for_vr` polls only the verified interface slots, revalidates before returning, and raises on timeout; it does not initialize VR.

Sampler `sample` functions require an already verified image base, reject observed pointer turnover, and return unsynchronized evidence. `summarize_input.summarize` reports sampled transitions and ranges; it does not infer physical button labels.

PowerShell entry points provide `Get-Help` summaries. Session scripts select the sole Witness process unless `-TargetPid` is supplied. `start-vr.ps1` preserves healthy sessions and refuses faults or incompatible loaded input builds. Its batch wrapper locates scripts relative to itself and does not change the user's execution policy persistently.
