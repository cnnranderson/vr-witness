# Native camera and input wiring

This investigation uses REA with Ghidra 12.1.4 against the executable fingerprint
in [native integration](native-integration.md). Evidence below is static analysis,
not an observed game or headset session. Full generated evidence belongs under
`out/reports/rea-wiring/`, outside version control.

## Findings and implementation

| Finding | Evidence and confidence | Result |
| --- | --- | --- |
| The native input poll waits for poses, then emits trigger key `135` before returning to the mod. | REA E1 observes the calls in `37AA00`; source review confirms the mod publishes its sample afterward. | Rearm inside the key callback after a gap over 100 ms or a thread change, before consuming the trigger latch. A held trigger must be released before it can activate again. |
| Pose loss resets the manual-stick latch on every poll. | Source review of `hooked_poll` and `hooked_cursor`; a native fixture exercises the actual detours. | Reset pointing and calibration on pose loss, while retaining the stick route when native headset tracking, focus, controller state, and puzzle context remain valid. |
| The translation filter combines headset and controller positions. | REA E1/E2 observe controller record writes and their consumption by `249FE0`. | Capture the first two native record positions alongside the filter output. These records are in enumeration order, not left/right role order. |
| The native record count does not prove controller tracking validity. | REA E3 observes OpenVR initialization setting the count to two. E1 checks HMD pose validity but converts controller matrices without equivalent validity checks. | Report the record count and positions as native data, not connected-device count or fresh valid poses. Stale controller contribution is a hypothesis to test. |
| The filter output participates in both body movement and eye compensation. | REA E4/E5/E6 observe the delta, movement consumer, and eye helper. | Capture player position, native eye position, and the renderer predicate as separate fields. Do not change one side of this relationship in isolation. |
| Native panel approach also has a destination-selection path. | REA E7/E8 trace `245620 -> A1490 -> A2990/A0E30`. | Preserve native destination/setup work. Optional suppression completes an eligible request after setup; native positioning remains on by default. |
| Destination and approach setup are conditional. | REA E11 selects from puzzle geometry/linked entities and current-position-dependent candidates. E12 compares directions toward the target from current and proposed positions and stores a decision at player `+F8`. | Investigate the shared native decision, not fixed transforms or an affected-puzzle list. The complete distance gate and meaning of every approach state remain unresolved. |
| The shared approach update directly changes player position. | E13 calls E14, which recomputes the destination through E11 and advances player `+24/+2C` until `+D4` is set. `+D8` separately records completion of its turn interval. | Capture approach mode, target ID, completion fields, and the view-origin selector from the same validated player entry. The optional hook completes selected requests before this later update. |

The input changes use existing detours and preserve the input protocol and export
ABI. Failed pointing poses do not relax native headset tracking, focus, runtime
capture, controller state, or neutral-rearm requirements. Pose recovery retains
manual-stick ownership until a fresh A recenter restores pointing.

The trigger check runs inside the caller-scoped key callback, before consuming
the latch. Checking only after `original_poll` would allow that callback to use
the old armed state first. Checking only before polling would miss a long
`WaitGetPoses` inside the native poll. Ordinary press/hold/release
across puzzle entry remains unchanged when polling is timely on the same thread.

## Camera data flow

Addresses below are RVAs for the supported build only.

1. `37AA00` converts the HMD matrix into `469A550` and controller matrices into
   records referenced by `469A570`, with a `38`-byte stride and position at `+10`.
2. `249FE0` copies the HMD translation when record count `469A57C` is below two.
   Otherwise it blends the HMD with the first two controller positions. Controller
   weights depend on distance from the HMD and half its native Z coordinate.
3. `23E540` rotates the HMD translation into `630520`. It separately calls the
   filter, subtracts the previous filtered position, and publishes `630550` when
   the renderer predicate and previous-sample flag permit it.
4. `246F30` adds the delta's X/Y components to the player's movement request,
   subject to the native collision/movement path.
5. `240FD0` adds rotated HMD translation to player position. When predicate
   `34C440` is true it also subtracts filtered X/Y. The predicate is
   `renderer[26] || (!renderer[27] && renderer[29])`; the flags' higher-level
   meanings are not established here.
6. Camera callers copy the eye helper result into `62D5AC`. This is a native
   camera intermediate, not a synchronized final per-eye render transform.

Changing controller positions can therefore change the filter, but this does not
by itself establish visible camera movement: native body movement and eye
compensation may cancel, and puzzle modes may use other positioning paths.
The new [capture wiring](puzzle-positioning.md) distinguishes these observations.

