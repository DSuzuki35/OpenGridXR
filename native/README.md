# Native OpenGrid XR

Build the x64 Windows DLL and clean-output executable with:

```powershell
.\scripts\build-native.ps1
```

Requires Visual Studio 2022 Desktop C++ tools, Windows SDK, and CMake 3.24+. The checked-in OpenXR headers are from Khronos OpenXR-SDK `release-1.1.46`. No OpenXR runtime is required to build. Artifacts are `native/build/Release/OpenGridXR.dll`, `OpenGridXR.json`, and `OpenGridXR-Output.exe`. The native build never registers a layer or changes game/runtime settings.

## Runtime behavior

The layer name is `XR_APILAYER_OPENGRIDXR`. The desktop's explicit install action owns registration. `DISABLE_OPENGRIDXR=1` disables the layer through its manifest. Executables `iRacingSim64DX11.exe`, `iRacingSim64DX11_test.exe`, `Le Mans Ultimate.exe`, and `LeMansUltimate.exe` select the corresponding profile. Other processes retain runtime projection and resolution recommendations.

Configuration is `OpenGridXR.ini` beside the DLL. `OPENGRIDXR_CONFIG` can override the complete path. Configuration is loaded once per OpenXR instance: restart the game after changes that affect rendering. Missing values are neutral: zero crop, 100% render scale, mask off. Unknown Mode values disable modification.

Primary stereo is supported. Rectangular crop works in tangent space, with proportional recommended image dimensions to preserve pixel density. Render scale multiplies both dimension recommendations. Cropping and extra rendering require `fovMutable`; immutable-FOV runtimes retain their original FOV. Scale can still change their resolution recommendation. Runtime maxima bound all recommendations. Applications may ignore recommendations.

Driving masks use contours normalized over the **original full eye image**, not the already cropped rectangle. A conservative 128×128 classification retains every cell touching the aperture or padding; outside runs become counter-clockwise triangles. The runtime's native hidden triangles are preserved and the custom triangles appended. Padding expands the opening conservatively. The custom mask starts after valid FOV data arrives; visibility-mask-change events request a refresh. This requires the game to enable `XR_KHR_visibility_mask` and use its **hidden triangle mesh**. Visible-triangle and line-loop requests pass through unchanged. A game can ignore masks entirely. This is a render-visibility aperture, not a rendered helmet model.

Streaming retains the full source FOV plus ExtraLeft/ExtraRight on both eyes when FOV is mutable. The restrictive custom aperture is disabled. During extra rendering the native mask reports unavailable, so the added region is not prematurely hidden. Rectangular crop controls affect output framing in streaming mode. Reserve, smoothing, horizon level, offsets, aspect fit, and optional reflected borders affect the separate clean output; they never alter headset pose. Correction is bounded to available pixels unless mirror edges is enabled.

Horizon lock chooses the upright Y hemisphere from the first approximately upright view, supporting submitted layer spaces where upright is −Y. It retains that choice until a new session, producer, or submitted reference-space handle is observed. Keep the headset approximately upright when opening the output. Horizon lock defaults to zero; close and reopen the output to reset its upright reference after a game recenter that retains the same space handle.

## D3D11 capture and clean output

Only D3D11 capture is implemented. Acquired swapchain pixels are copied **before** `xrReleaseSwapchainImage`; MSAA is resolved when supported. The submitted projection's selected eye, array slice, and image rectangle are then copied to a shared keyed-mutex texture. Capture is idle when no output consumer has a recent heartbeat. Unsupported graphics APIs, protected swapchains, unsupported shared texture formats, and non-stereo projections produce no stream; the game continues normally.

```powershell
.\OpenGridXR-Output.exe --config 'C:\path\OpenGridXR.ini' --game iracing
.\OpenGridXR-Output.exe --config 'C:\path\OpenGridXR.ini' --game lmu --fullscreen --display '1920,0,1920,1080'
```

