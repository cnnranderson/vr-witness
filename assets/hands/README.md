# Glove hand assets

These left/right gloves supply the optional native VR hand renderer. Meshes,
weights, inverse binds, and the 26-joint rigs are embedded in the render DLL; no external
asset files or Godot installation are needed to play. Enable **Show hands** in
the launcher to use them.

The model is [Glove by Quaternius](https://poly.pizza/m/l1zv4LaA4I), listed as
public domain under CC0. The supplied Godot scenes add a mirrored 26-joint skin.
The original FBX, scenes, and accompanying notices are retained unchanged in
`source/`; its README describes the originating Godot project's workflow,
not this mod. See [the supplied license notice](source/LICENSE.md).

| Asset | Geometry | Rig | Approximate rest bounds |
| --- | --- | --- | --- |
| `left_hand.glb` | 456 triangles, 1368 face vertices | 26 joints, four weights per vertex | 13.8 x 18.4 x 6.2 cm |
| `right_hand.glb` | 456 triangles, 1368 face vertices | 26 joints, four weights per vertex | 13.8 x 18.4 x 6.2 cm |

The GLB files are self-contained glTF 2.0 assets with positions, normals,
tangents, indices, skin weights, inverse bind matrices, and a simple brown rough
material. No external textures are required. The original Godot shader and
its menu-compositing passes are not carried into this material.

Coordinates and rig rest transforms remain those of the source scenes. Palm
and wrist are separate roots; most of the rigid glove is weighted to the Palm
joint. Controller alignment must be measured for the OpenVR integration. The
Godot wrappers' eight-degree hand alignment and OpenXR tracking scripts are
not baked into these assets.

## Rebuild

Use an installed Godot 4.7.1 stable console executable:

```powershell
.\scripts\export-hand-assets.ps1 -GodotExe 'E:\Tools\Godot\Godot_v4.7.1-stable_win64_console.exe'
```

The script uses an isolated project below `out/`, starts no XR session, and
round-trips each GLB through Godot before copying it here. It checks vertex
positions, bone names/rest transforms, weight layout and normalization, and
inverse binds. `export-report.json` records source/output hashes and counts.
The GLB bytes are not promised identical across repeated exports.

After re-exporting, run `python scripts/embed-hand-meshes.py` to regenerate
`src/render/hand_mesh_data.hpp`. Use `--check` to verify the header,
counts, bounds, normals, triangle winding, and rig data. Runtime skinning uses
the original weights to close each glove while its grip button is held.

The renderer has native pose-to-eye transforms and private depth for hand
self-occlusion. These checks establish conversion and graphics behavior; hand
alignment and scale still require a headset check. See
[tracked hands](../../docs/tracked-hands.md) for rendering limits.
