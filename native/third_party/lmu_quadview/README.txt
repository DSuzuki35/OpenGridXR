LMU QUAD VIEW MOD — v0.5.4

CPU PERFORMANCE
Preliminary measurements and user reports suggest around 0.5 ms lower CPU
render time from the optimizations, depending on the scene and setup.
Quad Views still adds roughly 3-4 ms CPU frametime compared with no mod.
It is worthwhile only with enough CPU headroom. If already CPU-limited,
performance can get worse even when GPU time improves.

SIGNED BUILD
I bought a code-signing certificate for this project. The DLL and executables
are signed through Certum SimplySign, with SHA-256 and a timestamp.
Publisher: Open Source Developer Daniel Sosulski. Windows reputation warnings
may still appear.

STEAM PLAY
Save settings, close LMU, then use Settings > Install / update automatic startup.
Approve the Windows administrator prompt. Clear the old --steam %command%
wrapper if you used it, preserving unrelated game arguments. Choose Steam's
OpenXR launch mode. No custom launch option or background starter is needed.
The signed implicit layer applies saved settings only in supported LMU sessions.
The benchmark launcher attaches minimized when enabled and closes with LMU.
Keep this package in place; reinstall after moving/updating it. Do not run LMU
as administrator. Use Settings > Remove automatic startup to undo registration.
See docs/STEAM-LAUNCH.md for details and the optional legacy wrapper.

OPTIONAL SUPPORT
Buy Me a Coffee: https://buymeacoffee.com/dansuzuki
Patreon: https://www.patreon.com/cw/DanSuzuki
Affiliate links and discounts: https://dansuzuki.com/discounts/
Affiliate purchases may earn me a commission at no extra cost to you.
Support is entirely optional and never expected. This tool will always be free.
No donation, purchase or subscription is required for any feature.

CORRECTED 0.2.1
Automatic GPU-driven focus resizing was removed after failed user testing.
Manual focus size, eye tracking and the actual rendered FOV overlay remain.
Old dynamic settings are ignored. Runtime resolution scaling is not implemented.

Quad launches bypass OpenGridXR to prevent its additional stereo crop from
misaligning outer and focus images. Installed profiles are preserved.
See docs/flicker-diagnosis-20261103.md and docs/OPENGRIDXR-INTEGRATION.md.

STEREO VRS (NVIDIA, EXPERIMENTAL)
Choose Stereo VRS for two native eye views with reduced peripheral shading.
Auto eye tracking falls back to a fixed region when unavailable. See
PORTABLE-VRS.txt. All 0.1.2 features remain; unfinished native SPS is excluded.
Hover over launcher options for explanations and performance tradeoffs.
Settings also includes Experimental CPU outer reuse and smaller outer SSR toggles,
off by default and Quad Views-only. See PERFORMANCE-EXPERIMENT.md.

Free experimental Windows x64 / D3D11 quad-view integration for Le Mans Ultimate.
Always free; donations are completely optional. Use offline. No Studio397
endorsement or anti-cheat approval is claimed.

BENCHMARK OVERLAY (EXPERIMENTAL)
Open Benchmark, enable it before launch, and leave this launcher running.
Minimal/Detailed overlays, live placement, separate joystick show/hide and record
bindings, and manual start/stop captures are available. HTML/CSV/JSON reports
are saved under benchmarks/. See BENCHMARK.md for setup and measurement limits.
Diagnose latest launch explains common log failures without exposing raw paths.
No live headset/physical joystick validation has been performed for these features.

ABOUT THIS PROJECT
I built this in my spare time with substantial help from AI. I'm an engineer
with a good understanding of technology, but I'm not a software engineer and
don't claim to be one. It began as an experiment to see whether this could be
done and whether I could make it work. The GPU-load reduction on my own setup
motivated me to make it reliable and keep optimizing it; results can vary.
If AI-assisted code isn't your thing, you're welcome to skip this project.
Bug reports, testing and code contributions are welcome.

INSTALL
Extract the WHOLE ZIP to a writable folder outside LMU. Open
LMU-QuadView-Launcher.exe and keep its .exe.config file beside it.
Requires .NET Framework 4.8. No PowerShell installation or script unblocking is
needed to run the distributed launcher. Release binaries are digitally signed.

