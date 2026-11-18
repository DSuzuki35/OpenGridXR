# Changelog

Release numbers use `MAJOR.MINOR.PATCH`. The root `VERSION` file supplies the
launcher title, Windows EXE/DLL metadata and default portable ZIP name.
Versions before 1.0 remain experimental and may change compatibility or settings.

## Unreleased (1.1.0)

Thanks to **Markus Stahl** for the 1.1.0 changes. NVIDIA MVR from 1.0.0 is available under **Settings > Performance**. It keeps the per-eye image check, scene reuse and native fallback.

The merge review fixed the identity used for the protected provider installation, the companion’s initial settings load, and cleanup when native engine hooks are refused. A timed-out OpenGrid channel now retries its handshake. For protected SimHub folders, the launcher builds the plugin as the current user and shows the two files to copy with Explorer.

**Reinstall automatic startup from Settings after updating** (it now carries the
built-in OpenGrid XR layer and the built-in rendering providers).

- **Security review (second pass).** Signatures are verified in-process with
  `WinVerifyTrust` (`Authenticode.cs`) instead of `powershell.exe`; same rule as before
  (valid, pinned signer, timestamped). The elevated automatic-start installer now also
  requires that signature on the bundled OpenGrid XR and Quad-Views-Foveated DLLs, and
  `tools/sign-release.ps1` signs them; `package.ps1 -RequireSigned` and the installer
  build check it. **Release builds must sign after building OpenGrid XR and the
  providers.** OpenGrid XR's shared channels are created for the current user and SYSTEM
  only and are initialised under their lock (two processes opening at once could stay
  disconnected). The SimHub plugin build no longer takes its compiler from an
  environment variable and survives a SimHub path ending in a backslash. The companion
  polls its settings by file metadata and reuses capture buffers.
- **Review of bundled code and the launcher window.** MinHook 1.3.4 and the
  Quad-Views-Foveated sources match their pinned upstream commits. The official
  Quad-Views-Foveated DLL shipped by Linux-built packages is accepted by its pinned
  SHA-256 and keeps mbucchia's signature. The engine hooks are switched on and off in one
  MinHook pass (the game's threads are suspended once instead of once per hook, and never
  run with only part of the hooks active). Launcher: *Launch LMU* and *Install update*
  now save changed OpenGrid XR settings (and the SimHub options) instead of leaving them
  behind; saving a profile no longer makes unsaved OpenGrid XR edits look saved; an
  attached launcher no longer stacks "save changes?" dialogs when LMU exits; the
  benchmark preview and overlay reuse one pixel buffer (no PNG round trip, no second
  rendering).
- **Quad-Views-Foveated with non-ANSI user folders.** QVFR reads `%LOCALAPPDATA%` as an
  ANSI string; with a Windows user name outside the system code page it found no
  `settings.cfg` and silently ignored the launcher's resolution and focus values. LMU now
  gets the folder's short (8.3) name as `LOCALAPPDATA` for QVFR launches (launcher and
  automatic startup; same directory). Without a short name the launcher says so and
  suggests VectorXR.
- **VectorXR resolution note.** The Rendering page explains what the inner resolution
  does with VectorXR: above 100% the whole image sent to the headset grows with it, below
  100% the focus area is softer than native.
- **Diagnose silent automatic-start failures under Easy Anti-Cheat.** The bootstrap
  writes one line per process to `%LOCALAPPDATA%\LMUQuadView\autoload-seen.log`
  (PID, executable path) before the executable-name check, only when this user has
  activated automatic startup, capped at 256 KiB. The LMU autoload log now records
  PID and executable path at loader entry. If a protected Steam start leaves no line
  at all, the DLL was never run in the game (layer load refused). Settings > Launch
  from Steam and `docs/STEAM-LAUNCH.md` explain this case and how to confirm it. The
  launcher starts LMU without EAC protection, so launcher starts are not evidence
  that EAC accepts the layer.
- **German user interface.** Settings > *Language / Sprache*: Automatic (Windows
  language), English or Deutsch; applies after restarting the launcher. The translation
  table `src/LmuQuadView.Launcher/Strings.de.tsv` covers every page, dialog and message.
  Settings files, profiles, logs and reports stay language-neutral.