`--display` takes **physical pixel** desktop bounds `x,y,width,height`, including negative coordinates for left/top displays. The executable uses Per-Monitor-V2 DPI awareness. Electron must convert display DIP bounds with `screen.dipToScreenRect`. F11 toggles borderless fullscreen; Escape returns fullscreen to its saved windowed placement, then closes a windowed output. The cursor is hidden over fullscreen output. Fullscreen on a capture-card display provides clean HDMI/DisplayPort output for a second PC. OBS can capture the window on one PC or the capture-card input on another. There is no direct OBS source plugin in this release.

The window shows an honest waiting screen until frames arrive and returns to that screen after a stale source. One consumer owns each game channel. Stabilization renders into a texture of the exact configured output width/height; a second pass scales that canvas into the window or display with letterboxing. Typeless runtime textures are captured using the typed OpenXR swapchain format, including MSAA resolve. sRGB texture sampling is re-encoded for SDR output to preserve source brightness. Output settings do not change the capture-card display's Windows resolution or refresh rate. Match that display's Windows mode to the desired capture-card mode.

## Selected VR overlays in Clean Output

Spatial sources are private to the headset by default. `showInStream` selects which sources also appear in Clean Output; there is no independent screen-position setting. Their captured VR position, orientation, roll and physical aspect follow the selected eye through the same output crop and stabilization transform as the game image. The compositor uses perspective-correct texture coordinates, clips against the camera near plane and canvas edges, and hides back faces like OpenXR quad layers. Later slots draw above earlier slots; item opacity is shared by both destinations.

The raw frame publication carries quad corners in that exact captured eye's camera coordinates. Receiver session, producer/consumer identity, source epoch and pixel dimensions must match before a panel is drawn. Unresolved or mismatched geometry is omitted. The helper supplies a separate double-buffered NT texture ring for selected sources only while an Output heartbeat is fresh; unchanged pages can be published without browser repaint. Output copies only changed source frames into local GPU textures and blends premultiplied SDR pixels into its encoded UNORM canvas, after stabilization and before display letterboxing. There is no CPU bitmap transfer and the headset and Output never compete for one keyed mutex. Private/disabled sources, cleared epochs and expired producers remove cached pixels; Output reset releases resources and ownership.

`opengrid-stream-overlay-tests --listen --seconds 30 [--adapter NVIDIA]` is a test-only 100×100 compositor on the isolated `test` mapping. Its synthetic raw eye and receiver spaces are identity; it consumes the synthetic receiver's published panel poses and emits `stream-fixture` readiness plus `stream-frame` pixel readback. It can run beside the synthetic headset receiver to test Electron → helper → both GPU consumers. Normal GPU tests cover privacy/selection, perspective texture interpolation, roll, crop/stabilization alignment, near-plane/back-face clipping, BGRA/RGBA alpha and stacking, static/replaced sources, ownership, metadata contention and caller state/predicate restoration. Actual sim/headset/capture-card timing and all-source GPU cost still need hardware validation.

## INI contract

Sections `[iracing]` and `[lmu]` accept:

| Keys | Units / values |
| --- | --- |
| Enabled, MaskEnabled, Mirror, EnhancementsEnabled | 0 or 1 |
| Mode | driving or streaming |
| SharpeningMode, SharpeningStrength | off/luma/cas; 0-100 (default off, 50) |
| LumaClamp, LumaRadius, LumaPattern | 0-0.1, 0.1-6, 1-4 (defaults .035, 1, 2) |
| CropLeft, CropRight, CropTop, CropBottom | 0–40 percent; opposing sum ≤70 |
| RenderScale | 50–200 percent per dimension |
| MaskPadding | 0–10 percent |
| MaskLeft, MaskRight | 3–64 simple polygon points `x,y;x,y;…`, normalized 0–1 |
| StreamEye | 0 left, 1 right |
| Smoothing, HorizonLock | 0–100 percent |
| ReserveLeft, ReserveRight, ReserveTop, ReserveBottom | 0–30 percent |
| ExtraLeft, ExtraRight | 0–30 percent of original tangent width |
| OffsetX, OffsetY | −100 to 100 |
| OutputWidth, OutputHeight, OutputFps | bounded output framing and target rate |

