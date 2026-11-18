# LMU Quad View Mod integration

OpenGrid XR 1.0.0 imports the native renderer from Dan Suzuki’s LMU Quad View Mod at commit `4a618648d90040e8e7e454d5dbac4e7613e7778f`. The upstream `VERSION` file identifies **1.1.0**. This is an untagged source revision. [Upstream project](https://github.com/DSuzuki35/LMU-Quad-View-Mod) · [Imported version](VERSION) · [Upstream change history](CHANGELOG.md)

The import includes native renderer sources, tests, LMU layout definitions, selected architecture notes and original license notices. OpenGrid builds the renderer into its own Windows x64 D3D11 layer and supplies its own editor, launcher and installation flow. The OpenGrid CMake build replaces the standalone build scripts and bootstrap.

## OpenGrid adaptations

- `mod/xr_layer.cpp` and `mod/xr_layer.hpp` expose the private factory, consume OpenGrid’s four-edge crop and report engine startup status to OpenGrid. Image callbacks receive waited images before release. A separate submission callback receives the rendered view metadata after successful runtime submission. OpenGrid uses these boundaries for the mask, sharpening, grading and same-frame Clean Output.
- `mod/engine_bridge.cpp` checks LMU identity, executable resources, instruction anchors, x64 function boundaries and independently anchored data layouts before installing engine or early MVR hooks. `mod/tests/engine_hooks_test.cpp` exercises the integration’s refusal and teardown paths.
- `mod/focus_mvr.cpp` tracks D3D device and shader creation callbacks across session teardown. The adapter disables interception during teardown and retains the forwarding trampoline until its active calls finish. The bridge associates focus work and per-eye diagnostics with the originating session. Late callbacks from an earlier session cannot arm hooks or discard the new session’s focus image. The WARP pipeline fixture covers these transitions.
- Tracking queries can return fresh poses while another thread submits a frame. The renderer keeps its locked snapshot. Same-thread runtime reentry is counted and refused. Outer-image waits request at most 5 ms or a shorter positive predicted frame period. A timed-out image stays acquired for retry.
- Session creation releases early MVR device hooks when graphics binding, foveation, view sizing or downstream session creation fails. The runtime and submission fixtures cover refusal, retry and protection of an existing session. On retry, shaders that cannot be verified on the bound device keep their ordinary focus passes.
- Upstream 1.1.0 already incorporates earlier OpenGrid crop, ownership and data-layout work. The original adapter, engine and safety regression fixtures are retained under `mod/tests`. OpenGrid integration fixtures live under `native/tests` in the repository root.

The optional multi-view path selects NVIDIA XYZW single-pass stereo or D3D11 viewport instancing. It pairs each eye’s outer and focus scene and checks the paired HDR result against native focus rendering before reuse. OpenGrid defaults the feature off and preserves the ordinary focus pass on an unsupported or unverified eye. Its atlas layout accounts for LMU’s float ceiling in both dimensions. Additional focus passes hold YEBIS exposure adaptation while each native eye advances it normally. Stereo VRS is independent. The upstream AMD cache-core scheduling algorithm remains an explicit machine-local preference and defaults off. These choices carry no measured performance claim for this integration. [Requirements and rig checks](../../../docs/lmu-rendering.md)

## Sim-QVFR 1.0.5-beta adaptations

OpenGrid 1.1.0 adapts the native focus alignment and fallback diagnostics from tag `v1.0.5-beta` at `a78fc1f0856ca0cac827cdaa6abe2ee26cad1029`. The LMU bridge draws the scene, menus and HUD at the runtime's focus projection. The native-provider path preserves those located focus fields of view through rendering and submission. Software providers retain OpenGrid's focus transforms. [Native focus change](https://github.com/Williem3/Sim-QVFR/commit/5a4ddd3)

OpenGrid records its own atlas preparation, image qualification and focus resolve failures. Live diagnostics show the last refusal and per-eye reuse counts. A verbose backend stop records the reason, dimensions and pass totals for the OpenGrid rendering path. The original DXBC converter remains at the revision credited below. [Upstream fallback diagnostics](https://github.com/Williem3/Sim-QVFR/commit/f17c581)

## Providers and licensing

OpenGrid adapts elapsed-time MVR recovery and per-draw metadata caching from Williem3's Sim-QVFR 1.1.1-beta source revision. [Source comparison and transferred changes](../../../docs/research/sim-qvfr-1.1.1.md)

The bundled QVFR provider comes from Matthieu Bucchianeri’s Quad-Views-Foveated 1.1.4. OpenGrid stages it as a private provider and reuses an existing downstream QVFR when present. Its separate source directory documents its Windows path and packaging adaptations and dependency licenses. The embedded VectorXR provider and its modifications remain MPL-2.0. [QVFR provenance and dependency revisions](../quad-views-foveated/NOTICE-LMU.md) · [VectorXR source](../vectorxr) · [VectorXR license](../vectorxr/LICENSE)

Original LMU work uses PolyForm Noncommercial 1.0.0. Upstream credits Markus Stahl for provider integration, performance and stability work. Williem3’s Sim-QVFR supplies the DXBC converter for both backends and LMU focus rendering guidance at tag `v1.0.4-beta` (commit `c7846d385c9fc5b7802299fd29a9a4f48c0df6bd`). OpenGrid retains its own anchored scene and focus hooks. [Reference architecture](https://github.com/Williem3/Sim-QVFR/blob/c7846d385c9fc5b7802299fd29a9a4f48c0df6bd/docs/architecture/LMU-MVR-ARCHITECTURE.svg) · [Exposure and four-pass rendering](https://github.com/Williem3/Sim-QVFR/blob/c7846d385c9fc5b7802299fd29a9a4f48c0df6bd/src/games/lmu/four_passes.cpp) MinHook, HDE, NVAPI and OpenXR retain their own notices. [LMU license](LICENSE.md) · [Upstream third-party notices](THIRD_PARTY_NOTICES.md) · [Sim-QVFR license](mod/vendor/sim_qvfr/LICENSE.md) · [OpenGrid notices](../../../THIRD-PARTY-NOTICES.md)

Required Notice: Copyright (c) 2026 Williem3 (https://github.com/Williem3/Sim-QVFR)

Required Notice: Copyright (c) 2026 Dan Suzuki (https://github.com/DSuzuki35/OpenGridXR)
