# Spatial overlays

Save up to 15 independently configured overlays per game profile. A source is an HTTP(S) webpage, an explicitly selected application window, a connected monitor, or the local [OpenGrid menu](quick-menu.md) (one per profile). Dashboard URLs on localhost are supported. Remote pages run in sandboxed renderers without Node integration or access to OpenGrid's controls. Downloads, popups, audio capture and unrequested permissions are blocked.


*0.5.0 demo profile; capture is inactive and no personal desktop is shown.*

## Add a source

1. Enable Spatial overlays, add a source and choose Web URL, Window, Monitor or OpenGrid menu.
2. Enter the URL or explicitly select the window/monitor; menu needs no URL or capture permission.
3. Enable the source, set placement and save. Restart the sim if enabling the engine for the first time.

For the local headset controls and default visibility/select keys, see [Menu setup](quick-menu.md).

For a stereo steering wheel with a fitted live dashboard and SimHub LEDs, choose **3D wheel**. [Steering wheel setup and model import](steering-wheels.md)

### Cockpit occlusion for web dashboards

**We recommend leaving cockpit occlusion off.** It can significantly increase CPU frametime in some sims, including Le Mans Ultimate. To check the cost on your setup, keep the same dashboard visible, record the same cockpit scene with occlusion off and on, and compare CPU frametime. For the second recording, confirm the source panel shows **Cockpit occlusion active**. [Benchmark guide](benchmarks.md)

Select **Web URL**, enable **Cockpit occlusion** and save the profile. This setting starts off for each dashboard. Place the dashboard behind a foreground object, such as the steering wheel. On a supported D3D11 session, closer surfaces cover the dashboard wherever the sim supplies usable depth for both eyes. Each masked dashboard uses one additional compositor layer. If depth is missing or there is no spare layer for masking, the headset shows the ordinary dashboard in both eyes. Check the source panel to see which mode is active.

To see the same effect in **Clean Output**, enable **Show in stream** for the dashboard. The output uses depth from the selected eye and captured game frame. A glove or wheel closer than the dashboard hides the covered pixels. If the sim supplies no usable depth for that frame, the dashboard appears normally.

For the first LMU test, use **D3D11 stereo** on the supported game build, enter the cockpit and position the dashboard just behind the wheel. The native path reads per-eye scene depth and supports MSAA. Turn the wheel and check each glove in each eye. Quad Views and MVR currently show the ordinary dashboard.

iRacing uses the depth supplied with a native D3D11 stereo OpenXR projection. The first version supports single-sample submitted depth. Generated frames, graphics bridges and sessions without a valid stereo depth pair show the ordinary dashboard.

## Monitor capture and desktop interaction

Choose **Monitor**, **Refresh monitors**, then **Use this monitor** and save the profile. Monitor permission lasts for the current OpenGrid run. A saved display reference alone never starts capture. Removing a monitor or changing its geometry/DPI revokes that selection; explicitly choose the current monitor again. Capture includes the ordinary Windows cursor. Turning the overlay off, setting opacity to zero, disabling spatial overlays or ending the XR session stops capture and withdraws its desktop input target.

The source summary below the picker shows the chosen monitor and physical resolution, application-window title, or dashboard title and URL. It stays visible when the picker clears or the desktop is hidden. **Showing in VR** means the native layer has actually consumed a rendered frame; a selection alone is labeled **Selected · not showing in VR**. Restarting OpenGrid requires selecting monitor/window capture again; a saved label never grants capture permission.

Selecting a monitor sets its source resolution to the monitor's aspect ratio within the 1920 × 1080 capture limits. Desktop video fills the entire overlay with the uncropped monitor. If you choose custom dimensions with a different aspect ratio, the image stretches to fit, so every visible position still maps to the corresponding monitor position without actionable letterbox bars. Application-window capture keeps its existing contain behavior.

**Desktop visibility key** accepts F1–F12 with optional Ctrl, Alt and Shift. Save it to activate the global shortcut. It toggles the saved monitor overlay without restarting the runtime; shortcut conflicts appear beside the source. Use the controller binding beside the monitor's visibility switch to learn a wheel, joystick or buttonbox button. Each time a local desktop is shown, its anchor aligns to the current head position and yaw; set X/Y and rotations to zero for a centered panel. Shortcuts and controller actions pause while profile edits are unsaved or the editor is busy.

**Physical mouse** is the default pointing mode. Use the ordinary PC mouse; OpenGrid does not move or click it. **Head pointing** explicitly hands cursor movement to the headset ray where it intersects the displayed overlay. Pointer coordinates use the selected monitor's physical Windows rectangle, including negative positions and mixed DPI. Desktop input requires a visible, valid, fresh native target. Movement of the physical mouse pauses head input; use **Resume pointing**, or its controller binding, to explicitly return control.

