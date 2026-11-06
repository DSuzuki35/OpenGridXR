# Native paired scheduling: pinned-build analysis

Read-only source/disassembly investigation, 2026-11-05. This describes a route
to integration and the evidence it still needs. It does not claim that native
paired scheduling or duplicate preparation removal is implemented.

## Preferred pairing

Pair outer/focus for the **same legacy eye** first. The existing renderer requires
their located poses to match and reuses the outer visibility work. This removes
the need to retain the left eye until the right-eye camera update. It also keeps
the ordinary left/right temporal ordering. The pair still needs distinct
projections, viewport/scissor rectangles, temporal inputs and intermediate targets.

`renderHook` currently computes the focus projection only after `renderOriginal`
has completed the outer view, including post-processing. Move only the pure
projection/history eligibility calculation earlier; do not call the native scene
twice and describe that as CPU sharing. A copied `ViewSnapshot` is not an owner:
its callbacks, children, main-pass records and visibility fields contain native
addresses and unresolved lifetimes.

The first useful subset is one static opaque group-31 shader family, single RTV,
single-sample depth, no GS/HS/DS, no instance input, predicates, result queries,
indirect draws, material callbacks or dependencies on mutable earlier outputs.
The two existing PS hashes and VS hash in `INSTANCED-STEREO-HANDOFF.md` are proof
candidates, not a declaration that every object using them meets these conditions.

## Exact native boundaries

These facts were checked against `analysis/reconstruction/51f040.asm` and
`all-code.asm` for executable SHA256
`66942337813b301b87d12edfa330f33354a2a4b8cd9b048885cbf77eb6f940cf`.

| Boundary | RVA / layout | Meaning |
| --- | --- | --- |
| Main-view call to native main pass | return `0x5e0891` | Main pass itself contains other work, including SSR |
| Main group-31 world-sort call | return `0x51df1d` | Call instruction at `0x51df18` |
| Scene group vector | `scene + (group + 0x73) * 24` | Pointer vector consumed by world sort |
| Material/bucket resolution | `0x527250`, `0x5ddb80` | Produces per-bucket lists; ownership remains native |
| Per-bucket mesh vector | object + `(group * 3 + 0x35) * 8` | Pointer vector; validate layout before inspection |
| Optional object callback | call `0x51f58d`, pointer at strip `+0xcc` | Refuse optimized packets if nonnull until its effects are known |
| Special prepare | call `0x51f5bf`, return `0x51f5c4` | Fourth argument `0x80000000` |
| Normal prepare | call `0x51f679`, return `0x51f67e` | Fourth argument zero; wrapper ORs shader `+0xf8` |
| Native submission | calls `0x51f636` / `0x51f6c9` | Renderer virtual `+0x720`, resolves to `0x5b5f30` |
| Mesh cleanup | `0x51f6cc` onward | Restores derived-view flags and temporary render state |
| Special cleanup | `0x51f64c` | Clears globals `0x1b154d8` / `0x1b154e8` |
| Bucket/group cleanup | `0x5e0d80`, `0x5e0e60` | Conditional calls at `0x51f7a3` / `0x51f811` |

Returning zero from the prepare hook at the two qualified callers naturally
skips the corresponding native submission while continuing the surrounding
native cleanup. This is a concrete candidate suppression boundary. It is **not
yet safe** merely because the wrapper normally returns one: omitted preparation
effects must be reproduced or proven irrelevant for the supported packet.

`0x5629e0` dispatches to `0x55db20`, `0x563d40`, `0x564bd0`, `0x5623d0`,
`0x565260`, `0x563710`, and `0x564df0`, depending on flags, reset, mesh and
global state. Several use the view/scene and mutable caches. The function's
constant return value proves no lack of side effects.

`0x5b5f30` increments renderer `+0x76ef8`, updates topology `+0x77958`, flushes
VS constants at `0x5b602f` (return `0x5b6034`), may flush PS constants at
`0x5b605a`, and resolves texture/resource bindings before issuing D3D draws.
Suppressing only the D3D call retains this CPU work; bypassing this native routine
without reproducing its required effects is not a complete optimization.

## Owned outputs and consumer order

The pair must target owned **opaque intermediate color and depth**, not the final
OpenXR image. The existing native outer bank supplies a validated wrapper/resource
construction pattern and owner-thread lifetime, but it does not supply array
ownership or a focus opaque bank.

Allocate a two-slice array at the maximum accepted outer/focus extent, with the
same native resource/view formats and sample count. Provide per-view viewport and
scissor routing. Each view's valid rectangle is independent; matching array slice
dimensions do not establish matching viewports. The existing private draw checker
explicitly refuses differing viewport/scissor/extents and needs extension for
this pairing. The generated geometry fallback also needs viewport routing when
the vertex route is unavailable. Depth copies require exact compatible resource
formats and whole-subresource restrictions appropriate to the resource. In
particular, an unequal-sized depth/stencil target cannot receive a boxed copy
from that array: D3D11 depth/stencil copies require a whole subresource. Prove
compatible native array-slice wrapper consumption, or explicitly export D32 via
a shader writing SV_Depth into the exact-sized target; refuse stencil until its
correct transfer is implemented.

At the outer group boundary, establish both seed states and both prepared packets
before committing any native-visible result. Execute the paired family into the
private arrays. Copy the outer valid rectangle into the native outer HDR/depth
targets before the first outer consumer, retain the focus result in owned storage,
and let native outer consumers run normally. At the matching focus boundary,
install the focus result before its first consumer and bypass only the matched
native preparation/submission. Preserve the existing later native effects and
post-processing. If the group contains later opaque work, preserve the exact
insertion order rather than pasting a whole final group result after it.

