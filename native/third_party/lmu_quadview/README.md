# LMU Quad View Mod

A free Windows x64 / D3D11 mod that adds two outer and two focus views to
**Le Mans Ultimate** through OpenXR. Use a native quad-view runtime, or install
Quad-Views-Foveated (QVFR) or VectorXR separately to provide quad views on a
stereo runtime.

**v0.5.4 is an experimental release.** Headset and runtime compatibility
still needs broader testing. Always free; donations are completely optional.

**CPU headroom matters:** preliminary measurements and user reports suggest around
**0.5 ms lower CPU render time** from the optimizations, depending on the setup.
Quad Views still adds roughly **3–4 ms of CPU frametime compared with no mod**.
The small savings do not remove that penalty. Use the mod when you have enough
CPU headroom; on a CPU-limited system it can make performance worse even if GPU
time improves.

Version 0.5.4 retains the **Benchmark** page with Minimal and
Detailed in-headset overlays, separate joystick bindings for visibility and
manual recording, local HTML/CSV/JSON reports, and latest-launch diagnostics.
Enable Benchmark before launching and leave the launcher open. See
[benchmark instructions and measurement limits](BENCHMARK.md). This release
also includes the motion-compensation and mod-owned crop changes below; real
headset and joystick compatibility still needs tester validation.

## Focus FOV and crop compatibility

Quad launches bypass OpenGridXR in the game process because its additional
stereo crop can misalign outer and focus geometry. Its installed profiles are
preserved; its crop, mask and enhancements are unavailable in these sessions.
Native stereo VRS keeps the inherited OpenGridXR preference.

Automatic GPU-driven focus resizing has been removed from the corrected 0.2.1
release after failed user testing. There are no dynamic min/max/reserve controls.
Old dynamic settings are ignored and removed on the next settings save. Focus
size remains manually configurable; provider eye tracking still moves the focus.
Runtime resolution scaling is not implemented.

Enable **Benchmark** and leave the launcher open to see live FOV in both overlay
layouts: actual left/right horizontal × vertical degrees after crop constraints.
Detailed also shows the source frame.
Updates follow the overlay's existing 5 Hz refresh; menu or failed frames show
`--` instead of a stale scene FOV.

Version 0.2.1 also fixes valid QVFR swapchain handle `1` being rejected as a
placeholder, which could result in zero submitted quad projections.

Live testing on Dream Air SE / SBoys3 / SteamVR / QVFR confirmed aligned and
steady images after bypassing OpenGridXR. Gaze recovered after closing the
SteamVR dashboard and restarting eye tracking in Pimax Play. Dynamic FOV was
off during this test. See the [diagnosis](docs/flicker-diagnosis-20261103.md)
and [OpenGridXR integration handoff](docs/OPENGRIDXR-INTEGRATION.md).

## Stereo VRS (experimental)

Choose **Stereo VRS (NVIDIA, experimental)** for two native stereo views with
full-rate shading in a focus rectangle and coarser peripheral shading. Auto
uses OpenXR eye tracking when available, with a fixed fallback; Fixed skips
eye tracking. Start with 2x2; 1x1 is the full-rate control, 4x4 is more aggressive.
Quad Views and all 0.1.2 features remain available. Hover over controls for
short explanations and CPU/GPU tradeoffs. Existing CPU upload experiments remain
quad-only. Native SPS is unfinished and is not included as a feature.

VRS reduces pixel-shader work and avoids the extra quad focus traversals, but
image quality and performance depend on the workload. No LMU speedup is claimed.
See [setup, compatibility and measurement limits](PORTABLE-VRS.txt).

The merged performance experiments add optional **CPU outer reuse** and
**smaller outer SSR** controls in Settings, off by default. They reduce redundant
constant uploads or outer reflection resolution in Quad Views. Hover help
explains dependencies and quality tradeoffs; see
[performance experiment details](PERFORMANCE-EXPERIMENT.md).

## About this project

I built this project in my spare time with substantial help from AI. I'm an
engineer with a good understanding of technology, but I'm not a software engineer
and I don't claim to be one.

It started as an experiment: could this be done, and could I make it work? It
gradually became something that reduces GPU load so much on my own setup that
I want to make it work reliably and keep optimizing it. Those results are my
motivation, not a promise of the same improvement on every system.

If AI-assisted code isn't your thing, you're welcome to skip this project.
If you'd like to help improve it, thoughtful bug reports, testing and code
contributions are welcome. See [the contribution guide](CONTRIBUTING.md).