In head mode, **Left click** and **Right click** can be assigned wheel, joystick or buttonbox inputs. These are momentary actions and do not change saved profile fields. Hidden, disconnected, stale or unavailable targets cannot click. The mode and input status are shown with the source. Desktop interaction is head-only or physical-mouse input; no eye cursor or wink selection is available. Legacy eye-input profiles migrate to head pointing. Game foveated rendering keeps its separate tracking path.

### Hold precision

Choose **Head pointing**, set **Precision zoom** between 100% and 300% (175% default), and assign a **Precision hold key** or learn a controller input beside **Hold precision**. Keyboard holds use F1–F12 with optional Ctrl/Alt/Shift; choose a different chord from the desktop visibility key. Save the profile before using it.

Hold that input while pointing at the desktop. Only the desktop panel enlarges, anchored around the current pointer target, so small buttons require less precise head movement. Use your normal left/right click binding while holding precision. Release to restore the normal panel. If both keyboard and joystick hold it, release both. The game camera and its FOV are unchanged; Clean Output uses the same enlarged overlay geometry when sharing is enabled.

Hidden/disconnected desktops, focus loss, physical-mouse takeover and source changes cancel precision. A held joystick press requires fresh hardware state reports; missing reports expire the hold within 300 ms. An old held press cannot rearm after cancellation until released and pressed again. Keyboard sampling and controller forwarding run outside the VR render thread. Use a play-space anchor for head pointing; a head-locked panel follows the head ray instead of letting it aim across the desktop.

## Placement

Head-following overlays use the headset view space. Local overlays stay at a chosen pose in the OpenXR local space and can be arranged around the seated driver. Position uses meters, with negative Z in front of the viewer; yaw, pitch and roll use degrees. The physical height follows the source aspect ratio. This is an overlay anchor, not a tracked attachment to a moving game's cockpit mesh.

### Cockpit dashboards on a motion rig

If a play-space dashboard slides against the cockpit as the platform moves under braking, check the OpenXR API layer order. OpenXR Motion Compensation (OXRMC) corrects the game's eye poses and the play-space quads it receives. If OpenGrid adds its dashboard after that correction, the cockpit follows the rig while the dashboard stays fixed in the room. [OXRMC frame submission](https://github.com/BuzzteeBear/OpenXR-MotionCompensation/blob/v0.3.10.0-beta/XR_APILAYER_NOVENDOR_motion_compensation/layer.cpp#L1526-L1647)

