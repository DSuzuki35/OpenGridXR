# Changelog

Release numbers use `MAJOR.MINOR.PATCH`. The root `VERSION` file supplies the
launcher title, Windows EXE/DLL metadata and default portable ZIP name.
Versions before 1.0 remain experimental and may change compatibility or settings.

## 0.5.4 - 2026-11-06

Integrates Markus Stahl's 0.5.2–0.5.4 community patches. **Thank you, Markus Stahl,
for the continued VRS optimizations, test fixes and CPU scheduling contribution.**

- Rename Setup to **Settings**, move the green focus-outline toggle there, and
  remove Outer smoothing from the launcher and shared profiles. Legacy smoothing
  values are ignored; launcher starts explicitly disable the old filter.
- Keep a separate Stereo VRS shading-rate map for each eye. Rebuild only when
  that eye's focus changes; upload only when its 16×16 tiles change. Reuse map
  storage and fill rows outside the focus region in one operation.
- Add `mapUploads` to VRS diagnostics and regression coverage comparing the
  optimized tiles to the previous implementation, including cache and retry cases.
- Add **Settings > Prefer the 3D V-Cache CCD (AMD X3D)**, enabled by default.
  On supported dual-CCD AMD systems with an unambiguous asymmetric L3 topology,
  request the larger-cache domain through Windows process-default CPU sets.
  Skip other/ambiguous topologies, failed queries, existing process affinity or
  default CPU sets. Explicit thread policies retain Windows precedence.
  The preference remains local to the machine and applies on next game launch,
  including Steam automatic startup. Diagnostics log the decision.
- Stabilize autoload verification fixtures with unique temporary paths, bounded
  image-lock/cleanup retries and ownership-aware cleanup. Trace-log tests now
  wait for asynchronous writes and account for intentional queue drops.
- Include a manually launched NVIDIA VRS diagnostic with version metadata and
  release-signing support. Normal builds compile it without running it.

These changes have not yet been benchmarked in LMU. Synthetic tile equality is
not a headset image-quality test, and the X3D preference is not a guaranteed
frametime improvement. The reported 1–1.5 ms gain below belongs to 0.5.1.

## 0.5.1 - 2026-11-05

Integrates Markus Stahl's 0.5.0 and 0.5.1 community patches on top of 0.4.0.
**A huge thank you to Markus Stahl for the performance improvements, SimHub
integration, stability fixes and all his continued help with this project.**

- Add experimental SimHub telemetry (`LmuQuadView.*`) and wheel-button actions
  for benchmark visibility and recording. Build the plugin against your local
  SimHub installation; enable Diagnostics and Benchmark and keep the launcher open.
- Reduce repeated render-hook pointer validation and repetitive error logging.
  Avoid routine per-frame allocations in VRS overlay submission and eye-gaze sync.
- Allocate the render profiler's large sample buffers only while profiling is
  enabled, and release them when disabled. This reduces the DLL's static data;
  file-size savings depend on the compiler and are not a measured RAM or
  frametime saving of the same size.
- Reject missing OpenXR extension/layer lists and bound application-name logging.
- Handle protected-process queries and benchmark polling errors in the launcher.
- Restrict elevated installer DLL lookup to System32, protect benchmark shared
  memory, check overlay snapshots, and reject implausible benchmark timings.
- Add Content-Security-Policy and no-referrer headers to local benchmark reports.
- Clean up generated update downloads older than seven days while retaining
  unrelated folders and links.
- Add input-validation, telemetry and launcher regression coverage.

### Performance expectations

In my benchmark scenario, I observed around **1–1.5 ms lower CPU frametime with
0.5.1**. This is a result from my own setup and scene, not a guaranteed improvement
across systems. Quad Views still needs sufficient CPU headroom; the remaining
overhead depends on the setup and workload.

## 0.4.0 - 2026-11-05