- **A/B comparison on the Benchmark page.** Pick two recordings: the launcher compares
  FPS, frame interval P95/P99/max, CPU-side app time, provider EndFrame and GPU span,
  shows the change in percent and a verdict (differences below 2 % count as noise), warns
  about partial or short recordings, dropped samples or different resolutions, and writes
  `logs/benchmark/comparison-*.html`. Each recording now stores the settings it was made
  with (`"label"` in the summary JSON).
- **Switch profiles while driving.** New actions *Next profile* / *Previous profile*
  for controller bindings and SimHub (`OpenGridNextProfile`, `OpenGridPreviousProfile`;
  15 SimHub actions in total). They step through the saved profiles that contain
  OpenGrid XR settings and apply that part live (color, sharpening, HUD, frame
  generation, helmet, overlays); rendering settings still apply on the next launch. The
  Profiles page shows the profile switched to.
- **LMU build check before launch.** Settings > Game installation shows whether the
  installed `Le Mans Ultimate.exe` is the tested build and what the last session reported.
  The mod writes `%LOCALAPPDATA%\LMUQuadView\last-session.ini` (hooks accepted or the
  reason they were refused). If the last session with this build refused the hooks, the
  launcher asks before starting.
- **SimHub live preview.** The SimHub page shows the values the plugin reads right now,
  with the unsaved options applied (FPS, statistics, budget, resolution, FOV, benchmark
  state).
- **VectorXR with OpenGrid XR HUD, frame generation and spatial overlays** (OpenGrid XR
  page, off by default, not yet headset-tested): the engine that composes VectorXR's
  quad views also uses the Performance HUD, frame generation and overlay settings. The
  OpenGrid XR layer itself (color, sharpening, helmet, Clean Output) stays off in quad
  views.
- **Rendered FOV crop moved behind an advanced toggle** (*Show rendered FOV crop
  (advanced)*). It opens automatically when a crop is set and asks for headset reports.
- **Windows CI.** `.github/workflows/build.yml` builds the release on `windows-2022`
  (`tools/release.ps1` including the providers), compiles the SimHub plugin against API
  stubs, runs the telemetry and automatic-start integrity tests and uploads the unsigned
  package and launcher screenshots.
- Cleanup: the unused QVFR/VectorXR manifest settings are dropped from
  `launcher-settings.xml` on the next save; the native sources follow
  `mod/.clang-format` (120 columns); `native_outer_bank_test` is skipped under Wine
  (`LMU_QV_WINE_ALL=1` runs it anyway).
- Fixes from a code review:
  - `benchmark_frame_test` failed since the SimHub telemetry channel was split from the
    benchmark overlay (it never asked for the overlay); it now does.
  - Per-lap benchmark: a frame log that could not be created (no write access, disk full)
    left the controller "running" with a callback writing to a missing log on every tick.
    The log is now opened before the recording starts, and the callback is cleared on finish.
  - `%LOCALAPPDATA%\LMUQuadView\autoload-<pid>.log` was written for every LMU start and
    never removed. The launcher now keeps the 10 newest and anything younger than 14 days.
  - `autoload-seen.log` is written only when this user activated automatic startup (the
    `autoload.ini` exists), as documented; before, the folder alone was enough, and every
    launcher-started LMU session creates it.
  - The one-shot automatic-start request (`runtime\autoload-<nonce>.ini`) is also deleted
    when its content is invalid.
  - OpenXR adapter: `xrLocateViews` and `xrCreateSwapchain` reject a missing `info`
    (spec-invalid) instead of dereferencing it; an outer swapchain that reports zero images
    on the second enumeration is refused; unreachable cleanup code in `xrCreateSwapchain` removed.
  - Launcher: an exception from the profile export/import file picker no longer ends the
    launcher; live profile switching compares names like the profile store (ordinal,
    case-insensitive) instead of by the current culture.
  - Large path buffers (64 KiB each, up to two per call) no longer sit on LMU's thread
    stacks in the adapter, the engine hooks, the automatic-start bootstrap and the built-in
    OpenGrid XR layer; new `mod/win_strings.hpp` reads environment values and module paths
    on the heap and never throws.
  - Profile rename, delete, export and import are ignored while a launch or setup step is
    running, like save and load.
  - Engine hooks: a diagnostic binding probe validates the renderer slot before reading it;
    the duplicated session reset in `engine_stop` is shared.
  - `tools/test-native-wine.sh` compiles shaders with the pinned Microsoft `d3dcompiler_47`
    (already downloaded by `fetch-deps.sh`, never shipped) instead of Wine's HLSL compiler,
    and lists the four tests that fail only through Wine's D3D11 emulation separately
    (`WINE-LIMIT`, with the reason). Under Wine: 52 passed, 0 failed (before: 49 and 7).
