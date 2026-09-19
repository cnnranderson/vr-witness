# Current working state

Updated 2026-09-19. Read this before historical experiment notes.

- Repository: `C:/Users/samic/Documents/GitHub/vr-witness`; keep development here and the game install clean.
- Supported Steam x64 EXE SHA-256: `8d672d444df6a6df7130f25a517bdab1af92731fe2138dc5b324d519561d55a5`.
- Latest process check: game and launcher closed. Do not assume this remains true before replacing loaded files.
- User-confirmed baseline: native stereo/6DOF, cursor/pause repair, walking/pause gating, pointing/trigger drawing, smoothing/A recenter, snap turns, manual cursor and live speeds.
- This update adds new defaults, optional capped logging, a B swap and pause-menu stick navigation. Owned-process checks are separate from headset validation; the new controls still need a headset check.

## Current controls and settings

- Defaults: stick 20%, pointing 60%, smoothing 80 ms, snap 22.5 degrees, legacy Knuckles compatibility on, DiagnosticLogging=0.
- Left stick walks; right stick snaps while walking or moves the puzzle cursor. Right A recenters and restores pointing. Right trigger clicks/draws natively.
- Left B opens/closes the game pause/settings menu. Right B sends native back in puzzles or menus; canceling a drawn line can take a press before leaving a puzzle.
- Either stick navigates open menus: up/down selects; left/right adjusts. Center on entry; held input repeats after 350 ms, then every 150 ms. Native press/release pairs avoid stuck keys. See `docs/menu-navigation.md` for verified mapping and guards.
- F7 toggles input, F8 toggles visual fixes, F9 stops both; focus required. Center sticks and release buttons after resuming or enabling.
- Speeds reload within about one second. Launcher changes to smoothing/snap/bindings restart input; logging changes restart affected sessions while preserving disabled state.
- Logging is off by default in the launcher; enabling it caps each file at 2 MiB. No activity counters are displayed. Explicit developer captures still produce logs.
- Role-selected input is essential. Legacy layout [1,3,3,-1,-1] uses axis0; right A bit2, both B buttons bit1, trigger bit33; right-stick deflection also sets bit32. Native pad/back alias is suppressed in puzzles and menus.

## Portable release

- Native standalone GUI under `src/launcher`; Steam discovery/Browse, Launch VR, Attach, Stop, status and settings. No installer or hardcoded game path.
- Closing the launcher leaves fixes active. DLLs stay pinned until game exit; another build/version requires normal game exit. Exact EXE/live-code checks remain enabled.
- Published package: `out/releases/WitnessVR-0.1.0.zip`, downloaded from GitHub. Tag `v0.1` points to `1cc3f6d`; all nine CI checks passed, and the downloaded ZIP checksum and 12-file layout were verified. Record: `out/reports/v0.1-publication.json`. Previous local test build: `out/release-0-1-0-preview-5-716be943`; UI evidence: `out/reports/launcher-preview5`.
- Repeatable command: `scripts/release.ps1 -Version <new-version>`. Fresh build, All checks once, minimal ZIP. Build/source/hash records remain under `out/release-records`; ZIP checksum sits beside download.
- Input session protocol 6 (2192 bytes), render session protocol 2 (2104 bytes), bounded diagnostics protocol 1. Empty session log path means logging disabled; use loader `--no-log`.
- Icon: approved `assets/launcher.svg` and `.ico`, no thin border accents; embedded in EXE. Regeneration instructions in `assets/README.md`.
- Development batch/session scripts still use the retained original `out/build/bin` + `out/input-settings/bin` binaries, with the OLD B mappings. The updated portable package is the current test target; do not confuse those copies.

## Testing and housekeeping

- Focused controller injection, B swap, both-stick menu directions and math checks passed. The log append-cap test caught a seek-position issue; it was fixed and launcher checks passed.
- Keep output small. Prior cleanup removed 3438 obsolete files/659.98 MiB; plan/result remain in `out/reports`. Save/source backups and five development fallback binaries are protected. Old historical disassembly/capture paths may have been pruned.
- Follow `docs/development-workflow.md`: targeted tests while iterating; All once for completed shared-protocol/loader changes. Do not rerun unchanged checks.
- Next headset check: left B pause/resume, both sticks up/down and left/right in options, right B menu back/puzzle cancel, then confirm walking/cursor behavior after resume. No new mapping capture is needed unless behavior differs.
- Deferred: hand-position parallax, fixed/mono puzzle fallback, Steam Frame validation and extended playability. Camera constraints: `docs/design.md`.


## GitHub CI

- Remote: `https://github.com/cnnranderson/vr-witness.git`, branch `main`.
- `.github/workflows/release.yml` builds/tests/packages main pushes, PRs and manual
  runs; `v*` tag pushes also publish the tested ZIP and external checksum.
  Suffix versions become prereleases. Deleted tags are ignored.
- Uses official actions pinned to commits, a verified compiler-download cache,
  and separate read-only build / write-enabled publication jobs. Developer
  records stay in short-lived Actions artifacts.
- Local validation: actionlint 1.7.12, PowerShell parsing, seven version cases,
  mocked stable/prerelease publication and corrupt-checksum rejection passed.
  `out/reports/ci-validation/results.json` records the checks. Existing native
  build tests were not repeated for this workflow-only change.
- Published [Witness VR v0.1](https://github.com/cnnranderson/vr-witness/releases/tag/v0.1).
  Tag `v0.1` uses binary/package version `0.1.0`; release run `35473231140`
  and the matching main build passed all nine checks. ZIP and checksum are assets;
  startup instructions and remaining headset validation are in the release notes.
- Hosted runner stalls exposed intermittent parallel fixture failures. CI now
  runs fixtures serially with 120-second deadlines and retains their logs;
  local tests keep four workers and shorter deadlines. No assertions were removed.
  Both initially failing tests also passed twice locally. Release commands:
  `docs/github-releases.md`.
