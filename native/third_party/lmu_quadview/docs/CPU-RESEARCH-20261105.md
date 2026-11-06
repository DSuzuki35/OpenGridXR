# CPU and NVIDIA stereo research, 5 November 2026

This branch is an unsigned, offline-test candidate. It has not been installed
over the active game, signed or published. The native SPS/MVR scheduler is not
connected to the game renderer.

## NVIDIA experiments

The isolated backend and DXBC adapter cover MVR2/MVR4 shader interfaces and
mocked NVAPI submission. WARP checks cover generated shader creation, per-view
positions, generic attributes, constant banks, masks and native instance IDs.
The portable stereo converter regression tests also pass.

Two physical NVIDIA experiments returned `DXGI_ERROR_DEVICE_HUNG` during
readback, although the preceding NVAPI shader/mode/reset calls returned success.
LMU was running during these attempts, so these were not isolated GPU runs.
Concurrent game activity is a possible confounder, not an established cause.
Driver success codes and WARP correctness do not prove hardware correctness.

A user-authorized repeat at 09:29:48 UTC on the same day used the unchanged
test binary (`B81D52C65B62AAD226E51926E3776A9146DD95115CE1BAAF1EBF0EA96B265B6F`)
and the same NVIDIA driver. No LMU process was present before or after the
3.84-second run. The MVR4 three-vertex test again failed on readback:
`Map=0x887a0005`, `GetDeviceRemovedReason=0x887a0006` (`DEVICE_HUNG`).
LMU being open is therefore not necessary to reproduce the failure. This does
not yet distinguish a prototype/API-use problem from a driver problem.
The local evidence is in `diagnostics/20261105-mvr-isolated-retest` in the
outer workspace. The default software/mock test passes before and after this run.

Do not enable this backend in LMU. The repeat without LMU still failed, so
further hardware experiments need a specific code/API or driver hypothesis. The explicit
`--hardware` test remains opt-in. There is no demonstrated in-game SPS/MVR CPU
improvement. Classic SPS and arbitrary four-position MVR are distinct paths.

## Conservative native validation

Retained COM interfaces in the outer bank are AddRef-owned and already reuse
cached validation. Renderer objects, wrapper slots and native vectors are
borrowed. Their checks remain until renderer destruction/resize generations
and lifetime ownership are proven. A matching pointer alone is not that proof.

## CPU changes

Fresh launcher settings disable diagnostics, overlays, optional render
experiments and startup update checks. Explicit user opt-ins are preserved.
Diagnostic output uses a bounded worker queue and asynchronous flush requests.
The active-mode readers use atomic lifecycle snapshots. Repeated periodic
messages are sampled at ten-second intervals after initial diagnostics.

The Begin regression reproduces the Pimax late application Begin sequence,
checks the state mutex from another thread during runtime Begin, preserves a
pipelined Wait and rejects teardown while the runtime call is in flight.
Normalized Begin runs at the valid Locate boundary; D3D diagnostic setup stays
on engine access. Abandoned acquired images prevent early normalization.

Outer ownership tests check unlocked acquisition, blocking/nonblocking waits,
release and destruction, prefetched ownership, timeout retry and rejection of
borrowed render targets during an in-flight runtime operation.

Tracking, VRS gaze sampling, EndFrame and application-image Acquire/Wait/Release
also release the state lock during runtime calls. Tests check callback guards,
independent application chains, submission metadata, failed releases and the
next pipelined Wait. Startup/teardown and optional benchmark-overlay runtime
operations still require their own lifecycle transactions; they have not all
been changed. Diagnostic capture finalization can still do CPU aggregation.

These are correctness and regression checks. A new matched cockpit capture is
still needed before making a CPU-frametime improvement claim.

## Verification

The final MSVC native build and full test script passed, including 80 printed
XR integration scenarios plus the focused lock/ownership, async writer lifetime,
shader and fallback tests. The complete launcher build passed its settings,
launch, profile, hardening and update tests and all hidden-window layout cases
at simulated 100/150/200 percent scaling and default/minimum sizes.

The runtime-pruning fixture now injects process presence, including a case that
preserves every generated file while a game is running. Production still checks
the real process list. The concurrently running user game was not closed or
replaced. All test binaries remain unsigned. Full logs are local build artifacts
`mod/build/cpu-final-msvc.log` and `mod/build/cpu-launcher.log`.