`Local\OpenGridXR.Frame.<game>` is versioned shared GPU metadata, guarded by its named mutex. GPU ownership is producer key 0 / consumer key 1. Stale pending textures are regenerated so an exited consumer cannot strand capture.

`OpenGridXR-status-<game>.json` beside the config is refreshed about every two seconds by real successful `xrEndFrame` calls, and marked inactive at session destruction. It contains PID, wall-clock timestamp, game/mode, mutableFov, captureSupported, maskCalls, maskAppliedCalls, frames, capturedFrames, captureWidth/Height, configuredFps, and a rendering config snapshot. Consumers must verify process liveness and timestamp freshness. `maskAppliedCalls` means a custom mesh was returned; it does not prove that a game rendered or benefited from it. Registration alone is never evidence of an active game session. `configuredFps` is a target, not a measured rate.

## Verification and remaining validation

`ctest --test-dir native/build -C Release --output-on-failure` runs the native suites, including:

- Geometry, projection density, process detection, conservative mask winding/padding, and Horizonlock correction bounds.
- Built-DLL loader negotiation with a synthetic runtime: mutable/immutable FOV, unsupported view pass-through, native mask preservation, count/fill/insufficient-buffer behavior, and validation errors.
- Headless output startup and clean shutdown with no running game.
- Real D3D11 shared texture consumption, compiled warp shader, configured-resolution canvas, nonuniform crop regression, UNORM/sRGB backbuffer pixel readback, and fullscreen placement/restoration.
- Real D3D11 layer snapshot/publish and readback for typeless images with typed UNORM/sRGB formats and 1×/4× MSAA, proving captured pixels survive subsequent runtime image reuse and respect the submitted subimage rectangle.

GPU tests need a Windows D3D11 hardware adapter. Tests use hidden windows and isolated files under the build directory. No tests register the layer. Actual iRacing/LMU sessions, headset FOV, app mask usage, GPU cost, runtime compatibility, and two-PC capture-card timing still require hardware validation. No performance improvement is claimed by synthetic tests.

## Attribution

