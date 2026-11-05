# Dynamic focus test build and log diagnosis

Base: GitHub main 02c0625 (0.2.0), fetched 2026-11-03. Implementation is included
in release 0.2.1. The native game executable and provider binaries are unchanged.

## Supplied log: quad-20261102-183224-1f695136.zip

The QVFR provider returns focus swapchain handles 1 and 2. At 18:32:30.995
the left chain is created at 3494x3556 and remains unassigned; at 18:32:31.027
the right chain is assigned by native eye-vector identity. At 18:32:32.891
submission refuses the pair. The log contains 2,123 zero-projection frame
records, no nonzero projection records, 25 throttled refusal messages, no left
assignment and one right assignment. Teardown is orderly; the log does not
establish a crash or a GPU bottleneck. Benchmarking/profiling were disabled.

Root cause: engine_game_eye rejected numeric handle 1 before checking LMU's
native eye vector. Handles are opaque; QVFR legitimately issued this value.
The fix removes that numeric assumption, keeping installed/runtime guards,
readable exact-size vector checks, non-null handle and unique eye identity.
A regression against the real engine function failed before the fix. The old
fallback test encoded the same incorrect sentinel assumption and was updated
to require accepting the uniquely identified handle.

## Dynamic resolution assessment

OpenXR supports an imageRect describing the valid region inside a swapchain
image: https://registry.khronos.org/OpenXR/specs/1.0/man/html/XrSwapchainSubImage.html
This makes variable viewport rendering conceivable without resizing the
underlying allocation. It does not automatically change LMU's rendering.
The current adapter advertises exact focus dimensions, validates native focus
chains against them, and engine_bridge.cpp uses matching full/focus dimensions
when selecting render targets. Focus resolution changes therefore need an
engine-level render-target/viewport/postprocess integration, not an FOV-only
patch. This build keeps resolution fixed; no live resolution support claimed.

## Automated coverage and remaining validation

- Native eye identity, valid handle 1, null/unknown/duplicate identities.
- Dynamic bounds, asymmetric response rates, hysteresis, invalid/stale/missing
  timing and recovery, disabled mode, identical min/max.
- Real adapter scene/menu/failed submission metadata and stable display-time
  focus scale, with fixed image dimensions.
- XR moving/gaze FOV, offsets, rendered crop, debug-border pixels and diagnostics off.
- Launcher defaults, persistence, validation, locale-independent child settings,
  VRS exclusion, scrolling and simulated DPI geometry.
- Overlay reports actual rendered scale and per-eye horizontal/vertical angles,
  using a seqlocked extension in reserved ABI-v1 header space. Old producers
  report unavailable. C++/C# binary fixture crosscheck and bitmap previews cover
  the extension, invalid/torn snapshots, and both overlay layouts.
- Independent code review: timing ownership and continuity findings corrected.

Run mod/build-msvc.cmd and mod/build-launcher.ps1 for full validation.
Build logs and launcher screenshots remain in the local ignored build outputs.
No dynamic-FOV-enabled LMU/headset run was performed. Later fixed-FOV hardware
validation is documented in [the flicker diagnosis](flicker-diagnosis-20261103.md).
Check visual alignment, gaze tracking, transitions,
missed frames and measured performance before relying on automatic FOV changes.
Disjoint GPU timing is shared with benchmark and pauses when render profiling
or legacy engine GPU timing is armed. Compositor GPU work is not measured.
