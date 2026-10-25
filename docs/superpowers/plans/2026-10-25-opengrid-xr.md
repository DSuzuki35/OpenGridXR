# OpenGrid XR Implementation Plan

> For agentic workers: implement scoped tasks with tests and an independent final review. Execution is authorized by the user's "leg los".

**Goal:** Deliver a runnable Windows desktop toolkit and own native crop/stream components using the agreed UI and first-release scope.

**Architecture:** React/Electron settings app with validated profile files and a narrow IPC bridge. Independent C++ OpenXR layer and clean-output executable, with borrowed stabilization code attributed to Horizonlock.

**Tech Stack:** React, Vite, Electron, Node test runner, C++/CMake, D3D11/OpenXR.

**Spec:** ../specs/2026-10-25-opengrid-xr-design.md

## Global constraints
Fully free and open source; iRacing and LMU; own helmet art; source projects read-only; no fake runtime effects; no expiry; no live registry writes during tests; external signing by user.

## Review focus
Malformed imported profiles and self-intersecting contours; stale writes/data recovery; runtime does not support mutable FOV or ignores mask; streaming and driving mask interaction; packaged paths and untrusted renderer IPC.

### Task 1 — Profile core and desktop bridge (root)
Files: src/model.mjs, desktop/*.cjs, tests/*.test.mjs, package.json.
Interfaces: shared document in spec; model exports createProfile(game,name?),createDocument(),validateDocument(doc),validateProfile(profile),polygonArea(points),isSimplePolygon(points),pixelBudget(profile),contourPreset(style),effectiveMask(profile).
- [x] Write and run failing geometry/import/persistence tests.
- [x] Implement validation, safe storage, backups, runtime INI mapping and Electron IPC.
- [x] Run node --test tests/*.test.mjs and production frontend build.

### Task 2 — Desktop UI (parallel, separate files)
Files: src/App.jsx, src/main.jsx, src/styles.css, src/components/*, public assets, index.html.
Consumes model and window.openGrid API specified in design.
- [x] Build polished session/view/helmet/stream/profile UI using reference style.
- [x] Implement interactive per-eye contour editor, live geometric previews, save/import/export and profile selection.
- [x] Verify actual browser rendering and workflow in integration stage.

### Task 3 — Native layer and stream output (parallel, separate files)
Files: native/**, scripts/build-native.ps1, native tests and native README.
- [x] Define config contract with root; write math/layer tests before implementation.
- [x] Implement own layer crop/mask and portable game profile selection; adapt useful Horizonlock streaming source without excluded features/expiry.
- [x] Build DLL/output EXE and run native tests without changing global layer registrations.

### Task 4 — Integration, delivery, independent review (root)
- [x] Package executable with native files/fonts/licenses and start launcher.
- [x] Run isolated desktop workflow tests and inspect screenshots.
- [x] Independent reviewer inspects runtime and data-boundary failures, fix important findings and rerun tests.
- [x] Document exact working scope and headset-only verification limits; show app to user.
