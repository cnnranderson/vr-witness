WITNESS VR - PORTABLE @VERSION@

QUICK START
1. Extract the entire ZIP into a folder you can write to.
2. Connect your PC VR headset and turn on both controllers.
3. Run WitnessVR.exe. No installer or administrator access is required.
4. Check the detected game folder, or use Browse to select a Witness executable.
5. Select Launch VR. Keep the headset awake while native VR starts.
6. Wait for Headset visuals and Controller input to show Attached and active.

Requires Windows 10/11 x64, Steam, SteamVR, and your own supported Steam Windows
copy of The Witness. Python, PowerShell and build tools are not required.
Steam Frame has not yet been validated. The tested controllers are Knuckles.

STATUS AND CONTROL
The launcher remains open while you play and refreshes status about every three
seconds. Loaded, active, disabled and faulted states are shown separately.
Attach to running game works only when that game was launched with -vr.
Stop fixes leaves the game running. Closing the launcher leaves the fixes active.
No files are installed into the game directory.

A DLL stays loaded until the game exits, even after Stop fixes. To switch mod
versions or move this package, close the game normally first. If another mod
build is loaded, the launcher asks for a restart instead of replacing it.

SETTINGS
The Settings tab controls stick cursor speed, pointing speed, smoothing, snap
angle, Knuckles legacy binding compatibility, and diagnostic logging. Click Save settings to apply.
Cursor speeds update within about one second. Changing smoothing, snap angle
or binding mode briefly restarts controller input; center the sticks afterward.
Settings are stored under config beside the launcher, including the selected
game location. Defaults contain no machine-specific installation path.
Defaults: stick 20%, pointing 60%, smoothing 80 ms, snap 22.5 degrees, legacy on.
Diagnostic logging is off by default. When enabled, each log stops at 2 MiB.
Changing logging briefly restarts affected VR fixes; release the controls.

CONTROLS
Left stick: walk. Right stick: snap turn while walking; cursor in puzzle mode.
Right trigger: activate/click/draw. Right A: recenter and return to pointing.
Left B: open/close the pause/settings menu. Right B: puzzle cancel or menu back.
F7: toggle controller features. F8: toggle visual fixes. F9: stop both.
Keep the game focused. Release buttons and center sticks after resuming.
In menus, either stick navigates up/down or adjusts left/right; hold to repeat.
Center sticks after opening a menu. The new menu controls need headset testing.

TROUBLESHOOTING
If VR is not ready, check SteamVR and wake the headset. A non-VR game must be
closed normally and launched again. An unsupported executable is refused;
this preview uses exact executable and live-code checks.
For diagnostics, enable Diagnostic logging in Settings, save, then use Open logs. Logs can contain local file paths;
review them before sharing. The distributed ZIP contains no personal logs.
This is an unsigned experimental mod; antivirus may flag its loader. Verify
where the package came from and report false detections for vendor review.
Do not disable antivirus protection to run it.

KNOWN LIMITS
Menu navigation and the swapped B buttons still need a headset check.
Hand-position parallax and fixed-view/mono puzzle fallback are not implemented.
Extended playability and Steam Frame testing are pending.
The new launcher has automated/UI checks; final headset validation is separate.

PACKAGE
WitnessVR.exe       Portable launcher
runtime/            Loader and two mod DLLs
config/input.ini    Default settings
licenses/           Third-party notices

An optional ZIP checksum is provided beside the download. Build records are
kept by the developer and are not needed to run the launcher.

The launcher creates config/launcher.ini and logs locally as needed. To share a
clean copy, share the original ZIP, not a used folder containing your settings
and logs. Closing the game and deleting this extracted folder removes the mod.
