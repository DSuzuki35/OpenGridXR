# Third-party notices

Starting with OpenGrid XR 0.3.12, original OpenGrid XR code and assets are offered under PolyForm Noncommercial 1.0.0 unless separate terms are stated. See [LICENSE](LICENSE). Original OpenGrid XR work is Copyright (c) 2026 Dan Suzuki and contributors. This source-available license restricts commercial use; it does not replace the licenses of dependencies or restrict recipients' rights in separately licensed source. Earlier releases retain their original license grants, including GPL-2.0.

Required Notice: Copyright (c) 2026 Dan Suzuki (https://github.com/DSuzuki35/OpenGridXR)

## Horizonlock VR / SteadyXR

The native stabilization math derives from [Dan Suzuki's Horizonlock VR / SteadyXR](https://github.com/DSuzuki35/horizonlock-vr), reference commit `0b2d88ae64d28907a72eb95b8920c7f90a7a42ae`. The imported `core.hpp` and `core.cpp` are in `native/third_party/horizonlock`. Dan Suzuki, as their copyright holder with authority to license them, additionally offers the copies included in OpenGrid XR 0.3.12 under PolyForm Noncommercial 1.0.0. The explicit additional grant and provenance are in [NOTICE-OpenGridXR.md](native/third_party/horizonlock/NOTICE-OpenGridXR.md). The original GPL-2.0 license text is retained beside those files, and prior GPL grants remain effective. No Director, gaze, natural-headmotion feature, timed licence or expiry mechanism is included in OpenGrid XR.

## GSI steering wheels and SimHub observer

The Hyper P1, Hyper SL, GXL V2, GT-MAX 32 and FPE2 models from oVRlay originate from the GSI configurator, credited to GSI and Neotix. The project owner authorized inclusion of these models. The OpenGrid code license does not grant independent rights to the models. Per-model credits, material sources, hashes and the font license accompany the assets. [Wheel provenance](assets/wheels) · [GSI configurator](https://custom.gomezsimindustries.com/)

OpenGrid includes oVRlay’s optional SimHub observer, using its existing plugin name and packet format. The observer was built against locally installed SimHub assemblies. [Observer source](integrations/simhub) · [Setup and build instructions](docs/simhub-wheel-lights.md)

## Sim-QVFR

Williem3’s Sim-QVFR inspired optional appearance controls and scene-reactive lighting, independently implemented with original artwork. Separately, the LMU multi-view renderer incorporates Sim-QVFR’s DXBC shader conversion code under PolyForm Noncommercial 1.0.0 and adapts its LMU focus-rendering work. [Imported converter and license](native/third_party/lmu_quadview/mod/vendor/sim_qvfr) · [Sim-QVFR LMU source](https://github.com/Williem3/Sim-QVFR/tree/c7846d385c9fc5b7802299fd29a9a4f48c0df6bd/src/games/lmu)

Required Notice: Copyright (c) 2026 Williem3 (https://github.com/Williem3/Sim-QVFR)

## OpenXR

Native OpenXR headers retain their original Khronos/Valve/LunarG notices and licence files under `native/third_party`. See the native notices for the exact version and chosen licence.

## Fonts and interface

Archivo, Inter and DM Mono fonts are distributed with their SIL Open Font License texts in `public/fonts` (and `licenses/fonts` in the packaged app). The interface uses Dan Suzuki's colour and typography direction. All racing/contour guide drawings in this app are original SVG code.

React and React DOM: MIT, Copyright Meta Platforms, Inc. and affiliates. Lucide: ISC, Copyright Lucide Contributors and Cole Bemis (Feather portions). Their bundled notices are retained by the frontend build where present. Complete licence texts are shipped under `licenses`.

Electron is MIT licensed and incorporates Chromium/Node dependencies. Its packaged LICENSE.electron.txt and LICENSES.chromium.html remain alongside the executable. Electron Builder, Vite and other development tooling are development dependencies; their licences remain in their packages.

## VectorXR

VectorXR is developed by DienerTech LLC, Copyright (c) 2026 DienerTech LLC, and licensed under MPL-2.0. OpenGrid XR bundles an adapted native engine from commit `87e575fb21cde8ad9ef87763c67471b7d0b7ee0a` under `native/third_party/vectorxr`. The matching source, modifications, MPL licence and provenance are included in the OpenGrid XR source distribution. The desktop app and upstream branding are not bundled. The engine uses private OpenGrid paths and loader identity; existing VectorXR installations are not modified. Original project: https://github.com/DienerTech/vectorxr .

**Source for binary recipients.** Published OpenGrid releases provide a source archive alongside the installer. Select the release matching the installed version and download its **Source code (zip)**. The archive’s `native/third_party/vectorxr` directory contains the adapted MPL-covered source, its license and build wrapper. [OpenGrid XR releases](https://github.com/DSuzuki35/OpenGridXR/releases) · [Corresponding source](native/third_party/vectorxr) · [MPL-2.0 license](native/third_party/vectorxr/LICENSE) · [Modifications](native/third_party/vectorxr/NOTICE-OpenGridXR.md) · [Native build instructions](native/README.md)

MPL-covered source files, including our modifications, remain available under MPL-2.0, and their notices are retained. MPL-2.0 section 3.3 permits a larger work under different terms while preserving the MPL requirements for covered files. OpenGrid XR's PolyForm terms do not restrict recipients' rights in that MPL-covered source. The licence grants permission to adapt and redistribute subject to its conditions, including source availability and preservation of notices; it does not grant trademark rights or imply endorsement by DienerTech LLC. See the pinned upstream licence at https://github.com/DienerTech/vectorxr/blob/87e575fb21cde8ad9ef87763c67471b7d0b7ee0a/LICENSE and the full MPL-2.0 terms at https://www.mozilla.org/en-US/MPL/2.0/ .

## Sharpening

LumaSharpen is based on CeeJay.dk's original MIT-licensed algorithm (also used by Sboys3). AMD FidelityFX CAS is MIT-licensed GPUOpen source. Their source/provenance and required notices are under `native/third_party/lumasharpen` and `native/third_party/cas`. No Sboys3 headset-driver binary, lens calibration asset, or shader-hook infrastructure is included.

Vulkan/OpenXR headers and other native dependency notices remain in the corresponding third-party directories. The targeted OpenGrid headset pipeline is D3D11; shipped source for other backends is not a claim of tested support.

## Telemetry

The iRacing SDK's BSD notice and TinyPedal's MIT notice are retained under `native/telemetry/third_party` and shipped in `licenses/telemetry`. The independent readers use the published telemetry ABI and documented mappings. Restricted game SDK headers are not redistributed. See `native/telemetry/README.md` and its third-party provenance for exact upstream revisions, field mappings and validation limits.

Spatial URL/window capture, GPU texture transport, overlay placement and the game-control helper are original OpenGrid XR implementations. Electron/Chromium provides the browser and window-capture platform; the desktop framework's notices apply as above.
## Application updater

The desktop app includes [electron-updater](https://github.com/electron-userland/electron-builder), version 6.8.11, under the MIT license. Its runtime dependencies and their license files are distributed inside the application package. Updates are obtained from the OpenGridXR GitHub Releases feed only.

## LMU quad rendering and stereo VRS

OpenGrid XR 1.0.0 incorporates the native renderer from Dan Suzuki’s LMU Quad View Mod with provider and performance contributions from Markus Stahl. Its `VERSION` file reads `1.1.0` at untagged commit `4a618648d90040e8e7e454d5dbac4e7613e7778f`. The original LMU code uses PolyForm Noncommercial 1.0.0. The integration includes the native renderer, its Sim-QVFR conversion dependency and associated notices. OpenGrid provides its own desktop interface. [Pinned source and integration changes](native/third_party/lmu_quadview/OPENGRID-PROVENANCE.md) · [LMU license](native/third_party/lmu_quadview/LICENSE.md) · [Upstream third-party notices](native/third_party/lmu_quadview/THIRD_PARTY_NOTICES.md)

MinHook 1.3.4 retains its BSD 2-Clause and HDE notices, the LMU OpenXR headers retain Apache-2.0, and the NVAPI interface headers retain their MIT notices. OpenGrid’s installer collects the license texts under `licenses/native`. NVIDIA supplies the driver implementation. [MinHook and HDE](native/third_party/lmu_quadview/mod/vendor/minhook-1.3.4/LICENSE.txt) · [NVAPI license](native/third_party/lmu_quadview/mod/vendor/nvapi/LICENSE-MIT.txt)

## Quad-Views-Foveated

OpenGrid bundles a Windows x64 build of **Quad-Views-Foveated 1.1.4** by Matthieu Bucchianeri, based on commit `79855a001302472a0f6c0703567dfe8ebfa6f988`. Copyright (c) 2022–2023 Matthieu Bucchianeri. The provider is MIT licensed. Local changes build it for OpenGrid’s private provider directory and use wide Windows paths for its configuration and logs. [Provider source](native/third_party/quad-views-foveated) · [MIT license](native/third_party/quad-views-foveated/LICENSE) · [Provenance and dependency revisions](native/third_party/quad-views-foveated/NOTICE-LMU.md)

The provider includes fmt, OpenXR-MixedReality utilities, FidelityFX CAS and WIL under MIT, OpenXR SDK headers under Apache-2.0 / MIT, and OpenXR SDK Source registry and generator files under Apache-2.0. Each subset retains its license and attribution. [Dependency sources and licenses](native/third_party/quad-views-foveated/external) · [Provider third-party notices](native/third_party/quad-views-foveated/THIRD_PARTY)
