# The Witness VR

A portable Windows mod for The Witness's native VR mode. Fixes headset cursor
and pause rendering, and adds controller movement, pointing, puzzle input,
snap turning, and menu navigation.

## Quick start

Requires Windows 10/11 x64, Steam, SteamVR, a PC VR headset, and the supported
Steam Windows build of The Witness. Knuckles controllers are supported.

1. Download and extract the complete ZIP from [Releases](https://github.com/cnnranderson/vr-witness/releases).
2. Connect the headset and both controllers.
3. Run `WitnessVR.exe`. Check the detected installation, or select **Browse**.
4. Select **Launch VR** and keep the headset awake.
5. Wait for both session indicators to show **Active**.

No installer, administrator access, or build tools are required. The game
installation stays unchanged. **Attach to Game** also supports a game started
with `-vr`. **Detach from game** stops the fixes; closing the launcher leaves
them running. Close the game and launcher before updating the mod.

## Controls

| Control | Action |
| --- | --- |
| Left stick | Walk |
| Right stick | Snap turn while walking; move the puzzle cursor |
| Right trigger | Activate, click, and draw |
| Right A | Recenter pointing and switch from stick cursor to pointing |
| Left B | Toggle the pause/settings menu |
| Right B | Cancel a line, leave a puzzle, or go back in menus |
| Either stick in menus | Up/down selects; left/right adjusts; hold to repeat |
| F7 | Calibrate height while walking |
| Shift+F7 | Restore the game default height |
| F8 | Toggle both fixes while attached |

Keep the game focused. Center sticks and release buttons after resuming controls.
Release the trigger before recentering. Canceling a line and leaving a panel
may require separate B presses.

## Settings

Change values in **Settings**, then select **Save settings**.

| Setting | Default | Options |
| --- | --- | --- |
| Analog stick cursor speed | 20% | 10-300% of view width per second |
| Controller pointing speed | 60% | 10-300% of view width per second |
| Pointing smoothing | 80 ms | 0-250 ms |
| Snap turning | 22.5 degrees | Off, 22.5, 45, 90 degrees |
| Controller type | Knuckles | Xbox 360 and Steam Frame unavailable |
| Debug logging | Off | 2 MiB limit per file |

Speeds update within about one second. Other changes briefly restart affected
fixes. Settings are stored in `config` beside the launcher.

Each game launch starts at its native default height. With the game focused,
outside puzzles and menus, press **F7** to set your current posture to the game's
standing eye height. Leaning and crouching remain tracked. **Shift+F7** removes
the adjustment. **Configured height** shows the target and signed offset in
meters, or **Game default**. Calibration is not saved between game launches.

## Status and troubleshooting

Sessions shows each component's state. Inactive DLLs remain loaded until the
game exits. Errors appear in dialogs. For diagnostics, enable **Debug logging**,
reproduce the issue, then select **Open logs**.

If VR is not ready, check SteamVR and wake the headset. Restart a non-VR game
through **Launch VR**. Close the game before switching mod builds.

## Limitations

- Only one [verified executable](docs/native-integration.md) is supported.
- Menu navigation and height adjustment still need headset validation.
- Excessive forward movement when entering some puzzles remains unresolved.
- Hand-position parallax and fixed-view/mono puzzle modes are not implemented.
- Steam Frame, other controller profiles, and extended play need broader testing.
- The portable executable is unsigned.

## Development

- [Build and test](docs/development.md)
- [Architecture and protocols](docs/architecture.md)
- [Native integration](docs/native-integration.md)
- [VR presentation design](docs/design.md)
- [Packaging and releases](docs/releases.md)
