# HorizonLock VR and OpenGrid XR

OpenGrid provides pose-stabilized Clean Output for iRacing and Le Mans Ultimate. It covers the core camera and capture-card workflow. Several other SteadyXR workflows still require SteadyXR, including direct OBS capture, stabilization of an existing window and Director. This comparison uses upstream main at `527c1c47a8ab8df51d12f0d2cdbb15a165c3ab2f` (0.14.0). [Upstream release source](https://github.com/DSuzuki35/horizonlock-vr/tree/527c1c47a8ab8df51d12f0d2cdbb15a165c3ab2f)

## Standalone capture and camera

| SteadyXR feature | OpenGrid coverage | Where it works or what is missing |
| --- | --- | --- |
| Capture submitted eye images with matched poses | Partial | Clean Output captures D3D11 stereo. The integrated LMU quad-view path combines peripheral and focus images when both are usable. Capture of native D3D12 game sessions and generic third-party quad-view sources remains absent. |
| Gaming-PC output for an HDMI capture card | Present | Open a separate window on the selected extended display. It supports a configured resolution and FPS, borderless fullscreen startup, F11 and Esc. Capture the same window with OBS on one PC. |
| Independent canvas resolution and picture aspect | Partial | The output fits the profile canvas to the display. Its picture aspect follows the canvas dimensions. There is no separate aspect field or editor for arbitrary dimensions. The SteadyXR importer reports a differing source aspect. |
| Waiting, pause and capture diagnostics | Partial | A missing or stale source shows a waiting screen. Focus Clean Output and press **P** to freeze the last frame and release capture demand. **P** resumes. The Stream composition illustration shows the configured FPS target. It lacks SteadyXR's live capture FPS meter. |
| Camera smoothing, roll lock, adaptive curve and reset | Present | Tune all three under **Stream → Camera behavior**. **R** in Clean Output resets the upright and pitch references. Camera stabilization bypass retains tuning. |
| Natural, Stream Comfort and Strong Leveling presets | Present | The presets use 25/25, 60/50 and 70/85 percent smoothing/roll lock respectively. They enable adaptive leveling and mirrored borders and set the four margins to 4%, 6% or 7.5% each. OpenGrid replaces the current margins, including an asymmetric set. Pitch and positioning retain their values. |
| Timed original, split and side-by-side comparisons | Partial | **B** in Clean Output shows the original pose for 20 seconds and ends comparison early on a second press. The same crop, reserve, overlays and output dimensions apply. Split, fitted side-by-side and effects-only comparison are absent. |
| Extra rendered scenery and reserve per eye | Partial | Select an output eye, set independent left/right expansion and four reserve margins. OpenGrid applies the same expansion to both eyes and keeps one reserve set per profile. SteadyXR stores two eye profiles and mirrors nasal/temporal edges across eyes. LMU's integrated quad path supports full outer area without extra expansion. |
| Four framing trims, offsets, mirrored edges | Partial | Four trims, X/Y position, finite mirror extension, softness and stretch are available. OpenGrid trims before reserve and aspect fitting. Limits differ and imported presets report conversions. Reserve and offsets preview live. Other camera tuning applies when Clean Output reopens. |
| Named profiles and complete INI presets | Partial | OpenGrid saves named game profiles and imports supported SteadyXR INI settings into a new draft. Import keeps the selected eye's values and reports skipped sections or clamps. It does not round-trip a complete SteadyXR INI, source-window identity, file paths, monitor bindings, keyboard shortcuts or unknown fields. |
| Global reset, pause, A/B and next-preset shortcuts | Partial | The focused Clean Output window handles R, P and B. The configurable system-wide shortcut group, shortcut conflict reporting and cycling through camera presets are absent. |
| Selected additional OpenXR layers | Partial | OpenGrid's spatial sources opt into the stream individually and follow their eye-space geometry. SteadyXR can enumerate and composite externally submitted quad, cylinder and additional color projection layers. OpenGrid has no equivalent selector for these external layers. |
| OpenXR registration and layer editor | Partial | OpenGrid installs its own connection, inspects 32/64-bit registrations, toggles individual enabled flags, and has checked backup/restore for those flags. SteadyXR's staged batch operations, registration removal and implicit-layer reordering are absent from the general editor. |
| Windows Graphics Capture as the primary stream source | Absent | OpenGrid can show captured windows as spatial overlays. It cannot select an existing sim window as the primary video source and stabilize it from image features without OpenXR capture. |

[OpenGrid streaming guide](streaming.md) · [Spatial overlay guide](spatial-overlays.md) · [OpenXR layer editor](OPENXR-LAYERS.md) · [SteadyXR preview reference](https://github.com/DSuzuki35/horizonlock-vr/blob/527c1c47a8ab8df51d12f0d2cdbb15a165c3ab2f/preview/README.md)

## Spectator, Director and OBS

| SteadyXR feature | OpenGrid coverage | What is missing |
| --- | --- | --- |
| Gaze framing, learned chat exclusion and natural head movement | Absent | The streamed camera follows submitted head poses. LMU eye-tracked focus controls rendering detail. It supplies none of the spectator gaze dead zones, offsets, learned chat region or Natural Head Movement tuning. |
| Viewer Director | Absent | Delayed-frame lookahead and its measured memory/audio-sync status, adaptive crop and speed-dependent framing are absent. OpenGrid reads speed for telemetry and benchmarks but does not feed it into the output camera. |
| Output image effects | Absent | SteadyXR offers manual and adaptive exposure with a metering region, bloom and color grading before spectator overlays. OpenGrid's separate VR color correction applies to the headset. Clean Output has no corresponding effects pipeline, comparison or Filmic preset. |
| Direct OBS OpenXR source and visual-effects filter | Absent | OpenGrid's separate output window is available to OBS Window Capture. OpenGrid does not supply an OBS module that receives the eye textures directly or grades a capture-card source. |
| OBS capture-card stabilizer | Absent | The upstream OBS effect offers SteamVR pose, image-feature microjitter and hybrid residual correction, ROI, shift/roll limits, per-axis smoothing, deliberate turn following, auto crop, horizon correction cap and framed A/B. OpenGrid's game-layer pose path does not include this filter or its scene-specific presets and OBS hotkeys. |
| SteamVR sender, calibration and timing tools | Absent | The UDP sender's selected eye, frame/live/demo modes, clock sync, delay/FOV fitting, connection diagnostics and photo timing overlay are separate upstream tools. OpenGrid pairs each captured image with its submitted pose locally. |

[SteadyXR spectator effects](https://github.com/DSuzuki35/horizonlock-vr/blob/527c1c47a8ab8df51d12f0d2cdbb15a165c3ab2f/docs/spectator-features.md) · [Director](https://github.com/DSuzuki35/horizonlock-vr/blob/527c1c47a8ab8df51d12f0d2cdbb15a165c3ab2f/docs/viewer-director.md) · [OBS source](https://github.com/DSuzuki35/horizonlock-vr/blob/527c1c47a8ab8df51d12f0d2cdbb15a165c3ab2f/docs/obs-openxr-source.md) · [OBS filter and calibration](https://github.com/DSuzuki35/horizonlock-vr/blob/527c1c47a8ab8df51d12f0d2cdbb15a165c3ab2f/README.md)

## Local SteadyXR Workshop

The sibling checkout also has an unpublished 0.16.0 branch at `a84c2689feb7017d717463c8020c2503c9823e0e`. It adds a Workshop with bounded H.264 recording, synchronized image/pose metadata, clip playback and a draggable crop editor, plus combined peripheral/focus source framing. OpenGrid's LMU output composes peripheral and focus views and its pitch control can anchor the initial pitch. Its Stream page has live reserve and offset previews. It has no SteadyXR Workshop clip recorder, metadata replay or video-based framing editor.

OpenGrid's profile, INI, native math, GPU readback and Stream UI fixtures cover the camera controls here. The comparison describes available code paths. Driving behavior on a live headset and capture-card timing need a hardware session.