## Conditional puzzle approach

The reported behavior changes with starting position, distance, and viewing
direction even at the same puzzle. Static evidence supports a shared conditional
path, but does not yet establish every runtime gate:

- `A2990` computes a destination and a look target from entity geometry and
  linked entities. Some branches compare candidate distances against `62D578`;
  another follows a linked viewpoint returned by `A21B0`.
- `A0E30` computes a distance-derived value at player `+DC` and an
  angle-derived value at `+E0`. It compares current and proposed directions
  toward the look target, with mode and entity-specific branches, and can set
  player `+F8` to one. E15 confirms this field selects the origin used to
  calculate target viewing angles; it is not a movement-enable flag.
- Entry from `245620` reaches this setup through `A1490`, with the current
  player and selected panel. This is a candidate integration boundary shared
  by entries, not evidence that every puzzle uses the same branch.
- `A8200`, reached from `A8F90` in the movement update, resolves player `+F0`
  to an entity and recomputes the destination. Its common approach path writes
  player X/Y and updates ground height; `+D4` marks translation completion.
  An elapsed-time comparison sets `+D8`, and the common path finishes when
  both fields are nonzero or its timeout expires. Other modes branch elsewhere.
- `A21B0` searches for a linked entity named `attention_proxy`, supporting
  authored viewpoint selection. Its spatial query is centered on the target
  entity; it does not establish the reported player-distance trigger threshold.

The capture reports these raw integers in `native_approach`, with mode `1`
observed statically at the panel-entry call and mode `0` returning from the common
approach update. Other modes are not labeled as puzzle entry. A retained target
ID or completion flag outside its mode can be stale; no field alone proves
movement, a perspective puzzle, or the final rendered camera. Type, lookup, and
repeated state reads reject observed changes, but cannot synchronize with a frame.

The implementation target is the game's current approach request and its
application to the player/camera. No per-puzzle offset table, location-based
transform, or correction inferred from a single recorded displacement is planned.
Any presentation change must retain the viewpoint the game requires and physical
head tracking. A requested viewpoint that cannot coexist with world VR belongs
on the planned tracked puzzle screen, rather than being discarded.

## Optional approach suppression

The **Automatic puzzle positioning** setting defaults to on. Turning it off
uses an experimentally inferred integration point: call original `A1490` with
all eight arguments unchanged, then finish the verified mode-1 panel approach
through `A7690` (E17). This preserves setup at player `+258`, target selection,
`A0E30`, and the conditional panel helper. Skipping the whole setup would lose
those side effects.

Caller returns `A19B1` (E20), `A1A34` (E21), `A7838` (E22), `23D1C1` (E23), and
`2456F1` (E7) cover observed panel entry and linked-panel navigation. `245620`
alone covers linked navigation, not every initial entry. The callback also
requires the current native-VR player, panel type, mode 1, and a matching target
after setup. Default-on positioning, disabled fixes, and unrecognized contexts
preserve native behavior.

The finish routine clears approach state without directly modifying player
position, headings, input mode, or cursor state. Its two leaf helpers (E18/E19)
perform small global reads/writes described in [native integration](native-integration.md).
The higher-level meaning of those globals is unknown. Calling completion early
is a design choice derived from this static trace, not an observed guarantee of
headset comfort or puzzle playability. Its completion flags can remain zero,
and touch-specific readers and special panel paths require further validation.

Mode 2 destination movement, mode 4 scripted approaches, mode 5 special paths,
and special panel updates before `A8200`'s mode-0 return remain outside the toggle.
The implementation has no per-puzzle table and does not accumulate camera offsets.
Re-enable positioning and re-enter when an intended viewpoint is needed.

## Validation and next decision

Native fixtures cover controller-pose failure, compositor read failure, native
tracking loss, HMD disconnect, focus loss, runtime capture, neutral rearm,
pointing recovery, trigger polling gaps, waits inside native polling, and thread
changes. They establish hook
routing and latches, not headset comfort or the cause of puzzle-entry movement.

Capture both repositioning and non-repositioning entries at the same panel,
varying distance and viewing angle separately; then broaden coverage to other
panels and ordinary pointing motion. Keep sticks centered during each entry.
Compare player motion,
eye motion, filter inputs/output, and predicate changes around entry and exit.
Null fields mean unavailable or inconsistent data; they must not be treated as
zero movement. External reads remain unsynchronized even when identity checks pass.

