# Native pause-menu navigation

Implemented for the supported Steam x64 EXE SHA-256
`8d672d444df6a6df7130f25a517bdab1af92731fe2138dc5b324d519561d55a5`.
Headset validation of these new controls remains pending.

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
