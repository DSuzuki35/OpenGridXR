# Spatial overlays

Save up to 15 independently configured overlays per game profile. A source is an HTTP(S) webpage or an explicitly selected application window. Dashboard URLs on localhost are supported. Remote pages run in sandboxed renderers without Node integration or access to OpenGrid's controls. Downloads, popups, audio capture and unrequested permissions are blocked.

## Placement

Head-following overlays use the headset view space. Local overlays stay at a chosen pose in the OpenXR local space and can be arranged around the seated driver. Position uses meters, with negative Z in front of the viewer; yaw, pitch and roll use degrees. The physical height follows the source aspect ratio. This is an overlay anchor, not a tracked attachment to a moving game's cockpit mesh.

Recenter overlays aligns local anchors to the current head position and yaw. It does not recenter the game's camera. **Center VR in game** sends the explicitly saved keyboard shortcut after a short delay so the driver can focus the sim. Assign the same shortcut to the sim's VR recenter action first. No input is sent if the selected sim is not foreground or modifier keys are held. No universal runtime recenter API is assumed.

## Choose what appears in the stream

Each enabled overlay has a separate **Show in stream** switch, off by default. Enable it only for sources intended for viewers. The stream uses the overlay's VR position, size, orientation and opacity. It projects that geometry through the selected captured eye and the same crop/stabilization camera as the game image, before display letterboxing. There are no separate 2D placement controls for the stream.

An independent keyed GPU texture ring carries selected overlays into Clean Output after the stabilized game image. It is allocated and updated only while both the source is selected and a live Clean Output consumer exists. Selecting a static dashboard can reuse its latest retained GPU image without forcing a webpage reload. Turning the switch off, clearing a source or losing its producer withdraws its cached stream image. List order controls stacking. Runtime VR layer limits do not by themselves prevent an explicitly selected source from appearing in the 2D stream.

## Resource controls

- Sources are created only for saved, enabled overlays while a fresh OpenXR session has the overlay engine loaded. No background service starts with Windows.
- Default source size is 1024 × 576 at 10 Hz. Maximum size is 1920 × 1080 at 30 Hz. The default combined budget is 60 million source pixels/second.
- The combined budget reduces effective frame rates. If it cannot fund even 1 Hz for every source, later sources pause; ordering provides priority. It bounds requested frame transport, not arbitrary JavaScript/WebGL work inside a webpage.
- Shared GPU textures avoid a full-frame CPU bitmap round trip. At most one source frame per overlay is awaiting acknowledgment. Busy sources drop frames; the VR render path retains the last complete image rather than waiting for a webpage.
- Unchanged pages produce no new texture frames. Disabled sources release browser/capture resources. Multiple active browser pages still consume memory and can run expensive scripts, so use compact dashboards and low update rates where possible.
- The OpenXR runtime's layer limit is respected after game and performance-panel layers. Fifteen saved overlays do not guarantee fifteen simultaneous layers on every runtime. Layer-limit and source errors are shown explicitly.
- Browser capture and the game must use compatible GPUs. Cross-adapter import failures stop the affected source rather than switching to expensive CPU transport. On hybrid-GPU laptops, configure the application and sim to use the same GPU in Windows graphics settings.

Window IDs are only trusted after a selection in the current application session. Reselect after restarting OpenGrid or reopening a captured window. Minimized/protected windows and GPU/runtime combinations can restrict capture. Overlays remain separate from the captured game image; only sources explicitly selected with **Show in stream** are composited into Clean Output.

## Verification limits

The source sandbox and real Electron shared-GPU handles have been exercised on the development machine. End-to-end animated webpage and window-capture transfer has passed through the native host into both D3D11 and D3D12 synthetic runtime images with GPU pixel readback. Closing the captured window reports capture termination. These tests do not establish live-game, capture-card or Pimax/RTX 5090 performance. Chromium's documented shared-texture API does not expose a producer fence for BGRA/RGBA on Windows; source lifetime is retained through our completed GPU-copy acknowledgment. Treat this path as experimental until tested with the target driver/runtime combination.
