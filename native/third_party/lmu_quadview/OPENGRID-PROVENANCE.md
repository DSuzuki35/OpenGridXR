# LMU QuadView integration

Upstream: https://github.com/DSuzuki35/LMU-Quad-View-Mod

Pinned published tag: **v0.5.4**.

Exact commit: `aa04884be7d8f52e585ffe9ad2d58be10a1ccd8f`.

Vendored tracked mod sources/tests/standalone launcher, required prototype headers,
architecture documentation, capture/version tools and license notices. Game
binaries, personal files, TODO files and build artifacts are excluded.

OpenGrid changes: private static factory, coordinated four-edge crop, immutable
render-frame image/submission callbacks, independently anchored data-layout
validation, bounded outer-image waits and minimal startup/error reporting.
Parallel tracking queries preserve submitted-frame metadata, while unsafe same-thread
reentry is rejected and counted. A timed-out acquired outer image remains owned
for retry. Waits request at most 5 ms or a shorter positive predicted frame
period; runtime scheduling can overrun the requested timeout. Startup failures remain explicit; no partially configured quad
session is silently changed to stereo. Upstream 0.5.4 removes adaptive
focus FOV; inherited dynamic-focus environment variables have no effect. Standalone launcher isolation
(`DISABLE_OPENGRIDXR`) remains unchanged.

Nine upstream `mod` files are intentionally adapted in OpenGrid 0.5.4:

- `mod/engine_bridge.cpp`: private integration, guarded engine/data binding and startup status.
- `mod/xr_layer.cpp` and `mod/xr_layer.hpp`: private factory/callback API, coordinated crop, rendered-frame ownership, runtime-call concurrency and finite image-wait requests.
- `mod/view_crop.hpp`: coordinated four-edge crop geometry.
- `mod/tests/native_outer_texture_test.cpp`: portable D3D11 active-descriptor regression.
- `mod/tests/end_frame_lock_test.cpp`, `mod/tests/locate_runtime_lock_test.cpp`, `mod/tests/outer_runtime_lock_test.cpp` and `mod/tests/late_begin_frame_test.cpp`: concurrency in both call orders, image ownership and read-only rendered-FOV regressions.

The OpenGrid-only `mod/data_abi.hpp`, `mod/data_abi_anchors.hpp` and
`mod/tests/data_abi_test.cpp` implement and test instruction-anchored data-layout
validation. The integration diagnostic API is outside the vendored tree in
`native/include/lmu_diagnostics.hpp`. OpenGrid provenance and collected license
notices are additional integration files, not upstream renderer changes.

The native outer-texture regression compares the active D3D11 view descriptor
fields instead of unused union storage, with poisoned-storage regressions for
Windows/WARP portability. Runtime texture cloning is unchanged by this test fix.

The v0.5.4 source includes per-eye VRS maps with reusable CPU storage and
uploads only when tile values change, asynchronous diagnostic logging, runtime
lock release with ownership reservations, late-Begin normalization, on-demand
profiler memory, native pointer validation, upload reuse/batching, outer reuse,
and optional outer SSR. CPU cache preference uses the upstream
`LMU_QV_CPU_AFFINITY=x3d` policy, preserving existing process CPU sets or affinity.
OpenGrid defaults this machine-local preference off when absent and preserves
explicit saved on/off choices across normal Steam, offline and legacy launches.
The upstream scheduling algorithm is unchanged; no measured speedup is claimed.
SPS/MVR research sources are retained as upstream references and are not enabled
in the integrated library. The VRS hardware diagnostic is a manual build target,
never an automatically run test.

Upstream credits include **Markus Stahl** for hook safety, cleanup, QVFR encoding
preservation, profiles, performance work, per-eye VRS caching, CPU cache
preference, and stability fixes. See the upstream README for the full credits.

See LICENSE.md and THIRD_PARTY_NOTICES.md. Original code is licensed under
PolyForm Noncommercial 1.0.0; the upstream license and separate component notices
remain applicable. This integration does not relicense upstream sources.
