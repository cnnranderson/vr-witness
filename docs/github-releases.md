# GitHub builds and releases

The `Build and release` workflow uses the same `scripts/release.ps1` command as
local releases. It installs the pinned, checksum-verified compiler, builds on
Windows x64, runs all nine owned-process checks once, and creates the minimal
portable ZIP. Neither The Witness, SteamVR nor a headset is needed on the runner.
CI does not replace testing new features in the headset.

## One-time setup

1. Commit and push the current project to `main`, including `.github`, `assets`,
   `packaging`, `src/launcher`, release scripts and third-party license files.
   These portable-launcher files must be committed along with the workflow.
   Keep `.tools` and `out` ignored; never upload game files, saves or personal logs.
2. On GitHub, open **Actions** and enable workflows if the repository asks.
3. Check the first **Build and release** run. Repository or organization policy
   must allow GitHub's official actions and the release job's `contents: write`.
   No personal access token or repository secret is required.

Pull requests and pushes to `main` build and test without publishing a release.
**Actions > Build and release > Run workflow** also builds without publishing.
Those runs provide a temporary `WitnessVR-0.0.0-ci.*` Actions artifact.

## Publish a release

Start with all intended changes committed and pushed. Choose a new version;
`v0.1.0-preview.6` is an example, not a tag created by this setup.

```powershell
git push origin main
git tag -a v0.1.0-preview.6 -m "Witness VR 0.1.0-preview.6"
git push origin v0.1.0-preview.6
```

The tag push builds that exact commit. Only after a successful build and tests
does the workflow publish a GitHub Release with generated notes and two assets:

- `WitnessVR-0.1.0-preview.6.zip`
- `WitnessVR-0.1.0-preview.6.zip.sha256`

Tags must start with `v` and contain a two- or three-part version. A short tag
such as `v0.1` publishes as **Witness VR v0.1**, with `0.1.0` used for the
Windows binary version and package filenames. A suffix such as
`-preview.6` creates a prerelease and does not replace the latest stable release.
A tag such as `v0.1.0` creates a normal release. Pushing a tag publishes
automatically; use a normal/manual build when you only want to try a package.

Download links appear on the repository's **Releases** page. Users extract the
ZIP and run `WitnessVR.exe`. Build records and source hashes stay in a separate
Actions artifact; they are not added to the user ZIP or release assets.

## Maintenance

The compiler download is cached, then reverified and extracted on every run.
Actions are pinned to full commit hashes. Package artifacts expire after 14 days;
build records expire after 7 days. GitHub Release assets are separate from this
short-lived Actions storage.

Only the publishing job has write permission. It downloads the tested package
from the same run, verifies its checksum, and requires the tag to exist. It
cannot silently create a release from the current default branch instead.
Existing releases/assets are not overwritten. For a failed run, inspect its
logs and rerun the failed jobs; if an interrupted upload left a draft release,
remove that incomplete draft first. Use a new tag for code changes.

Local releases still work with `scripts/release.ps1 -Version <version>` and do
not contact GitHub. The workflow does not upload anything from your local `out`
folder and does not depend on your Steam installation path.

References: [GitHub token permissions](https://docs.github.com/en/actions/tutorials/authenticate-with-github_token),
[GitHub release creation](https://cli.github.com/manual/gh_release_create).
