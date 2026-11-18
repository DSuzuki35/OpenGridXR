# Set up Clean Output

![OpenGrid XR 1.1.0-beta Clean Output composition preview and Open output button](images/screenshots/beta-clean-output.png)

*The beta editor shows an illustration of the configured canvas and its FPS target. The separate Clean Output window receives game frames.*

## Camera presets

1. Open **Stream → Camera behavior** and choose a **Camera preset**. Each preset enables adaptive roll leveling and mirrored borders, then sets all four reserve margins equally. This replaces any individual reserve adjustments.
2. Select the **Source eye**. For the view in the screenshot, choose **Stream Comfort**. Save the profile, then close and reopen **Clean Output** to apply the camera tuning.
3. Focus the output window and press **B** to compare the original pose for 20 seconds with the same crop and overlays. Press **B** again to finish early. **R** resets the upright and pitch references, and **P** freezes the last frame and stops requesting new captures until resumed.

| Camera preset | Motion smoothing | Horizon lock | Reserve per edge |
| --- | ---: | ---: | ---: |
| Natural | 25% | 25% | 4% |
| Stream Comfort | 60% | 50% | 6% |
| Strong Leveling | 70% | 85% | 7.5% |
| Dan Suzuki (Motion) | 82.5% | 21.1% | 18% |
| Dan Suzuki (No Motion) | 40% | 21.1% | 18% |

