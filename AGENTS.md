# Repository working rules

- Read `docs/architecture.md` for module boundaries and lifetime contracts. Read `docs/design.md` before changing camera or VR presentation behavior.
- Keep generated builds and diagnostics under `out/`, and downloaded tools under `.tools/`. Keep the game installation unchanged; never replace its shipped OpenVR DLL.
- Bind native addresses to the supported executable fingerprint. Verify live bytes and historical OpenVR ABI before installing hooks; never guess addresses.
- Keep `DllMain` minimal. DLLs and trampolines stay pinned while callbacks or return addresses can reference them. Close the game before replacing loaded binaries; use a fresh build directory for active development.
- Preserve wire sizes, field order, versions, calling conventions, and export names. Update protocol versions only for an actual compatibility change.
- Use `scripts/build.ps1 -BuildName <name> -Test -TestSuite <suite>` for focused checks. Run All once for completed cross-feature, loader, ABI, or lifetime changes. Retain native routing and lifetime tests. Separate fixture results from actual-game and headset validation.
- Follow `.clang-format` and `pyproject.toml`; run `scripts/format.ps1 -Check` after source formatting. Do not format vendored code.
- Use descriptive names and ASCII punctuation. Document units, ownership, failure behavior, and shared contracts. Keep public docs about the product and development workflow, without machine paths, session transcripts, or assistant handoff notes.
- Do not commit game binaries, disassemblies, saves, downloaded tools, personal logs, or generated packages. Preserve existing local backups and loaded/fallback binaries when cleaning generated output.