In Settings select the LMU folder. Ensure the headset's OpenXR runtime is active.
In Rendering select Native OpenXR, Quad-Views-Foveated (QVFR), VectorXR, or
Stereo VRS (NVIDIA, experimental).
Select Auto or Fixed, save, then launch LMU offline. Restart after settings changes.

PROVIDERS
Native requires a quad-view runtime and uses its resolution/FOV settings.
QVFR must be installed separately; Settings links to upstream instructions and
accepts an optional manifest path. See PORTABLE-QVFR.txt.
VectorXR must be installed separately with its LMU Quadviews profile enabled.
Select VectorXR and configure resolution/focus/tracking in its app; the launcher's
QVFR sizing controls are inactive. See PORTABLE-VECTORXR.txt.
OpenXR Toolkit is not required. The game EXE, global OpenXR registration and active
runtime are not modified. LMU-only QVFR profile changes are backed up.

SETTINGS
Resolution percentages scale width and height: 200% means four times the pixels.
QVFR focus dimensions support 10–90%. Run LMU once, then use Read latest headset
FOV for equal-axis controls: 50/50 is approximately square near the center.
Before calibration fresh controls show per-axis percentages. Old settings retain
their region. Re-read after changing headset, runtime or FOV; this is a snapshot.
QVFR may widen/clip focus near gaze limits. Fixed requests stationary focus;
Auto depends on provider tracking capability. Use the green outline to inspect it.
Focus position offers horizontal/vertical offsets for all providers, Auto and
Fixed. Positive moves right/up; negative moves left/down. Each value is -50..50%
of that eye's full projected view width/height. Auto keeps following gaze with
the requested offset. Focus size and render resolution stay unchanged. Large
offsets may place part of the region outside the visible view; offsets are not
clamped to the outer edges when crop is off. Reset offsets restores zero/provider placement.
Save and restart LMU to apply. Old settings default to zero.
Outer smoothing has been removed; legacy values are ignored by the launcher.
The smaller outer-target optimization can be disabled in Settings for diagnosis.
Rendered FOV / crop is the mod's own crop: independent width/height, 25..100%.
Crop is still HIGHLY EXPERIMENTAL. Please test and report results or problems:
https://github.com/DSuzuki35/LMU-Quad-View-Mod/issues
Include headset, runtime/provider, crop values and reproduction steps, especially
for image alignment, menus/HUD or head motion. Reset to 100/100 to disable crop.
No reliable FPS or CPU gain has been established by the short cockpit tests.
100/100 is full view. Lower values trim the centered view without zoom and reduce
outer target dimensions; uncovered edges are black. Focus resolution is unchanged.
Focus is kept inside the cropped view, and narrowed if it cannot fit. Actual GPU
savings depend on the smaller native outer bank; full-size fallback/PPFX costs remain.
Restart LMU for crop, resolution or FOV changes. Live controls are not implemented.
LMU's own crop settings remain bypassed. Native hook and resource checks remain enabled.

MOTION COMPENSATION TEST CANDIDATE
Submission preserves compensated poses and accepts changed projection spaces.
First test Qubic with crop at 100/100, then compare compensation on/off. A valid
test should show quadProjections=1 in the actual quad log and correct stable
tracking. Then test crop separately. Fake-runtime tests do not verify a real rig.

LIMITS
Original eye draws plus two focus scene traversals remain. Shared-path CPU
housekeeping improvements are not full single-pass stereo or a confirmed 4–5 ms
fix. Experimental upload reuse/batching and paired-draw checks default off.
Compatibility and performance still require headset testing.
Full single-pass stereo remains a 0.2.0 research target, not a feature of 0.1.2.

SUPPORT AND PROFILING
Report version, game build, headset/GPU/driver, runtime/provider, settings and
reproduction steps in GitHub Issues. Include logs/quad-*.log, not only *-run.json.
Review personal paths before sharing logs or settings.
Settings -> Enable diagnostics and logging controls mod logs, metadata and profiling
on the next launch; provider/runtime logging is separate. For captures enable
render profiling, select 30–300 seconds and press Ctrl+Alt+F9 while driving.
Close normally to save partial results. See RENDER-PROFILING.txt for limitations.

See README.md for the full overview and CONTRIBUTING.md in the source repository
for build/test and contribution guidance. Source license: PolyForm Noncommercial
1.0.0 (LICENSE.md), source-available, not OSI open source. Third-party licenses
remain separate. No game or asset rights are granted.
