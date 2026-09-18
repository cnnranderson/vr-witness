# Witness VR working rules

- Keep development in this repository. Keep compilers in `.tools/`, builds in `out/build/`, and diagnostics in `out/logs/` or `out/reports/`. These generated directories are ignored by Git.
- Prefer loading the DLL from its absolute project path. Do not replace the game's shipped OpenVR DLL or copy development dependencies into the installation. Any future minimal deployment must be reversible and documented.
- Read `docs/design.md` before changing VR or camera behavior. Fixed-view/mono puzzle modes must preserve tracked headset presentation, with a visible mode notification and manual return control. A frozen headset view is not the intended fallback.
- Distinguish static clues, compiled code, owned-process tests, actual game tests, and headset validation in every progress report. The module probe only observes modules; input/render behavior is documented separately in docs/code-guide.md.
- Bind executable-specific findings to the recorded SHA-256. Never write guessed addresses. Resolve imports dynamically where possible; verify signatures and historical OpenVR ABI before applying hooks.
- Keep `DllMain` minimal. Do not unload a module while any thread, hook, or runtime callback can still execute its code.
- Start by reading `docs/current-state.md`, then only the code/evidence needed for the next change. Historical experiment notes are references, not a required reread.
- Build with `scripts/build.ps1 -Test -TestSuite <Math|Input|Snap|Render|Lifecycle|All>`. During iteration choose the affected group; rerun only failed tests after a test-only correction. Run All once for a completed cross-feature build before hardware deployment, or when the loader, ABI, hook lifetime or shared protocol changes. A settings-only session restart needs no rebuild or full suite. Defaults run independent fixture processes in parallel (4 jobs).
- Close the game before replacing any loaded input/render DLL: it stays pinned until process exit. Keep the loaded binary intact and prepare changes in a separate repo output directory. Use process checks for loading/lifetime changes; do not rerun unrelated 30-second lifetime checks on every edit.
- Save raw captures/disassemblies under out and inspect small summaries/selected functions. Do not print whole logs or repeatedly reread whole source files. Keep current-state.md concise and update it at a handoff/deployment boundary.
- Do not commit or publish game binaries, disassemblies, saves, toolchain downloads, or personal logs. This repository has no configured remote at initial setup; do not assume it has been published to GitHub.
- Follow `.clang-format` and `pyproject.toml`; run `scripts/format.ps1 -Check` after source formatting. Do not format vendored code.
- Use ASCII punctuation in project-owned files. Keep comments concise and document units, ownership, failure behavior and shared interfaces where they are not clear from names.
