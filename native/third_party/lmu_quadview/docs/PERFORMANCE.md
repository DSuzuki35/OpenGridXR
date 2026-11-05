# Performance changes and measurement

## Version 0.1.0

The safe default path is shared by QVFR and VectorXR. This pass removes redundant
CPU bookkeeping without changing scene draws, resolution, FOV, gaze, composition
or D3D11 state restoration. It does not enable the experimental upload/stereo options.

- `layerEndFrame` keeps its one converted projection and four views in local
  storage. Up to 16 input layer pointers fit on the stack; larger submissions use
  one allocation. These objects live through the synchronous provider call.
- `xr_outer_target` returns a borrowed RTV and cached dimensions validated during
  successful outer capture.
  Outer UI rendering no longer repeats `GetResource`, `QueryInterface`, `GetDesc`
  and their matching releases for both eyes on every UI invocation.
- If neither outer target is available, there is no additional UI draw and no
  need to snapshot/restore the pipeline. Drawing with an available target still
  preserves the complete tested state.

### Synthetic evidence

The regression test executes the actual adapter against an allocation-free fake
runtime, with diagnostics disabled. The same test was built against the previous
and updated adapter using LLVM-MinGW, C++17 and `-O2`. Median of seven batches of
200,000 submissions on the development machine:

| Input layers | Previous allocations | 0.1.0 allocations | Previous ns/call | 0.1.0 ns/call |
| ---: | ---: | ---: | ---: | ---: |
| 1 | 3 | 0 | 119.344 | 39.525 |
| 4 | 5 | 0 | 185.708 | 39.059 |
| 16 | 7 | 0 | 266.555 | 42.899 |
| 97 | 10 | 1 | 505.426 | 97.717 |

These are nanoseconds of **adapter bookkeeping**, not complete frame times,
provider latency or a game benchmark. The normal one-projection saving here is
about 0.00008 ms. No live CPU/FPS gain is established by this table, and no matched
in-headset measurement was available for this pass. The UI savings have not been
timed separately. Run `mod/build/frame_submission_test.exe --benchmark` after a
native build to repeat the synthetic test; `--profile` tests scene/menu boundaries.

Tests also cover projection metadata/order, invalid and duplicate projections,
large layer lists, release failures, focus-image ownership, per-eye dimensions,
unavailable targets and restored pipeline/pixel behavior. They cannot replace
game/runtime validation.

## Investigating CPU spikes or +4-5 ms

The current adapter calls the native scene renderer for two additional focus
views. Lower outer resolution reduces GPU pixel work but does not remove those
CPU traversals. The stereo prototype checks shader/draw equivalence in private
targets; it does not yet replace native scene traversal. Enabling it is not a
CPU optimization. Upload reuse/batching also lacks a matched demonstrated win;
see [the experiment's results](../CPU-UPLOAD-FASTPATH.md).

For useful before/after evidence:

1. Keep the same LMU executable, replay/session, car count, camera, weather,
   headset refresh rate, runtime, resolution, focus size and tracking mode.
2. Compare the prior build and 0.1.0 separately for each provider. Warm up first,
   repeat both orders and capture the same driving interval. Keep diagnostics
   and profiling settings identical. Avoid stereo audit, pixel capture and
   upload experiments during baseline runs.
3. Follow [render profiling](../RENDER-PROFILING.txt): enable diagnostics and render
   profiling, choose a duration, then press Ctrl+Alt+F9 while driving. Separate
   scene samples from menu samples. Compare `outer0/1`, `focus_scene0/1`,
   `native_prepare0/1`, `ui`, `outer_wait0/1`, `provider_submit`, CPU P95/P99 and
   sparse GPU timings. Nested CPU scopes overlap and include waits; do not sum
   parent and child scopes or interpret them as CPU utilization.
4. Retest normal gameplay with profiling disabled. Record visual quality,
   tracking, frametime spikes and GPU cost alongside CPU timing. A lower mean
   alone does not establish an improvement if tail latency or GPU cost regresses.

Include the mod/provider/runtime versions, exact settings, actual `quad-*.log`
and capture output in a report. Review logs for personal paths before sharing.
State whether eye tracking was working and whether CPU numbers come from the mod
profiler, runtime overlay or another tool; they do not necessarily measure the
same interval.
