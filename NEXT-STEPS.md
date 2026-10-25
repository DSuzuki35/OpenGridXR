# Continue on the game PC

Handoff: 2026-10-25. Repository: https://github.com/DSuzuki35/OpenGridXR

Current development build: **0.2.1**. Local transfer bundle: `release/0.2.1/OpenGridXR-0.2.1-windows-x64.zip`. Source builds and CI packages use the same version. Validation: 64 JavaScript tests, 19 native suites, packaged desktop/save-race/controller UI tests, and GPU transport checks passed. Physical target hardware remains untested.

Target rig: RTX 5090, Pimax OpenXR, Dream Air and Dream Air SE. SteamVR with Sboys3 is a second supported integration target, not a required runtime switch.

## Get started

1. Clone this repository on the game PC. Keep the complete packaged application directory together when transferring a build. The executable alone is insufficient.
2. For a source build, install Node.js 24+, CMake and Visual Studio 2022 Desktop C++/Windows SDK. Run `npm ci`, `powershell -ExecutionPolicy Bypass -File scripts/build-native.ps1`, then `npm run package`.
3. Start with neutral settings and both frame generation and spatial overlays disabled. Close both sims and Clean Output before installing/updating the OpenGrid layer from Setup.
4. Start iRacing through OpenXR and verify the actual live session status. Repeat separately with LMU. Registration alone does not prove the layer loaded.
5. Signing is intentionally external. No actual sim, headset or live driver configuration was modified during development tests.

## First hardware checks

- Confirm baseline frame times with OpenGrid disabled, installed with neutral settings, then one feature at a time. Measure GPU time, dropped frames and VRAM, not just FPS.
- Validate crop and supersampling in both eyes. Restart the game for geometry changes. Check actual allocated resolution and hidden-area mask support.
- Test stabilization and expanded source area in Clean Output, first windowed, then fullscreen on the capture-card display. Check alignment, FOV, roll, clipping and 2-PC pacing.
- Start with one static URL overlay at 10 Hz. Add a window source, recenter, then test head/local anchors. Selected stream overlays must retain their VR position. Unselected sources must stay private.
- Repeat source enable/disable, source replacement, game restart, hidden/minimized window and capture-window closure. No stale textures should remain visible.
- Increase towards 15 sources only while measuring cost. Default aggregate source budget is 60 megapixels/second. Arbitrary web-page JavaScript can still consume CPU beyond its capture budget.
- Validate joystick/buttonbox learning, held buttons, reconnect, multiple identical devices and operation with the sim foreground. Start with a harmless overlay visibility binding. Dirty editor changes pause background actions.
- Check iRacing and LMU telemetry freshness and missing-field handling against the game. LMU's read-only double-copy is best effort; it is not a producer-coordinated atomic snapshot.
- Enable sharpening and then experimental frame generation separately. The shader estimator is the default. Test HUD visibility, frame pacing and image artifacts on the 5090 before recommending hardware optical flow.

## Next development work

1. **Runtime optics:** see `docs/runtime-optics.md`. Requested controls are Wide FOV, Eye Rotation/eye alignment, FOV Zoom and Flat FOV Zoom for both Pimax OpenXR and Sboys3/SteamVR. Verify the current Dream Air native API before exposing controls. Legacy `fov_level` is not evidence of Dream Air Wide FOV support. Do not add sliders that only save values without a working runtime connection.
2. Implement runtime-specific optics adapters with capability reporting, exact backups, narrow reviewed changes and stale-write rejection. Preserve separate Dream Air and Dream Air SE settings. Never overwrite optical calibration with generic guessed profiles.
3. Evaluate live toggle application. Some current settings are snapshots at XR session or Clean Output startup; button bindings do not remove those restart requirements. Frame generation and HUD already have separate native keyboard A/B shortcuts.
4. Create original helmet assets in Blender and build the actual headset helmet renderer. The existing helmet panel is a fit guide and preference editor.
5. Add a direct OBS source if useful, horizon-reset UI, and broader validated capture graphics/view modes. Current Clean Output capture is D3D11 stereo.

## Known test limits

Development GPU tests ran on Intel Iris Xe and an RTX 3070 Ti Laptop, not the target 5090/headsets. Main native and selected private engine suites passed before the input addition. One timing-sensitive HUD test required an isolated retry after a heavily loaded parallel run. A supplemental NVIDIA optical-flow quality comparison failed; its tolerance was not relaxed. A supplemental Intel D3D11/D3D12 bridge run hung and was stopped; NVIDIA bridge checks passed.

Electron shared GPU textures have passed actual transport/pixel tests, but producer-fence behavior and sustained real headset use still need validation. Performance with fifteen expensive web pages is not guaranteed by the slot count.

## Project decisions to preserve

- Completely free and open source, no account, expiry or paid features.
- iRacing and LMU first. Resource use takes priority over decorative background work.
- Stream overlays follow the same VR position; only visibility is independently selectable. No separate fixed 2D arrangement.
- Horizonlock belongs to Dan Suzuki and its relevant stabilization work may be reused.
- Original helmet assets only. Director/natural-headmotion features are out of scope.
- Use normal short hyphens in application text. Keep upstream license notices and source provenance.

Final build and verification details are recorded in `docs/progress.md`.