The executable identity, trigger callback order, filter-to-camera connection,
and a guarded approach-completion route are answered. The complete cause of the
reported movement across all conditions, renderer flags' meanings, and suitability
of the optional suppression for each perspective interaction remain unresolved.
A further correction must preserve the
body/eye relationship and intended puzzle viewpoint, and avoid an offset jump
when enabling, disabling, or changing modes.

## REA evidence index

These IDs identify retained observations in the local evidence bundle. Function
names are recovered addresses, not original source names. Decompiler types and
signatures can be approximate; assembly corroborates the field accesses above.

| ID | Operation / RVA | REA Evidence ID |
| --- | --- | --- |
| E1 | `analyze_function`, `37AA00` | `ev_1452964e9b23eeb36bb04c6685141ca2af84b080cd9550a4e1e8db67d5cefd19` |
| E2 | `analyze_function`, `249FE0` | `ev_d1a017df7cded9492eab725631ee95525c923e97d243deee4032f695b6a711df` |
| E3 | `analyze_function`, `37A4F0` | `ev_eff2bc9703abfb69deddac4bd7acfd2c7242a8da5ce932ac511048b1c5990554` |
| E4 | `analyze_function`, `23E540` | `ev_f4012ebb03656e2feab5fa44a10f0e84ad69b952a2b44ee793c9b6f588c646cb` |
| E5 | `analyze_function`, `246F30` | `ev_5c7fda636cfcc8eaca3ad7813f77dd96ff9d382f71d560690fc3d9ac3ad94c41` |
| E6 | `analyze_function`, `240FD0` | `ev_f4f7402351240ae335751c5223f341b8a3f7f10c7eb131373e46eb575a285f1e` |
| E7 | `analyze_function`, `245620` | `ev_35aa9b5fe4127787de55a7b504140c6259c50641f810427b2e5ff10024879342` |
| E8 | `analyze_function`, `A1490` | `ev_283f039a4c4b8cdb3a6bf823856dec3f9e42b42e99cae1b24d8860f13cb71004` |
| E9 | `analyze_function`, `34C440` | `ev_ab1e2ac9c31e96186f68efa32f5d761b9076f6a218f38fa8e5b9476197e57e5a` |
| E10 | `analyze_function`, `241140` | `ev_679b26583944d39fd38de96fed866980868da77699712c4580e77ca7b78efc65` |
| E11 | `analyze_function`, `A2990` | `ev_c27a874017f53a59f2f1709891efed487656afb13dcfaa139b3e02dd775aaec8` |
| E12 | `analyze_function`, `A0E30` | `ev_aee581e48347c1ff197d431edf8200b4ad6fe5a6189687b795cf1407d5edcfed` |
| E13 | `analyze_function`, `A8F90` | `ev_d57ce23181c87b49a63ca12e5bb27deb8a8aa348004addea1a2dd057c804bd58` |
| E14 | `analyze_function`, `A8200` | `ev_76b73c573c56ee433d16237d61b15b0eafc370dc02bcd9e899e5a8f12f6aae20` |
| E15 | `analyze_function`, `A3FB0` | `ev_1d0d13eb0284f0b484b32ffe3869ad8494f47033cf260191249cc6dd4f0f1868` |
| E16 | `analyze_function`, `A21B0` | `ev_0d69164657ed37b9e57a9d8425a3f446992eb6b6a25463b95233dc39128a471c` |
| E17 | `analyze_function`, `A7690` | `ev_dc624753a768414a0dbf429edfe41e283fedafc25cbf8b0c026ef03002a97ebd` |
| E18 | `analyze_function`, `294F20` | `ev_a4de00a1c11e54d0dfe866576735481f4f7041c457acb292c53dd73a93f3997b` |
| E19 | `analyze_function`, `29A760` | `ev_2ed2dfdf97727f4f4aefd9178b6f25737dff5b691136b7a7fe5526b206526089` |
| E20 | `analyze_function`, `A1950` | `ev_eb8a60e550216008ce308d75f1dfa0f4e0ae9fbde2c425da19eff404d50077b1` |
| E21 | `analyze_function`, `A19D0` | `ev_3eff7f458ff82fdb8f5e3d43e8e0c7e28dfdcec7b1714633ac426627082b6d90` |
| E22 | `analyze_function`, `A7700` | `ev_dda56d9f24a8cc94d52551f1463f7ed48b87c869e59934193af2da346df7ccfa` |
| E23 | `analyze_function`, `23CFF0` | `ev_9e51b87b5534dddb806c9500c8be5601d18108e3089dfee2ad363325eaf8fca7` |
