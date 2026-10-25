# OpenGrid XR implementation progress

Plan: docs/superpowers/plans/2026-10-25-opengrid-xr.md

- User authorized implementation after agreeing free/open-source, iRacing/LMU, driving and streaming, custom helm contour profiles and reference UI.
- Empty directory was not a Git repo. Initialized development branch locally; source reference directories remain read-only.
- Root profile model and storage implemented with tests first: corrupt/stale saves, exact backups and recovery, invalid/self-crossing contours, mode semantics, runtime serialization and status freshness.
- UI task owns src/App.jsx, styles, components, public fonts. Native task owns native/ and build script. Shared profile and INI contracts agreed.
- First implementation deliberately disables custom contour mask for streaming, preserves full source plus expansion, applies rectangular crop as stream framing. This preserves stabilization reserve. More selective union optimization is later.
- Helmet is an original editable fit guide only; production helmet assets and in-headset helmet rendering are later work, as the user specified original Blender assets must be made separately.
- Version 0.1 shipped without the optional enhancement engine; version 0.2 adds it privately without changing another application's configuration.
- Native runtime writes heartbeat files; desktop reports active only for fresh actual session with live PID.
- No test changes live OpenXR registration or runs a sim. Real headset performance/compatibility remains to verify.
- User explicitly added fullscreen preview on a capture-card output for a two-PC setup. Implemented physical display selection, borderless startup, F11/Esc, configurable offscreen output canvas and letterboxed presentation.
- Independent review identified typeless/sRGB capture, missing stream crop UV mapping, oversized saved documents, wrong saved active-profile output selection, and an editing-during-save race. Each received a reproducing regression and fix; final full-suite results are recorded below.
- Horizon lock defaults to 0 (explicit opt-in); native output maintains the initial upright Y hemisphere so an inverted submitted reference space does not flip the view.

## Version 0.1 verification — 2026-10-25

- `npm test`: 23 passed, 0 failed.
- `scripts/build-native.ps1`: x64 Release build succeeded; `ctest --test-dir native/build -C Release --output-on-failure`: all 5 suites passed, including real D3D11 GPU pixel readback and capture fixtures.
- `npm run package`: produced the unsigned Windows application with its own original icon, native DLL/output, fonts and licences.
- Both desktop workflows passed against the packaged executable with isolated data: contour editing, save/reload, actual import/export IPC, capture-card monitor/fullscreen persistence, compact layout, and delayed-save editing protection. Layer registration is blocked in these tests.
- SHA-256 comparisons confirmed that all three packaged native artifacts exactly match the tested native build.
- Inspected full-size UI screenshots; opened the packaged app for the user. No real OpenXR registration or sim configuration was changed.
- That release was the 0.1.0 development preview. Its follow-up engine integration is implemented in 0.2. Actual sim/headset and capture-card timing validation, original rendered helmets, and a direct OBS source remain separate work.

## Version 0.2 implementation

- Symmetric stream expansion, integrated private frame generation/performance HUD, native Luma/CAS sharpening and reviewed iRacing native-foveation settings are implemented.
- Read-only iRacing/LMU telemetry includes bounded shared-memory parsers, explicit stale/unsupported states, synthetic process fixtures and a demand-driven desktop dashboard.
- Spatial profiles support up to 15 independent URL/window sources, placement presets, head/local anchors, resource budgets and explicit overlay/game centering controls.
- GPU capture uses sandboxed Electron sources and a bounded native host; URL and window sources have passed actual shared-GPU transport and pixel-readback tests through D3D11 and D3D12 synthetic runtime images.
- Independent reviews found and fixed blocked URL shutdown, lost static repaint, stale source retention and missing packaged telemetry notices. Additional native lifecycle and full-package verification is recorded when completed.
- Public repository created at https://github.com/DSuzuki35/OpenGridXR. Initial publication uses a clean source history; local earlier development history is retained separately.

## Version 0.2.1 verification - 2026-10-25

- The user's final stream requirement is implemented: each selected source follows its real VR placement through the same captured camera, crop and stabilization. Independent fixed screen coordinates were removed.
- Independent review caught a ring-buffer identity mismatch that hid overlay geometry on ordinary dynamic source updates. Alternating-buffer, resize/recovery and source replacement regressions now pass.
- Final full native CTest run: 19/19 passed, including input self-tests, actual private engine integration, D3D11/D3D12 overlays, capture, image processing, stream composition and telemetry.
- JavaScript suite: 64/64 passed after the input addition. Input tests cover lazy helper ownership, background action gating, capture-dialog isolation, malformed messages, duplicate assignments, symmetry and synthetic process shutdown.
- Seven selected private engine tests passed earlier on the same engine DLL. One HUD timing check needed an isolated retry after a heavily loaded parallel run. Supplemental hardware optical-flow/bridge limitations remain recorded in NEXT-STEPS.md.
- Actual Electron URL/D3D11 and window/D3D12 sources passed GPU transport and private-versus-public stream pixel checks. Packaged 0.2.0 UI/save-race and GPU window-capture checks passed before the final buffer fix and input addition; the updated 0.2.1 package is checked separately below.
- Controller bindings use a background nonexclusive DirectInput helper, with optional per-profile bindings for every existing boolean control and per-source stream visibility. Native and desktop input code received independent review. Physical wheel/buttonbox behavior still requires the game PC.
- Application text and window titles use short ASCII hyphens. The 0.2.0 preview was opened for the user; 0.2.1 is packaged separately to preserve the running preview.
- Wide FOV and both optics zooms remain researched, not implemented. docs/runtime-optics.md records exact Sboys contracts and the unverified Pimax native API gap. NEXT-STEPS.md is the game-PC handoff.
- Final 0.2.1 packaged desktop smoke, delayed-save race and actual native-fixture controller UI tests passed. The final packaged renderer/model/controller files match their source; SHA-256 comparisons match all eight native artifacts. Window capture through the packaged helper and D3D12 VR/stream receivers passed; a later package rebuild changed only UI/model hardening and documentation.
- Public source was pushed successfully to `DSuzuki35/OpenGridXR` on `main`. The initial release commit is `473da47`; the earlier local `develop` history remains local. GitHub Actions is configured; local verification does not assert the remote workflow has completed.
