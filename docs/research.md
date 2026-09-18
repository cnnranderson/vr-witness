# Initial binary findings

Inspection date: 2026-09-18 UTC. These findings are specific to the local Steam build, not a cross-version patch specification.

| Item | Value |
| --- | --- |
| Steam app | 210970 |
| Installed Steam build ID | 11370412 |
| Executable | `witness64_d3d11.exe` |
| Architecture | PE AMD64 (`0x8664`) |
| Executable SHA-256 | `8d672d444df6a6df7130f25a517bdab1af92731fe2138dc5b324d519561d55a5` |
| Bundled OpenVR DLL SHA-256 | `0ea5be6651bb42e74a2869636685a4b990452c30e05290ffcd96bdb6ed0b42ff` |
| Preferred image base | `0x140000000` (runtime base must be discovered; ASLR applies) |

The executable imports `D3D11CreateDevice` from `d3d11.dll`, and `CreateDXGIFactory` / `CreateDXGIFactory1` from `dxgi.dll`.

## OpenVR import address table

These are **IAT slot RVAs**, not function implementations or camera addresses.

| Function | IAT RVA |
| --- | --- |
| `VR_InitInternal` | `0x512a40` |
| `VR_GetInitToken` | `0x512a48` |
| `VR_IsInterfaceVersionValid` | `0x512a50` |
| `VR_GetGenericInterface` | `0x512a58` |
| `VR_ShutdownInternal` | `0x512a60` |
| `VR_GetVRInitErrorAsEnglishDescription` | `0x512a68` |
| `VR_IsHmdPresent` | `0x512a70` |

The image contains version strings including `IVRSystem_012` (string RVA `0x5438f0`) and `IVRCompositor_013` (string RVA `0x5438c8`). Static disassembly shows references to these strings immediately before calls through `VR_GetGenericInterface`. These are stronger leads than unreferenced strings, but are not evidence of execution in a current VR session.

Static investigation landmarks (RVAs, same executable hash only):

- `0x3799f4`: call through the `VR_InitInternal` IAT slot.
- `0x37a3a4`: call through the `VR_IsHmdPresent` IAT slot, followed by a conditional path containing initialization at `0x37a3c7`.
- `0x3797db`: `VR_GetGenericInterface` call after loading the compositor version string.
- `0x37993a`: `VR_GetGenericInterface` call after loading the system version string.

These callsites have not been instrumented or patched. Future hooks should resolve by module/import name where possible and validate any executable-specific signature against the actual code before applying changes.

## Camera clues

The image contains `player_camera_forward`, `player_camera_left`, `player_camera_up`, `free_camera_speed`, `vr_fov_horizontal`, `teleport_in_vr_enabled`, and `updateCamera`. These are string locations, not verified camera fields, callable functions, or supported configuration options. The binary also contains Oculus tracking/frame-submission function names. No camera pointer or matrix address is known yet.

Follow-up after the first live attach:

- The initialization path at RVA `0x37a3a0` calls `VR_IsHmdPresent` and returns immediately when it reports false. It does not reach `VR_InitInternal` on that branch.
- Graphics initialization tests a flag bit `0x10` before attempting VR. It first tries another path associated with Oculus code; if that fails, the OpenVR initialization call at `0x36197f` is reached. The command-line connection to this flag bit still needs tracing; no flag patch has been applied.
- A successful OpenVR system result is stored at global RVA `0x469ab38`; the compositor helper result is stored at `0x469ab40`; the init-token cache is at `0x469ab78`. `tools/runtime_state.py` reads these only after matching the executable and validating the corresponding live instructions against disk.
- The registration routine at `0x2a3cd0` receives the settings object from the global pointer at RVA `0x61c1a0`. Its registration calls associate offsets `0xc0`, `0xc4`, and `0xc8` with `player_camera_forward`, `player_camera_left`, and `player_camera_up`.
- In the observed desktop process, that settings pointer was `0x14062d0a0` and the scalars were approximately `0.20`, `0.05`, and `1.69`. These are scalar configuration values, not a measured headset/camera pose. They have not been changed.
- Static references to the up scalar include collision/position-related code as well as the helper ending at RVA `0x24109f`. Treat changing a shared scalar as potentially affecting more than rendering. Prefer a verified view transform hook for final head tracking.

