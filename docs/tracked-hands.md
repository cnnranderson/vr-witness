# Tracked glove hands

The launcher offers **Show hands**, off by default. Enable it and select
**Save settings** to restart only the render session. If fixes were disabled,
they stay disabled until re-enabled. The supplied [Quaternius gloves](../assets/hands/README.md)
follow both controllers; holding the grip button closes each glove and releasing
it reopens it. Meshes and their rigs are embedded in the DLL;
no external asset loader or second VR session is used.

Hands have self-occlusion and stereo projection. World occlusion is unavailable
from the v0.4.0 launcher after its headset test failed. There is no individual finger tracking,
arm IK, or physical interaction. Native fixture and software-renderer checks pass;
placement, scale, world-depth mapping, latency, and comfort still need a headset
check. The first test confirmed visibility in game and exposed a raw-controller
alignment error, addressed by the palm attachment below.

## Rendering and tracking

`hands_native.cpp` borrows the game's `IVRSystem_012` and `IVRCompositor_013`
objects. Its historical OpenVR v0.9.19 layouts match the existing 80-byte
tracked-pose ABI. It samples HMD and both role-selected controllers once at eye
zero with compositor `GetLastPoses`, then uses that sample for both eyes.
It never calls `WaitGetPoses`, initializes VR, or shuts down the native runtime.

For each hand, the transform is:

```text
clip_from_model = projection * inverse(eye_to_head)
                  * inverse(tracking_from_head)
                  * tracking_from_controller * controller_from_palm
                  * palm_from_model
```

Matrices are row-major with column-vector multiplication; tracking and assets
use meters. Native eye projection and eye-to-head transforms come directly
from verified virtual slots. The controller-to-HMD relation includes position
and orientation, without per-puzzle or location offsets. This does not alter
the existing orientation-based puzzle cursor.

