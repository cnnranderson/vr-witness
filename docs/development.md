# Development

## Build

Requires Windows x64, CMake 3.25+, and PowerShell. Install the pinned compiler
inside the repository, then build into a named directory under `out`:

```powershell
.\scripts\setup-toolchain.ps1
.\scripts\build.ps1 -BuildName dev -Test -TestSuite All
```

The build produces `WitnessVR.exe`, its loader and DLLs, and the test fixtures
under `out/dev/bin`. The GUI expects the portable package layout; use the
[release command](releases.md) to create a runnable portable folder.

Never rebuild a DLL that is loaded in the game. Use a fresh build name or close
the game normally first. Stop/start reuses the same loaded binary; changing
binaries requires process exit. Game files and the shipped OpenVR DLL stay unchanged.

## Tests

`build.ps1 -Test -TestSuite <suite>` builds before running the selected checks.

| Suite | Coverage |
| --- | --- |
| Math | Input transformations, smoothing, latches, buttons, and menu repeat |
| Input | Math, input injection, manual cursor and resting-height routing |
| Snap | Math and snap-turn routing |
| Render | Eye submission, pause redraw, and route-fault behavior |
| Lifecycle | Persistent input/render sessions, stop/start, and callback lifetime |
| Launcher | Argument quoting, Steam discovery, settings, status parsing, and bounded logs |
| All | All ten native checks |

Run the affected suite during iteration and All after a completed cross-feature,
loader, ABI, or lifetime change. Tests start their own fixture processes; they
do not require The Witness, SteamVR, or a headset. Native call targets remain in
separate translation units so compiler optimization cannot bypass detours.
Local checks use four workers; CI runs process fixtures serially with longer deadlines.

The optional UI test is a separate executable:

```powershell
.\scripts\build.ps1 -BuildName dev -UiSmokeTest
```

With the game closed, this builds and runs `witness-launcher-ui-test.exe`.
It saves and resets settings beside its own executable, verifies the result,
captures its own two tabs under the build directory, and exits. The test window
is placed off-screen. It is not included in release packages.

Fixture success establishes routing and lifecycle behavior. Headset validation
is still required for visual alignment, comfort, menu usability, tracking loss,
and extended play sessions.

## Diagnostics

The session scripts operate on a game already running with `-vr`. Supply its
installation folder and the build whose DLLs should be used:

```powershell
$gameFolder = 'E:\Games\The Witness'
.\scripts\cursor-session.ps1 start -GameDir $gameFolder -BuildName dev
.\scripts\controller-session.ps1 start -GameDir $gameFolder -BuildName dev -LegacyAxis0 -Aim -Snap
.\scripts\controller-session.ps1 status -GameDir $gameFolder -BuildName dev
```

Both session scripts also support `stop`, `enable`, and `disable`. `-TargetPid`
selects a process explicitly. Feature flags apply at session start.

For a passive controller capture, omit behavior-changing flags:

```powershell
.\scripts\controller-test.ps1 -GameDir $gameFolder -BuildName dev -Seconds 30
python tools/summarize_input.py out/logs/<capture>.jsonl
```

Captures record both hand roles, axes, button masks, and game context. Hold each
control for at least 200 ms when investigating mappings; 50 Hz sampling can miss
brief edges. Cached device slots are not hand roles. Logs stay under `out/logs`.
`runtime-state.ps1` reads verified native VR state; `probe.ps1` inventories loaded
modules; `render-trace.ps1` captures the guarded render routes. Each script has
PowerShell help. Python 3.10+ is needed only for the Python diagnostic tools.

For excessive movement when entering puzzles, use the bounded read-only
[puzzle-entry capture](puzzle-positioning.md). It works alongside the current
release without rebuilding or restarting DLLs.

## Formatting

C++ uses `.clang-format`; Python uses Black and `pyproject.toml`.

```powershell
python -m pip install --target .tools/formatters -r config/formatters.txt
.\scripts\format.ps1
.\scripts\format.ps1 -Check
```

Use descriptive names and one statement per line. Comments explain units,
ownership, failure behavior, and invariants. Keep project text in ASCII and do
not format vendored sources. Generated builds, logs, captures, and toolchains
stay ignored under `out` and `.tools`. Keep personal settings out of packages;
`packaging/input.ini` supplies release defaults.