The first successful injected capture is local log `probe-20260917-230842-6856-a15d1a44.jsonl`: 106 modules over 5.001 seconds, no VR client observed, successful finish and loader-reported unload. A read-only snapshot of the same process found null system/compositor pointers and zero init-token cache. These values describe that moment; they do not prove startup initialization was never attempted.

Reproduce the JSON inventory with `python tools/inspect_game.py`. The local report is `out/reports/game-inspection.json`. A local disassembly used to inspect references is `out/reports/witness64-disassembly.txt`; it is intentionally ignored by Git and must not be published. The inspection script uses Python's standard library and never executes the game or its DLLs.

## Native VR and cursor investigation (2026-09-18)

`-vr` reached working native stereo/head tracking on the connected headset. The user verified rotation and leaning, then clarified keyboard/mouse movement works. Mouse puzzle input works, with a cursor only on the monitor. Knuckles input remains a separate issue. Do not infer support on Steam Frame until tested there.

The historical [OpenVR v0.9.19 header](https://github.com/ValveSoftware/openvr/blob/v0.9.19/headers/openvr.h) matches the executable's `IVRSystem_012` and `IVRCompositor_013` strings. Its compositor slot 2 (`+0x10` on x64) is `WaitGetPoses`; slot 5 (`+0x28`) is `Submit`. A reference copy is kept under ignored `.tools/reference/`, SHA-256 `b62312cec0cf270e265f9c973646c0d222bb2cfa429ead6d33a5e9fc9fa50609`.

Static landmarks for this same executable hash:

- `0x37aa00` obtains poses through compositor slot 2. The matrix conversion writes four orientation components at `0x469a540`, three position components at `0x469a550`, and a validity byte at `0x469a55c`.
- It enumerates device classes in index order. Controller poses go into two preallocated 0x38-byte records rooted at `0x469a570`. This array length is not a connected-device count. Only the first enumerated controller's button/axis data is decoded. It reads application-menu bit 1, touchpad bit 32, trigger bit 33, and legacy axis 0; it does not consult hand roles or a modern action manifest in this routine. This is a lead for Knuckles support, not a tested binding fix.
- `0x1cd770` draws cursor/reticle UI, including a separate stereo path. The object pointer is at `0x62d4d0`; screen coordinates at object offset `0x18`; ray origin at `0x9c`; projection-plane origin/basis at `0xa8/0xb4/0xc0`. Live telemetry labels raw flags/scalars conservatively; it is not a supported engine API.
- In the main eye loop, callsite `0x1c9321` calls `0x25f5d0`, which reaches eye submission through `0x25f769 -> 0x34c520 -> 0x37a260`. **Only afterward**, callsite `0x1c935c` calls the cursor renderer at `0x1cd770`.
- `0x37a260` uses the per-eye texture table at `0x469ab58`, builds the historical texture descriptor, and calls compositor slot 5. Thus this is actual SteamVR submission, not merely a similarly named internal render stage.

This call order is now dynamically confirmed, not just inferred from static code: `render-20260918-000906-30928-7a82e013.jsonl` recorded 732 eye begins, 724 submissions, and 732 cursor enter/exit pairs over the four-second requested capture. Events on the render thread show `eye.begin -> eye.submit.immediate -> cursor.enter -> cursor.exit` for both eye indices. No events were dropped; hooks were disabled afterward; the game remained responsive. Missing submissions in some eye iterations require context (the native function has tracking-state conditions); they are not automatically classified as dropped VR frames.

The experimental fix defers only submissions nested inside `0x25f5d0` until that thread's cursor call returns. It preserves the game's own texture submission and head tracking. The first 30-second hardware test succeeded: the user confirmed that the cursor appeared and aligned correctly in the headset. The accompanying trace recorded 5,366 submissions after cursor return, with no pending-work fallback or log loss. Hooks were disabled afterward. Menus/loading paths, long-session stability, and full UI order remain untested.

## Focused pause capture (2026-09-18)

For the recorded executable hash, menu drawing is at RVA `0x1fc3f0`, taking stereo, eye index, and mirror arguments. The world loop calls it at `0x1c93c8`, after eye finalization and cursor drawing. Other native menu loops can already submit after drawing; deferral is restricted to submission nested inside the world eye routine.

Actual-game trace `render-20260918-004752-54836-f3149c61.jsonl` captured 5,372 eye begins, 5,366 immediate submissions, and 8,058 menu passes with no lost events. The per-eye sequence remained begin, submit, cursor, menu through the pause transitions. The accompanying read-only sampler `playability-20260918-004753-159891-54836.jsonl` recorded menu fade rising to 1 on three occasions, menu state 4, and renderer draw suppression `+0x2b` remaining zero. All 126 fully faded-in samples had distinct head orientations. Thus cached tracking continued while the user reported a frozen/doubled displayed view; this is evidence of a presentation issue, not proof that pose acquisition stopped.

The menu-completion experiment waits for the correct stereo eye and excludes mirror passes. Its first owned-process run recorded 40 actual submissions after menu completion, confirmed independently by the host. Full regression subsequently passed, and an actual-game 30-second test recorded 5,366 matching-eye completions without fallback. The user reported **no improvement** in the headset. Submission timing alone is therefore insufficient. The existing cursor session was restored after both captures.

Static follow-up found the pause predicate at `0x1fe730` called at `0x1c8a41`; its inverse result is kept in `sil`. At `0x1c8e7a`, a fully paused state skips the scene-drawing block and jumps to postprocessing/eye copy. Both eyes can consequently receive the retained scene image while head poses still update. This explains a plausible source of the frozen/doubled background but has not been changed or independently traced at that branch. Do not globally falsify the pause predicate: other callers govern simulation/input.

Target binding at `0x357710` stores the selected target at `0x469a5d0`, depth target at `0x469a5d8`, and dimensions at `0x469a5f0/4`. OpenVR eye targets are the first pointers in 24-byte records at `0x469ab58` and `0x469ab70`; renderer offsets `+0x38/+0x40` name other presentation targets. The menu writes a 16-float transform at `0x630b50`. The read-only sampler now records these values after validating their instruction references. It records raw target addresses without dereferencing those changing resources. Asynchronous sampling cannot establish which target a particular draw used; an event-aligned capture may still be required.

The subsequent draw-aligned capture in PID 17288 confirmed correct eye targets at every recorded stereo menu entry and exit. All 3,355 fully paused menu exits carried changing matrices. The missing menu is therefore not explained by a simple wrong target at the routine boundaries or a frozen transform. Timing-only deferral already failed the headset test. Next candidates are actual UI draw/shader/projection state, while the independently identified paused world-render skip remains a lead for the frozen background. GPU image capture would distinguish a missing draw from invisible/clipped output; no GPU readback or pause-predicate override has been implemented.

## Sources and background

- [The Witness developer on the original VR implementation](https://the-witness.net/news/2014/01/what-that-vr-post-was-about/): historical stereo/input work, not a promise of modern runtime compatibility.
- [Valve Steam Frame documentation](https://partner.steamgames.com/doc/steamhardware/steamframe): PC streaming and SteamVR/OpenXR are the initial hardware route.
- [Valve OpenVR SDK](https://github.com/ValveSoftware/openvr): authoritative interface definitions; retrieve the historical ABI for the versions actually requested.
- [Microsoft DLL best practices](https://learn.microsoft.com/en-us/windows/win32/dlls/dynamic-link-library-best-practices): defer work out of `DllMain`/loader lock.
- [Microsoft CreateRemoteThread](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createremotethread): cross-process thread behavior and side effects.
- [Pinned portable compiler release](https://github.com/skeeto/w64devkit/releases/tag/v2.10.0): x64 archive SHA-256 `18d0a4c71a166f8401ab6305781bec5882b40b5e06ba9807c61cb5f3b3c6325e` verified against official release asset metadata.
- [Firsthand historical VR setup guide](https://steamcommunity.com/sharedfiles/filedetails/?id=2566709878): reports `-vr`, cursor, and pause issues; reproduce before adopting old workarounds.
- [Recent VR alpha announcement](https://www.reddit.com/r/virtualreality/comments/1vap7a2/the_witness_mod_vr_alpha/): author reports an alpha; no source/release was verified during initial research.