Two unresolved dependencies prevent treating this as a ready scheduler:

1. The focus depth/color seeds at group 31 may contain previous groups. Reusing the
   outer seed is invalid because projection/resolution differ. Identify every
   prefix producer and either execute its required focus version earlier into an
   owned bank or restrict eligibility to an independently initialized pass whose
   seeds can be constructed exactly. Do not recursively run the full focus scene
   inside outer rendering without proving all mutated temporal/global state.
2. Focus shader constants are not established by knowing the projection matrix.
   Need a verified generator for the supported fields or a split native prepare
   routine that performs shared preparation once and view-dependent preparation
   twice. Arbitrary PS constants and screen-position varyings cannot be obtained
   by changing only clip-space position. The seven CPU shadow payload layouts are
   known; the semantics of all view-dependent bytes are not yet known.

If either dependency is unknown, fallback must occur before copying private
results into native targets. An optimistic outer commit followed by discovering
an unmatched focus packet cannot satisfy that guarantee. Full preflight must cover
the entire bounded family, including an unsupported packet appearing late in it.

For same-eye visibility, conservatively retain the outer set only when the focus
frustum is mathematically contained within it and both share near/far/pose. Validate
the actual ordered native packet stream as well; callback-driven visibility or
LOD may differ despite pose equality. Cross-eye pairing instead requires a real
union or an equivalent conservative visibility owner, which is not reconstructed.

## Next bounded offline observation

Extend the existing two-frame audit; keep native rendering unchanged. Report
anonymized identities and bounded counters, never raw pointers or shipped shaders.
Use one monotonically increasing event sequence across the render thread.

For each group-31 entry/exit record frame, eye, outer/focus, view identity and native
bank generation, caller RVA, flags, last-main-pass, recursion depth, viewport,
scissor, current HDR/depth identities/descriptors/subresources, and renderScale.
Record all previous groups and target clears/writes since the main pass began to
locate the producers of group-31 color/depth seeds. Include unclassified writes
and deferred command-list execution as explicit invalidations.

For each native prepare record caller, shader identity, effective flags
(`argument | shader[+0xf8]` only when the wrapper applies that rule), reset, strip
identity and owning mesh identity (`strip + 0x80`), optional callback presence,
native submit arguments, and the resulting shader-program hashes. Record before
and after dirty masks and content hashes for all seven CPU shadow payloads, and
the full bytes only in the bounded local private capture when needed for semantic
analysis. The fourth payload is 2688 bytes. Do not read mapped WRITE memory.

Capture per-helper timing and before/after write footprints for the seven prepare
helpers, limited to verified native shadow/binding ranges. Unknown writes outside
those ranges remain unresolved; a hash over selected ranges cannot prove purity.
Also capture relevant global cache changes, including `0x1b154c8`,
`0x1b154d8`, and `0x1b154e8`, without assuming pointer equality means unchanged
content. Compare post-prepare native state at the first following draw and at the
next packet boundary, because some side effects may survive an omitted draw.

At D3D submission record full VS/PS SRV slots, used sampler masks, CB content
versions/ranges, VB/IB write epochs and source-resource write events through the
last focus consumer. Existing raw audit hashes cover only 16 PS slots and are
insufficient. COM AddRef preserves object lifetime, not contents. GPU snapshot or
a demonstrated no-writer lifetime is required for any retained mutable resource.

The report should answer these concrete questions:

- Is the same ordered group-31 packet family present in outer and focus, and
  which callbacks, geometry, states and nonconstant resource versions differ?
- What writes seed group-31 depth/color in each view, and when is the first
  subsequent consumer of each output?
- Which native preparation helper and bytes actually depend on projection,
  dimensions, history, eye or current mutable scene inputs?
- How much CPU time belongs to shared preparation, view-dependent preparation,
  submission/binding, bookkeeping and audit overhead?

These observations choose the smallest real integration slice; they are not a
performance feature and should not be exposed as enabled SPS.

`mod/native_pair_pass.hpp` now models the preflight/commit/drain contract with
owned packet values and generation-bound owner-thread tokens. It has no production
caller, graphics resource allocation, automatic native suppression or evidence
generator. Its synthetic test checks refusal before commit, stale tokens,
changed packet epochs and required draining after commit. A completed state model
does not establish a correct native scheduling route or CPU benefit.

## Required implementation checks

- A synthetic native-call fixture must show the qualified second preparation and
  submission are eliminated while mesh/bucket/group cleanup and required counters
  remain correct. Include normal and special caller paths and unknown callers.
- A scheduler fixture must exercise a late unsupported packet, changed resource
  epoch, nested view, skipped focus, camera reset, frame abandonment and teardown.
  Native-visible targets must remain byte-identical on every precommit refusal.
- Extend private color/depth/stencil reference comparisons to unequal outer/focus
  rectangles, asymmetric projection, nontrivial prefix depth, occlusion, screen
  varyings, temporal history and a focus-only visible sliver. Verify consumers read
  the correct intermediate subresource at the correct time.
- Native rendering caches must agree with actual D3D state after private draws and
  copies. Saving/restoring D3D state alone does not update native logical caches or
  restore NVIDIA extension state. Validate both portable and NVIDIA routes.
- Count actual paired native submissions, qualified preparations removed, fallback
  reasons, ownership failures, bytes copied and all new preparation/copy cost.
  Warm variants and allocations; no readbacks/compilation/broad getters in timing.
- Only a matched offline on/off/on workload can establish the net CPU benefit.
  The reported 0.4 capture (focus hook about 1.143 ms, outer total about 3.19 ms)
  gives no basis to promise a 3 ms saving from eliminating focus work alone.

