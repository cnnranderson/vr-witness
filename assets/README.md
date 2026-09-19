# Launcher icon

`launcher.svg` redraws the maze and completed solution from the Witness puzzle
screenshot supplied by the user. The approved design retains the golden panel
and charcoal gradient border, with both thin highlight accents removed.
The path starts at the lower-left circle and exits at the upper right.
The puzzle layout comes from The Witness; the SVG is a vector redrawing.

`launcher.ico` contains 16, 20, 24, 32, 40, 48, 64, 96, 128 and 256 pixel frames.
It is embedded in the launcher executable and used for its window/taskbar icon.
Both files are source assets; ordinary builds use the ICO without image tools.

After editing the SVG, regenerate the ICO with Node.js and Sharp 0.35.4:

```powershell
npm install --prefix .tools/icon-tools --no-save --package-lock=false sharp@0.35.4
node scripts/build-icon.cjs
```

The script also writes a preview to `out/icon-review/launcher-icon.png`.
Rebuild with the normal release command afterward; no extra release step is
needed when the icon has not changed.
