# QuadView integration into OpenGridXR

Source baseline: https://github.com/DSuzuki35/LMU-Quad-View-Mod/tree/v0.2.1

Goal: integrate QuadView rendering into OpenGridXR with one consistent crop and
frame state. This document is a handoff, not a claim that integration is done.
Read `flicker-diagnosis-20261103.md` and `ARCHITECTURE.md` first.

## Confirmed conflict and temporary protection

OpenGridXR ran above the QuadView adapter and cropped the game-facing stereo
FOV by 8% at the top and 21% at the bottom. Native outer geometry used this
cropped camera; focus rendering used the downstream provider FOV. HUD and
debug borders could align while cockpit geometry did not. Same-frame captures
and projection coefficients reproduced the mismatch exactly.

The launcher now sets `DISABLE_OPENGRIDXR=1` only for quad child processes.
Installed profiles are untouched. Stereo VRS retains its inherited preference.
Retain this protection until a coordinated rendering path replaces it; merely
removing the environment variable reintroduces the known conflict.

## Integration requirements

- Give crop/FOV computation one owner. Native outer geometry, focus rendering,
  HUD projection and OpenXR submission must all derive from the same frame
  state, including effective crop, per-eye FOV, pose and reference space.
- Preserve the locked rendered-view snapshot. Late tracking queries still
  return fresh data to callers but must not replace already rendered metadata.
- Preserve QVFR crop composition: when rendering a cropped outer, its output
  is embedded into a black full-size outer texture and submitted with the
  original full outer FOV. Removing that embedding stretches the geometry.
- Preserve the application's submitted outer pose and derive the submitted
  focus pose using `submission_focus_pose` (`mod/submission_pose.hpp`). Do not
  replace compensated submission poses with raw cached locate poses.
- Preserve native quad, mbucchia QVFR and VectorXR providers, plus stereo VRS
  as a separate two-view path. Avoid loading duplicate quad providers.
- Preserve dynamic focus min/max/reserve controls and the actual rendered FOV
  overlay. Current dynamic focus changes FOV with fixed-size textures; it is
  not dynamic resolution and has no demonstrated general GPU-saving guarantee.
- Keep executable/hash and hook-anchor validation, resource/state restoration,
  launch isolation, diagnostics, regression tests and third-party notices.
- Integrate OpenGridXR mask, sharpening and streaming at explicitly defined
  stages using the same projection state. Do not independently recrop stereo
  outputs while assuming the focus uses those new camera parameters.

## Code entry points and verification

`mod/xr_layer.cpp`: view snapshots, swapchains, crop composition, submission.
`mod/engine_bridge.cpp`: LMU hooks, native outer and focus rendering, UI.
`mod/launcher/LaunchService.cs` and `mod/launch.ps1`: child-process isolation.
`mod/launcher/Launcher.cs`, `QvfrSettings.cs`, `BenchmarkController.cs`: controls,
settings and telemetry. `docs/dynamic-focus-validation.md`: dynamic-FOV checks.

Run the native/MSVC, launcher and PowerShell suites described in CONTRIBUTING.
`tools/start-frame-capture.cmd` arms a four-view scene capture; its run manifest
names the trigger. Compare outer images cropped using recorded FOV tangents
with corresponding focus images for both eyes in the same frame. Ignore BMP
alpha when inspecting RGB: it is not uniformly opaque.

Validate fixed and tracked gaze, head motion, crop on/off, dynamic focus min/max
transitions, HUD/menus and provider output. Measure performance separately from
capture readback. Preserve the pinned engine build checks during integration.

## Hardware evidence and limits

Dream Air SE / SBoys3 1.3.0 / SteamVR 2.18.4 / QVFR: bypassing OpenGridXR produced
aligned same-frame images and the user confirmed a steady matching picture.
SteamVR's dashboard was closed through its desktop `Toggle Dashboard` menu;
OpenVR then reported inputAvailable=1 and LMU became FOCUSED. Gaze remained
stationary until the user restarted eye tracking in Pimax Play, then worked.
The reason that service restart was necessary remains unknown.

Dynamic FOV was disabled during this hardware validation. Do not turn that
result into a dynamic-FOV performance or universal compatibility claim.