1. Close the sim. In SteadyXR, open **Capture → Layer order…** and select the **64-bit** view. Select the system-wide **OpenGrid XR** implicit layer, use **Move up** until it sits immediately above **OpenXR Motion Compensation**, then choose **Apply changes**. OpenXR API Layers GUI also provides a **Move Up** control. Keep standalone Quad Views Foveated below OXRMC. [OXRMC compatibility notes](https://github.com/BuzzteeBear/OpenXR-MotionCompensation/blob/v0.3.10.0-beta/docs/README.md#conflict-with-other-openxr-api-layers) · [OpenXR API Layers GUI](https://github.com/fredemmott/OpenXR-API-Layers-GUI)
2. Restart the sim. In the latest OpenGrid enhancements log, check that the line **OpenXR API layers below OpenGrid XR** includes `XR_APILAYER_NOVENDOR_motion_compensation`. Activate and calibrate motion compensation, then recenter the play-space dashboard from the driving position.
3. Align one dashboard edge with a fixed cockpit landmark. Check that alignment during braking, then lean sideways with the platform stationary to check normal head parallax. A head-locked overlay follows your head during this check. If braking still moves the cockpit against the dashboard, repeat the test with **Head Physics** at zero in LMU. This isolates the position and rotation that LMU adds to the driver camera.

Drag an overlay in the **Placement map**. **Top · X/Z** adjusts left/right and distance forward. **Front · X/Y** adjusts left/right and height. The third axis stays unchanged, and numeric controls follow the map. Escape cancels the current drag. The map shows positions relative to each overlay’s anchor. Save the profile to apply edits.

### Place a web dashboard

With the dashboard visible in VR, look where you want it, choose **Place in front of me** and save the profile. For hands-on-wheel placement, assign a controller button beside that control. Press once to grab the dashboard at its current position. Move or turn your head and press again to fix it. You can then move your head freely and grab again for another adjustment. Bind **Reset position** to a key or wheel button to bring that dashboard back in front of you at its current size.

For rough placement, drag a slider. For an exact adjustment, type into the value field using a decimal point or comma, then press **Enter** or **Tab**. **Escape** cancels typing. The **+** and **−** buttons adjust position, width and pivot offsets by 1 mm, rotation and steering tilt by 0.1°, and opacity by 0.1 percentage points.

While aiming in VR or with the dashboard focused on the desktop placement map, use the following keys. Each press moves or resizes the dashboard by 25 mm, or rotates it by 1°. Hold **Shift** for 1 mm or 0.1° steps.

| Keys | Adjustment |
| --- | --- |
| Left / Right | Move left / right |
| Up / Down | Move up / down |
| Ctrl + Up / Down | Move farther / nearer |
| Alt + Up / Down | Increase / decrease dashboard width |
| Ctrl + Left / Right | Decrease / increase yaw |
| Alt + Left / Right | Decrease / increase roll |
| Ctrl + Alt + Up / Down | Increase / decrease pitch |

To customize one car in the desktop editor, enable **Remember placement for this car** before adjusting the position and saving. OpenGrid identifies the player’s car through the Telemetry module. When that car returns, the dashboard restores its own saved position, rotation, width and steering settings. An unknown car or one without saved settings uses the dashboard’s default placement. Switching off **Remember placement for this car** and saving removes that car’s override.

For a wheel-mounted display, center the steering wheel, position the dashboard, then enable **Follow steering wheel**. Offset the pivot from the dashboard center to the wheel hub. Positive X moves the pivot right, positive Y moves it up, and positive Z moves it toward the driver. Adjust **Steering axis tilt** to match the shaft. Turning the wheel rotates the dashboard around that pivot. Save the profile when it lines up.

Under **Recenter overlays**, assign a keyboard shortcut, a wheel or joystick button, or both. Save the profile, face forward in the cockpit and press the binding. All play-space overlays align with your current headset position and heading while retaining their offsets. Head-locked overlays continue to follow your head.

To center both the game and the overlays with one press, assign the same input to the sim’s own VR recenter action. OpenGrid listens for the keyboard shortcut while the sim has focus and leaves the key available to the game. You can also recenter from the desktop button.

## Choose what appears in the stream

Each enabled non-menu overlay has a separate **Show in stream** switch, off by default. Enable it only for sources intended for viewers. The stream uses the overlay's VR position, size, orientation and opacity. It projects that geometry through the selected captured eye and the same crop/stabilization camera as the game image, before display letterboxing. There are no separate 2D placement controls for the stream.

An independent keyed GPU texture ring carries selected overlays into Clean Output after the stabilized game image. It is allocated and updated only while both the source is selected and a live Clean Output consumer exists. Selecting a static dashboard can reuse its latest retained GPU image without forcing a webpage reload. Turning the switch off, clearing a source or losing its producer withdraws its cached stream image. List order controls stacking. Runtime VR layer limits do not by themselves prevent an explicitly selected source from appearing in the 2D stream.

## Resource controls

- Sources are created only for saved, enabled overlays while a fresh OpenXR session has the overlay engine loaded. No background service starts with Windows.
- Web/window/monitor default source size is 1024 × 576 at 10 Hz. The local menu defaults to 1024 × 768 and is capped at 15 Hz. Maximum size is 1920 × 1080 at 30 Hz. The default combined budget is 60 million source pixels/second.
- The combined budget reduces effective frame rates. If it cannot fund even 1 Hz for every source, later sources pause; ordering provides priority. It bounds requested frame transport, not arbitrary JavaScript/WebGL work inside a webpage.
- Shared GPU textures avoid a full-frame CPU bitmap round trip. At most one source frame per overlay is awaiting acknowledgment. Busy sources drop frames; the VR render path retains the last complete image rather than waiting for a webpage.
- Unchanged pages produce no new texture frames. Disabled sources release browser/capture resources. Multiple active browser pages still consume memory and can run expensive scripts, so use compact dashboards and low update rates where possible.
- The OpenXR runtime's layer limit is respected after game and performance-panel layers. Fifteen saved overlays do not guarantee fifteen simultaneous layers on every runtime. Layer-limit and source errors are shown explicitly.
- Browser capture and the game must use compatible GPUs. Cross-adapter import failures stop the affected source rather than switching to expensive CPU transport. On hybrid-GPU laptops, configure the application and sim to use the same GPU in Windows graphics settings.

Window IDs are only trusted after a selection in the current application session. Reselect after restarting OpenGrid or reopening a captured window. Minimized/protected windows and GPU/runtime combinations can restrict capture. Overlays remain separate from the captured game image; only sources explicitly selected with **Show in stream** are composited into Clean Output.

## Verification limits

The source sandbox and real Electron shared-GPU handles have been exercised on the development machine. End-to-end animated webpage and window-capture transfer has passed through the native host into both D3D11 and D3D12 synthetic runtime images with GPU pixel readback. Closing the captured window reports capture termination. These tests do not establish live-game, capture-card or Pimax/RTX 5090 performance. Chromium's documented shared-texture API does not expose a producer fence for BGRA/RGBA on Windows; source lifetime is retained through our completed GPU-copy acknowledgment. Treat this path as experimental until tested with the target driver/runtime combination.
