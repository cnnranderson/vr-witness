# Builds and releases

## Local package

After [setting up the compiler](development.md), create a new version:

```powershell
.\scripts\release.ps1 -Version 0.1.1
```

The command builds Release in a fresh directory, runs all nine native checks,
verifies source files did not change during the build, and packages only the
tested binaries. Existing versions are never overwritten. Outputs:

- `out/releases/WitnessVR-<version>.zip`: complete portable package.
- `out/releases/WitnessVR-<version>/`: extracted runnable copy.
- `out/releases/WitnessVR-<version>.zip.sha256`: archive checksum.
- `out/release-records/WitnessVR-<version>/`: local source/build manifests,
  binary hashes, and test results.

`package.ps1` is the lower-level packaging step. It requires a matching passed
release record and unchanged tested binaries. Its explicit allowlist includes
only the launcher, loader, two runtime DLLs, clean defaults, README, and licenses.
Test hosts, tools, game files, captures, and local settings are excluded.

Compiler runtimes are linked statically; runtime imports are Windows system
DLLs. Source and binary hashes identify inputs and detect changes, but do not
make the build byte-for-byte deterministic or provide a digital signature.
Keep the matching source commit for every release.

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

To publish committed changes, choose a new version tag:

```powershell
git push origin main
git tag -a v0.1.1 -m "Witness VR 0.1.1"
git push origin v0.1.1
```

A `v*` tag triggers publication after successful tests. Two-part tags such as
`v0.1` normalize to package version `0.1.0`. A suffix such as `-preview.1`
creates a prerelease. The Release contains the ZIP and its checksum; build
records remain in a separate Actions artifact. Existing assets are not overwritten.

The build job has read-only permissions. The publication job has `contents: write`,
verifies the downloaded checksum, and requires the tag to exist. Official
actions are pinned to commits. The cached compiler archive is reverified before
extraction. Packages expire from Actions storage after 14 days and build records
after 7 days; published Release assets are separate.

CI runs process fixtures serially with 120-second deadlines. Local builds use
four workers and shorter deadlines. Inspect retained fixture logs when a run
fails. For interrupted publication, inspect any incomplete draft before retrying;
use a new version for code changes. Headset validation is separate from CI.
