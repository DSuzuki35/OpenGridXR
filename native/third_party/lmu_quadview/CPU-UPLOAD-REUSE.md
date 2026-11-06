Experimental CPU upload reuse
============================

This is a runnable CPU optimization prototype for the verified LMU code layout. It is
independent of the portable stereo shader work. It does not combine eye draws.
Full game-renderer single-pass stereo and shared object preparation remain work
in progress. No LMU frametime or FPS improvement has been measured yet.

The prototype targets the native VS constant-upload function inside focused
opaque world group 31. That function binds its normal buffers, uploads dirty CPU
shadows with Map(WRITE_DISCARD)/copy/Unmap, then clears their dirty flags. The
separate binding helper changes binding bookkeeping, not CPU payloads or storage.

When the complete CPU payload and GPU buffer identity match a successful upload
earlier in the SAME group scope, the prototype clears that redundant dirty flag.
The original function still performs all binding/bookkeeping and all changed
uploads. It never filters a draw or changes matrices, FOV, resolution or shaders.
The first upload in each scope is always retained. Buffer replacements invalidate
the match; aliased/unsupported buffers retain all their uploads. The optimization
ends on unexpected native writers/nested groups and never carries assumptions
across groups, eyes or frames. Resource references prevent pointer reuse while
retained. A group-level lock allows teardown to wait for an active scope; there
is no per-upload locking, GPU readback, hashing, timer, or heap allocation.

This relies on the examined native renderer owning these CPU shadows/GPU buffers.
It is opt-in and checks native function boundaries, function/call-site/binding-helper
anchors, exact cache identity/context, thread and buffer descriptors. It reads
ordinary CPU shadow memory and never reads mapped WRITE memory. Unsupported
anchors leave the native path enabled. Other passes and native helpers stay on
their existing path.

Testing the prototype
---------------------

In Settings enable Experimental CPU upload reuse. For a measured trial also enable
diagnostics and render profiling, and leave stereo draw audit / paired rendering
tests off. Press Ctrl+Alt+F9 on track; closing LMU normally saves the profile.
The native log ends with cpu_upload_reuse summary examined=... skipped=...
bytes_skipped=.... A nonzero skip count proves eliminated uploads, not an FPS
gain. Compare with this same package and CPU upload reuse unchecked, on the same
replay/workload. Inspect car/track lighting and geometry for differences too.

Runtime A/B comparison (diagnostic builds)
----------------------------------------

Start once with CPU upload reuse, diagnostics and render profiling enabled.
Leave outer upload reuse, upload batching and stereo draw audit/proof off.
The log confirms `runtime toggle armed hotkey=Ctrl+Alt+F10 initial=on` only
when the verified upload hook is available. Ctrl+Alt+F10 then toggles focus
upload reuse before the next complete scene frame, without restarting LMU.
Each transition clears cached GPU-content assumptions; all hooks stay installed.
OFF retains the hook overhead, so it is a controlled comparison with ON, not
an exact measurement of a build without these hooks.

Press and release Ctrl+Alt+F10 once to select OFF, then press Ctrl+Alt+F9 once
to capture 30 seconds (or the configured duration). After completion select ON
with Ctrl+Alt+F10 and capture again. Repeat OFF to check drift. Keep the same
standing cockpit, head/gaze direction, graphics and background workload. Keep
the headset rendering throughout; reject captures with missing focus samples.
Each capture logs its CPU experiment configuration. F10 presses during a capture
are rejected and consumed, never queued for later. The hotkey is only available
in profiling runs; ordinary play and launch settings keep their existing behavior.

Diagnostics may be disabled for ordinary play; reuse still runs and its optional
counters/logs are disabled. All prototype options default off in distributed
settings. Untick CPU upload reuse and relaunch to restore the native upload path.

Owned synthetic tests on WARP and an RTX 4080 check exact GPU buffer contents,
retained bindings, data/buffer changes, scope resets, incomplete uploads, aliases
and counters-off behavior. These are not an LMU performance benchmark. AMD/Intel
hardware and live LMU behavior still need testing. The implementation uses no
NVIDIA-specific API.
