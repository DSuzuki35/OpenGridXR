# LMU VectorXR and renderer work: maintainer handoff

This handoff covers the VectorXR contribution, profiling and rendering changes,
the experimental CPU upload optimization, and the unfinished vendor-agnostic
instanced-stereo work. It is intended to let another maintainer build, review and
continue the implementation without private captures or this conversation.

Status recorded on 2026-10-31. Branch names and commit references describe the
local source history; publication and pull-request inclusion must be checked
separately.

## Status

**The portable GPU backend and a sampled in-game shader comparison work. Native
paired-pass scheduling and duplicate CPU preparation removal are not implemented.**
The current renderer still executes the native eye draws. The upload fast-path
candidate is a separate optimization, not single-pass stereo or a prerequisite
for it. No confirmed live-game performance win is claimed.

This document describes the local `research/cpu-stereo-rendering` work based on
the VectorXR/diagnostics contribution. It is a handoff for implementation and
review, not an announcement of completed SPS. The research commits have not been
added to the earlier feature pull request automatically.

The earlier contribution branch is `feature/vectorxr-profiling-diagnostics`
at `4188139`. Subsequent work is on `research/cpu-stereo-rendering`. Relevant
research history, in dependency order:

| Commit | Change |
| --- | --- |
| `bb996db` | Portable stereo proof and bounded draw audit |
| `4ad3acd` | Early shader/layout collection and shader-cache fix |
| `96aa3a1` | SM4 stereo shader converter |
| `1379153` | Native preparation observation, private draw checker, upload reuse |
| `ea39c67` | Query guard and checked pixel-shader variant coverage |
| `a3cb73a` | Native depth/stencil snapshots and comparison |
| `bac0051` | Declared sampler masks and unused-sampler refusal fix |
| `8913ddf` | Combined upload fast-path candidate and tests |
| `6fd99ca` | Dormant focus-image ownership bridge and fake-runtime tests; no native scheduler |

These are dependent research commits, not a list of independent cherry-picks.

Do not assume opening or updating a pull request for the earlier feature branch
includes the research branch. Inspect its base and commit list explicitly. If
the earlier contribution is merged first, rebase the research work onto that
accepted history and resolve any overlap before submitting a separate review.

| Area | State |
| --- | --- |
| Optional VectorXR provider, per-process layer setup | Implemented |
| Native smaller outer targets and sampled-input path | Implemented; active in the latest run |
| Optional profiling, normal-close saving, diagnostic master switch | Implemented |
| Standard D3D11 instanced two-view backend | Implemented and tested offscreen |
| SM4 vertex/pixel shader conversion | Implemented for a guarded subset |
| One real LMU object's paired color/depth comparison | Passed in private targets |
| Native focus-pass scheduling / consumer coordination | No scheduler implementation; dormant focus-image ownership bridge added |
| Replacing native eye draws with the paired pass | Not implemented |
| Skipping duplicated native CPU preparation | Not implemented |
| CPU upload filtering / batching | Separate experimental candidate; gain unconfirmed |

## Existing VectorXR, profiling and outer-render changes

These features precede the portable stereo research. Keep their behavior and
tests intact while changing the renderer:

| Change | Entry points and review focus |
| --- | --- |
| Optional VectorXR provider | `mod/launcher/LaunchService.cs`, `QvfrSettings.cs`, `Launcher.cs`, `mod/launch.ps1`; provider discovery/validation, separate saved manifest paths, explicit layer order, child-only environment and conflict disabling |
| Direct compositor inputs | `mod/swapchain_sampling.hpp`, `mod/xr_layer.cpp`; eligible color swapchains request sampled usage, preserving metadata and retrying original usage when unsupported |
| Smaller native outer rendering | `mod/engine_bridge.cpp`, `native_outer_bank.hpp`, `native_outer_bindings.hpp`, `native_outer_texture.hpp`; transactional native resource/view changes, exact restoration and full-size/resampling fallback |
| Optional profiling and shutdown saving | `mod/render_profile.hpp`, `gpu_timing.hpp`, engine/XR hooks; per-XR-frame wall scopes, sparse GPU queries, periodic checkpoints and normal-close partial saving |
| Diagnostic master switch | launcher settings/environment plus native enforcement; disables mod collection/logging at the next launch, independently of the rendering options |

