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

## Turning

The right stick turns the player only while walking. Snap mode requires neutral
between turns; smooth mode integrates deflection and high-resolution elapsed
time at the chosen speed. Both update the game's native and VR headings together before its VR
update, without changing tracked headset poses. Smooth mode also bypasses native
22.5-degree rounding in the three VR rotation consumers. That continuous heading
is retained through puzzles and menus. Each input sample is consumed once.
Menus, puzzles, focus loss and stale input block turning and require centering
before it resumes. The stick remains available for puzzle cursor and
menu input in those contexts.

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

## Optional puzzle positioning

Automatic puzzle positioning is on by default. Turning it off suppresses verified
native panel-approach requests after their setup, using the game's own completion
routine. It applies to subsequent eligible entries and linked-panel navigation;
it does not undo an existing approach or maintain a compensating camera offset.
Normal walking, tracked head movement, puzzle input, and other approach modes
retain their native paths. Disabling the input fixes also restores native approach
behavior. Re-enabling the setting takes effect when entering again.

This is an experimental preference, not a replacement for required puzzle
viewpoints. Some perspective puzzles may become harder to view or interact with
when it is off. It covers the verified common panel approach routes, not every
scripted movement or special panel update. Both directions of the setting and
panel interaction need headset validation.

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

Optional controller-driven glove hands are available through **Show hands**,
off by default. They use tracked controller position and orientation relative
to the HMD, with controller-specific palm attachment and mirrored glove bases.
Holding grip closes each glove; release reopens it. This does not change puzzle cursor aiming.
Hands hide during menus and invalid tracking. They have self-occlusion;
the unsuccessful world-occlusion experiment is hidden from the launcher in v0.4.0.
The developer-only candidate native-depth path remains available for investigation. See
[tracked hands](tracked-hands.md) for evidence and remaining headset validation.

Native fixtures verify hook routing, latches, and lifetime. They cannot establish
world scale, visual alignment, comfort, or complete puzzle playability. Validate
those in a headset, including both panel and perspective puzzles. Keep solution
spoilers out of general documentation. Steam Frame and standalone execution
have not been validated.
