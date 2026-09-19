# Witness VR

A Windows x64 mod for The Witness's native VR mode. It repairs headset cursor
and pause rendering, and adds tracked-controller movement, pointing, puzzle
input, snap turning, and menu navigation.

## Getting started

Download the portable ZIP from [Releases](https://github.com/cnnranderson/vr-witness/releases).
Requires Windows 10/11 x64, Steam, SteamVR, a PC VR headset, and the supported
Steam Windows version of The Witness. Knuckles controllers are the tested configuration.

1. Extract the complete ZIP to a writable folder.
2. Connect the headset and turn on both controllers.
3. Run `WitnessVR.exe`. Check the detected game location, or use **Browse**.
4. Select **Launch VR** and keep the headset awake.
5. Wait for **Headset visuals** and **Controller input** to show **Attached and active**.

No installer, administrator access, Python, or build tools are needed.
The launcher reads Steam libraries to find the game and does not modify the
game installation. **Attach** supports a game already started with `-vr`.

Closing the launcher leaves the game and fixes running. **Stop fixes** stops
the mod sessions. Close the game before moving, updating, or deleting the mod
folder; its DLLs stay loaded until the game exits.

## Controls

| Control | Action |
| --- | --- |
| Left stick | Walk |
| Right stick | Snap turn while walking; move the puzzle cursor |
| Right trigger | Activate, click, and draw |
| Right A | Recenter and return from stick cursor to pointing; release the trigger first |
| Left B | Open or close the pause/settings menu |
| Right B | Cancel a puzzle line, leave the puzzle, or go back in menus |
| Either stick in menus | Up/down selects; left/right adjusts; hold to repeat |
| F7 / F8 / F9 | Toggle controls / toggle visuals / stop both fixes |

Keep the game focused. Center the sticks and release buttons after enabling
controls or returning from a menu. Canceling a line and leaving its panel can
require separate B presses. Legacy Knuckles bindings may alias trackpad and stick input.

## Settings

Use the launcher's **Settings** tab and select **Save settings**.

| Setting | Default | Range |
| --- | --- | --- |
| Stick cursor speed | 20% | 10-300% of view width per second |
| Pointing cursor speed | 60% | 10-300% of view width per second |
| Pointing smoothing | 80 ms | 0-250 ms |
| Snap turning | 22.5 degrees | Off, 22.5, 45, or 90 degrees |
| Knuckles legacy compatibility | On | On/off |
| Diagnostic logging | Off | On/off; 2 MiB limit per file |

Speeds update live within about one second. Other changes restart the affected
sessions and preserve their enabled/disabled state. Settings live in `config`
beside the launcher. For troubleshooting, enable logging, reproduce the issue,
and use **Open logs**. Review local paths before sharing logs.

## Compatibility and limitations

The mod accepts one verified game executable and checks live code before
installing hooks. Other releases or modified executables may be rejected.
The supported fingerprint is documented in [native integration](docs/native-integration.md).

Headset testing has confirmed stereo/6DOF, cursor and pause repair, walking,
pointing, drawing, recentering, and snap turning on the development setup.
The B-button layout and menu navigation have automated fixture coverage;
their headset validation is still pending. Other PCs, controller profiles,
Steam Frame, and extended play sessions need broader testing.

Hand-position parallax and fixed-view/mono puzzle presentation are not
implemented. The portable build is unsigned.

## Development

The source is organized by responsibility under `src`: `launcher`, `loader`,
`input`, `render`, `game`, `common`, and `diagnostics`.

- [Development](docs/development.md): build, test, formatting, and diagnostic commands.
- [Architecture](docs/architecture.md): module boundaries, protocols, threading, and lifetime.
- [Native integration](docs/native-integration.md): supported build, OpenVR ABI, and guarded native routes.
- [Design](docs/design.md): tracking, presentation, and planned puzzle modes.
- [Releases](docs/releases.md): local packaging and GitHub publication.
