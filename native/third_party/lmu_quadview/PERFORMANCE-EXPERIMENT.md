# Performance experiment

Integrated from `experiment/performance` (`873143e`) into 0.2.0.
Both experiments are opt-in and use the existing pinned LMU executable guard.
There is no measured live-game speedup yet.

## Implemented paths

- `-CpuOuterReuse`: extends unchanged constant-upload filtering to outer opaque
  group 31, as well as focus. Buffer alias topology is cached while identities
  stay unchanged. The native preparation, bindings, changed uploads and draws
  still execute. Reuse never crosses a group, eye or frame boundary.
- `-OuterSsr`: requests smaller outer-only screen-space reflection resources,
  with associated depth and native resources restored before focus rendering.
  Each axis is capped at both the outer scene size and the original SSR size.
  Reduction below 40% of either original axis is refused. This quality floor is
  an experimental policy, not a perceptual guarantee. Unsupported layouts retain the native
  path. Reflections can look softer; this does not lower focus resolution.

The GUI launcher exposes both as Experimental toggles with hover help. They are
off by default and apply only to Quad Views. CPU outer reuse implies upload reuse;
the CPU upload fast path takes precedence if both preferences are saved. Smaller
outer SSR requires native outer resolution. Stereo VRS disables both paths while
preserving the saved preferences. The CLI switches remain available through
`mod/launch.ps1`; inherited controls are cleared by the GUI child environment.

## Build and run

Build with `mod/build-msvc.cmd` as described in CONTRIBUTING.md. From this
checkout's root, use your existing game directory and provider manifest:

```powershell
.\mod\launch.ps1 -Mode quad -Backend QVFR -UseRuntimeSettings `
  -GameDirectory '<your LMU directory>' `
  -QuadViewsManifest '<your QVFR manifest path>' `
  -NativeOuterResolution -CpuOuterReuse -OuterSsr -RenderProfiling
```

VectorXR uses `-Backend VectorXR` and its provider manifest instead. Native uses
`-Backend Native`. Retain the same provider and graphics settings in comparisons.
`OuterSsr` requires `NativeOuterResolution`; outer/focus sizes must be compatible.
`CpuOuterReuse` excludes stereo audit, shader capture and paired proof. It works
with ordinary render profiling and also with diagnostics disabled.

Run an A/B sequence with neither new switch, CPU only, SSR only, then both.
Keep native outer resolution enabled in every run. Warm the same replay segment,
wear the awake headset, retain identical weather/grid/pose and refresh settings,
and trigger Ctrl+Alt+F9 at the same point. Compare CPU/GPU scopes, frame P95/P99,
and image quality. Repeat in reversed order. Sparse GPU scopes are not total GPU
frame time; nested CPU wall times include waits. Check logs for accepted paths
or fallback before attributing a result to either experiment.

Check wet asphalt, mirrors, cockpit edges, focus boundaries, rapid gaze/head
motion, menu transitions, MSAA settings and both eyes. Remove the two switches
to return to baseline behavior. Existing game/profile selection is unchanged.

## Remaining research

The proposed sharing of object preparation is not implemented: the current
native helper has material/cache side effects beyond the known matrix output.
The CPU change here is narrower upload optimization, not single-pass stereo.

The internal PPFX pool remains full-size. Its singleton/global cache and temporal
history ownership are not established well enough to create independent banks.

Cross-view material shading caches and motion-dependent temporal reuse are not
implemented. They need complete input invalidation, stable object/surface data,
decoded motion/depth semantics, disocclusion handling and a scheduler that can
actually replace native work. Private proof draws or cached images without
removing that work would add cost rather than implement the proposed savings.

## Validation

On the original performance branch, baseline and final MSVC native/WARP suites passed. Its DLL was
built after SSR metadata invalidation hardening. Existing compiler warnings remain.
Focused MinGW WARP tests cover SSR mip generation/readback, initialized history,
matching depth, resource replacement, metadata changes, alias refusal, fallback
and restoration. CPU tests cover exact GPU contents, buffer replacement, nested
groups, opt-in handling and diagnostics-off behavior; the focused reuse test also
passed on the available hardware device.

The performance, portable-launch, focus-offset and QVFR-profile PowerShell tests
passed without launching LMU. Independent review identified and corrected the
SSR history rectangle mismatch; no further actionable blockers were reported.
Headset/provider image validation and matched live frametimes remain outstanding.

For the 0.2.0 integration, the complete LLVM-MinGW native/WARP suite, launcher
settings/environment/DPI-layout suites and all four PowerShell compatibility
suites passed. Native resource names now include bank identity and generation;
a regression covers asymmetric outer banks with equal capped SSR dimensions and
rebuilds while an old wrapper is still alive. This does not replace headset
image-quality checks of the opt-in SSR path.
