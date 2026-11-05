# Third-party notices

Starting with OpenGrid XR 0.3.12, original OpenGrid XR code and assets are offered under PolyForm Noncommercial 1.0.0 unless separate terms are stated. See [LICENSE](LICENSE). Original OpenGrid XR work is Copyright (c) 2026 Dan Suzuki and contributors. This source-available license restricts commercial use; it does not replace the licenses of dependencies or restrict recipients' rights in separately licensed source. Earlier releases retain their original license grants, including GPL-2.0.

Required Notice: Copyright (c) 2026 Dan Suzuki (https://github.com/DSuzuki35/OpenGridXR)

## Horizonlock VR / SteadyXR

The native stabilization math derives from [Dan Suzuki's Horizonlock VR / SteadyXR](https://github.com/DSuzuki35/horizonlock-vr), reference commit `0b2d88ae64d28907a72eb95b8920c7f90a7a42ae`. The imported `core.hpp` and `core.cpp` are in `native/third_party/horizonlock`. Dan Suzuki, as their copyright holder with authority to license them, additionally offers the copies included in OpenGrid XR 0.3.12 under PolyForm Noncommercial 1.0.0. The explicit additional grant and provenance are in [NOTICE-OpenGridXR.md](native/third_party/horizonlock/NOTICE-OpenGridXR.md). The original GPL-2.0 license text is retained beside those files, and prior GPL grants remain effective. No Director, gaze, natural-headmotion feature, timed licence or expiry mechanism is included in OpenGrid XR.

## Helmet visor inspiration

The adjustable visor feature was inspired by [Williem3's Sim-QVFR](https://github.com/Williem3/Sim-QVFR). OpenGrid XR independently implements its visor behavior and original helmet artwork; no Sim-QVFR code or assets are copied or distributed. Sim-QVFR is not a bundled dependency. This credit acknowledges the inspiration and does not grant rights to Sim-QVFR or apply OpenGrid XR's license to it.

## OpenXR

Native OpenXR headers retain their original Khronos/Valve/LunarG notices and licence files under `native/third_party`. See the native notices for the exact version and chosen licence.

## Fonts and interface

Archivo, Inter and DM Mono fonts are distributed with their SIL Open Font License texts in `public/fonts` (and `licenses/fonts` in the packaged app). The interface uses Dan Suzuki's colour and typography direction. All racing/contour guide drawings in this app are original SVG code.

React and React DOM: MIT, Copyright Meta Platforms, Inc. and affiliates. Lucide: ISC, Copyright Lucide Contributors and Cole Bemis (Feather portions). Their bundled notices are retained by the frontend build where present. Complete licence texts are shipped under `licenses`.

Electron is MIT licensed and incorporates Chromium/Node dependencies. Its packaged LICENSE.electron.txt and LICENSES.chromium.html remain alongside the executable. Electron Builder, Vite and other development tooling are development dependencies; their licences remain in their packages.

## VectorXR

VectorXR is developed by DienerTech LLC, Copyright (c) 2026 DienerTech LLC, and licensed under MPL-2.0. OpenGrid XR bundles an adapted native engine from commit `87e575fb21cde8ad9ef87763c67471b7d0b7ee0a` under `native/third_party/vectorxr`. The matching source, modifications, MPL licence and provenance are included in the OpenGrid XR source distribution. The desktop app and upstream branding are not bundled. The engine uses private OpenGrid paths and loader identity; existing VectorXR installations are not modified. Original project: https://github.com/DienerTech/vectorxr .

**Source for binary recipients:** download the source for the tag matching your installed OpenGrid XR version from https://github.com/DSuzuki35/OpenGridXR/releases . For OpenGrid XR 0.4.0, the matching source is https://github.com/DSuzuki35/OpenGridXR/archive/refs/tags/v0.4.0.zip . The adapted engine is in `native/third_party/vectorxr`, with its MPL licence in `LICENSE` and modification provenance in `NOTICE-OpenGridXR.md`; the build wrapper is `native/third_party/vectorxr/CMakeLists.txt`, with build instructions in `native/README.md` and `scripts/build-enhancements.ps1`. The original upstream source alone does not include the OpenGrid XR modifications.

MPL-covered source files, including our modifications, remain available under MPL-2.0, and their notices are retained. MPL-2.0 section 3.3 permits a larger work under different terms while preserving the MPL requirements for covered files. OpenGrid XR's PolyForm terms do not restrict recipients' rights in that MPL-covered source. The licence grants permission to adapt and redistribute subject to its conditions, including source availability and preservation of notices; it does not grant trademark rights or imply endorsement by DienerTech LLC. See the pinned upstream licence at https://github.com/DienerTech/vectorxr/blob/87e575fb21cde8ad9ef87763c67471b7d0b7ee0a/LICENSE and the full MPL-2.0 terms at https://www.mozilla.org/en-US/MPL/2.0/ .

## Sharpening

LumaSharpen is based on CeeJay.dk's original MIT-licensed algorithm (also used by Sboys3). AMD FidelityFX CAS is MIT-licensed GPUOpen source. Their source/provenance and required notices are under `native/third_party/lumasharpen` and `native/third_party/cas`. No Sboys3 headset-driver binary, lens calibration asset, or shader-hook infrastructure is included.

The native engine's NVIDIA Optical Flow interface headers are MIT-licensed; the optional `nvofapi64.dll` implementation is supplied by the user's graphics driver, not distributed by OpenGrid. Vulkan/OpenXR headers and other native dependency notices remain in the corresponding third-party directories. The targeted OpenGrid headset pipeline is D3D11; shipped source for other backends is not a claim of tested support.

## Telemetry

The iRacing SDK's BSD notice and TinyPedal's MIT notice are retained under `native/telemetry/third_party` and shipped in `licenses/telemetry`. The independent readers use the published telemetry ABI and documented mappings. Restricted game SDK headers are not redistributed. See `native/telemetry/README.md` and its third-party provenance for exact upstream revisions, field mappings and validation limits.

Spatial URL/window capture, GPU texture transport, overlay placement and the game-control helper are original OpenGrid XR implementations. Electron/Chromium provides the browser and window-capture platform; the desktop framework's notices apply as above.
## Application updater

The desktop app includes [electron-updater](https://github.com/electron-userland/electron-builder), version 6.8.11, under the MIT license. Its runtime dependencies and their license files are distributed inside the application package. Updates are obtained from the OpenGridXR GitHub Releases feed only.

## LMU quad rendering and stereo VRS

OpenGrid XR 0.4.0 incorporates the native LMU adapter from [DSuzuki35/LMU-Quad-View-Mod](https://github.com/DSuzuki35/LMU-Quad-View-Mod), tag v0.2.1, commit bf1189a6d841866b90eab2673b538eb06b471965. Original code remains PolyForm Noncommercial 1.0.0; its license, pinned source and modification provenance are under `native/third_party/lmu_quadview`. The original standalone launcher is not shipped as an executable. No game executable, game assets, captures or driver binaries are included.

MinHook 1.3.4 retains its BSD 2-Clause and HDE notices; the imported OpenXR headers retain Apache-2.0. NVAPI headers retain their MIT copyright and permission notices, collected in `mod/vendor/nvapi/LICENSE-MIT.txt` for binary distributions as well as preserved in source. The corresponding license files are included under `licenses/native/lmu_quadview` in the installer. Runtime providers installed separately retain their own terms and are not redistributed by selecting them in the launcher.
