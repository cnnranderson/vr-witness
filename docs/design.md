# VR presentation design

## Current implementation

The mod reuses The Witness's native D3D11/OpenVR `-vr` path. The game owns stereo
rendering, headset tracking, simulation, and runtime initialization. The mod
repairs submission timing and paused world redraw, then adds controller input
through verified native routes. It does not create a second VR session or
replace the shipped runtime DLL.

Head rotation and position remain tracked. Controller pointing uses the eye
origin and hand orientation; hand-position parallax is not implemented.
Input is gated by focus, tracking, game mode, menus, and session state. Centering
and button release are required after context changes to prevent stale input.

## Resting height

Every game process starts with the native eye position unchanged. On a fresh F7
press, capture the difference between native tracked height and the game's
standing eye height while focused, tracked and walking outside a menu. Add that
fixed signed offset to subsequent native player eye-position results. Preserve
later leaning and crouching; holding F7 does not continuously normalize height.
Shift+F7 removes the offset. After invalid contexts, require key release before
another calibration.

Do not write headset poses, player-body position, world origin or saved state.
Retain calibration through menus, focus loss and input session restarts in the
same process. Turning fixes off suspends the offset until re-enabled. Never save
calibration or restore it in a new game process. The launcher reports the target
height and offset in meters, with an inactive indication when suspended.
Native fixture coverage cannot verify world scale or stereo alignment; this
feature needs a headset check. It does not correct puzzle-entry repositioning.

## Planned puzzle presentation

Some perspective puzzles may need a fixed camera or a mono view. These modes
are design requirements, not implemented features:

1. **World VR:** the native stereo world remains head tracked.
2. **Fixed-view puzzle screen:** render the puzzle from a fixed game camera onto
   a virtual screen in a tracked VR environment.
3. **Mono puzzle screen:** show one game-camera view on that screen when stereo
   views disagree on a perspective-dependent solution.

Keep the game camera distinct from the tracked headset view. Freezing the
headset image is not a fallback. Each mode needs an in-headset indicator and a
manual return control. Preserve locomotion position and puzzle input state
across transitions, and restore the tracking origin deliberately.

Pause, menus, loading, focus loss, tracking loss, and runtime shutdown must not
leave a stale full-field image attached to the viewer's head. Any future
presentation work must preserve native simulation timing and render correct
per-eye views; a final swapchain Present hook alone cannot supply stereo.

## Validation boundary

Native fixtures verify hook routing, latches, and lifetime. They cannot establish
world scale, visual alignment, comfort, or complete puzzle playability. Validate
those in a headset, including both panel and perspective puzzles. Keep solution
spoilers out of general documentation. Steam Frame and standalone execution
have not been validated.