## CPU cache preference (AMD X3D)

**Settings > Prefer the 3D V-Cache CCD (AMD X3D)** is enabled by default and applies
on the next game launch, including Steam automatic startup. On supported AMD
CPUs with two clearly asymmetric L3 domains, it requests the larger-cache CCD
using Windows process-default CPU sets. It skips single-domain, symmetric,
non-AMD, ambiguous and unsupported multi-group topologies, failed queries and
existing process affinity/default CPU-set assignments. Explicit thread policies
retain Windows precedence. This is a machine preference and is not exported in
shared profiles. Enable diagnostics to see `cpu cache affinity:` decisions.

No LMU performance gain has been measured for this option yet. Compare it on and
off in the same scene; fewer available cores can also hurt other game work.

## SimHub (experimental)

The `simhub` folder contains a SimHub plugin that shows live frame timing,
render resolutions and FOV as `LmuQuadView.*` properties and adds actions to
toggle the benchmark overlay and recording from wheel buttons. It needs
Diagnostics and Benchmark enabled and is built on your PC; see
[simhub/README.md](simhub/README.md).

## Install and launch

Use the **Setup-x64.exe installer** for Start menu and uninstall integration, or extract the complete portable ZIP. Both contain the same native launcher and rendering layer; no Electron runtime is required. Updates are offered in Settings and installation follows your approval. Close LMU before updating, then reinstall automatic startup from the updated package.

Use **Profiles** to save or share rendering settings as `.lmuqv` files. Imports keep your local paths, devices, diagnostics and calibration. Copy the `profiles` folder when moving a portable package. Profile support was contributed by **Markus Stahl**.

Rendering sliders show their current **Allowed** interval. Purple marks the usable range and grey marks unavailable values. QVFR resolution and focus limits update together so the outer image fits the focus buffer; mouse, keyboard and numeric entry use the same bounds. Calibrated headset display units are taken into account. Native/VectorXR retain runtime-controlled settings.

See [CPU work and profiling](docs/CPU-040.md), [installer behavior](docs/INSTALLER.md) and [EAC integrity diagnosis](docs/EAC.md) for details.