- **SimHub built in; no longer experimental.** New page **OpenGrid XR > SimHub** (sidebar
  entry below OpenGrid XR):
  - Finds SimHub, **builds the plugin on this PC** with the .NET Framework compiler that
    ships with Windows against the installed SimHub, and installs, updates or removes it.
    The page shows whether the installed plugin matches the launcher. No PowerShell script
    needed (`simhub/build-simhub-plugin.ps1` remains as a manual fallback).
  - **Native live data channel**: *Publish live data for SimHub* opens the mod's data
    channel without diagnostics or the benchmark and without a headset overlay
    (`LMU_QV_TELEMETRY`); *Include GPU frame time* adds GPU timestamps
    (`LMU_QV_TELEMETRY_GPU`). Also for Steam automatic startup.
  - Options (`%LOCALAPPDATA%\LMUQuadView\simhub.ini`): averaging window, stale timeout,
    millisecond decimals, target FPS with presets, missed-frame tolerance, six property
    groups and three action groups. Live values reach a running SimHub within two seconds.
  - New properties: `FrameMsMin`, `FrameMsP95`, `FrameMsP99`, `FpsLow1Percent`,
    `WindowFrames`, `TargetFps`, `FrameBudgetMs`, `HeadroomMs`, `MissedFramesPercent`,
    `BelowTarget`, numeric outer/focus widths and heights, `ProcessId`, `Frames`,
    `DiagnosticReason`.
  - New actions: `ShowOverlay`, `HideOverlay`, `StartRecording`, `StopRecording` and seven
    OpenGrid XR actions (HUD, HUD layout, frame generation, spatial overlays, recenter,
    color preset, visor), executed by the companion like controller bindings.
- **Advanced settings are grouped under Performance and Diagnostics.**
  - **Performance** (Quad Views only, off by default): *Smaller outer render targets*,
    *Smaller outer reflection (SSR) resources* and one **CPU upload optimization**
    selector that replaces the three former toggles (upload reuse, outer upload reuse,
    upload fast path): Off / skip unchanged focus uploads / skip unchanged focus and
    outer uploads / fast path. The three saved flags stay in `launcher-settings.xml`
    and profiles, so old settings load with the same mode (fast path wins, as before).
  - **Diagnostics**: logging, render profiling and the developer tools *Stereo draw
    audit*, *Shader program capture* and *Paired rendering check*, which are
    diagnostics for development and not meant for normal play.
  - The native log lines of these paths no longer say "experimental". Their behaviour
    in `lmu_quadview.dll` is unchanged; in-game gains are still unmeasured.
- **All rendering providers are built in; the "experimental" labels are gone.** The
  Rendering menu lists Native OpenXR, Quad-Views-Foveated (mbucchia), VectorXR and
  Stereo VRS (NVIDIA). Nothing has to be installed, registered or located:
  - **Quad-Views-Foveated** 1.1.4 (MIT) is vendored in `third_party/quad-views-foveated`
    (pinned commit and dependency subsets in `NOTICE-LMU.md`), built with CMake by
    `tools/build-providers.ps1` and shipped in `providers/QVFR`. The LMU profile in
    `%LOCALAPPDATA%\Quad-Views-Foveated\settings.cfg` keeps working.
  - **VectorXR** is the VectorXR quad-view compositor already inside the bundled engine
    `opengrid-runtime/OpenGridXR-Enhancements.dll`. VectorXR launches load it directly
    below the adapter and enable its LMU-only quad policy. Outer/inner resolution,
    focus size and Auto/Fixed now come from the launcher (per-launch `vectorxr-lmu.json`);
    the VectorXR app and its LMU Quadviews profile are no longer needed.
  - Native OpenXR and Stereo VRS were already part of the adapter.
  - The QVFR and VectorXR manifest selectors and download links in Settings are replaced
    by a **Rendering providers** card that shows what this package contains. Saved
    manifest paths are dropped from `launcher-settings.xml` on the next save. QVFR can still retain a legacy VectorXR overlay for its independent OSD.
  - Steam automatic startup installs and verifies protected copies of the providers
    (`providers\QVFR`, `opengrid\OpenGridXR-Enhancements.dll`).
