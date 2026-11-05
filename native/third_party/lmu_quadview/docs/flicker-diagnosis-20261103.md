# Track flicker and missing gaze: 2026-11-03

Observed setup: Dream Air SE / SBoys3 1.3.0 / SteamVR 2.18.4 / QVFR 1.1.3.
Run `quad-20261103-091752-4e90ba67`: dynamic FOV off, outer 25%, focus
250%, native outer targets on, green border on. Actual targets were outer
806x754 and focus 3578x3540 per eye. Provider focus width was 44.44332107%
(UI-calibrated display approximately 46%), height 47%.

## Evidence

- QVFR reports no eye-tracker data received after 60 seconds.
- OpenVR read-only Background client reports `dashboardVisible=1`,
  `inputAvailable=0`, scene focus PID equal to LMU PID 52060. Headset activity
  also reached UserInteraction while input remained unavailable. SteamVR's
  OpenXR session reaches VISIBLE but never FOCUSED in this run. This blocks
  active OpenXR action input; there may still be further gaze problems after
  dismissing the dashboard.
- Desktop UI attempts and a temporary enableDashboard off/on toggle did not
  dismiss the dashboard. The setting was read back as restored to true. No VR
  services were stopped. User ended LMU; subsequent scene PID was zero.
- Live shared-memory snapshot: 3514 submitted scene samples, mean application
  interval 15.82 ms (p95 21.79), app span 13.79 ms (p95 19.44), provider CPU
  submission 0.55 ms. 439 GPU samples averaged 11.21 ms (p95 17.89).
  GPU span excludes compositor work. These are one window, not a benchmark of
  every reported flicker event, and do not establish GPU load as sole cause.
- Sampled frame 15000 locates views, renders both focus eyes and copies both
  outers, releases both app images, then locates again at the same display
  time before submitting. Previously every locate overwrote `s.views`, even
  after rendering consumed those FOVs.

## Correction and validation

The first successful renderer read locks that frame's views, uncropped outer
FOV, locate time/space and dynamic scale. Subsequent calls still return fresh
tracking to their caller but cannot replace the rendered image metadata. New
frames unlock the snapshot. Late failed or invalid queries also cannot erase
an already rendered frame. Sparse `render_views preserved` diagnostics report
FOV and position changes between these queries.

The real-adapter regression fails before the correction and passes afterwards:
moving gaze, fresh caller output, both-eye consistency, failed/invalid late
queries, different time/space, uncropped FOV, stable dynamic scale, composition
submission, and next-frame refresh. Existing invalid-FOV tests now explicitly
exercise failures before the renderer consumes its snapshot. An independent
review found no important issue. At this stage actual symptom resolution was
not yet verified; see the subsequent live validation below.

## Next capture

`tools/start-frame-capture.cmd` launches with the package's saved settings and
the usual launcher executable/provider checks. It arms a single four-image
capture in a unique `logs/capture-*` directory, without creating its trigger.
After loading the track, create the empty `capture.trigger` file named by
`capture-run.json`. Do not measure performance during this one readback frame.
The log records all four captured FOVs/poses and successMask=15 on success.
This can distinguish misalignment already present in the input textures from
provider composition/reprojection. No files are uploaded.

References: OpenXR xrSyncActions specification and Valve OpenVR public headers.
https://registry.khronos.org/OpenXR/specs/1.1/man/html/xrSyncActions.html
https://github.com/ValveSoftware/openvr/blob/master/headers/openvr.h

## Same-frame capture identifies an additional crop conflict

Native LMU run `quad-20261103-095200-f66dcdb6`, PID 43688, captured frame
4619 with all four images successful (mask 15). The failed preceding run had
fallen back to OpenComposite after SteamVR reported HmdNotFound at startup;
this capture is from the subsequent successful native OpenXR start.

The outer image cropped using the recorded runtime focus tangent bounds has
matching debug border and HUD positions, but vertically displaced cockpit
geometry compared with the focus image. BMP RGB must be inspected with alpha
ignored: source alpha is not uniformly opaque and ordinary alpha compositing
incorrectly makes some car surfaces black in diagnostic previews.

VectorXR logs the layer order VectorXR -> OpenGridXR -> LMU adapter -> QVFR.
OpenGridXR's installed LMU profile has CropTop=8, CropBottom=21, Enabled=1;
its live status confirms active cropping in this PID. The adapter sees outer
vertical projection scale 0.8741776 and zero offset, while LMU's native camera
has scale 1.231236 and offset -0.1830986. These are exactly the 71% remaining
height and asymmetric crop: 0.8741776/0.71 and -0.13/0.71. Native outer geometry
uses the upstream crop; focus rendering and submission use the downstream FOV.
The old dimension normalization masks the corresponding 3540 -> 2513 height
change without removing the incompatible FOV change.

The launcher now sets the installed manifest's DISABLE_OPENGRIDXR switch only
in quad child processes (Native, QVFR, VectorXR). VRS preserves the inherited
preference. No registry or installed settings are changed. Run metadata exposes
OpenGridXrDisabled. A regression test failed before the change and verifies
quad isolation, untouched parent environment and VRS behavior afterwards.
At this stage headset validation after restarting was still required.
Missing gaze remained separate: QVFR again reported no data after 60 seconds.
The next section records the subsequent validation and recovery.

## Live validation and eye-tracking recovery

Run `quad-20261103-101622-4da8547f`, PID 12180, uses the isolated launcher.
VectorXR confirms the downstream chain contains LMU adapter and QVFR with no
OpenGridXR. Native and adapter projection scales/offsets now match exactly
(vertical 0.8741776, offset zero). All four images of frame 5781 were captured
successfully. Tangent-space comparisons of both eyes show aligned geometry;
the user reports the picture is now aligned and steady. Dynamic FOV was off
during this validation, so this is not a new dynamic-FOV performance test.

The SteamVR desktop menu's `Toggle Dashboard` action finally closed the
dashboard at approximately 10:21:25 local time. Read-only OpenVR reports
dashboardVisible=0 and inputAvailable=1; LMU subsequently reaches FOCUSED.
This alone did not restore gaze: the user still observed a stationary border.
The user then restarted eye tracking in Pimax Play and confirmed it worked.
SBoys3's `AppData/Roaming/CustomHeadset/diagnostic.json` also reported
eyeTracking.valid=true. Do not infer that the previous no-data warning proves
permanent hardware or runtime incompatibility.

Recovery for this observed setup: use the isolated quad launcher, close the
SteamVR dashboard, and restart eye tracking in Pimax Play if gaze remains
stationary after the session becomes FOCUSED. The underlying reason the Pimax
eye-tracking service needed restarting has not been established.
