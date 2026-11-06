# CPU upload fast path: experimental candidate 1

This is a real in-game CPU upload optimization. It combines unchanged-upload
filtering with adaptive small-buffer batching in native opaque group 31, for
both outer and focus rendering. Native outer-resolution savings can stay on.
It keeps the original shaders, per-eye preparation, draw calls and image targets.
Full single-pass stereo and duplicated CPU traversal removal are still pending.
No live-game frametime improvement has been established for this candidate.

The pinned native uploader normally checks seven VS constant buffers. Four small
ones can share a 2304-byte dynamic buffer with standard D3D11.1 range bindings.
Byte-identical dirty uploads are filtered first; batching starts only when two
small buffers actually need writes. Large bone/light buffers retain individual
dirty-only uploads. The first native call warms each scope. Scope exit restores
native buffer objects, zero-offset bindings, GPU contents and the PS b5 alias.
Restoration cost is included in the counters. No mapped WRITE memory is read.

The native light-buffer range is 2688 bytes, confirmed by the pinned uploader's
copy size; the earlier reuse guard expected 2624 and conservatively rejected it.
Runtime buffer sizes, slots, aliases and context identity are checked. Invalid
layouts fall back to native uploads. The old reuse-only option is superseded
while this combined option is on. Driver constant-buffer offsetting must be
supported; otherwise the feature reports unavailable and leaves native uploads.
There is no NVIDIA-specific API dependency. WARP and RTX 4080 tests passed;
AMD/Intel hardware has not been tested.

## Run and compare

Enable **Experimental CPU upload fast path** in Setup. Leave native outer
resolution as it was. Enable diagnostics and render profiling for measurement.
The draw audit, shader export and paired diagnostic are disabled for this mode:
the old audit does not record D3D11.1 CB window offsets. They must not be used to
compare its constant data. Profiling remains available. Turning the master
diagnostics switch off stops mod logs/profiling/counters while retaining the
optimization.

Use the same warmed replay segment, pose, runtime, refresh rate and graphics
settings for on/off runs. Press Ctrl+Alt+F9 at the same point in each run and
close normally. The current private folder uses a 120-second capture; closing
earlier saves a partial profile. Untick the CPU fast path and relaunch for the
baseline. Leave the older reuse-only option off for that baseline.

## Logs

Startup states whether the hook and D3D11.1 support were accepted. Fallback lines
identify the failing native range, reason and HRESULT. Repeated failure messages
are bounded; the cumulative counts and last failure remain in checkpoints.
Checkpoints are flushed approximately every 1200 focus renders, and normal
shutdown writes a final summary. The existing render profiler also checkpoints
and saves on normal close.

The `cpu_upload_batch` summary separates unchanged maps/bytes skipped, outer and
focus batched calls, small native maps replaced, combined maps, large native
maps, extra restoration maps, bytes copied, invalidations and Map failures.
`net_maps_removed` includes unchanged filtering and subtracts batching and
restoration costs. These counters show eliminated API work, not a CPU/FPS gain;
use matched frame-time captures for that conclusion. A batch call count of zero
can be normal when filtering already leaves fewer than two small dirty buffers.

Tests compare byte-identical shader output with the original uploader, all seven
CB payloads and windows, the PS b5 alias, unrelated bindings, native GPU-content
and offset restoration, buffer/slot changes, aliases, owner-thread cleanup and
diagnostics-off behavior. The combined filtering/batching path is tested against
the original path as well. These standalone checks do not replace an LMU run.
