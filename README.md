# Witness VR

A Windows x64 mod for The Witness's native `-vr` mode. Development files and mod DLLs stay in this repository; the game installation is unchanged.

Working on the tested PC: headset cursor and pause-menu repair, left-stick walking, right-stick snap turns, controller pointing and drawing, manual stick cursor, A recenter, left-B puzzle back, and live cursor-speed settings. The user confirmed the launcher and current controls work. Steam Frame support and extended playability still need testing.

## Start playing

1. Connect the headset and turn on both controllers.
2. Double-click **Start Witness VR.bat** in this folder.
3. Wait for **Ready**, focus the game, center the sticks, and release the triggers.

The launcher starts SteamVR if needed, launches The Witness with `-vr`, waits up to five minutes for native VR, and enables both mod sessions. It reuses a running VR game and healthy sessions. Close a non-VR game normally before using it. The window stays open for status/errors; closing it after success leaves the mod running until game exit.

You can create a desktop shortcut to the batch file. Keep the actual file in this repository. No administrator privileges or rebuild are needed. Python must be installed for the read-only readiness check. Logs are under `out/logs`.

The default installation is `D:\SteamLibrary\steamapps\common\The Witness`. Another path can be passed to `scripts/start-vr.ps1 -GameDir 'E:\Games\The Witness'`.

## Controls

| Control | Action |
| --- | --- |
| Left stick | Walk while outside puzzle mode and menus |
| Right stick | Snap turn while walking; move the cursor in puzzle mode |
| Right trigger | Activate a puzzle, click, and draw |
| Right A | Recenter and return from stick cursor to pointing; release the trigger first |
| Left B | Puzzle back/cancel; a drawn line may need to be canceled before leaving the panel |
| Right B | Pause/resume |
| F7 | Toggle controller features |
| F8 | Toggle headset cursor and pause repair |
| F9 | Stop both mod sessions |

Hotkeys require game focus. Center the sticks and release buttons after enabling or resuming. The legacy Knuckles binding can alias trackpad input with stick input. Menu navigation still uses mouse/keyboard.

## Cursor settings

Edit `config/input.ini` and save:

```ini
[Input]
StickCursorSpeedPercent=40
MotionCursorSpeedPercent=80
```

Each value is independent, from 10 to 300 percent of cursor view width per second. Lower is slower. The worker reloads saved values within about one second; invalid or missing values retain the last valid setting. No restart is needed.

Motion smoothing is set when the input session starts: `-AimSmoothingMs 80` (range 0..250 ms). Higher values smooth more and add lag; zero disables smoothing. `-AimSpeed` supplies the initial motion-speed fallback if its INI key is absent. Snap angles are 22.5, 45 (default), or 90 degrees via `-SnapAngle`.

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

Still unfinished: controller menu navigation, hand-position parallax, fixed-view/mono puzzle presentation, a portable signed release, and Steam Frame validation. Native head tracking is preserved; freezing the headset view is not the planned puzzle fallback.
