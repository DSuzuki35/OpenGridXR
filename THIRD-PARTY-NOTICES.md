# Third-party notices

OpenGrid XR is free software under GPL-2.0. See LICENSE. Original OpenGrid XR work is Copyright (c) 2026 Dan Suzuki and contributors.

## Horizonlock VR / SteadyXR

The native stabilization math derives from the GPL-2.0 source in the user's horizonlock-vr repository. Reference commit: `0b2d88ae64d28907a72eb95b8920c7f90a7a42ae`. Adapted source is included in this repository under `native/third_party` with provenance. Original project: https://github.com/DSuzuki35/horizonlock-vr . No Director, gaze, natural-headmotion feature, timed licence or expiry mechanism is included in OpenGrid XR.

## OpenXR

Native OpenXR headers retain their original Khronos/Valve/LunarG notices and licence files under `native/third_party`. See the native notices for the exact version and chosen licence.

## Fonts and interface

Archivo, Inter and DM Mono fonts are distributed with their SIL Open Font License texts in `public/fonts` (and `licenses/fonts` in the packaged app). The interface uses Dan Suzuki's colour and typography direction. All racing/contour guide drawings in this app are original SVG code.

React and React DOM: MIT, Copyright Meta Platforms, Inc. and affiliates. Lucide: ISC, Copyright Lucide Contributors and Cole Bemis (Feather portions). Their bundled notices are retained by the frontend build where present. Complete licence texts are shipped under `licenses`.

Electron is MIT licensed and incorporates Chromium/Node dependencies. Its packaged LICENSE.electron.txt and LICENSES.chromium.html remain alongside the executable. Electron Builder, Vite and other development tooling are development dependencies; their licences remain in their packages.

## VectorXR

VectorXR is developed by DienerTech LLC, Copyright (c) 2026 DienerTech LLC, and licensed under MPL-2.0. OpenGrid XR bundles an adapted native engine from commit `9e8b49b1721eb30ce27d7ba72f1488050160a29d` under `native/third_party/vectorxr`. The matching source, modifications, MPL licence and provenance are included in the OpenGrid XR source distribution. The desktop app and upstream branding are not bundled. The engine uses private OpenGrid paths and loader identity; existing VectorXR installations are not modified. Original project: https://github.com/DienerTech/vectorxr .

**Source for binary recipients:** download the source for the tag matching your installed OpenGrid XR version from https://github.com/DSuzuki35/OpenGridXR/releases . For OpenGrid XR 0.3.1, the matching source is https://github.com/DSuzuki35/OpenGridXR/archive/refs/tags/v0.3.1.zip . The adapted engine is in `native/third_party/vectorxr`, with its MPL licence in `LICENSE` and modification provenance in `NOTICE-OpenGridXR.md`; the build wrapper is `native/third_party/vectorxr/CMakeLists.txt`, with build instructions in `native/README.md` and `scripts/build-enhancements.ps1`. The original upstream source alone does not include the OpenGrid XR modifications.

MPL-covered source files, including our modifications, remain available under MPL-2.0, and their notices are retained. MPL-2.0 section 3.3 also permits distribution of covered code under GPL-2.0 as part of this GPL-2.0 larger work when it is not marked incompatible with secondary licences. No such additional incompatibility notice was found in the vendored VectorXR source; the standard Exhibit B text within the complete MPL licence is not itself an attached incompatibility notice. Recipients retain their MPL source rights. The licence grants permission to adapt and redistribute subject to its conditions, including source availability and preservation of notices; it does not grant trademark rights or imply endorsement by DienerTech LLC. See the pinned upstream licence at https://github.com/DienerTech/vectorxr/blob/9e8b49b1721eb30ce27d7ba72f1488050160a29d/LICENSE and the full MPL-2.0 terms at https://www.mozilla.org/en-US/MPL/2.0/ .

## Sharpening

LumaSharpen is based on CeeJay.dk's original MIT-licensed algorithm (also used by Sboys3). AMD FidelityFX CAS is MIT-licensed GPUOpen source. Their source/provenance and required notices are under `native/third_party/lumasharpen` and `native/third_party/cas`. No Sboys3 headset-driver binary, lens calibration asset, or shader-hook infrastructure is included.

The native engine's NVIDIA Optical Flow interface headers are MIT-licensed; the optional `nvofapi64.dll` implementation is supplied by the user's graphics driver, not distributed by OpenGrid. Vulkan/OpenXR headers and other native dependency notices remain in the corresponding third-party directories. The targeted OpenGrid headset pipeline is D3D11; shipped source for other backends is not a claim of tested support.

## Telemetry

The iRacing SDK's BSD notice and TinyPedal's MIT notice are retained under `native/telemetry/third_party` and shipped in `licenses/telemetry`. The independent readers use the published telemetry ABI and documented mappings. Restricted game SDK headers are not redistributed. See `native/telemetry/README.md` and its third-party provenance for exact upstream revisions, field mappings and validation limits.

Spatial URL/window capture, GPU texture transport, overlay placement and the game-control helper are original OpenGrid XR implementations. Electron/Chromium provides the browser and window-capture platform; the desktop framework's notices apply as above.
## Application updater

The desktop app includes [electron-updater](https://github.com/electron-userland/electron-builder), version 6.8.11, under the MIT license. Its runtime dependencies and their license files are distributed inside the application package. Updates are obtained from the OpenGridXR GitHub Releases feed only.
