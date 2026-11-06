# Benchmark overlay and lap reports

This candidate adds an optional in-headset overlay and manually marked captures.
It does not detect lap boundaries or read car/track telemetry. Press record before
crossing the start line and stop after crossing it again for a lap-length capture.

## Setup

1. Open the launcher and select Benchmark. Enable the benchmark feature and keep
   diagnostics enabled, then launch LMU. A restart is required to load the feature.
2. Leave the launcher running: it handles input, drawing, logging and reports.
3. Choose Minimal or Detailed. Position and size adjust the head-locked panel.
4. Assign separate joystick buttons for visibility and recording. Release buttons
   before binding. Bindings identify a device and button, not just a button number.
5. Toggle recording to start; toggle again to stop. Hiding the overlay does not
   stop capture. Open the finished report from the launcher.

Reports are written locally under benchmarks/ as self-contained HTML plus CSV and
JSON. No browser connection, external chart library or network service is needed.
Checkpoint data can help recover an interrupted run; a crash can still lose the
latest unwritten samples. Connection loss produces an explicitly partial report
when the launcher can finalize it. Keep comparisons on the same car, track, grid,
weather and actual render sizes. The tool does not infer those conditions.

## What the numbers mean

- App scene FPS / interval: cadence of consecutive verified scene frames. Menus and
  discontinuities are not silently merged into lap statistics.
- App wall time: time from the successful frame-begin boundary through preparation
  for submission. Includes CPU work, driver/runtime waits and measurement overhead;
  it is not CPU utilization or a pure CPU execution-time measurement.
- Provider time: elapsed time in downstream xrEndFrame. It may include waits.
- GPU span: sparse asynchronous timestamps for the app-side command interval.
  Excludes downstream provider/compositor work and may include GPU idle gaps.
  It is not total headset GPU frametime or GPU utilization. Invalid/disjoint/missing
  results remain unavailable and are never substituted with zero.
- Percentiles describe recorded samples; frame-interval p95/p99 expose spikes.
  Report sample counts and dropped/overwritten data accompany the metrics.

No CPU/GPU bottleneck verdict, performance gain, displayed/reprojected FPS, or
motion-to-photon latency is inferred from measurements that cannot establish it.
The overlay, shared-memory sampling and recorder have overhead. Use identical
instrumentation settings when comparing runs. Disabled benchmark skips its work.

## Diagnosis

Live status distinguishes missing connection, stale heartbeat, no accepted scene,
invalid eye resources, invalid poses, and an overlay-only failure. A successfully
submitted scene does not prove that the headset displayed it correctly.

Diagnose latest launch also works from ordinary mod logs. It uses the newest run,
checks the last frame before reporting older recovered failures, and explains
what to compare next. A run JSON saying started does not prove successful VR.
When sharing support data, include the actual quad-*.log; raw native logs and
launcher-settings.xml can contain local paths and device identifiers. Benchmark
reports intentionally omit installation paths and joystick identity.

## Design and validation

Visual tokens follow the Dan Suzuki UI kit: carbon/graphite surfaces, ultraviolet
actions, restrained labels and monospaced data. Installed Archivo, Inter and
DM Mono may be used, with Windows font fallbacks; no font download is required.

This remains a tester candidate. Automated ABI, recording/report, input-state,
OpenXR and layout tests do not replace tests in a real headset with a real wheel.
