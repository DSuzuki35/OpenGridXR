# Portable QVFR adapter candidate

Crop work remains paused. This branch starts at the tested crop-bypass/eye-identity
checkpoint 6a22973. It does not replace that delivered ZIP.

The selected architecture is LMU -> LMU renderer adapter -> mbucchia
Quad-Views-Foveated -> active stereo OpenXR runtime. Reuse QVFR's compositor and
eye-tracker integrations rather than duplicate a compositor or vendor SDKs.
Native quad runtimes remain an alternative backend. This is broad compatibility,
not a guarantee for every headset/runtime/driver combination.

Implementation tasks:
- [x] Per-process QVFR discovery and explicit layer order, with no registry edits.
- [x] Launcher backend and auto/fixed choices, backward-compatible saved settings.
- [x] Explicit fixed foveation requests, matching view recommendation/locate flags.
- [x] Recommended LMU-only QVFR settings and accurate compatibility instructions.
- [x] Automated launcher, fake-runtime, native-hook and D3D11 WARP regressions.
- [x] Separate candidate ZIP, manifest/privacy checks and independent review.
- [ ] Live Dream Air/sboys3/SteamVR and fixed-foveation checks (hardware required).

Constraints: keep the native hook signatures and renderer guards; preserve auxiliary
swapchain identity, gamma handling and runtime output sizes. Retain outer<=focus
per-axis validation. QVFR default dimensions may violate that constraint; supply
an LMU-only profile and report incompatible dimensions rather than guessing.
The native renderer integration still assumes matching outer/focus eye poses.

Auto follows the provider's available tracking. Fixed requests stationary views.
QVFR's prefer_foveated_rendering default can override recommendation hints, so the
LMU profile must set it to zero; explicit per-call requests then select the mode.
Profile edits must preserve other applications' settings and have a backup.

Evidence: upstream wiki lists Dream Air + sboys3 + SteamVR OpenXR as eye-tracked,
and supports fixed rendering on headsets without trackers. This does not establish
that LMU works on those combinations until this adapter is tested there.

https://github.com/mbucchia/Quad-Views-Foveated/wiki
https://github.com/mbucchia/Quad-Views-Foveated/blob/main/openxr-api-layer/layer.cpp

Verification (30 October 2026): full native/WARP/fake-runtime suite passed;
launcher and profile tests passed; independent source review found no blocker.
ZIP manifest/privacy checks passed. Live headset testing remains pending.
DLL SHA256: 602F7248B0A903052EC58DE7369611A3D55EB7FC1D3B36A87C4433336458FBCE.

Launcher revision (30 October 2026): direct C# process launch replaces runtime
PowerShell. In-app QVFR tuning preserves other app sections and backs up profile
changes; ranges match upstream clamps. Dark UI verified at normal/minimum size.
Optional outer-only spatial smoothing defaults off; same-size typed encoding
mismatches retain the original copy with a diagnostic. Native/WARP/XR and C#
regressions passed, including both encoding mismatch directions. No headset or
GPU performance measurements were made. Temporal filtering is not implemented.
Delivered ZIP SHA256: 1662337704CB0A2F4A2BCB9FB8846B57BA4F612ABF5F4051A7D996585228181C.