1. Use the complete `LMU-Quad-View-Mod-0.5.4.zip` package. Published builds are on [GitHub Releases](https://github.com/DSuzuki35/LMU-Quad-View-Mod/releases).
2. Extract the **complete ZIP** to a writable folder outside the LMU installation.
3. Open **LMU-QuadView-Launcher.exe**. Keep its `.exe.config` file beside it.
4. In **Settings**, select the LMU game folder. Ensure your headset's OpenXR runtime
   is active, and choose your provider in **Rendering**.
5. Choose Auto or Fixed, save settings, and launch LMU offline. Restart LMU after
   changing rendering settings.

Requires Windows x64, D3D11 and .NET Framework 4.8. The distributed launcher does
not require PowerShell or script unblocking. The release DLL and executables
are signed through Certum SimplySign by **Open Source Developer Daniel Sosulski**.
Windows reputation warnings may still appear. OpenXR Toolkit is not required.

### Start with Steam Play

Save settings, close LMU, then use **Settings > Install / update automatic startup**.
Approve Windows' administrator prompt once. Remove any old `--steam %command%`
wrapper from Steam's custom Launch Options, preserving unrelated game arguments.
Use Steam's **OpenXR** launch mode. No custom Steam option or background starter
is required. Flat-screen and OpenVR launches do not activate this OpenXR mod.

The installer registers a signed implicit OpenXR bootstrap and copies its DLLs
to Program Files. It runs the adapter only in the configured, supported LMU
process, applies saved settings, and attaches the minimized launcher when the
benchmark is enabled. Other OpenXR applications pass straight through.
Do not run LMU as administrator. Keep the extracted package in place; reinstall
automatic startup after moving or updating it. Changes to rendering settings
still require a game restart. **Remove automatic startup** reverses registration.
See [Steam startup details](docs/STEAM-LAUNCH.md) for the optional legacy wrapper.

| Provider | Configuration |
| --- | --- |
| Stereo VRS | Native stereo runtime and a compatible NVIDIA GPU/driver. Focus size, offsets and peripheral shading rate are set in the launcher. No quad provider required. |
| Native OpenXR | Requires a runtime exposing quad views. Resolution and FOV come from runtime settings. |
| Quad-Views-Foveated | Install mbucchia's QVFR separately; Settings links to upstream instructions and can select its manifest. Configure resolution and focus size in the launcher. See [QVFR instructions](PORTABLE-QVFR.txt). |
| VectorXR | Install separately, enable its LMU Quadviews profile, and select VectorXR. Settings can locate its manifest or accept a JSON path. Configure resolution, focus and tracking in VectorXR; launcher QVFR sizing controls are inactive. See [VectorXR instructions](PORTABLE-VECTORXR.txt). |

The launcher starts LMU with process-local provider settings and layer ordering.
The optional automatic-start installation registers its own implicit layer first,
preserving third-party enabled states and relative order. Neither path modifies
the game EXE or active runtime.
QVFR profile changes are scoped to LMU, backed up and applied atomically. VectorXR
launch preparation preserves installed manifests and disables conflicting layers
for the launched process.

## Settings and calibration

Auto queries the provider's tracking capability before requesting tracked or
fixed focus views; working gaze tracking depends on the provider. Fixed requests
stationary focus views. Use the green outline to inspect focus placement.

**Focus position** adds horizontal and vertical offsets for all three providers,
in both Auto and Fixed modes. Positive horizontal moves right; positive vertical
moves up. Negative values move left/down. Values range from -50% to +50% of the
full view on each axis, measured in projection space. For example, +5% horizontal
shifts the region right by 5% of that eye's full projected view width.

In Auto, the offset is relative to the provider's current gaze position; eye
tracking continues to move the region. The shift does not change its projected
size or render resolution. It is not clamped to the outer-view edges, so large
offsets can put part of the region outside the visible view when crop is off. **Reset offsets**
sets both to zero, restoring provider placement exactly. Save and restart LMU
after changing offsets. Existing settings default to zero.

QVFR exposes outer/inner resolution and independent focus width/height. Resolution
percentages scale each dimension: 200% means twice the width and height, or four
times the pixels. Focus dimensions use QVFR's supported 10–90% range.

For equal-axis focus controls, run LMU once, then click **Read latest headset
FOV**. With a measured FOV, 50/50 produces an approximately square region near the
center. Before calibration, a fresh installation explicitly displays per-axis
percentages. Existing settings preserve their region. Calibration reads the latest
local run; it is a snapshot, not live headset detection. Re-read after changing
headset, runtime or FOV. QVFR can widen or clip the region near gaze limits.
Stored percentages and native rendering are unchanged.

Outer smoothing has been removed from the launcher. Legacy smoothing settings
are ignored, and launcher starts explicitly disable that filter.
It does not replace temporal anti-aliasing. The smaller outer-target optimization
can be disabled in Settings for diagnosis. Launcher crop removes edge coverage
without changing the angular image scale. Cropped peripheral renders are padded
with black before composition; full-size output swapchains remain allocated and
the padding adds one GPU blit per eye. In-game crop support remains deferred.

**Rendered FOV / crop** controls the mod's own crop independently of LMU's saved
crop settings. Width and height each range from 25–100%; 100/100 is the full view.
**Crop is still highly experimental. Please test and report your results through
[GitHub Issues](https://github.com/DSuzuki35/LMU-Quad-View-Mod/issues).** Include your
headset, runtime/provider, crop values and reproduction steps, especially for
image alignment, menus/HUD or head-motion problems. Use 100/100 to disable crop.
The available short cockpit measurements do not establish a reliable FPS or CPU
gain; do not expect performance to improve in proportion to the crop percentage.
Lower values trim the centered projected view without zoom, leaving uncovered
edges black. Outer target dimensions shrink with the retained tangent-space FOV;
75% width and 50% height use approximately 37.5% of the original outer pixels.
This is not a total GPU/frame-time saving: focus textures keep their resolution,
PPFX intermediates may remain full-size, and the full-size outer fallback loses
the native outer-render saving. Focus follows gaze until the crop boundary, where
it is kept inside; a focus region larger than the crop is narrowed to fit.
Save and restart LMU to apply. Resolution and FOV controls are not live controls.

The motion-compensation candidate preserves incoming projection poses and transfers
their correction to the focus views, including when a layer changes reference space.
Fake-runtime regressions pass; actual Qubic compensation and cropped composition
still need headset validation. Disable crop (100/100) for the first Qubic A/B test.

## What this release does

The adapter presents stereo to LMU and submits four rendered views downstream.
The game still performs its original eye draws plus **two additional focus scene
traversals**. A smaller native outer render-target bank reduces outer work where
compatible; otherwise full-size outer rendering and resampling provide a fallback.
Auxiliary swapchains are identified using native eye handles rather than size or
creation order.

Shared-path CPU housekeeping avoids per-frame heap allocations for submissions
of up to 16 layers (larger lists use a fallback), borrows the outer RTV with cached
dimensions, and skips D3D state capture when there is no outer UI draw. These are
not full single-pass stereo, and no confirmed 4–5 ms frametime improvement is
claimed. See the [measured results and comparison procedure](docs/PERFORMANCE.md).

Experimental upload batching/reuse and private paired-draw checks remain disabled
by default. They require explicit opt-in and do not remove normal eye draws.
See [CPU fast-path research](CPU-UPLOAD-FASTPATH.md),
[upload reuse](CPU-UPLOAD-REUSE.md), [stereo research](PORTABLE-STEREO-RESEARCH.txt)
and the [renderer handoff](INSTANCED-STEREO-HANDOFF.md) for their scope and limits.

Full single-pass stereo is a **0.2.0 research target**, not part of 0.1.2 or a
guaranteed release date. It still needs shared native preparation, correct
per-eye intermediate buffers and render order, image/depth equivalence and
matched CPU/GPU measurements. The [SPS research branch](https://github.com/DSuzuki35/LMU-Quad-View-Mod/tree/codex/sps-0.1.1)
retains its historical name; its additional diagnostics are excluded from this
offset release.

Previously validated LMU executable SHA256:
`66942337813B301B87D12EDFA330F33354A2A4B8CD9B048885CBF77EB6F940CF`.
Different executable hashes are accepted. Native hook signatures and resource
checks remain enforced; alpha branches and game updates still need rendering
validation. No Studio397 endorsement or anti-cheat approval is claimed.
Broad headset support is a target, not a guarantee.

## Diagnostics and support

Report bugs through this repository's GitHub Issues. Include the mod version,
LMU build, headset, GPU/driver, runtime/provider versions, rendering settings,
reproduction steps and expected versus actual result. Attach the actual
`logs/quad-*.log`, not just the matching `*-run.json` launch summary. Review files
for personal paths before sharing. A started process alone does not establish
correct VR rendering.

**Settings → Enable diagnostics and logging** controls mod logs, per-run metadata
and diagnostic collection on the next launch. It defaults to disabled. Provider
and runtime diagnostics are controlled separately.

For a performance report, enable diagnostics and **Enable render profiling**,
choose 30–300 seconds (120 default), then press **Ctrl+Alt+F9** while driving.
Exit normally to save partial captures. Forced termination may lose final results.
CPU scopes overlap and include waits; sparse GPU timings omit some work. Follow
[profiling instructions](RENDER-PROFILING.txt) for matched comparisons.

## Build and contribute

See [CONTRIBUTING.md](CONTRIBUTING.md) for Windows x64 prerequisites, exact build
and test commands, review expectations and release steps. The
[architecture guide](docs/ARCHITECTURE.md) maps the launcher, XR adapter, hooks and
resource ownership. Native builds run tests; graphics tests use D3D11 WARP and
cannot replace headset QA. Release versions come from the root `VERSION` file.

## Contributors

**Markus Stahl** has contributed hook safety, launcher integrity, cleanup, QVFR encoding preservation, portable profiles, SimHub integration, performance improvements, per-eye VRS caching, CPU cache preference and stability fixes. Huge thanks to Markus for the detailed patches, continued testing and all his help improving this project.

## Optional support

I bought a code-signing certificate so the distributed DLL and executables can
identify their publisher. If you would like to support the certificate and
development, there is [Buy Me a Coffee](https://buymeacoffee.com/dansuzuki),
[Patreon](https://www.patreon.com/cw/DanSuzuki), and my
[affiliate links and discounts](https://dansuzuki.com/discounts/).
Affiliate purchases may earn me a commission at no extra cost to you.
**Support is completely optional and never expected. This tool will always be
free; no donation, purchase or subscription is required for any feature.**

## License

Original source is licensed under [PolyForm Noncommercial 1.0.0](LICENSE.md):
source-available for permitted noncommercial reuse, **not OSI open source**.
[Third-party licenses](THIRD_PARTY_NOTICES.md) remain separate. No rights to LMU or
its assets are granted. The mod is always free; donations are optional.