- **OpenGrid XR runtime built in.** The OpenXR layer loads straight from the
  package's `opengrid-runtime` folder for Stereo VRS launches (explicit layer above the
  LMU adapter) and from the protected automatic-start installation for Steam starts.
  Nothing is installed or registered and there is no administrator prompt. The
  **OpenGrid XR runtime** card (Install/Update/Remove) is removed, and
  `OpenGridXR-Connection.exe` is no longer packaged. A data marker keeps settings and
  artwork in `%LOCALAPPDATA%\OpenGridXR\runtime`. Clean Output starts from the package.
  A system-wide OpenGrid XR registration from earlier versions is disabled in every
  launch; switch it off for other games on the OpenXR API layers card.
- **Profiles store all OpenGrid XR settings**: every section of the OpenGrid XR page,
  helmet & visor (including the graphic), spatial overlays, controller bindings,
  telemetry, module switches and "Keep OpenGrid XR active in Stereo VRS launches".
  Loading a profile applies and saves them. Exports leave out controller bindings and
  window selections; imports keep local bindings. Profiles from 1.0 still load.
- **The separate OpenGrid XR profile selector is removed** (New, Duplicate, Rename,
  Delete on the OpenGrid XR page); the page edits the one LMU OpenGrid XR profile the
  runtime uses, and named setups live on the Profiles page.

## 1.0.0 - 2026-11-10

