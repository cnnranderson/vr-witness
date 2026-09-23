THE WITNESS VR - PORTABLE @VERSION@

QUICK START
Requires Windows 10/11 x64, Steam, SteamVR, a PC VR headset, and the supported
Steam Windows build of The Witness. Knuckles controllers are supported.

1. Extract the complete ZIP into a writable folder.
2. Connect the headset and both controllers.
3. Run WitnessVR.exe. Check the detected installation, or select Browse.
4. Select Launch VR and wait for both session indicators to show Active.

No installer, administrator access, or build tools are required.
The game installation stays unchanged.

CONTROLS
Left stick: walk.
Right stick: snap turn while walking; move the puzzle cursor.
Right trigger: activate, click, and draw.
Right A: recenter pointing and switch from stick cursor to pointing.
Left B: toggle the pause/settings menu.
Right B: cancel a line, leave a puzzle, or go back in menus.
Either stick in menus: up/down selects; left/right adjusts; hold to repeat.
F7: calibrate height while walking. Shift+F7: restore game default height.
F8: toggle both fixes while attached.

Keep the game focused. Center sticks and release buttons after resuming.
Release the trigger before recentering. Canceling a line and leaving a panel
may require separate B presses.

SETTINGS
Change values in Settings, then select Save settings.
Defaults: stick speed 20%, pointing speed 60%, smoothing 80 ms, snap 22.5 degrees.
Controller type: Knuckles. Xbox 360 and Steam Frame are unavailable.
Speeds update within about one second. Other changes restart affected fixes.
Settings are stored in config beside the launcher.
Debug logging is off by default; each log is capped at 2 MiB.

Each game launch uses its native default height. Outside puzzles and menus,
press F7 in your resting posture to calibrate to the game's standing eye height.
Leaning and crouching remain tracked. Shift+F7 removes the adjustment.
Configured height shows the target and signed offset in meters, or Game default.
Calibration is not saved between game launches.

SESSIONS AND UPDATES
Attach to Game supports a game already started with -vr.
Detach from game stops the fixes. Closing the launcher leaves them running.
DLLs remain loaded until the game exits. Close the game and launcher before
updating or moving the mod. Share the original ZIP, not a used folder.
To remove the mod, close the game and delete the extracted folder.

TROUBLESHOOTING AND LIMITATIONS
If VR is not ready, check SteamVR and wake the headset. Restart a non-VR game
through Launch VR. Errors appear in dialogs. For diagnostics, enable Debug
logging, reproduce the issue, and select Open logs.
Only one verified game executable is supported. Menu navigation and height
adjustment need headset validation. Forward movement on puzzle entry remains
unresolved. Steam Frame and extended play need broader testing.
The executable is unsigned.

PACKAGE
WitnessVR.exe       Launcher
runtime/            Loader and two mod DLLs
config/input.ini    Default settings
licenses/           Third-party notices

The launcher creates config/launcher.ini and optional logs locally.
The ZIP checksum beside the download is optional; build records are not required.