The launcher does not install VectorXR, replace the active headset runtime or
change global layer registration. It selects one quad-view provider for the child
game process. VectorXR's own LMU profile supplies resolution/focus/tracking
settings; QVFR-specific sizing controls are inactive with this provider. Native
OpenXR and mbucchia QVFR remain selectable. See [VectorXR setup](PORTABLE-VECTORXR.txt)
and [provider design](PORTABLE-DESIGN.md).

The smaller outer bank addresses some peripheral GPU cost. It does not eliminate
the game's full post-processing pipeline, replace all internal transient targets
or share the focus eye draws. Unsupported bank layouts fall back to full-size
outer rendering and resampling, so visible quad views alone do not prove the bank
savings are active. Confirm the startup acceptance/fallback messages.

The native profiler replaces dependence on an OpenXR Toolkit overlay for these
mod measurements. Its default capture is 120 seconds (configurable 30-300),
triggered by Ctrl+Alt+F9. Normal teardown saves a partial capture, including on
another teardown thread; forced termination can lose recent data. The diagnostic
master stops the mod's instrumentation, while provider/runtime diagnostics remain
separately controlled. See [profiling semantics](RENDER-PROFILING.txt) before
interpreting the inclusive CPU scopes or partial GPU intervals.

The tests most relevant to these established changes are
`mod/launcher/LaunchServiceTests.cs`, `mod/launcher/SettingsTests.cs`,
`mod/launcher/GuiLayoutTests.cs`,
`mod/tests/swapchain_sampling_test.cpp`, `native_outer_bindings_test.cpp`,
`native_outer_fallback_test.cpp`, `render_profile_test.cpp` and `mod/xr_layer_tests.cpp`.
Synthetic/fake-runtime tests cannot establish every provider/headset combination.

## Files to start with

Paths below are relative to the source repository:

- `mod/engine_bridge.cpp`: native render, view, world-sort and preparation hooks.
- `mod/xr_layer.cpp`, `mod/bridge.hpp`: frame state, swapchain ownership,
  outer capture and focus-completion bridge.
- `mod/stereo_instancing.hpp`: capabilities, instance adaptation and submission.
- `mod/stereo_shader.cpp`, `mod/stereo_shader.hpp`: DXBC conversion and binding plan.
- `mod/stereo_draw_proof.cpp`: private targets, snapshots and original/paired comparison.
- `mod/stereo_audit.cpp`: early device interception, shader metadata, draw capture,
  native mesh identities, queries and checker selection.
- `prototype/lmu_reconstructed.hpp`: verified native view/camera layouts; unknown
  fields remain explicitly unknown.
- `mod/native_outer_bank.hpp`: existing native resource-bank ownership pattern.
- `mod/native_upload_reuse.hpp`, `mod/native_upload_batch.hpp`: separate CPU experiment.
- `mod/tests/`: owned synthetic shader/rendering fixtures and native-layout models.

Further descriptions: [shader conversion](STEREO-SHADER-CONVERSION.md),
[research capture workflow](PORTABLE-STEREO-RESEARCH.txt),
[upload fast path](CPU-UPLOAD-FASTPATH.md), [older reuse experiment](CPU-UPLOAD-REUSE.md).

## Portability

The implemented drawing path uses standard D3D11 instancing, not NVAPI SPS/MVR.
It queries `D3D11_FEATURE_D3D11_OPTIONS3` and
`VPAndRTArrayIndexFromAnyShaderFeedingRasterizer` for vertex-stage array routing.
When unavailable, compatible shader families can use the generated pass-through
geometry shader. Unsupported combinations must retain native rendering.

One submission covers both eyes, but vertex/geometry work still executes for each
eye. This does not promise NVIDIA-style shared geometry processing. WARP and an
RTX 4080 have been tested. Actual AMD/Intel driver compatibility and performance
remain unverified. The initial headset/runtime environment was Quest 3 with
Virtual Desktop / VDXR; a runtime's quad-view support does not remove duplicated
CPU work in LMU's renderer automatically.

The upload experiment additionally requires driver support for D3D11.1 constant-
buffer offsetting. That requirement belongs to the upload experiment, not to the
portable stereo shader backend.

## What the shader backend does

The converter accepts a bounded subset of legacy SM4 VS/PS programs. Selected
constant buffers become two eye banks in spare slot b13. The VS derives the eye
from the instance ID, chooses that bank and exports an eye varying. The PS uses
the corresponding bank independently. Original shader math remains in the program.

