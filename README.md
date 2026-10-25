# OpenGrid XR

**THE SIM RACING XR TOOLKIT — by Dan Suzuki**

[Build and test](https://github.com/DSuzuki35/OpenGridXR/actions/workflows/build.yml) · [Contributing](CONTRIBUTING.md) · [Telemetry API](docs/telemetry-api.md) · [Overlay guide](docs/spatial-overlays.md)

Continuing on another machine? Start with [NEXT-STEPS.md](NEXT-STEPS.md), including the target-rig checks and pending runtime optics work.

Free and fully open-source Windows toolkit for iRacing and Le Mans Ultimate. Version 0.2.1 is a development preview with native crop/capture/output, integrated headset frame generation and performance overlay, headset sharpening, native iRacing foveation controls, read-only telemetry, and spatial URL/window overlays. Real sim/headset compatibility and performance still need validation.

## Run

Double-click `Start OpenGrid XR.cmd`, or run `release/0.2.1/win-unpacked/OpenGrid XR.exe`. Keep the complete `win-unpacked` directory together. Close the older OpenGrid window before starting an updated app. The build is unsigned; signing can be performed externally.

1. Select iRacing or Le Mans Ultimate, then a profile.
2. In **View & crop**, set rectangular crop and render scale. Enable the custom visibility mask only if wanted. Edit its control points, use arrow keys for fine adjustment, or start from a Formula/GT/Open contour.
3. Choose **Driving** or **Streaming**, then **Save profile**. Saving writes only OpenGrid XR's own files.
4. Close both sims and Clean Output. Use **Setup → Install runtime layer** when ready. This explicitly registers only OpenGrid XR's own current-user 64-bit OpenXR entry; it does not switch runtimes or remove other layers.
5. Start the sim through OpenXR. A native OpenVR path will not load this layer. Refresh status in Setup to see whether an actual session has connected.

Start from neutral settings. Overlapping FOV/visibility modifications from another layer can interact with OpenGrid XR; use one crop mechanism while validating a setup. Registration is configuration, not proof that a running game loaded it.

## Driving and streaming

**Driving:** rectangular crop adjusts the OpenXR projection and recommended render dimensions together. Render scale controls pixel density. A freeform contour can add a conservative exterior hidden-area mesh, with padding. The game must query and use the hidden-triangle visibility mask for that optimization to have an effect. A polygon is not a guarantee of GPU savings. The runtime must permit mutable FOV for rectangular crop.

**Streaming:** the native layer preserves the full source and can expand its horizontal projection. The saved custom contour is inactive. Rectangular crop becomes output framing; it does not reduce headset rendering in this mode. Both eyes currently receive the requested expansion. This reserves source content for smoothing and horizon stabilization. Larger render dimensions are recommendations; the game may ignore them.

Render/contour changes are snapshots at game session startup: restart the game to apply them. Clean Output reads its settings when launched; close and reopen it after changing output/framing settings. No live geometry resizing occurs underneath the game.

## Two-PC capture-card output

1. Connect a gaming-PC GPU output to the capture card on the stream PC. Configure it as an **extended Windows display**.
2. In **Stream**, select that output display, set the internal canvas resolution/FPS, and enable **Start fullscreen**.
3. Save the Streaming profile, install the connection if needed, and start/restart the OpenXR game.
4. Select **Open clean output**. It opens a separate native window on the selected monitor, in borderless fullscreen. The settings app stays separate from the captured image.
5. **F11** toggles fullscreen. **Esc** returns to windowed mode; a second Esc closes the window. If the monitor is disconnected, choose a connected output before launching.
6. In **Spatial overlays**, use **Show in stream** per enabled source. Selected overlays retain their VR placement in the streamed camera view. Sources are VR-only by default. The additional stream texture ring runs only while Clean Output is active.

Set the Windows display mode to the capture card's supported resolution and refresh rate; selecting a canvas/FPS in OpenGrid XR does not reconfigure Windows or the capture card. Game audio is configured separately. On one PC, OBS can capture the Clean Output window. A direct OpenGrid XR OBS source plugin is not included.

**Symmetric extra area** links both expansion sliders. Turning it on sets both to the larger existing side; turning it off keeps the values for independent editing. The choice is stored in each profile.

The output uses D3D11 shared textures, so its graphics adapter must be compatible with the game's. Supported stereo D3D11 projection capture is the initial path. Unsupported capture formats/view modes should remain pass-through for the game and must not be represented as working capture. No automatic game launch or runtime switch is performed.

## Helmets

The current helmet page is an **original visual fit guide**. Settings are saved, but production helmet meshes/textures and an in-headset helmet renderer are not included yet. Those will be authored independently in Blender. The native custom contour crop works independently of the preview drawing.

## Telemetry and spatial overlays

**Telemetry** reads the native iRacing SDK and LMU shared-memory interfaces. Enable it in a game profile, save, and open the telemetry page. It displays source freshness, speed, RPM, pedals, fuel, laps and supported flags; unknown or stale values are not shown as live. Collection defaults to 20 Hz, is capped at 30 Hz, and pauses without a consumer. Current supported LMU builds use their built-in interface, with no external shared-memory plugin. See the [telemetry API](docs/telemetry-api.md) for the standalone read-only JSON Lines interface and [source requirements](native/telemetry/README.md).

**Spatial overlays** supports up to 15 URL/window sources with head-following or local placement, presets, individual opacity/size and a combined frame-transport budget. Save settings and restart the sim when enabling the engine for the first time. Select capture windows again after restarting OpenGrid. **Recenter overlays** aligns their local anchors; **Center VR in game** sends your explicitly configured in-game recenter shortcut after a delay. See the [overlay guide](docs/spatial-overlays.md) for placement, resource controls and GPU/runtime limits.

## Profiles and recovery

Profiles and exact-file backups live in the app's data directory (**Setup → Open data folder**). The runtime lives under `%LOCALAPPDATA%/OpenGridXR/runtime`. The last saved profile for each game is selected for its next session. Imports are validated drafts until saved. Exported profiles contain the contour and all local settings; no executable code or asset paths are imported.

Stale saves are rejected. A damaged profile file is preserved; the app can show its backups and restore a valid prior file. Restore also preserves the displaced file. **Setup → Remove layer** removes only this application's current-user registration and keeps files/profiles for recovery.

## Headset frame generation and VR overlay

The optional native engine is included in OpenGrid XR. No VectorXR desktop app or second layer installation is needed. OpenGrid privately loads `OpenGridXR-Enhancements.dll` downstream of its crop/capture layer. Source and modifications are included under MPL-2.0 with attribution to DienerTech's VectorXR. Existing VectorXR settings are not used or changed.

In **Frame generation**, enable the feature, select interpolation or extrapolation, choose Dynamic/Auto/2×/3×/4× and adjust quality/artifact controls. Shader motion estimation is the portable default; NVIDIA hardware estimation uses the optical-flow component in the installed graphics driver and can fall back where unavailable. Enabling frame generation requires a game restart. Disable the runtime's own motion smoothing while using it. Frame generation is experimental: generated frames can exhibit artifacts and interpolation adds latency. The streaming window continues to show captured real game frames.

In **Performance HUD**, select compact or expanded layout, position, distance, scale, opacity, colors, update rate and displayed information. The real head-following panel can show application FPS/frame intervals, a timing graph, performance metrics and actual frame-generation state. The toolkit summary is labelled **Configured**: a saved setting is not proof that a running session has applied a restart-required change. This is a configurable status panel, not an interactive in-headset settings menu. Desktop previews use placeholders, not invented live metrics.

Default shortcuts are **Ctrl+Alt+F9** for the frame-generation A/B toggle, **Ctrl+Alt+F10** for overlay visibility and **Ctrl+Alt+F11** for overlay layout. Keys are configurable or can be unbound; duplicate assignments are rejected. Both features default to off. After upgrading from 0.1.0, close the sims/output and use **Setup → Install/update runtime layer** to deploy the added engine.

## Supersampling, foveation and sharpening

**View & crop → Supersampling / render scale** sets OpenGrid's per-axis resolution recommendation, from 50 to 200%. At 200%, width and height double, so pixel count quadruples. Game and runtime supersampling multipliers combine with this setting; a game may ignore recommendations.

**Foveated rendering** exposes iRacing's documented native settings: peripheral resolution (`FoveatedOuterPctRes`), focus width (`FoveatedInsetWidthPct`), and game resolution scale (`ResolutionScalePct`). iRacing's native Quad View mode uses NVIDIA MVP/VRWorks; it is distinct from an external `XR_VARJO_quad_views` provider. Enable **Foveated** in iRacing's VR graphics mode and restart. Eye tracking is configured in the game's **Allow Eye Tracking / Show Eye Tracking** controls and needs a compatible headset/runtime. OpenGrid does not invent or write undocumented eye-tracking keys. See the [original native foveation release](https://support.iracing.com/support/solutions/articles/31000176558-2025-season-3-release-notes-2024-06-10-01-/), [eye-tracking release](https://support.iracing.com/support/solutions/articles/31000177148-2025-season-4-release-notes-2025-09-08-02-/), and [resolution-scale release](https://support.iracing.com/support/solutions/articles/31000179016-2026-season-3-initial-release-notes-2026-06-09-01-/).

Save these values in an OpenGrid profile, locate the existing `rendererDX11OpenXR.ini`, then **Review game changes → Apply to iRacing** with the sim closed. Saving a profile alone never edits the game. Changes preserve unrelated settings/comments/encoding, require the reviewed file revision to remain current, and create an exact backup. Restoring a selected backup restores the entire original file and backs up the displaced version. If no OpenXR INI exists, launch iRacing in OpenXR once and exit first. The adapter does not create a guessed game configuration.

LMU has no verified native foveation adapter in this build. Do not interpret the UI's geometry illustration as measured GPU cost or FPS. Generic VRS injection and external Quad Views are not enabled automatically for either sim.

**Headset sharpening** provides Luma Sharpen (strength, clamp, radius and sample pattern) and AMD FidelityFX CAS. It processes supported D3D11 projection images before frame generation; the status overlay remains separate and clean stream capture precedes headset sharpening. The original MIT-licensed LumaSharpen algorithm also underlies the Sboys3 implementation. Hardware-specific Sboys3 distortion profiles are deliberately deferred: lens correction belongs to the headset driver/runtime and must not be applied twice as a generic application warp.

## Included and planned

Included: profile editor and backup/import/export, per-eye contour editor, supersampling/crop, symmetric stream expansion, own OpenXR layer, D3D11 capture, smoothing/horizon stabilization, fullscreen clean output, integrated experimental headset frame generation, configurable VR overlay, Luma/CAS sharpening, native iRacing foveation configuration with review/backups, read-only iRacing/LMU telemetry, up to 15 spatial URL/window overlays, and explicit centering controls.

Later: original in-headset helmets, direct OBS source, horizon-reset UI controls, wider validated graphics/view configurations, hardware-specific distortion integration and more selective streaming-mask optimization. Director, gaze framing and natural-headmotion features are outside this project's initial scope.

## Wheel, joystick and buttonbox bindings

The controller icon beside each toggle learns a button or POV direction. Save the profile after learning. The **Controller bindings** page lists assignments and supports removing or relearning them. Bindings are per profile and use device instance IDs rather than enumeration order; reconnecting through a different device instance can require relearning.

The native DirectInput reader supports up to 128 buttons and four POV hats per device, uses background nonexclusive acquisition, and sends press transitions only. Holding a button does not repeatedly toggle. Initial and reacquired device states establish a baseline without firing actions. The helper starts only for enabled saved bindings or learning, and stops when unused. No force-feedback or keyboard injection is used for toggle bindings.

Actions work on the saved selected profile while the app remains open, including when a sim is in the foreground. They pause while the editor is dirty, busy or learning a binding. Settings that require a game or Clean Output restart still require it; a binding does not turn a startup setting into a live runtime control. Physical wheel/buttonbox validation on the game PC is still required.

The requested native Pimax and Sboys optics controls are researched but not implemented in this release. See [runtime optics](docs/runtime-optics.md) for exact configuration contracts, verified limitations and the next implementation steps.

## Develop and verify

Requires Node.js 24 or newer, npm, CMake 3.24+ and Visual Studio 2022 with Desktop C++ tools/Windows SDK.

```powershell
npm ci
npm test
powershell -ExecutionPolicy Bypass -File scripts/build-native.ps1
npm run build
npm run test:desktop
npm run package
```

`npm run dev` opens a browser-only interface with local browser storage. Native installation and output require the desktop app. Tests use isolated user data; `OPENGRIDXR_TEST_MODE=1` disables layer installation/removal. Native tests load the actual DLL through a fake runtime and use local D3D11 hardware/shared texture fixtures without registering it globally.

Source is GPL-2.0. See [LICENSE](LICENSE), [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md) and [native details](native/README.md). No expiry, account or paid feature gates.
