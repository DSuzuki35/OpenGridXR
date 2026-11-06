# Focus validation and log diagnosis

Corrected release 0.2.1 removes automatic GPU-driven focus resizing after the
user reported that it did not work. The controller, settings and UI are removed.
Old XML fields are ignored, including malformed obsolete values, and disappear
on the next save. Launcher child environments discard inherited dynamic flags;
the native layer ignores those flags even when launched directly.

Manual focus sizing, provider gaze tracking, crop fixes and actual rendered FOV
telemetry remain. Existing eye-tracking Auto/Fixed selection is unrelated to
the removed automatic size controller.

## Supplied log: quad-20261102-183224-1f695136.zip

QVFR returned focus swapchain handles 1 and 2. The left chain (3494x3556) stayed
unassigned; the right was identified by the native eye vector. The log contained
2,123 zero-projection frame records and no nonzero projection records.
The adapter incorrectly rejected numeric handle 1 before checking eye identity.
The fix accepts valid opaque handles while retaining null, unknown, duplicate,
installed-build and exact-size-vector guards. Its regression failed before the
fix. Teardown was orderly; this log did not establish a crash or GPU bottleneck.

## Retained and replacement coverage

- Valid handle 1, null/unknown/duplicate native eye identities.
- Fresh caller gaze with locked rendered views across late, failed and
  foreign-reference-space locate calls; new frames refresh their snapshot.
- Moving/asymmetric focus FOV, manual sizing, offsets, crop, debug-border pixels
  and unchanged behavior when obsolete dynamic environment flags are present.
- Overlay actual per-eye horizontal/vertical angles, invalid/torn ABI snapshots
  and clearing stale scene data over menus. The reserved ABI scale slot remains
  100; obsolete dynamic flag bits are no longer emitted or displayed.
- Launcher legacy-settings migration, removed UI controls, child-environment
  isolation, settings persistence, provider selection and simulated DPI layout.
- Benchmark GPU sampling respects armed profiler/legacy timing ownership.

Run `mod/build-msvc.cmd`, `mod/build-launcher.ps1` and the PowerShell suites in
CONTRIBUTING. Build logs and layout screenshots stay in ignored build outputs.
See [the live diagnosis](flicker-diagnosis-20261103.md) for fixed-FOV alignment
and gaze recovery on Dream Air SE / SBoys3 / SteamVR / QVFR.

Resolution remains fixed. Variable viewport/imageRect rendering would need
explicit LMU render-target, viewport and postprocess integration; it is not
implemented by this release. No automatic-size or dynamic-resolution speedup
is claimed.