DXBC checksum, chunk bounds, instruction lengths, signatures and buffer capacity
are checked. Unsupported profiles, selected dynamic CB indexing, existing stereo
or instance inputs, occupied b13 and unsupported declarations/control flow are
refused. Stale reflection/debug metadata is removed from generated variants.
Failure leaves no usable variant; fallback must be complete.

Sampler masks come from actual SM4 declarations, with body operands validated
against those declarations. Unused stale samplers may differ. Used samplers and
all 128 VS/PS SRV slots remain checked by the private comparison. The initial
programs declare PS samplers s0, s1, s11, s13 and s14; a difference in unused s8
caused a false refusal before this source fix.

The instance helper preserves original IA base-instance semantics and adapts
per-instance step rates. Do not blindly double an application's instance count
or assume `StartInstanceLocation` is the shader's eye ID.

## Real-game GPU evidence and its limits

The initial opaque family is restricted to these program hashes:

```text
VS a99b2cd689aa55f5f705b6ddd01d39db16d1dc1e83405446281d8d0f2f583d5d
PS 79bed2230b50db6c1f4db928a55f421f72f5ca19828aa2901d32583685554f56
PS d41d95fc9c8fed235e1336ef9749f7aefc7ce6eb45c35b10e6fbd68b19eef76d
```

On 2026-10-31, the private checker matched one 4,104-index object using the
d41d95fc pixel shader. The standard instanced VPRT draw at 1352x1734 per eye
produced 1,100,595 passed samples, equal to the two references combined
(535,280 and 565,315). No pixels exceeded the color tolerance; maximum color
error was 0.0000610352. Depth matched exactly. This sampled native pass used
D32 depth, GREATER comparison and depth writes enabled, without stencil.

The checker snapshots per-eye CB and native depth/stencil data on the GPU. It
seeds both private rendering paths from the same native input. It supports D16,
D24/S8, D32 and D32/S8, ordinary/reversed depth, EQUAL/no-write shading, stencil
operations and read-only views in its tests. Coverage uses private occlusion
queries, not an assumption that every visible depth is less than one.

All checker draws go into private targets, while native game draws remain enabled.
The checker performs blocking diagnostic readbacks and is unsuitable as the
measured optimization path. It does not establish correctness for the whole
opaque pass, later frames, other shaders, animation or temporal effects.

Other textures and geometry are retained rather than universally snapshotted.
COM identity is not a content version. A passing sampled pair is not permission
to reuse resources after updates or after ownership ends. Raw capture signatures
also cover only the first 16 PS resource/sampler slots; do not use those signatures
as proof of full resource equivalence.

The local corpus contained 150 captured programs: 80 converted and were accepted
by D3D11; 70 were refused. This was checksum/creation validation, not image
equivalence for 80 programs. Decoded sampler masks matched D3DReflect for the
convertible programs. Game shader binaries are not part of the source handoff.

## Why native integration is unfinished

The current order is effectively:

```text
outer left -> focus left, including its consumers/post-processing
outer right -> focus right, including its consumers/post-processing
```

Both native eye renders reuse the **same mutable ViewSnapshot object**. Its eye
index, transforms, projection and derived state are overwritten between eyes.
Saving a pointer does not preserve the left view. Copying the struct does not
deep-copy its vector-shaped fields, callbacks, visibility lists or native wrappers.

The left focus output is already consumed before the matching right focus draw
arrives. A late paired draw cannot simply overwrite the final left image: it
would bypass its depth/post-processing consumers, and the downstream image may
already have been released. The active opaque HDR/depth targets are also not
interchangeable with the final runtime color images.

The XR layer currently holds its own **outer** images until submission for UI
work. Application focus-image releases are forwarded normally in production.
A dormant focus-image ownership bridge now supports explicit owner-thread leases,
deferred downstream release, retry and stale-token rejection. No production
renderer calls its preparation function, and it is not enabled by a user option.
It does not own opaque HDR/depth banks, pause consumers or schedule a paired pass.

