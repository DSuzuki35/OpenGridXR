# Performance branch implementation plan

Goal: implement opt-in CPU/GPU experiments on release main in experiment/performance.
Authorization: user requested implementation in a separate performance branch and accepts slight visible quality loss.

Architecture: retain native preparation and frame scheduling until their side effects are understood. Extend scoped upload reuse to outer opaque rendering; reduce independently owned SSR resources within the native outer bank. Restore native resources before focus rendering. All switches default off.

## Tasks
- [x] CPU: cache upload buffer alias topology with replacement/alias/scope regression tests; enable existing dirty upload filtering in outer group 31 via LMU_QV_CPU_OUTER_REUSE.
- [x] GPU: validate native SSR resource layout and implement bounded outer-only reduction via LMU_QV_OUTER_SSR. Reject unsupported formats/mips/aliases rather than altering focus.
- [x] CLI: expose OuterSsr and CpuOuterReuse, validate prerequisites and record run configuration. Exercise real launch configuration without starting LMU.
- [x] Integrate tests into native build, run MSVC/WARP and CLI regression checks, review changes; preserve the result on the performance branch.

## Review focus
Buffer replacement and aliasing must invalidate upload reuse. Nested groups must not inherit eligible scopes. SSR resources and associated depth must restore together. Unsupported SSR layouts must retain native rendering. Default/trace/stereo modes must retain existing behavior. Diagnostics off must not disable requested optimizations.

## Research limits and rulings
Shared object preparation remains unimplemented: existing SPS numeric transforms do not model native material/cache side effects. The implementation must not skip native preparation on numerical agreement alone.
PPFX remains unchanged: its singleton owns shared global state and temporal histories whose independent ownership is not established.
Cross-view material caching and temporal object reuse remain research targets: current hooks do not establish complete resource writes, stable material identity, motion/depth ownership or a scheduler that can remove original work safely. Adding a cache without suppressing native work would not implement an optimization.
These limitations narrow this first runnable experiment; they are not claimed as completion of all four research proposals.

## Verification record
Baseline MSVC native suite passed before edits. Integrated MSVC native/WARP suite
also passed, including a fresh full run after final metadata invalidation hardening.
The performance, portable launch, focus offset and QVFR profile PowerShell tests passed.
Independent review found an SSR history rectangle mismatch in the initial design;
the implementation now caps dimensions at both original SSR and outer scene sizes.
A 40% per-axis quality floor refuses excessive reduction. This is an experimental
policy rather than a perceptual guarantee. Unsupported SSR falls back once to the
small primary bank without repeated allocations on subsequent frames.
No live headset benchmark or performance improvement is implied by standalone tests.