The renderer borrows `IVRRenderModels_005` through the game's existing
`VR_GetGenericInterface` export. System slot 26 supplies the role-selected
render-model name; render-model slot 13 supplies `openxr_handmodel`'s
`mTrackingToComponentLocal`. It never guesses a controller-specific Euler offset.
Valve maps this attachment to [OpenXR palm_ext](https://github.com/ValveSoftware/openvr/wiki/Render-Model-Reference).
The [palm pose convention](https://registry.khronos.org/OpenXR/specs/1.0/man/html/XR_EXT_palm_pose.html)
requires different left/right model bases: fingers point forward, thumbs up,
and palms inward in a handshake pose. The supplied mirrored eight-degree fit
is then applied. Missing or invalid palm components hide the affected hand;
`handgrip` and `grip` are not interchangeable fallbacks. Controllers that lack
this component need a separately verified alignment profile.

The existing legacy controller state supplies the grip press (button bit 2).
The 26-joint glove rig bends authored finger joints using its original weights
and inverse binds, preserving joint anchors and segment lengths. Both eyes use
the same approximately 120 ms transition between open and closed. This is
button animation, not skeletal finger tracking. Invalid controller state hides
that hand and discards its old grip state.

SteamVR's installed default Index legacy binding routes both a firm grip squeeze
and A to the same grip bit. With that binding, A also closes the glove; the
legacy mask cannot distinguish them. Custom bindings determine the physical
button. No SteamVR bindings, manifests, or existing input behavior are changed.

Invalid poses hide each hand independently. Duplicate device roles, invalid
HMD tracking, focus loss, runtime input capture, menus, stale samples over
100 ms, or changed native interface identities suppress rendering. Eye one
requires a preceding eye-zero capture. Runtime reinitialization or a changed
render thread requires restarting the game.

The existing hooks provide the draw point: after scene copy, before native
cursor/menu drawing, while the matching eye is pending submission. A per-eye
latch rejects duplicate and nested cursor passes. Native submission then
resolves/copies the modified source texture into its usual destination.
Fallback routes suspend hand rendering along with existing deferred rendering.

`hands_draw.cpp` creates resources lazily on the render thread. It uses
D3D11.1 context-state isolation and private matching-size/sample depth, restoring
all native pipeline bindings afterward. Unsupported context/texture types or
GPU failures latch `hands_fault` independently of the established render fixes.
`hand_draws` counts successful per-eye draws; a successful draw is not evidence
that hands were inside the visible frustum.

A single renderer stays pinned at stop because the game's D3D device may be
single-threaded. Session start requests a cache reset that executes only on
the next owning render callback. GPU resource releases never run on the
launcher/session worker or in DLL detach. The OS reclaims retained resources
at process exit.

## Native evidence

Addresses are RVAs for the supported executable fingerprint in
[native integration](native-integration.md), guarded against live bytes before
use. These observations come from REA static analysis, separate from headset
or real-game rendering validation.

| Evidence | Observation and implementation consequence |
| --- | --- |
| H1: `37A260` | Eye wrapper pointers start at `469AB58`, stride 24. Native submit first calls `357140`, then submits wrapper destination `+8`, resource `+40`. Draw into wrapper source `+0`, resource `+40`, before that resolve/copy. |
| H2: `357140` | Source and destination may differ; flags select copy/resolve or shader paths. Query the source resource for `ID3D11Texture2D` and let native submission retain its existing resolve path. |
| H3/H4: `25F5D0` / `25F5E0` | The existing eye hook passes through scene copy. Its completion provides eye color, but does not establish availability of original world depth. |
| H5: `37A7B0` | System virtual slot 1 returns projection through explicit storage: `this, output, eye, near, far, API`. The native call uses DirectX API value zero. |
| H6: `37A890` | System virtual slot 4 returns eye-to-head through explicit storage: `this, output, eye`. Preserve the MSVC aggregate-return ABI when calling from MinGW. |
| H7: `37A4F0` | Native initialization stores the compositor at `469AB40` and creates the eye target wrappers; the existing system is at `469AB38`. |

Evidence IDs:

- H1: `ev_7e30aedb2f465d10693accc65921a73e1f3370a7418925169eeda9a5a3c52910`
- H2: `ev_5b79a18f1d00286ffcf38ace9e3284eae473607e898af6c83843284115fcc8f9`
- H3: `ev_764a545e243774c60703ac6674f05165bf40cc71a1b22e78b1ac882779979748`
- H4: `ev_7d9f0c43dfc4939e2a46e8371508adc21eb93f69ffad58dac69447b378849961`
- H5: `ev_f67e7ced5eb3ff8d6a0278554bcdb0cf070d2f6da48184c84af1c16ecd8568f1`
- H6: `ev_4ff246406326b087f945557fceb4743bd046708b551e7c59c4c59c073ff3b887`
- H7: `ev_6d4cfa65d4040f9c7f14284935f7d2a88951693880ba3b6ebff71fdee52a7df6`

## Verification and headset checks

Automated coverage includes asymmetric projection, stereo disparity, common
head/controller transform invariance, invalid poses, and normal transforms.
WARP tests check real glove pixels, self-depth, full pipeline restoration,
multisampling, target resize, sRGB, device replacement, grip deformation, normal/reversed
world-depth rejection, unsupported-depth fallback, and wrong-thread
rejection. Native hook fixtures cover once-per-eye ordering, nesting,
duplicates, fallback, disable/enable/stop/restart, counters, and short protocol
buffers ending at a protected page. Launcher tests cover persistence, default
off, status parsing, settings save, and all tab transitions.

For the first headset test:

1. Close the old launcher and game before using the new package. Enable
   **Show hands**, save, and launch VR.
2. Compare left/right placement, palm orientation, size, and binocular alignment
   with the physical controllers. Check near and far arm positions, including
   the right-hand handshake pose. Squeeze/release each grip independently.
3. Turn and lean, calibrate height with F7, and enter ordinary and perspective
   puzzles. Check that the gloves remain attached to the controllers.
4. Open/close menus, lose/recover controller tracking, and switch focus. Check
   that hidden hands recover without freezing.
5. Toggle fixes with F8, then change **Show hands** in both directions. Check
   that the cursor and menus remain visible and usable.

## Experimental world occlusion

REA identified the main-view depth wrapper at `630C20`, paired with main color
`630C18`. Target binding at `357710` takes the depth resource through wrapper
`+8`, reads its DSV at resource `+50`, and records the current depth wrapper at
`469A5D8`. The main loop binds that pair before world drawing. Before
postprocessing, it resolves color at `1C92EC`, selects depth through `356AD0`,
and passes color/depth into `283B00`. With multisampling, `356AD0` can select a
different resolved resource at wrapper `+10`.

The preview's **Experimental world occlusion** option did not work in the reported
headset test. v0.4.0 removes it from the launcher, ignores old saved preferences,
and clears them on save. It is not a supported release feature. The developer
CLI still accepts `--hand-occlusion` for future investigation; protocol 4 and its
tests remain intact. The candidate path uses the existing cursor hook with no
additional hooks or native depth writes. Invalid or unsupported depth data leaves
ordinary hands visible instead of faulting the hand renderer.

Projection setup at `2C87D0` writes the current view projection at `+AC` and its
reverse-depth flag at `+16C`. The integration extracts `depth = A + B / distance`
from the validated projection, accommodating the native camera-axis basis and
ordinary or reversed depth. Distances are eye-forward meters. It reads the
original depth texture through wrapper `+8`, resource `+40`, queries its COM type,
and retains a reference only for that render-thread draw. The world viewport
uses the full depth texture or the reduced-resolution dimensions selected by
the main loop from `630C50`, `630C54`, `620478`, and `621E6C`.

The renderer copies the scene depth into an owned shader-readable texture while
the game's context state is isolated. D32, D24+stencil, and D32+stencil families
are supported, including multisampling. Each fragment compares its distance to
the sampled scene depth with a five-millimeter bias toward the viewer. MSAA
uses the closest covered depth sample. Private glove depth still handles
self-occlusion. Native resources and pipeline bindings remain unchanged.

Static ordering supports this choice of buffer, but does not prove real-game
mapping after all quality settings and postprocessing. Misaligned clipping,
edge halos, or missing occlusion around transparent/perspective surfaces remain
possible. `world_occlusion` reports the
requested setting, `occlusion_active` reports depth use on the latest draw while
hands are enabled, and `occlusion_draws` counts per-eye depth-tested draws.
Those counters indicate use of the candidate path, not correct visual alignment.
Changing **Show hands** restarts only rendering and retains a disabled session.

- Main loop: `ev_56bf0d2b39dbceebf32f4f567360a6be7029ff7dd2b0cf397835dc2d8d339077`
- Target binding: `ev_5ecc3309ec8d92851ce0802b134ad4949ff6a268e02e3b5d8cea16a4b1b401b7`
- Depth selection: `ev_22d67b9a43c0649e19c1d2f340b637d2f0ebe4c59ea916f597468d05cb075656`
- Postprocess: `ev_a6b19c093f73fd663ff1c7e82f7928ff7ba515f0323f834033455d7ccb07096f`
- Projection setup: `ev_60ea8751be6da2ff5a20d4f9208f90d4573fdaa602626926aa75970840a86ce2`