Its API is in `mod/bridge.hpp`: `xr_prepare_focus_image_ownership`,
`xr_hold_focus_image`, `xr_focus_lease_valid` and `xr_retire_focus_image`.
Preparation must occur before application swapchain creation; holding requires
one waited acquisition on the selected native eye chain in the current frame.
The owner must retire before frame advancement, another acquire on that chain,
end-session or swapchain destruction. An unresolved owner is rejected rather than
allowing writes after downstream release. Session destruction invalidates state.
The future scheduler must implement fallback/draining for those boundaries before
enabling the bridge. `mod/tests/focus_image_lease_test.cpp` exercises actual layer
release/acquire/frame paths with a fake runtime and WARP texture; it does not
attach to LMU or demonstrate game-pass replacement.

Consequently, these remain two connected integration tasks:

1. **Schedule and own a real paired pass.** Preserve current-frame per-eye view,
   visibility, depth, mutable inputs and temporal state; provide owned compatible
   array targets; arrange the consumers so both eye outputs exist before they
   are read. Respect actual downstream acquire/wait/release ownership, including
   skipped views, abandoned frames, reloads and session teardown.
2. **Remove duplicated native work for that pass.** Once its output is correct,
   submit eligible packets once and bypass the corresponding native preparation
   and draws, while retaining required cleanup, counters, binding-cache state and
   bookkeeping. Unknown or incomplete work needs a full native fallback before
   the optimized pass changes visible output.

Potential scheduling approaches still need evaluation: an owned pair of native
focus resource banks with delayed/replayed consumers, or an explicitly owned
native pass invocation after preparing both views. Neither approach is implemented
or endorsed as already proven. Delaying a view without preserving its visibility
and resource state is unsafe. Recording D3D11 commands alone also does not freeze
subsequently overwritten buffers/textures or repair native CPU state caches.

Start with one coherent opaque family. Defer mirrors, transparency, SSR,
tessellation, unsupported geometry stages and temporal dependencies until their
inputs and ordering are understood. A left-only visibility list can miss objects
visible to the right eye; use a valid union or equivalent conservative approach.

Do not add more optional optimizations as substitutes for these integration tasks.

## Native boundaries and guards

All addresses are RVAs for the pinned build, not portable API entry points.
The executable disk hash and live code anchors must match before hooks activate:

```text
SHA256 66942337813b301b87d12edfa330f33354a2a4b8cd9b048885cbf77eb6f940cf
```

| Boundary | RVA / field | Notes |
| --- | --- | --- |
| Verified DYN eye-loop render call return | 0xcd1763 | Existing render-hook qualification |
| Native scene repeat | 0x5de200 | Focus currently repeats this with outer visibility reuse |
| Native main pass / world sort | 0x51d8a0 / 0x51f040 | Known opaque group is 31 |
| Per-mesh preparation | 0x5629e0 | Original always retained; mesh pointers anonymized in audit |
| Verified preparation call returns | 0x51f5c4 / 0x51f67e | Additional caller guards |
| Native renderer draw routine | 0x5b5f30 | Native wrapper/bookkeeping before D3D submission |
| Native VS constant flusher | 0x5cac50 | Verified caller return 0x5b6034 |
| Native VS CB binder | 0x5c3f20 | Also binds first VS CB to PS b5 |
| Current native view pointer | 0x1a51370 | One mutable object, not an immutable eye snapshot |
| Renderer immediate context | renderer + 0x76f68 | Owner-thread assumptions apply |
| Native VS shadow cache | renderer + 0x77fc8 | CPU shadows, not mapped GPU WRITE memory |
| Native binding cache argument | renderer + 0x77940 | VS CB weak pointers at argument + 0x78 |

`worldSortOriginal` traverses, renders and performs control/cleanup work. Returning
early from the whole function is not a validated optimization. The preparation
wrapper calls multiple helpers and has side effects; its constant return value
does not mean it can safely be skipped. Native binding-cache coherence matters
when introducing private buffers, context changes or command-list playback.

The seven verified CPU shadow ranges are defined in `native_upload_ranges`.
The fourth payload is **2688 bytes**, not 2624. Never read mapped WRITE memory
to recover constants; use CPU shadows with a proven layout or owned GPU copies.

## Separate upload candidate: measured outcome

Candidate `cpu-fastpath1` combines byte-identical upload filtering and adaptive
small-buffer batching in outer/focus group 31. It uses the original shaders and
draws. It has native-layout/context guards, fallback messages, per-component
counters, periodic flushed checkpoints and normal-close summaries. Draw audit
is disabled in this mode because the old audit does not capture CB range offsets.

