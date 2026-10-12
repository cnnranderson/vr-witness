# Tracked low-poly gloves

The supplied `glove-hand-right.fbx` is [Glove by Quaternius](https://poly.pizza/m/l1zv4LaA4I),
released as CC0. It is preserved unchanged. The source has 230 geometric vertices
and no skeleton; Godot triangulates its faces into 456 triangles.

`left_hand.tscn` and `right_hand.tscn` contain that same mesh, with an authored
26-joint skin and mirrored winding. The broad palm, finger webbing, cuff, face
normals and original topology are retained. No replacement finger geometry or
runtime mesh generator is used. The model is approximately 18 cm long, including
the cuff. Its palm is about 18% shorter and cuff 45% shorter than the initial
source-scale rig; the finger sections retain their original length.

## Rebuild

Import the source FBX once, then run Godot 4.7.1 from the repository root:

```text
godot --headless --editor --path . --import
godot --headless --xr-mode off --path . --script tools/build_glove_hands.gd
```

The builder contains the joint landmarks and skinning bands in the source mesh's
coordinate system. It bakes the two reusable Godot scenes. Blender is not needed
to rebuild or run the game. The retired procedural hand generator has been removed.

## Tracking and interaction

The wrappers in `scenes/xr/` attach Godot's `XRHandModifier3D`. All five fingers
use controller-supplied or optical OpenXR joint rotations. Authored bone lengths
are preserved, and finger knuckles remain anchored to the palm, so tracked hand
sizes or metacarpal offsets cannot pull the fingers apart. The rigid body and
knuckle anchors follow the Palm joint, not the runtime wrist frame; the wrist
joint is present but unweighted. The thumb has a blended
web for opposition. The whole gloves turn inward by eight degrees, mirrored on
the two hands; adjust `alignment_degrees` on their wrapper scenes to tune fit.

Poke gestures use tracked finger extension, but their contact position and direction
come from the evaluated glove's index fingertip. This keeps contact on the visible
finger even when the runtime reports a different physical hand size. Invalid joint
poses disable poking; missing tracking hides the glove. Existing controller rays
and input remain available independently.

The shared brown shader uses the actual posed face normal and writes depth while
compositing after spatial menus. Depth writes prevent curled fingers from drawing
through the back of the palm. Hands still occlude menus only when closer to the
viewer, and pointer contacts remain visible.

## Validation

`tests/xr_hands_smoke.gd` runs synthetic tracking without initializing the headset.
It checks both hands open, curled, pointing, spread, and with an opposed thumb;
source topology, inverse binds, normalized weights, fixed knuckle roots, triangle
orientation, tracking loss/recovery, inward alignment, an 80-degree wrist-frame
mismatch and fingertip contact.
Independent Godot demo reference rigs in `tools/hand_rigs/` exercise different
anatomy at 70% and 140% scale. They are test fixtures, not the glove's runtime rig.

Rendered checks reverse triangle submission order to catch self-occlusion bugs.
The old depth-prepass shader fails this regression. `tests/xr_overlay_smoke.gd`
checks hand/menu depth and pointer visibility with the shared material.

Validated with Godot 4.7.1 / RTX 4090: 103 hand checks passed with both
Forward Mobile/Vulkan and Compatibility/OpenGL. All 3 overlay checks passed. Captures and logs are in `.runtime/xr-hands-*.png`,
`.runtime/glove-hands.log` and `.runtime/glove-overlay.log`. Physical Vive/Index
controller fit and comfort still require a headset check.
