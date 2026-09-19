# Portable launcher

## User flow

Extract the complete ZIP to a writable folder and run `WitnessVR.exe`. Steam's registry entry and library manifests are read to find the game; Browse can select either Witness executable when detection fails. No installation path is embedded in default settings.

Launch VR starts SteamVR if needed, waits for its process before launching the game with `-vr`, and waits up to five minutes for the verified native VR interfaces. It then enables the existing render and input sessions. Attach follows the same checks for a running game. No mod files are copied into the game installation.

Status refreshes about every three seconds from process inspection and the existing loader's session responses. DLL presence alone is not reported as active. Startup, disabled controls, stopped/pinned DLLs, faults and uncertain loader outcomes are distinct states. A timed-out loader is not retried automatically. Another loaded mod copy requires a normal game restart.

Close leaves the fixes active. Stop fixes stops both sessions without ending the game or unloading pinned code. Small windows can scroll vertically to reach all controls.

## Settings

`config/input.ini` stores both speeds, smoothing, snap step count legacy binding mode, and DiagnosticLogging (default 0). Speeds reload on the DLL worker. Smoothing, snap and binding changes restart input; changing logging restarts affected sessions. Both preserve disabled session states. Defaults are 20/60 percent, 80 ms, 22.5 degrees, and the tested Knuckles legacy binding. `config/launcher.ini` stores the selected game folder and is created only locally.

Packaged DLLs resolve config relative to the parent `WitnessVR.exe`; development and test-host paths remain supported. Logging is off by default. When enabled in Settings, logs go under `logs` beside the runner, with a 2 MiB cap per file. The Status tab reports state without activity counters. Used folders can contain local paths; distribute the clean ZIP.

For automatic GitHub releases, see [GitHub builds and releases](github-releases.md).

## Releasing a future fix

From the repository root, run one command with a new version:

```powershell
.\scripts\release.ps1 -Version 0.1.1
```

On a new development machine, install CMake 3.25+ and run
`.\scripts\setup-toolchain.ps1` once to get the checksum-verified compiler.
The release command also finds CMake in its standard Program Files location.
Recipients need none of these build tools.

The command builds Release in a fresh `out/release-<version>-<id>` folder,
runs all nine CTest checks once, and packages only after they pass. It stops
if source changes during the build. Version numbers are embedded in the
launcher's Windows file properties and the packaged README.

Outputs are under `out/releases`:

- `WitnessVR-<version>.zip`: distribute this complete ZIP.
- `WitnessVR-<version>/WitnessVR.exe`: run this for a local check.
- `WitnessVR-<version>.zip.sha256`: checksum for the ZIP.

The ZIP contains only the launcher, runtime binaries, clean default settings,
README and runtime licenses. Developer records stay under
`out/release-records/WitnessVR-<version>`: `build-info.json`, `source-hashes.txt`,
per-file `SHA256SUMS.txt` and the test log. The optional ZIP checksum sits beside
the download. These hashes detect changes when compared with a trusted copy;
they are not signatures or antivirus clearance.

Versions are never overwritten. Changing to a new package requires closing the
game normally first; loaded DLLs remain pinned.

This makes the release process repeatable, not byte-for-byte deterministic.
PE/ZIP timestamps and different tool versions can change output hashes. Keep
the matching source revision and ZIP for each release; hashes identify source
inputs but do not replace a source backup or Git commit.

`scripts/package.ps1` is the lower-level packaging step. It requires a matching
release record and unchanged tested binaries, uses an explicit file allowlist,
and strips only copied binaries. It excludes test hosts, compilers, captures,
developer settings and game files. The required runtime files are the launcher
EXE, loader EXE, input DLL and render DLL; their imports are Windows system DLLs.

The project uses exact game-hash and live-instruction guards; path discovery
does not relax compatibility. The GUI uses a worker thread for process and
loader operations so its message loop remains responsive.

## Validation

Release `0.1.0-preview.2` passed the complete one-command pipeline, including all nine CTest checks (39.49 seconds). New coverage includes command-line quoting, secondary Steam libraries, settings persistence and invalid-value rejection, status parsing, and development/portable config paths. UI smoke checks exercise saving settings and capture only the app's own Status and Settings tabs. The ZIP was extracted to a different folder containing spaces. All 15 files were accounted for, checksums and clean defaults passed, and the relocated runner saved settings and rendered both tabs correctly. Runtime imports were checked as Windows-only. The same-version overwrite guard was also exercised.

The mod's prior headset behavior is user-confirmed. The portable launch/attach flow and settings-driven input restart still need a headset session. This unsigned preview does not claim antivirus clearance, arbitrary game-build support, Steam Frame validation or fixed/mono puzzle fallback. The added controller menu navigation still needs headset validation.

Release `0.1.0-preview.3` adds the approved launcher icon. All nine checks passed in 39.44 seconds, and the packaged EXE icon resource and relocated GUI were verified.

## Icon assets

The approved puzzle icon is embedded in the EXE and used for its window and
taskbar. `assets/launcher.svg` is the editable source; `assets/launcher.ico` is
checked in for builds. See `assets/README.md` only when changing the artwork.
Normal release builds need no Node.js or image conversion step.

Release `0.1.0-preview.4` moves developer manifests out of the ZIP and labels
activity as eye submissions/input polls. All nine checks passed in 39.18 seconds;
the 12-file package and external hashes passed validation. The previous portable
copy remains running for the user, so its GUI session was not interrupted for
another UI smoke check. Old scratch captures/builds referenced above were pruned
after their results were recorded.


Release `0.1.0-preview.5` removes status activity counters, adds optional logging
(default off, capped at 2 MiB per file), uses the requested 20/60%, 80 ms,
22.5-degree defaults, swaps B, and adds both-stick pause-menu navigation.
All nine checks passed in 40.01 seconds. The packaged UI passed save/reset and
visual review, with no logs created by default. Native menu mappings and
remaining headset checks are in `menu-navigation.md`.