- Performance expectations: preliminary measurements and user reports indicate around **0.5 ms less CPU render time**, depending on the workload. Quad Views still adds roughly **3–4 ms CPU frametime versus no mod**, so it is worthwhile only with sufficient CPU headroom; GPU gains can be outweighed when CPU-limited.
- Add portable rendering profiles contributed by **Markus Stahl**: save, load, rename, delete, import and export `.lmuqv` files. Paths, devices, diagnostics, benchmark placement and headset calibration remain local. Imports reject DTD/entities, oversized files, unsupported format versions and invalid setting ranges.
- Refresh the native Windows launcher with an OpenGrid XR inspired dark violet palette, rounded cards and clearer navigation. Keep the small .NET distribution.
- Show live allowed slider intervals with dimmed unavailable sections. Constrain QVFR resolution/focus combinations consistently across dragging, keyboard and numeric entry, including calibrated headset units.
- Keep slider dragging responsive by updating only changed bounds and drawing range captions without repeated row layouts.
- Offer opt-in CPU upload reuse in Setup. Profiling sessions can toggle focus-only reuse with Ctrl+Alt+F10 between captures; active captures keep a fixed setting.
- Offer a per-user installer alongside the portable ZIP. Preserve settings, profiles and logs across upgrades.
- Check for stable GitHub updates in the launcher. Downloads and installation require user action; verify the installer SHA256 and timestamped publisher signature, and require LMU to be closed.
- Reduce repeated cached outer-bank validation and stop hidden benchmark bitmap/preview work. Add opt-in lock-wait profiling to investigate CPU frametime spikes; no multi-millisecond game improvement is established yet.
- Default diagnostics, overlays, optional render experiments and startup update checks to off. Preserve explicit saved preferences. Queue diagnostic writes/flushes on a worker and throttle periodic messages.
- Release the XR state lock around per-frame tracking/submission and image ownership calls. Prefetch outer images after Begin; preserve late-Begin captures, pipelined Wait results and retry ownership.
- Add a read-only automatic-start integrity check for signature/hash/activation problems. Valid signatures do not guarantee Easy Anti-Cheat acceptance.
- Retain the 0.3.3 hook, startup and Pimax frame-order fixes. New headset/runtime combinations still need testing.

## 0.3.3 - 2026-11-04

Based on a patch contributed by **Markus Stahl**, reviewed and extended for the Windows build.

- Handle LMU's late `xrBeginFrame` sequence reported with Pimax: begin the runtime frame when quad rendering needs it, then acknowledge the application's later begin without discarding the captured eyes. Keep timestamp, renderability and submission checks intact.

- Skip the FOV-scale hook when runtime settings are inactive, including Stereo VRS.
- Check native hook targets against executable sections and x64 function boundaries. The two known leaf functions without unwind entries require their complete code signatures. This improves rejection of incompatible game builds; it does not guarantee support for future or alpha versions. No executable SHA256 allowlist is reinstated.
- Pin the launcher EXE and its .NET configuration to hashes stored in the protected autostart installation. Verify both before starting a helper and hold read-only file locks for the child lifetime. Reinstall automatic startup after updating the package.
- Clean generated runtime manifests and one-shot autoload INIs older than one day when LMU is not running. Preserve logs, unknown files and linked paths. Keep the newest three generated QVFR profile backups.
- Remove the product's layer-order backup during autostart removal; preserve unrelated registry data and recovery/conflict keys.
- Preserve UTF-8/UTF-16/UTF-32 encoding and BOM in QVFR settings. Refuse malformed or unrecognized encoding without changing the file.
- Add Windows tests for hook boundaries, launcher/config tampering and file locks, cleanup, backup retention and Unicode preservation.

## 0.3.2 - 2026-11-04

- Remove fixed executable SHA256 restrictions from the launcher, diagnostic CLI and native DLL.
  Keep native code-anchor/resource checks and record the actual executable hash
  in run metadata. Alpha builds still require rendering validation.
- Fix automatic-start backup creation for SYSTEM-owned registry keys by using
  a scoped restore privilege. Preserve the original owner and access rules
  without manual Registry Editor ownership changes or granting Users write access.

## 0.3.1 - 2026-11-03

- Read automatic-start activation from a per-user local file, with the older
  registry location as fallback. Fix silent passthrough when the protected game
  cannot see the launcher's HKCU activation. Record early startup/skip reasons
  in `%LOCALAPPDATA%\LMUQuadView\autoload-<pid>.log`.
- Preserve retained implicit overlays above the LMU adapter and place the
  selected provider directly below it. This prevents VectorXR's OSD layer from
  consuming QVFR's quad-view request during automatic startup.
- Fix smaller outer targets rejecting PSVR2's DepthResolve wrapper (role 5).
  LMU can own this texture through its SRV/RTV without a direct resource pointer;
  resolve and validate the actual texture instead of skipping the depth target.
- Defer outer-bank warmup/preparation until a renderable, located XR frame.
  Retry unavailable/unsupported banks up to three times with 120 renderable
  view calls between attempts, retaining full-resolution quad fallback.
- Log individual outer precheck failures and source ownership. Unsupported FSR
  render scaling now explicitly recommends Bilinear for smaller outer targets.
  Log failed quad-view recommendation queries and downstream session creation.
- Optional one-time automatic OpenXR installation: start LMU in Steam's OpenXR
  mode without a custom launch option. Apply saved settings and attach the
  benchmark launcher automatically. Setup also removes the integration.
- A small signed implicit bootstrap loads the LMU adapter. Installation copies
  signed DLLs to Program Files and verifies layer order, with rollback on error.
  Other games pass through; third-party enabled states and relative order remain.
- Keep quad-only OpenGridXR crop isolation, provider ordering, supported-build
  checks and protection against loading the adapter twice. No game file changes.
