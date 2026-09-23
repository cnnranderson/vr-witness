# Native integration

## Supported executable

All addresses below belong to the Steam Windows x64 `witness64_d3d11.exe` with
SHA-256 `8d672d444df6a6df7130f25a517bdab1af92731fe2138dc5b324d519561d55a5`.
The on-disk size is 6,864,896 bytes; the mapped image size is 74,457,088 bytes.
The launcher and DLLs verify executable identity and live instructions before
using game-specific addresses. Never reuse an RVA for a different fingerprint.

The input ABI matches OpenVR v0.9.19 `IVRSystem_012` and
`IVRCompositor_013`. Preserve method slots, field widths, calling conventions,
and structure-size assertions. Contemporary OpenVR headers are not a drop-in
replacement for these historical interfaces.

## Rendering

Eye submission is deferred until the matching stereo menu/cursor pass finishes.
The pause predicate at RVA `0x1FE730` is overridden only for the render caller
returning to `0x1C8A46`, allowing a fresh per-eye world draw during pause. Other
callers, stored pause state, simulation, and menu/input behavior are preserved.
Both the predicate and caller instructions are checked before hooking.

A missing completion route drains pending work and faults the session. Disable
new deferrals before teardown; keep trampolines and the DLL pinned until process
exit. Cursor-only and trace diagnostics do not enable the pause predicate hook.

## Controller mapping

Input uses native left/right hand roles. In the tested legacy Knuckles layout,
axis types are `[1,3,3,-1,-1]` and axis 0 supplies the stick. A is bit 2, B is
bit 1, trigger is bit 33, and right-stick deflection also sets bit 32. This alias
requires suppressing the native pad/back route where manual cursor or menu
navigation owns the input. Do not infer physical labels from OpenVR bit names.

## Pause-menu navigation

Either stick selects the dominant direction after centering. The first pulse is
immediate; holding repeats after 350 ms, then every 150 ms. Up/down selects menu
items; left/right adjusts the current item. Left B toggles pause/settings and
right B sends native back in puzzles or menus. Confirm stays native.

The existing input-poll hook calls the verified native key setter (RVA 364210)
with a press/release pair, so no synthetic key remains held after a callback.
There is no Windows keyboard injection or additional hook. Focus, tracking,
runtime capture, session enable, deadline and fully open menu state gate input.
Stopping or losing an allowed context requires centering again.

Static evidence and live signature guards:

- The key-name switch at RVA 1FA2CE uses table 1FA914. Keys 13F/140/141/142
  resolve to handlers 1FA5F0/1FA5DC/1FA5B4/1FA5C8, named D-pad up/down/left/right.
- Menu selection at 1F9A83 passes 13F/140 to the native repeat helper 1FF070;
  horizontal adjustment at 1FB327 passes 141/142 to the same helper.
- The menu gate at 1FC852 checks fade 61E994 >= 1, transition flag 61E99C,
  state 630010 >= 4 and current menu pointer 6300E0. The input context mirrors
  these checks without changing native state.
- The native pause key 13D is emitted at 37AC12 (return 37AC17). Only this
  caller is consumed by the B swap; left B supplies a fresh press afterward.
- The legacy right-stick pad/back alias (key 136, return 37AC55) is suppressed
  in menus as well as puzzles. Right B supplies the replacement back edge.

Owned-process tests exercise both hands in all four directions, B hold/rearm,
pause/resume events, unrelated key callers, and complete directional releases.
Math checks cover the repeat interval, dominant axis, device changes and
context rearming. These checks do not establish headset usability.

## Resting height

The eye-position helper at RVA 240FD0 returns a Vec3 pointer using the x64 ABI:
output in RCX, entity in RDX. In VR it adds native tracked Z at 630528 to entity
Z at +2C. Its non-VR branch instead adds 630434 + 62D168, which supplies the
standing-height reference. Native conversion 37A090 reads pose translation
through 2E45A0 and changes axes/signs without scaling; OpenVR Y becomes native Z.
The update at 23E540 rotates around Z and retains its units.
[OpenVR matrices use meters](https://github.com/ValveSoftware/openvr/blob/master/headers/openvr_driver.h).

The detour calls the original first, then adds a fixed Z offset only when the
entity matches the current player. Player lookup mirrors 241140: ID 630470
indexes the entity table at 62D0A0, using first ID +8, count +10 and entries +18.
The helper's prologue, height references, final stores and lookup instructions
are verified against live bytes after fingerprint validation. Camera callers
247A25 and 248065 copy its result into the native eye/camera position globals.

Each game process starts with zero adjustment, ignoring the retired RestingHeight
setting. A fresh F7 press on an eligible walking frame captures the signed
standing-minus-tracked offset; invalid data rejects that press. Shift+F7 clears
it. Menus, puzzle entry and later head motion do not recalibrate. Only returned
eye coordinates change, never player position or tracking storage. Stopping
restores the native route and keeps the trampoline pinned. Calibration survives
restarting the input session in the same process. Owned-process checks cover
original forwarding, entity filtering, held keys, retained leaning, status
reporting, mode changes, F8 consumption, disabling and teardown; real-game height
and puzzle alignment remain to verify.
