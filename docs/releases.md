# Builds and releases

## Local package

After [setting up the compiler](development.md), create a new version:

```powershell
.\scripts\release.ps1 -Version 0.4.0
```

The command builds Release in a fresh directory, runs all native checks,
and packages the tested build. Finish editing before running it. Existing
versions are never overwritten. Outputs:

- `out/releases/WitnessVR-<version>.zip`: complete portable package.
- `out/releases/WitnessVR-<version>/`: extracted runnable copy.
- `out/release-records/WitnessVR-<version>/`: local tool versions and test results.

`package.ps1` is the lower-level packaging step. It requires a matching passed
release record. Its explicit allowlist includes
only the launcher, loader, two runtime DLLs, clean defaults, README, and licenses.
Test hosts, tools, game files, captures, and local settings are excluded.

Compiler runtimes are linked statically; runtime imports are Windows system
DLLs. Packages are unsigned and are not guaranteed byte-for-byte deterministic.
Keep the matching source commit for every release. No separate checksum files
or source hash manifests are generated.

## Package layout

```text
WitnessVR.exe
runtime/
  witness-probe-loader.exe
  witness-input-probe.dll
  witness-render-probe.dll
config/input.ini
README.txt
licenses/
```

The launcher creates `config/launcher.ini` for the chosen installation and
optional `logs` beside itself. Distribute the original ZIP, not a used folder.
Updating the package requires closing the game because loaded DLLs stay pinned.

## GitHub Actions

`.github/workflows/release.yml` runs the same release command on Windows.
Pushes to `main`, pull requests, and manual runs build and test without
publishing. Their portable ZIPs and build records are temporary Actions artifacts.
Neither the game nor a headset is needed on the runner.

Before tagging, add `docs/releases/<tag>.md` with concise **Additions**,
**Changes**, and **Bugfixes** sections, update instructions, known limitations, and a full changelog
link. The [v0.4 notes](releases/v0.4.md) show the format. Commit the notes with
the changes, then publish a new version tag:

```powershell
git push origin main
git tag -a v0.4 -m "The Witness VR v0.4"
git push origin v0.4
```

A `v*` tag triggers publication after successful tests. Two-part tags such as
`v0.3` normalize to package version `0.3.0`. A suffix such as `-preview.1`
creates a prerelease. The Release contains the ZIP; build
records remain in a separate Actions artifact. The matching notes file becomes
the Release description; publication requires it. Existing assets are not overwritten.

The build job has read-only permissions. The publication job has `contents: write`
and requires the tag to exist. Official
actions are pinned to commits. The cached compiler archive is reverified before
extraction. Packages expire from Actions storage after 14 days and build records
after 7 days; published Release assets are separate.

CI runs process fixtures serially with 120-second deadlines. Local builds use
four workers and shorter deadlines. Inspect retained fixture logs when a run
fails. For interrupted publication, inspect any incomplete draft before retrying;
use a new version for code changes. Headset validation is separate from CI.