- Re-sign the launcher, both DLLs and the portable stereo check with SimplySign.
- Validate the running game's path with limited process-information rights.
  Avoid a startup failure when Windows denies reading the game's loaded modules.
- Replace registry-key renaming with verified backup/recreation and rollback.
  This bypasses the registry-rename kernel path implicated in local 0x3B crashes.
  A compiled-import guard runs before launcher tests to reject that API.

## 0.3.0 - 2026-11-03

- So, who wants to get banned by EAC? Use at your own risk 😅🙈
  Seriously: no anti-cheat approval or ban-safety guarantee is claimed. Use offline.

- Start the mod from Steam's Play button using the launch option copied from
  Setup. The launcher automatically applies saved settings, preserves Steam's
  game arguments, adds +XR when missing and stays available for the overlay.
  The supported-game guard and process-local provider/crop isolation still apply.
- I bought a code-signing certificate for this project. The release DLL
  and executables are signed through Certum SimplySign, with SHA-256 and a
  timestamp. Signing identifies the publisher and makes changes detectable;
  it does not guarantee that Windows reputation warnings will disappear.
- If you would like to help with the certificate and ongoing development, you
  can use [Buy Me a Coffee](https://buymeacoffee.com/dansuzuki),
  [Patreon](https://www.patreon.com/cw/DanSuzuki), or my
  [affiliate links and discount codes](https://dansuzuki.com/discounts/).
  Affiliate purchases may earn me a commission at no extra cost to you.
  **Support is entirely optional and is never expected. The tool will always
  be free, with no payment or subscription needed to use its features.**
- Retain the corrected 0.2.1 QVFR handle, rendered-view and OpenGridXR alignment
  fixes. Automatic dynamic FOV remains removed; resolution stays fixed.
- Native and launcher automated tests pass. The new Steam start path has not
  yet been validated in a live Steam/headset session.

## 0.2.1 - 2026-11-03

- Isolate quad launches from OpenGridXR's additional stereo crop, which can
  misalign native outer and focus geometry. Preserve installed profiles and VRS.
- Preserve rendered view metadata across late tracking queries in the same frame.
- Corrected release: remove automatic GPU-driven center FOV resizing and its
  min/max/reserve controls after failed user testing. Ignore obsolete saved
  settings and inherited environment controls. Manual size and gaze remain.
- Show actual per-eye rendered FOV in benchmark overlays.
- Keep benchmark GPU timing separate from other armed GPU profilers.
- Accept valid QVFR handle 1 when uniquely identified in the native eye vector.
- Add same-frame four-view capture and a documented OpenGridXR integration handoff.
- Dream Air SE / SBoys3 / SteamVR / QVFR live validation confirmed aligned,
  steady images after bypassing OpenGridXR. Gaze recovered after closing the
  SteamVR dashboard and restarting eye tracking in Pimax Play.
- Dynamic FOV was off during that successful hardware test and was subsequently
  removed. Wider headset compatibility remains unverified.

## 0.2.0 - 2026-11-02

- **Rendered crop is still highly experimental.** Please test and report image,
  menu/HUD, head-motion or performance issues through GitHub Issues. Include your
  headset/runtime/provider, crop settings and reproduction steps. Reset crop to
  100/100 to disable it. No reliable FPS or CPU improvement has been demonstrated.
- Keep menus and HUD at their uncropped physical size when vertical crop changes
  LMU's UI mesh. Preserve native curvature, depth and saved scale preferences.
- Fix rendered crop with QVFR: embed the smaller peripheral render into a black
  full-FOV output so QVFR does not stretch it against the focus views. Preserve
  submitted pose compensation and focus geometry. This adds one peripheral blit
  per eye when cropping; full-size output swapchains remain allocated.
- Allow the independently configured VectorXR overlay alongside mbucchia QVFR;
  remove the blanket VectorXR layer block in QVFR GUI and CLI launches.
  Keep VectorXR's own Quadviews feature off when QVFR is the selected provider.

- Add experimental NVIDIA Stereo VRS as an alternative to Quad Views. It keeps
  two original eye renders, with 1x1 shading inside the focus rectangle and
  selectable 1x1, 2x2 or 4x4 peripheral shading in verified world-sort scopes.
- Auto gaze uses OpenXR eye-gaze input when available; missing, inactive or
  invalid gaze falls back to the fixed focus region. Focus size/offsets apply
  per eye. Unsupported VRS hardware retains full-rate stereo.
- Preserve all 0.1.2 quad crop, pose compensation, offset, benchmark overlay,
  joystick capture, report and diagnostic features. Quad remains the default.
  VRS uses ordinary stereo resolution/in-game crop rather than quad-only sizing.
- Add explanatory hover help, an explicit experimental VRS selector and shading
  rate control. Existing experimental CPU upload reuse/fast-path switches remain
  available in Quad Views mode.
- Integrate `experiment/performance`: optional outer CPU upload reuse and smaller
  outer SSR resources, with dependency-aware Experimental toggles and hover help.
  Both are off by default and Quad Views-only; SSR can soften reflections.
- Full native SPS remains unfinished and is not included as a 0.2.0 feature.
  VRS can reduce shading work; removing extra quad traversals may reduce CPU work.
  No measured LMU FPS or frametime improvement is claimed.

## 0.1.2 - 2026-11-02

- Add opt-in head-locked Minimal/Detailed benchmark overlays, Dan Suzuki UI-kit
  styling, live placement, separate DirectInput joystick bindings, and manual
  lap-length captures. Hiding the overlay does not stop recording.
- Save bounded local CSV checkpoints and HTML/JSON reports with frame-time
  percentiles, sparse app GPU spans, actual resolutions, data-loss counts and
  explicit measurement limits. No hardware-utilization or compositor FPS claims.
- Add live and latest-run diagnosis, including missing logs, invalid resources,
  invalid poses, recovered projection errors and overlay-only failures.
- Add native/managed ABI, ownership, sparse GPU, report, input-state and layout
  tests. Real headset/joystick testing remains required.

- Preserve submitted stereo pose corrections in all four views instead of rejecting
  changed reference spaces or overwriting upper-layer compensation.
- Add independent 25–100% rendered crop width/height in the launcher. Crop reduces
  outer FOV and target dimensions together; focus resolution stays unchanged and
  its FOV is kept inside the crop. Settings apply after restarting LMU.
- Add pose, crop geometry, fake-runtime and launcher/layout regressions.
- Fix MSVC build argument quoting for checkout paths containing spaces.
- Qubic hardware and cropped compositor output still require tester validation;
  no measured GPU improvement or live-setting support is claimed.

## 0.1.1 - 2026-11-01

### Added

- Horizontal and vertical focus-region offsets for fixed and eye-tracked
  foveation, available with Native OpenXR, QVFR and VectorXR.
- Saved signed controls and Reset offsets. Positive values move right/up;
  negative values move left/down. Units are percent of each eye's full projected
  view span, from -50 to +50. Existing settings default to zero.
- Equivalent CLI parameters and offset values in diagnostic run metadata.

### Improved

- Sidebar branding shares a consistent left edge across font sizes.
- Numeric controls leave more space between the value and spinner arrows.

### Behavior and scope

- Offsets follow the provider's current focus position without accumulating or
  changing pose, projected box size, render resolution or GPU passes. Rendering,
  the green outline and submitted composition use the same shifted focus FOV.
- Zero preserves the provider placement exactly. Offsets are applied by the mod,
  rather than written to provider profiles. Large offsets can move the region
  beyond the outer-view edges. Save and restart LMU to apply changes.
- Full native single-pass stereo is deferred to a 0.2.0 research target. The
  separate SPS branch's new diagnostics and planning code are not included.
  This release makes no new CPU/FPS improvement claim.

## 0.1.0 - 2026-11-01

First numbered release after the provider and stereo research branches were merged.
This supersedes earlier date-based development builds and `v0.1.0-alpha`.

### Added

- Optional VectorXR provider alongside Pimax/native OpenXR and mbucchia's
  Quad-Views-Foveated. Providers are installed separately.
- Saved diagnostics switch and opt-in CPU/GPU render profiling.
- Contributor guide, architecture overview and performance measurement guidance.
- Consistent release version in the launcher, both binaries and portable package.
  Packaging rejects binaries whose version differs from `VERSION`.

### Improved

- Shared frame submission uses stack storage for up to 16 input layers, removing
  heap allocations from the common conversion path. Larger submissions keep a
  dynamically sized fallback. Layer ordering, projection validation and image
  release behavior are preserved.
- Outer UI drawing uses cached dimensions validated during outer capture,
  eliminating repeated COM resource/descriptor queries. It captures/restores
  D3D11 state only when there is an acquired outer target to draw into.
- Launcher discovery tolerates malformed unrelated implicit-layer manifests while
  continuing to validate explicitly selected provider manifests strictly.
- MinGW build compatibility for the imported stereo research tests.

### Experimental / known limits

- Stereo shader conversion, private paired-draw checks and CPU upload reuse/batching
  remain opt-in research. Full game-renderer single-pass stereo is not implemented;
  original eye draws and additional focus scene traversals still run.
- Synthetic tests confirm lower submission bookkeeping cost. They do **not** prove
  that reported +4-5 ms CPU overhead or spikes are fixed. Matched in-headset QVFR
  and VectorXR captures are still needed. See [performance notes](docs/PERFORMANCE.md).
- No resolution, FOV, gaze behavior, rendering quality or default research settings
  are changed by the performance pass. Headset compatibility remains dependent on
  the provider, runtime and supported LMU build.
