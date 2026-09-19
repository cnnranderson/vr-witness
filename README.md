# Witness VR

A Windows x64 mod for The Witness's native `-vr` mode. Development files and mod DLLs stay in this repository; the game installation is unchanged.

Working on the tested PC: headset cursor and pause-menu repair, left-stick walking, right-stick snap turns, controller pointing and drawing, manual stick cursor, A recenter, puzzle back, and live cursor-speed settings. The user confirmed the development batch launcher and current controls work; the new portable GUI still needs a headset launch/attach check. Steam Frame support and extended playability still need testing.

## Portable launcher

The portable preview is built as `WitnessVR.exe` with a `runtime` folder and clean default settings. Extract the entire ZIP and run the executable; no installer, Python, PowerShell, compiler, or administrator access is required. It detects Steam libraries or lets the user browse to a Witness executable. The chosen path is saved beside the runner, not hardcoded in the package.

The Status tab shows SteamVR, game PID, native VR readiness, and separate active/disabled/fault states for visuals and controls. Launch VR starts SteamVR and the game, then attaches automatically after readiness checks. Attach supports an already-running VR game; Stop fixes leaves the game running. Closing the launcher leaves active fixes running as requested.

The Settings tab offers independent stick/pointing speeds, smoothing, snap angle, legacy binding compatibility, and optional diagnostic logging. Speeds update live; other changes briefly restart affected sessions. Logging is off by default; enabled logs are capped at 2 MiB each. Settings and optional logs stay beside the executable. The original ZIP excludes machine paths, personal logs and game files.

For future fixes, run `.\scripts\release.ps1 -Version 0.1.1` from the repository root (choose a new version each time). It builds, runs the checks once and creates `out/releases/WitnessVR-0.1.1.zip` with clean defaults. Build records stay local; an optional ZIP checksum is written beside the download.

Build/package instructions and limitations: [portable launcher](docs/portable-launcher.md).

GitHub Actions builds pushes/PRs and publishes a release when a version tag is pushed, such as `v0.1.0-preview.6`. See [GitHub releases](docs/github-releases.md) for one-time setup and the three release commands.

## Development batch launcher

`Start Witness VR.bat` remains available inside this repository and uses the existing tested development builds. Unlike the portable runner, this development script needs Python for readiness checks. Keep the batch file here and create a shortcut if desired. It starts/reuses VR and game sessions; no rebuild or administrator access is needed.

## Controls

| Control | Action |
| --- | --- |
| Left stick | Walk while outside puzzle mode and menus |
| Right stick | Snap turn while walking; move the cursor in puzzle mode |
| Right trigger | Activate a puzzle, click, and draw |
| Right A | Recenter and return from stick cursor to pointing; release the trigger first |
| Left B | Open/close the game pause/settings menu |
| Right B | Puzzle back/cancel, or back in menus; a drawn line may need to be canceled before leaving the panel |
| Either stick in menus | Up/down selects an item; left/right adjusts it; hold to repeat |
| F7 | Toggle controller features |
| F8 | Toggle headset cursor and pause repair |
| F9 | Stop both mod sessions |

Hotkeys require game focus. Center the sticks and release buttons after enabling or resuming. The legacy Knuckles binding can alias trackpad input with stick input. The new B layout and menu navigation have fixture coverage; a headset check is still needed.

## Cursor settings

Edit `config/input.ini` and save:

```ini
[Input]
StickCursorSpeedPercent=20
MotionCursorSpeedPercent=60
AimSmoothingMs=80
SnapSteps=1
LegacyAxis0=1
DiagnosticLogging=0
```

Each value is independent, from 10 to 300 percent of cursor view width per second. Lower is slower. The worker reloads saved values within about one second; invalid or missing values retain the last valid setting. No restart is needed.

Motion smoothing is set when the input session starts: `-AimSmoothingMs 80` (range 0..250 ms). Higher values smooth more and add lag; zero disables smoothing. `-AimSpeed` supplies the initial motion-speed fallback if its INI key is absent. Snap angles are 22.5 (default), 45, or 90 degrees via `-SnapAngle`.

## Manual session commands

Run these from this repository after starting the game with `-vr`:

```powershell
.\scripts\cursor-session.ps1 start
.\scripts\controller-session.ps1 start -BuildName input-settings -LegacyAxis0 -Aim -Snap

.\scripts\cursor-session.ps1 status
.\scripts\controller-session.ps1 status -BuildName input-settings
```

Both scripts also accept `stop`, `enable`, and `disable`. `-TargetPid` selects a process when needed. Use the matching `-BuildName` for the loaded input DLL. Flags such as `-Aim` and `-Snap` are chosen at session start; omitted features stay disabled.

DLLs and hook trampolines stay loaded until the game exits. Stop/start can reuse the same build, but switching builds or replacing a loaded DLL requires a normal game exit. The launcher reports faults or incompatible builds instead of killing the game. F9 stops the sessions without unloading their code.

## Development

The current executable is checked by SHA-256 and live instruction bytes before game-specific hooks are installed. Keep source, builds, toolchains and captures here. Do not replace the shipped OpenVR DLL or commit game binaries, saves, captures or downloaded tools.

On this PC, CMake, Python and the local compiler are installed. A fresh checkout needs CMake 3.25+, Python 3.10+ and the checksum-verified compiler installed by `scripts/setup-toolchain.ps1`.

```powershell
.\scripts\setup-toolchain.ps1
# Choose an output name that is not loaded by the game:
.\scripts\build.ps1 -BuildName dev -Test -TestSuite Input
# Run once after a completed cross-feature, protocol or hook-lifetime change:
.\scripts\build.ps1 -BuildName dev -Test
python -m unittest discover -s tests -p 'test_*.py'
```

Test groups: `Math`, `Input`, `Snap`, `Render`, `Lifecycle`, and `All`. Independent process tests run in parallel by default. Formatting and review guidelines are in [the development workflow](docs/development-workflow.md).

For a passive controller capture alongside the current input session:

```powershell
.\scripts\controller-test.ps1 -BuildName input-settings -Seconds 30
python tools/summarize_input.py out/logs/<capture>.jsonl
```

Omit feature flags for a passive capture. Logs contain both hand roles, axes, button masks and game context. Cached controller slots are not hand roles; use role-selected captures for button mapping.

## Code and evidence

- [Code guide](docs/code-guide.md): modules, DLL protocol, input helper contracts and lifetime rules.
- [Current state](docs/current-state.md): last deployment and next work.
- [Design](docs/design.md): VR presentation and planned puzzle fallback requirements.
- [Development workflow](docs/development-workflow.md): focused tests and controller capture procedure.
- [Testing history](docs/testing.md), [controller investigations](docs/controller-plan.md), and [research notes](docs/research.md): historical evidence, including superseded experiments.
- [MinHook provenance](third_party/minhook/PROVENANCE.md): vendored source, license and local allocator changes.

Still needs headset validation: controller menu navigation. Still unfinished: hand-position parallax, fixed-view/mono puzzle presentation, a portable signed release, and Steam Frame validation. Native head tracking is preserved; freezing the headset view is not the planned puzzle fallback.
