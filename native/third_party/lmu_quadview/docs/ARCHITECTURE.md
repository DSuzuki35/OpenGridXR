# Architecture and ownership

## Stereo VRS (0.2.0)

`LMU_QV_MODE=vrs` retains native two-view OpenXR enumeration and submission.
It neither requests quad views nor repeats the focus scene. The native render
hook scopes NVAPI only around the existing verified world-sort callers. A
16-pixel tile map uses 1x1 shading for tiles intersecting the focus rectangle and
the selected coarse rate elsewhere; unsupported driver/device paths keep ordinary
stereo. UI and unverified passes are outside the VRS scopes. The map is updated
per eye and uses the actual native projection, including in-game crop.

`vrs_gaze.hpp` owns optional EXT eye-gaze actions/spaces. Application attachment
and synchronization are augmented without removing their sets. For LMU without
actions, the helper attaches privately at first locate. Missing or invalid gaze
falls back per frame. Gaze spaces are destroyed before session destruction.
`vrs_foveation.hpp` owns bounded numeric projection and conservative tile mapping.
NVAPI initialization remains resident for the process lifetime because D3D
devices can outlive a helper reset; per-helper maps/views are still released.

The benchmark ABI keeps its existing fields: in VRS, outer0/1 contain stereo
eye dimensions and focus dimensions are zero. The launcher identifies them as
stereo eyes. No separate focus image or SPS pass is implied.

This is a Windows x64 D3D11 OpenXR API layer plus a .NET Framework launcher.
The adapter exposes two views to LMU while arranging four views downstream.
It depends on a verified game executable and renderer hooks; it is not a generic
quad-view injector. See [CONTRIBUTING.md](../CONTRIBUTING.md) for builds and checks.

## Normal call flow

1. `mod/launcher/Launcher.cs` collects settings. `LaunchService.cs` validates the
   game/provider selection, prepares process-local manifests/environment, and
   starts LMU. `QvfrSettings.cs` backs up and edits only the LMU profile;
   `FocusGeometry.cs` handles the calibrated UI conversion.
2. `mod/xr_layer.cpp` negotiates the OpenXR layer, intercepts session/view and
   swapchain calls, and validates provider recommendations. The runtime owns
   swapchain images; the layer manages image acquisition, waiting and release.
3. `xrLocateViews` obtains the four provider views and caches validated poses/FOVs
   for the active frame. The game sees stereo; `mod/bridge.hpp` exposes the
   frame-scoped data and image-transfer operations to the engine hooks.
4. `mod/engine_bridge.cpp` installs hooks only after verifying the process name and native code anchors. Executable hashes are diagnostic metadata, not an allowlist.
   Each original eye render is followed by an additional focus scene traversal,
   with focus camera/projection/history preparation and outer-state restoration.
   A compatible smaller outer resource bank reduces outer resolution; invalid
   layouts retain full-size rendering with resampling as needed.
5. Outer color is copied into acquired outer swapchain images. Extra UI work can
   draw into these images through a borrowed RTV. Focus views use their validated
   output path. `xrEndFrame` replaces eligible stereo projection submissions with
   four-view projections and forwards the layer list to the provider. Auxiliary
   layers and swapchains must retain their separate identity.

`xr_layer.cpp` owns XR/session/swapchain state. `engine_bridge.cpp` owns hook
installation and restoration of game state. Neither side should independently
advance the other's resource lifetime. `bridge.hpp` documents their contract.

## Resource invariants

- Frame views are valid only after a successful locate and until end-frame on the
  rendering thread. Stale or invalid views must not be used for a later frame.
- Native eye handles establish eye identity. Dimensions and creation order cannot
  distinguish an eye from an auxiliary swapchain.
- The XR layer acquires/waits during `xr_copy_outer` and releases at end-frame
  (or cleanup). `xr_outer_target`
  returns an RTV and cached, validated dimensions borrowed until end-frame;
  callers do not release the pointer or keep it across frames.
- Experimental `FocusImageLease` textures are borrowed only while the token/frame
  lease is valid. A held, waited image must be retired before frame advancement
  or another acquire on its chain. The final focus image is distinct from the
  game's opaque HDR/depth targets.
- Game bindings, viewports, cameras, projection/history data and D3D state changed
  by a hook must be restored, including rejected/failing paths. Resource layouts,
  formats, sample counts and sizes must be checked before selecting a fast path.
- Hooks start after successful session creation and stop before session teardown.
  Unknown builds cannot be patched. Logging and profiling must respect the master
  diagnostics setting; provider logging is controlled outside this layer.

## Default path and research

The default quad path retains the two original eye draws and adds two focus scene
traversals. Shared-path housekeeping uses inline storage for up to 16 submitted
layers with a large-list fallback, cached outer dimensions and borrowed RTVs,
and avoids D3D state capture when no outer UI draw is needed. These changes do not
constitute single-pass stereo or establish a measured frametime gain.

Upload reuse/batching (`native_upload_reuse.hpp`, `native_upload_batch.hpp`),
shader conversion, stereo auditing and private paired-draw proof
(`stereo_shader.cpp`, `stereo_audit.cpp`, `stereo_draw_proof.cpp`) are experimental
and disabled by default. Private proof targets test a paired draw against original
references; normal game eye draws remain enabled. A native renderer-wide paired
pass is unfinished. Read the [handoff](../INSTANCED-STEREO-HANDOFF.md) before
extending this work.

`render_profile.hpp` and `gpu_timing.hpp` measure CPU scopes and sparse asynchronous
GPU queries; `benchmark_control.hpp` coordinates diagnostic capture. CPU scopes
can nest and include waits, while GPU slots omit copies, UI and composition.
See [profiling limits](../RENDER-PROFILING.txt). Native/fake-runtime and WARP tests
cover contracts and rendering helpers; hardware/provider QA remains necessary.
