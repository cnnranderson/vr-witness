# Architecture

The launcher manages settings and session requests. The loader validates a target
process and invokes the DLL exports. Input and rendering run inside the game;
their callbacks retain the native game's VR ownership and simulation timing.

## Source layout

| Location | Responsibility |
| --- | --- |
| `src/launcher/main.cpp` | Application entry point and single-instance guard |
| `src/launcher/ui/` | Window lifecycle, layout, named controls, Status and Settings pages |
| `src/launcher/backend.*`, `backend_commands.cpp` | Worker queue, snapshots, startup, and session transitions |
| `src/launcher/services/` | Settings persistence, Steam discovery, process utilities, readiness verification, and loader client |
| `src/launcher/resources/` | Windows manifest, icon resource, and version template |
| `src/loader/main.cpp` | Target validation, remote calls, and diagnostic/session CLI |
| `src/input/module.cpp` | Native input hooks, sample publication, and input session lifecycle |
| `src/input/*.hpp` | Controller state, movement, pointing, stick cursor, snap, buttons, and menu latches |
| `src/render/module.cpp` | Deferred eye submissions, paused redraw repair, and render session lifecycle |
| `src/game/build_validation.hpp` | Executable fingerprint and live instruction guards |
| `src/common/` | Wire protocol, handle ownership, paths, bounded logs, and caller addresses |
| `src/diagnostics/module_probe.cpp` | Bounded module inventory; no input or rendering behavior |
| `tests/input`, `tests/render`, `tests/diagnostics` | Native test hosts and separately compiled hook targets |
| `tests/launcher` | Launcher service checks and a test-only UI driver |
| `tests/integration` | Process injection, behavior, and lifecycle checks |

The launcher core is a static library shared by the application and its service
tests. UI test capture code is compiled only into `witness-launcher-ui-test`.
The portable executable contains no screenshot or smoke-test command path.

## Launcher flow

`Window` owns controls on the UI thread. It sends `Request` values to `Backend`
and renders copied `Snapshot` values after a posted status message. A single
worker owns mutable session state and serializes process inspection and loader
calls. The shared request slot and published snapshot are protected by a mutex.

`Launch VR` starts SteamVR, launches the game with `-vr`, waits for verified
native interfaces, and starts the render/input sessions. `Attach` uses the same
verification for an existing process. Readiness caches executable bytes by PID
and clears that cache when the process or selected folder changes.

`SessionKind` identifies input or rendering explicitly at the loader boundary.
A failed loader operation marks the outcome uncertain and prevents automatic
retry. Settings edits reload cursor speeds live; other settings restart only
the affected sessions, retaining a disabled state. Closing the window joins the
worker but does not stop the game's sessions.

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

Session and launcher logs use `src/common/session_log.hpp`: empty paths disable file creation; complete records stop at 2 MiB per file. Disk-write failures and reaching the cap do not stop gameplay. Bounded developer captures explicitly requested with `--log` remain diagnostic output.

## Threads and lifetime

Hook callbacks execute on game threads. The input poll publishes a sample under `sample_lock`; consumers require a fresh sample from the same thread and recheck focus, context and tracking. Session commands are serialized separately. Worker threads own log output and periodic INI reads; hooks do not read settings files.

Create all trampolines before enabling hooks. Stop accepting new deferred eyes before draining pending submissions. Pinned DLLs and trampolines remain allocated until process exit because callbacks or return addresses can still reference them. A timed-out remote call preserves its argument allocation for the same reason. Do not replace a loaded build.

Native-call fixtures stay in separate translation units so compiler optimization does not assume calls bypass detours. Fixture evidence verifies routing and lifetime; the headset remains necessary for visual alignment and comfort.

## Diagnostic tools

`ProcessReader` owns a read-only handle; callers close it in `finally`. `read` returns exactly the requested bytes or raises. `snapshot` verifies executable identity and live instruction references before reading known fields. `wait_for_vr` polls only the verified interface slots, revalidates before returning, and raises on timeout; it does not initialize VR.

`summarize_input.summarize` reports sampled transitions and ranges from the
current role-selected input capture. It does not infer physical button labels.
PowerShell session scripts require an explicit game folder and default to the
`dev` build. Use the build name matching the DLL already loaded in the process.