The latest session reported 54.31 million net Map calls removed, including
restoration costs, with zero upload failures or invalidations. These counters
span approximately 235 seconds of scene rendering, not just the 120-second F9
capture. VectorXR reported direct inputs 4/4 and outputs 2/2 with no copy fallbacks.

The F9 capture lasted 120.002 seconds; CPU and GPU samples were clean and normal
shutdown saved the logs. The following historical comparison is **not matched**:

| Measurement | Historical OFF | Candidate |
| --- | ---: | ---: |
| Focus CPU wall time, both eyes | 1.874618 ms | 1.771892 ms |
| Outer CPU wall time, both eyes | 3.823699 ms | 3.823632 ms |
| Average scene cadence | 117.76 FPS | 118.69 FPS |
| Sum of four GPU render-region intervals | 7.045926 ms | 7.051776 ms |
| Mod frame-interval P95 | 9.516500 ms | 9.516300 ms |
| Mod frame-interval P99 | 10.6683 ms | 10.9952 ms |

The candidate had 8.13% fewer focus sort scopes per frame and substantial workload
variation within its capture. The apparent focus reduction / FPS increase cannot
be attributed to the code from this comparison. CPU wall scopes include waits;
GPU region intervals are not pure shader execution time. The user also observed
worse VectorXR overlay App-frametime P95; equivalence to the mod's cadence metric
has not been established. Provider cadence bucket bounds are not an exact
full-capture percentile for that overlay metric.

The batching tradeoff deserves attention: each batch copies 2304 bytes, consisting
of 1648 payload bytes and 656 padding bytes. Across the session this was 42.729 GB:
12.166 GB padding and 30.563 GB payload. Only 9.649 GB represented changed small
native data; approximately 20.914 GB was unchanged payload recopied. Filtering
separately avoided 18.26 GB. Fewer API calls alone do not establish a speedup.
Keep the option experimental until matched testing shows a net benefit.

## Build, test and acceptance

From an x64 Visual Studio C++ environment, or through the wrapper:

```powershell
.\mod\build-msvc.cmd
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\mod\build-launcher.ps1
.\mod\build\msvc\stereo_instancing_test.exe --hardware
.\mod\build\msvc\stereo_draw_proof_test.exe --hardware
.\mod\build\msvc\native_upload_batch_test.exe --hardware
.\mod\build-msvc.cmd -FocusLeaseOnly
```

The MSVC build runs native and XR-layer tests and creates the DLL. Launcher
checks cover settings, child environment and simulated 100/150/200% layouts.
WARP and RTX 4080 tests passed. The optional D3D11 debug layer was unavailable
locally; MinGW and AMD/Intel hardware validation remain outstanding.

For the upload fixture, the combined path reduced 336 modeled native Map calls
to 70 including restoration while producing byte-identical shader output.
That synthetic result is not an LMU FPS result.

For future paired-pass acceptance:

- Count actual paired game submissions and native preparations/draws eliminated.
- Compare against native per-eye output with asymmetric views, moving head/scene,
  occlusion, animation, shadows, fallback objects and temporal transitions.
- Validate ownership before every image access, including reacquire, skipped eye,
  replay seek/loading, failed release and other-thread teardown.
- Warm variants and reuse allocations. Keep diagnostic readbacks, broad state
  inspection and shader compilation out of the measured fast path.
- Measure all new packet construction, snapshots, copies, synchronization,
  deferred execution and fallback work. Do not report only the native time removed.
- Use repeated matched on/off/on replay segments, identical runtime/settings,
  diagnostic configuration and pose. Assess CPU execution/waits, total cadence,
  GPU cost and P95/P99 together; average FPS near the refresh cap is insufficient.

An earlier engineering target was at least 0.3 ms net CPU saving in matched runs,
with no material GPU or tail-pacing regression. It is an acceptance target, not
a forecast. A correct but slower prototype should be reported as such.

## Contribution hygiene

Publish implementation, owned synthetic tests and these documents. Do not include
local logs, profiler CSV/JSON, launcher settings, compiled game shaders, shader
caches/disassembly, executable or memory dumps, runtime manifests generated for
a particular machine, machine paths, personal identities or credentials.

Generate shader variants at runtime from the user's own game, rather than shipping
captured game programs. The repository ignores many local diagnostic artifacts,
but review the staged file list explicitly. Keep experimental rendering work
separate from the established VectorXR/diagnostics feature contribution until it
is ready for its own maintainer review. Existing license notices remain required.