Thanks to **Markus Stahl** for the complete 1.0.0 integration. This release adds an opt-in NVIDIA focus rendering path based on [Williem3's Sim-QVFR](https://github.com/Williem3/Sim-QVFR/tree/bb94821e17b2f0992e778fbc74ed019a203b1a39/src/games/lmu) and a native D3D11 crop optimization inspired by [srounce's DXVK work](https://github.com/srounce/dxvk/blob/548008900fff1882239a66f30b7dab9b78e641db/docs/lmu-foveation.md). Dan reports approximately **2 ms lower CPU frametime** in his benchmark scenario with MVR enabled versus ordinary Quad Views. The benefit depends on the scene, hardware and settings.

Under **Settings > Advanced**, selecting NVIDIA MVR enables smaller outer render targets. Use a quad-view provider, Bilinear at native scale, and MSAA in LMU. The launcher identifies conflicts with smaller outer SSR or stereo draw auditing. Save and restart LMU. With diagnostics enabled, `focus_passes_reused` shows how many extra focus passes were skipped. In a separate stationary cockpit test with MSAA 2×, both eyes passed two native image comparisons and the counter reached 2,995 after 3,000 outer passes.

First 1.0 release: the launcher and all helpers are native .NET 10 programs
(NativeAOT, no .NET installation), every OpenGrid XR app feature is part of LMU
Quad View, and legacy code is removed. Settings, profiles and OpenGrid XR data
from 0.9.0 are kept. **Reinstall automatic startup from Settings after updating.**

- Fixed missing right-eye cockpit geometry with experimental NVIDIA MVR. The mod now preserves LMU’s vertex constant buffer when switching shaders and checks each eye against native focus rendering before reusing it. Both eyes passed in a stationary cockpit with MSAA 2×.

- **All remaining OpenGrid XR app features are native (.NET 10); the OpenGrid XR
  app is removed** (Electron editor source, its build and packaging, the "start
  OpenGrid XR with LMU" option and the optional app location):
  - **Helmet & visor**: all helmet settings, sun strip with printed text and an
    uploaded graphic projected onto the curved visor, preview, "fit crop to helmet".
    A SkiaSharp port of OpenGrid XR's compositor renders the runtime images on save
    (including live visor variants and the Clean Output helmet).
  - **Spatial overlays**: up to 15 monitor, application-window or headset-menu
    panels with placement, size, rate, opacity, head pointing and stream
    visibility. The new `LMU-QuadView-Companion.exe` (NativeAOT, starts and stops
    with LMU) captures them and feeds OpenGrid XR's overlay host, which gained a CPU
    pixel input and now ships in `opengrid-runtime`. Web-page sources are not
    supported (no browser engine); capture a browser window instead.
  - **Controller bindings**: DirectInput buttons for HUD, HUD layout, frame
    generation, all overlays, VR color preset, visor position, menu selection,
    desktop pointing and VR centering (LMU's recenter key), on buttons and POV hats
    (D-pad, all eight directions), executed by the companion
    through OpenGrid XR's driving-control and color protocols.
  - **Telemetry**: port of the native LMU reader with a live view; missing values
    stay empty.
  - **Lap benchmark** on the Benchmark page: per-lap application FPS, 1%/0.1% lows,
    percentiles, CPU/GPU time and generated frames from the OpenGrid XR engine, with
    weather/traffic context, optional frame-time CSV and HTML/CSV reports.
  - **OpenXR API layers**: list and switch every registered layer, machine-wide
    changes through one elevated helper call, backup and restore of each change.

- **.NET 10 core library (stage 1).** The launcher's UI-free logic (settings,
  profiles, launch, autostart, updates, OpenGrid XR port) is `src/LmuQuadView.Core`
  (`net10.0-windows`), tested by `src/LmuQuadView.Core.Tests`. Fixes found while
  porting: environment lookups of absent variables (which throw on .NET 10) go
  through a null-safe helper; the `LaunchServiceTests` diagnostics case only passed
  where LMU was installed under `D:\SteamLibrary`; the `AutoStartTests` fixture
  changed a key's ACL without `WRITE_DAC`.
- **NativeAOT automatic-start helper (stage 2).** New `LMU-QuadView-Autostart.exe`
  (`src/LmuQuadView.AutoStart`) prepares the saved settings during LMU's OpenXR
  startup and performs the elevated layer registration/removal. It needs no .NET
  runtime, starts in milliseconds instead of loading the .NET Framework inside the
  game's startup, and imports only Windows system DLLs. `lmu_autostart.dll` verifies
  it against `autostart.sha256` from the protected installation and keeps its image
  locked while it runs; the launcher keeps only the benchmark `--attach` mode.
  **Reinstall automatic startup after updating.** Linux cross builds link it with
  lld-link (`tools/nativeaot`); the core test suite also runs as NativeAOT under Wine.
- Run metadata is now written without reflection (ordered dictionary), and the
  joystick reader uses the generic marshalling APIs, so both work under NativeAOT.

- **New launcher (stage 3): Avalonia 12, NativeAOT.** `LMU-QuadView-Launcher.exe`
  (`src/LmuQuadView.Launcher`) replaces the WinForms launcher with the same pages
  (Rendering, Setup, Benchmark, Profiles, OpenGrid XR) and behavior. It is a single
  native executable plus `libSkiaSharp.dll` and `libHarfBuzzSharp.dll`; no .NET
  installation is needed. Rendering is software-only (Skia on the CPU) so the
  launcher never holds a GPU context next to LMU and the VR runtime. Per-monitor DPI
  aware, keyboard navigable, tooltips also exposed to screen readers. The range
  sliders show the allowed range for the current provider. The OpenGrid XR page now
  tracks unsaved changes by value (undoing an edit clears the warning). Headless UI
  tests (`src/LmuQuadView.Launcher.Tests`) check page logic and render every page;
  `tools/build-dotnet.ps1` builds and tests on Windows, `tools/cross-build.sh` on
  Linux. An in-place upgrade removes files 0.9.0 builds shipped at the old
  locations (launcher `.exe.config`, notes now under `docs/`).

- **Release pipeline on .NET 10 (stage 4).** `tools/release.ps1` builds, tests,
  signs and packages a release in one run (portable ZIP, setup EXE, signed setup,
  `SHA256SUMS.txt`); without a certificate it makes a clearly marked unsigned test
  build. `sign-release.ps1 -Path` signs the setup. The launcher's SkiaSharp/HarfBuzzSharp
  libraries keep Microsoft's signature: packaging, installer build and signing verify
  it instead of requiring the LMU version and signer (the installer build previously
  rejected them). Installer and version tests use a NativeAOT stand-in
  (`src/LmuQuadView.TestFixture`) instead of the .NET Framework C# compiler, so no
  .NET Framework tooling is left in the build. Installer upgrades delete retired files
  only after the reparse-point checks.

- **Legacy cleanup.**
  - Removed the WinForms launcher (.NET Framework 4.8) and its build path; the new
    .NET 10 launcher replaces it (stage 3). `JavaScriptSerializer`, the C# 5
    restriction and `HttpWebRequest` are gone (updates use `HttpClient`).
  - Removed the `--steam %command%` wrapper (superseded by OpenXR automatic startup
    in 0.3.1) and the `HKCU\Software\LMUQuadView\AutoStart` activation fallback;
    the per-user `autoload.ini` is the only activation. The launcher approval no
    longer covers an `.exe.config`.
  - OpenGrid XR no longer contains its own copy of LMU Quad View (0.5.4) or its LMU
    quad/VRS integration (native, editor page, offline/Steam launch helpers, QVFR
    provider, `Lmu*` INI keys). LMU Quad View is the only LMU renderer; the mod's
    `OPENGRIDXR_LMU_INTERNAL` code paths are removed.
  - Removed the PowerShell launch scripts (`launch.ps1`, `portable-launch.ps1`,
    `start-demo.ps1`, `setup-qvfr-profile.ps1`, `test-no-native-outer.ps1`) and
    their tests, and the research prototypes (`prototype/`; the OpenXR headers
    moved to `third_party/`). `tools/start-frame-capture.ps1` now uses
    `LMU-QuadView-Autostart.exe --capture`.
  - Research notes and test-candidate instructions moved to `docs/archive`;
    feature guides moved to `docs/`.
- Fixes: benchmark report times used the current culture's time separator;
  profile-name lookup used culture-sensitive case folding (Turkish `i`) while file
  names compare ordinally; the update size parse and an OpenGrid number format were
  culture-dependent; `tools/opengrid-fixtures.mjs` failed with a relative path.


Compiled and tested automatically (Linux, Wine, installer round trip); not yet
tested in a headset, with a running LMU session or on Windows hardware.

## 0.9.0 - 2026-11-07

Integrates **OpenGrid XR 0.6.0** into LMU Quad View and adds a bug-fix pass.

- New **OpenGrid XR** launcher page that configures OpenGrid XR **without the
  OpenGrid XR app**: runtime install/update/remove, LMU profiles (new, duplicate,
  rename, delete), module switches, VR color correction, sharpening, Performance
  HUD, frame generation, view & crop and Clean Output (open/close on a chosen
  monitor). Native C# port of OpenGrid XR's settings core; it writes the same
  profiles and byte-identical runtime configuration (tested against reference
  files from OpenGrid XR's JavaScript). Packages ship only the native runtime in
  `opengrid-runtime`.
- The OpenGrid XR runtime (layer DLL, Clean Output, connection helper) is now
  built with the mod's MinGW toolchain (`tools/opengrid-runtime`), import-checked
  and shipped in `opengrid-runtime`, including the Enhancements engine (HUD,
  frame generation): its shaders are compiled with Microsoft's d3dcompiler_47
  under Wine (`fxc-lite`), with pinned, hash-checked inputs. No Visual Studio
  needed. The connection helper also accepts a runtime without the engine and
  removes stale copies.
- Helmet design, spatial overlays, bindings, telemetry, lap benchmarks and the
  OpenXR layer editor remained in the optional OpenGrid XR app (replaced by native
  pages in 1.0.0).
- Machine-local option: keep OpenGrid XR active in Stereo VRS (default on,
  previous behavior). Steam automatic startup follows it through
  `LMU_QV_OPENGRID`. Quad launches still always isolate OpenGrid XR.
- Adopt OpenGrid XR's LMU renderer hardening: data-layout validation before
  hooks, bounded outer-image waits (max 5 ms, image kept on timeout), fresh poses
  for parallel tracking-only locate calls without replacing the rendered
  snapshot, rejected same-thread re-entry, and related regression tests.
- Fixes: profile/OpenGrid buttons lost their specific tooltips to the generic
  default help; a failure while filling settings could leave the form in
  "filling" mode and silently skip provider/range updates; launched browser and
  report processes leaked handles; a repeated Steam-mode launch leaked the
  previous game handle; render-profile log used a signed value for `%u`.
- OpenGrid XR: Clean Output framing preview could drop the last drag value when
  the event loop was otherwise idle (timer no longer unreferenced).
- Installer refuses to run while OpenGrid XR is open and skips the LMU version
  and signer check for the bundled OpenGrid XR files.

Not yet tested in a headset. Compare VRS with/without OpenGrid XR on your rig.

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