**Advanced smoothing** splits yaw and pitch into horizontal and vertical sliders. The roll slider sets smoothing for head tilt. Regular smoothing uses one strength for all three. Both settings are saved with the profile and can be imported from a steadyXR INI. [steadyXR 1.0.2](https://github.com/DSuzuki35/steadyXR/releases/tag/v1.0.2)

**Dan Suzuki (Motion)** uses the right eye, pitch stabilization of 3.3%, and framing offsets of −77 and −28.3. Mirrored borders use a 29.3% limit, 100% softness and 31.6% stretch. The No Motion variant uses the same framing with a 10% mirror limit. The preset enables the full render area with zero extra area.

<img src="images/screenshots/beta-stream-camera.png" alt="Stream Comfort applied in the beta editor with 60 percent motion smoothing, 50 percent horizon lock and adaptive roll leveling" width="365">

**Camera stabilization** switches smoothing, roll leveling and pitch correction on or off together while retaining your tuning. Preset import also retains the values of a disabled camera. The stabilization core and the Natural, Stream Comfort and Strong Leveling presets come from **Dan Suzuki's HorizonLock VR / SteadyXR**. [Upstream project](https://github.com/DSuzuki35/horizonlock-vr) · [Feature-by-feature comparison](horizon-lock.md)

## Choose how much the game renders

![Explicit full render area option and output controls](images/screenshots/render-area.png)

*0.5.0 demo settings. This option replaces the former global Driving/Streaming selector.*

With **Keep full render area for stream off**, rectangular crop adjusts the OpenXR projection and recommended render dimensions together. Render scale controls pixel density. A freeform contour can add a conservative exterior hidden-area mesh, with padding. The game must query and use the hidden-triangle visibility mask for that optimization to have an effect. A polygon is not a guarantee of GPU savings. The runtime must permit mutable FOV for rectangular crop.

With **Stream → Keep full render area for stream on**, the native layer preserves the full source and can expand its horizontal projection. The saved custom contour is inactive. Rectangular crop becomes output framing; it does not reduce headset rendering in this mode. Both eyes currently receive the requested expansion. This reserves source content for smoothing and horizon stabilization. Larger render dimensions are recommendations; the game may ignore them.

The global Driving/Streaming selector was removed in 0.5.0. Existing profiles migrate to the matching explicit option; the saved contour is retained. Disabling the Clean Output module makes the effective option off without erasing its preference.

Render/contour changes are snapshots at game session startup: restart the game to apply them. From **0.6.0**, stabilization reserve and horizontal/vertical framing preview **while you move the controls** in an open Clean Output window launched by the app. **Save profile** makes them permanent; discarding edits or switching profiles restores saved framing. Other output settings still require closing and reopening Clean Output. No live geometry resizing occurs underneath the game.

**Stream stabilization:** **Adaptive roll leveling** uses the Horizon lock strength to reduce small tilts while retaining deliberate lean. **Pitch stabilization** reduces small up/down movements around the initial view. Press **R** with the output window focused to reset the horizon/pitch reference. **Mirrored borders** includes a maximum extension, softness and stretch. Stabilization and border effects run in the existing output pass; softness adds samples only in reflected areas.

**steadyXR presets:** use **Stream → Import steadyXR preset** to read a saved `.ini` file into a new, unsaved profile with full render area enabled for the selected game. The summary lists imported settings and any clamping, selected-eye conversion or unsupported fields. Monitor bindings, source paths, shortcuts and other steadyXR modules are not applied. Review the summary, choose your output display and save.

**Disable crop**, available in both **View & crop** and **LMU rendering**, bypasses rectangular crop without clearing its saved values. Save and restart the sim to apply it; turning it off restores the saved crop behavior. Stream offsets, expansion and stabilization remain configured.

For LMU quad views, the integrated adapter coordinates outer and focus projections. Keeping full render area preserves the provider's full outer view; extra stream FOV expansion is unsupported for this path. Clean Output combines the current frame's outer and high-resolution focus image before normal framing and stabilization, falling back to the current outer image if a usable focus image is unavailable. See the [LMU projection and processing contract](lmu-rendering.md#projection-and-processing).


## Two-PC capture-card output

1. Connect a gaming-PC GPU output to the capture card on the stream PC. Configure it as an **extended Windows display**.
2. In **Stream**, select that output display, set the internal canvas resolution/FPS, and enable **Start fullscreen**.
3. Save the profile, install the connection if needed, and start/restart the OpenXR game.
4. Select **Open clean output**. It opens a separate native window on the selected monitor, in borderless fullscreen. The settings app stays separate from the captured image.
5. **F11** toggles fullscreen. **Esc** returns to windowed mode; a second Esc closes the window. If the monitor is disconnected, choose a connected output before launching.
6. In **Spatial overlays**, use **Show in stream** per enabled source. Selected overlays retain their VR placement in the streamed camera view. Sources are VR-only by default. The additional stream texture ring runs only while Clean Output is active.

Set the Windows display mode to the capture card's supported resolution and refresh rate; selecting a canvas/FPS in OpenGrid XR does not reconfigure Windows or the capture card. Game audio is configured separately. On one PC, OBS can capture the Clean Output window. A direct OpenGrid XR OBS source plugin is not included.

**Symmetric extra area** links both expansion sliders. Turning it on sets both to the larger existing side; turning it off keeps the values for independent editing. The choice is stored in each profile.

**Stabilization reserve** crops the game image to leave room for stabilization: more reserve means more zoom. **13% on each side** keeps the central **74%**, approximately **1.35× zoom** compared with zero reserve. Expand the individual controls to fine-tune left, right, top and bottom. With different margins, the global value shows their average; moving it sets every margin to the new value. Changes preview live; save to keep them.

The output uses D3D11 shared textures, so its graphics adapter must be compatible with the game's. Supported stereo D3D11 projection capture is the initial path. Unsupported capture formats/view modes should remain pass-through for the game and must not be represented as working capture. No automatic game launch or runtime switch is performed.

## Inactive output

In **Stream → Frame & output**, choose what the output window shows when OpenXR frames have stopped for three seconds. **Show waiting screen**, **Make window transparent**, **Close output window** and **Clone a monitor** are available. Closing ends the output process. Use **Open output** to start it again. Saved choices apply when opening the window.

**Only mirror OpenXR while in the car** adds a separate choice for menus while the game continues publishing frames. OpenGrid checks iRacing's player `IsOnTrack` flag or LMU's player telemetry and realtime scoring state. Unknown or stale driving data uses the outside-car choice. A one-second grace period allows telemetry to start before the close action takes effect.

While outside the car, the game layer reports XR activity without copying eye textures for Clean Output. When driving resumes, the output process requests eye textures again.

**Monitor to clone** selects the primary or another connected display. Place the output window completely outside the selected monitor to avoid capturing itself. Cloning uses Windows Desktop Duplication on the source adapter with no Windows Graphics Capture recording border. OpenGrid releases the capture session, device and frame buffers as soon as output switches to OpenXR, transparency or the waiting screen.