`third_party/horizonlock/core.hpp` and `core.cpp` are copied from [DSuzuki35/horizonlock-vr](https://github.com/DSuzuki35/horizonlock-vr), commit `0b2d88ae64d28907a72eb95b8920c7f90a7a42ae`, with only the include path changed. This GPL-2.0 code provides quaternion math, smoothing history, horizon correction, reserve framing, and bounded inverse projection. The full original license is beside those files. No Director, gaze, natural-headmotion, telemetry, original registration, original branding, or expiry component is used. OpenGrid owns the layer, profile parsing, capture transport, and output window. Khronos headers retain their Apache-2.0 OR MIT notices; OpenGrid elects MIT, with the matching official MIT license text and copyright attribution included.


## Integrated headset enhancements

`EnhancementsEnabled=1` loads adjacent `OpenGridXR-Enhancements.dll` through a private chain: game -> OpenGrid crop/raw capture/sharpening -> headset frame-generation/OSD engine -> original runtime chain. Only OpenGridXR.json is registered. No extra desktop app, manifest registration, or installed VectorXR is used. A missing/incompatible engine falls back to base cropping and reports engineError; engineLoaded does not prove FG activity. An instance-creation error after engine initialization is returned without unsafe retry.

The engine adapts VectorXR commit `9e8b49b1721eb30ce27d7ba72f1488050160a29d` (MPL-2.0). Native parsing/resolution forces Depth, Pivot, Quadviews, Turbo, extra head prediction, app tracking and sounds off. FG and OSD are integrated, along with OpenGrid's own GPU overlay receiver. The clean output still captures real application frames before headset sharpening, FG, OSD and spatial overlays.

Version-3 JSON is adjacent: `OpenGridXR-enhancements-iracing.json` / `OpenGridXR-enhancements-lmu.json`. Unknown executables use a disabled filename. Test override: `OPENGRIDXR_ENHANCEMENTS_CONFIG`. Logs, traces, caches and runtime status/control relay files live under adjacent `enhancements-data/`. Internal synchronization events are unnamed. No VectorXR environment variable or directory is used. FG/graphics bridging is session-latched and needs game restart; OSD settings/tuning reload. `core.osd.toolkitSummary` (max180 bytes) shows configured settings in the expanded TOOLKIT row, not proof of active crop settings.

The source engine supports D3D11/D3D12 and a Vulkan bridge; this integration targets D3D11 games. Native Vulkan compute shaders are not built. OpenGrid capture/sharpening is D3D11 only; other APIs pass through without clean output. Optional NVIDIA optical flow loads from the installed driver; no proprietary driver binary is bundled. Shader estimation remains the default. FG and hardware optical flow remain experimental until intended Pimax/SteamVR headset validation.

## Headset sharpening

Luma/CAS affect only stereo color projection rectangles/slices learned from the previous successful application frame. First frames, HUD-shared images, unknown composition, depth/protected swapchains and unsupported formats are skipped. Supported formats: typed/typeless RGBA8/BGRA8 UNORM/sRGB and RGB10A2. MSAA resolves into a sampling texture, draws selected regions into an original-sample-count clone and copies back before release. Raw capture occurs first. Touched pipeline state, predication, UAV counters and constant-buffer ranges are preserved. Settings latch at instance creation; restart after changes.

Status reports actual `sharpenAppliedCalls` and `sharpenStatus` (0 skipped, 1 applied, 2 unsupported, 3 invalid, 4 failed). No physical lens distortion, VRS or synthesized quad-view hooks are enabled.

## Additional validation for 0.2

`scripts/build-native.ps1` builds/tests both engine and base layer in Release; `-SkipEnhancements` limits an iteration to the base layer. Base suites include missing/disabled/fixture/actual private-chain paths. The real-engine D3D11 test checks raw red pixels survive a downstream release that overwrites the runtime image blue, then submits an actual shared-texture quad through the production private engine. Sharpening tests cover per-sample MSAA preservation outside regions, stereo isolation, alpha/sRGB, all Luma patterns, CAS, repeated raw-capture resolves and D3D state restoration on WARP, Intel Iris Xe and explicit NVIDIA RTX3070Ti Laptop.

Seven selected engine CTests cover desktop JSON parsing, module/path isolation, DLL loading/static CRT imports, OSD raster/upload, FG D3D11/D3D12 GPU routines and the D3D11/D3D12 bridge. Upstream GPU CTests use WARP. Supplemental FG kernel tests passed on Intel hardware. The explicit NVIDIA bridge test passed with both debug layers. The full optional NVIDIA hardware-flow suite failed its repeating-facade quality assertion (0.07747 error vs 0.06985 holding); the default Intel hardware bridge test stalled and was stopped. Those supplemental failures are retained unmodified, not claimed as passing. Shader estimation is the default; optional hardware-flow quality remains experimental. No RTX5090, DreamAir/DreamAirSE, Pimax OpenXR or SteamVR headset session has been tested.

## Spatial GPU overlays

The private engine's downstream submission hook adds up to fifteen stereo quad layers after frame generation and OSD. `core.overlays.enabled` controls this receiver. The runtime's `maxLayerCount` budget always reserves the application's submitted layers and OSD first; fifteen configured sources do not guarantee fifteen visible quads. D3D11 is direct, and D3D12 swapchain images use D3D11On12 on the runtime's queue. Vulkan overlay rendering is unavailable. No CPU eye-image capture is used.

`OpenGridXR-OverlayHost.exe --game iracing|lmu --parent-pid <desktop PID>` reads bounded JSON lines from stdin and writes ACK/status JSON lines. It opens textures only by duplicating the parent process's supplied NT handle. The receiver publishes its exact adapter LUID; the helper creates its D3D11 device on that adapter, and a producer on a different GPU reports an import failure instead of a CPU fallback. There is one pending source frame per slot, two keyed shared textures per slot, and no texture allocation before a frame arrives. Resources retire on clear, disable, source replacement, producer expiry or session destruction. Temporary headset inactivity retains unchanged static panels. The game thread uses nonblocking metadata/keyed-mutex/image waits and skips busy work.

Frame fields are `op:"frame"`, `slot` (0..14), `frameId` (nonnegative safe integer), `handle` (decimal uint64 string), `width` (1..1920), `height` (1..1080), `format` (`bgra` or `rgba`). The desktop must keep Electron's texture alive until the corresponding ACK or process exit. `copied` ACK follows a GPU completion query; `dropped` and `error` mean the frame was not published. Helper control commands are retried in order through metadata contention; rejected queued frames receive ACKs. Lines are limited to4096 bytes and control storage is bounded.

`configure` accepts `slot`, `enabled`, `anchor` (`head` or `local`), `x/y` (-5..5m), `z` (-10..-0.1m), yaw/pitch/roll (-180..180 degrees), `widthMeters` (0.1..3), and opacity (0..1). Height derives from the current image aspect. `clear` invalidates a slot's source epoch, including clear/reconfigure between two game frames. `recenter` takes slot0..14 or -1 for all. Head anchors use VIEW space; local anchors retain the head's position and yaw when first displayed or explicitly recentered. This only moves overlays, never the game's reference space.

The mapping `Local\\OpenGridXR.Overlays.<game>` uses the fixed structs in `overlay_transport.hpp`, magic0x564f474f, version4 and exact struct size. Status reports receiver availability, remaining capacity, visible count, `streamActive` from Clean Output's heartbeat, and per-slot state (0 waiting,1 ready,2 disabled,3 runtime limit,4 GPU import mismatch,5 error). `--test-namespace` is an isolated helper fixture flag. `opengrid-overlay-renderer-tests --listen --seconds120 [--d3d12] [--adapter NVIDIA]` provides a synthetic receiver and pixel readback without a headset or registration. Production never sets the test flag.

Each source is hidden from the stream by default. `configure` additionally accepts `showInStream`. A selected panel keeps its VR spatial placement, perspective, roll, size and opacity in Clean Output. The helper creates a separate keyed stream ring only when the source is selected and Output's heartbeat is fresh. It copies its retained pixels, so selecting a static page works without a new Chromium paint and does not extend the Electron texture lifetime. VR and stream consumers never compete for the same target mutex. Output shutdown/expiry, deselection and clear retire stream buffers. A source may appear in Clean Output even if the headset's layer budget is exhausted.

The engine publishes each resolved quad pose and its actual VIEW/LOCAL space. Base capture locates that space into the application's submitted projection space at the raw frame's exact `displayTime`, then transforms its four corners by the inverse selected eye pose. These corners, source revision, dimensions, receiver session and process identities are atomically published alongside that GPU image in version2 `SharedFrame`. This avoids pairing a raw image with a later generated-frame camera pose. The compositor projects these raw-eye corners through the actual crop/UV framing, inverse stabilization correction and output FOV, with perspective-correct texture coordinates. New/changed/untracked geometry is skipped until a matching pose exists; no world-space placement is guessed. There is no independent two-dimensional stream layout.

Native tests cover actual D3D11/D3D12 GPU pixels, sRGB/opacity, layer budgeting, local recenter, static frames, clear/replacement, paused rendering, retirement, and helper process lock contention. Desktop smoke tests exercise real Electron URL and window GPU handles through the helper into the synthetic receiver. Electron's producer-readiness synchronization remains experimental: retaining the source and waiting for our copy establishes lifetime and completion, but does not add a missing producer fence to Electron's handle API. Real headset latency, composition cost and all-fifteen-source performance still need validation. The desktop caps capture rate and aggregate pixel budget and stops producers when the game is inactive.

Build outputs also include `OpenGridXR-Telemetry.exe` (read-only game data, see `telemetry/README.md`) and `OpenGridXR-Control.exe` (explicit configured foreground-game recenter shortcut). Their fixture executables are tests only and should not ship.
