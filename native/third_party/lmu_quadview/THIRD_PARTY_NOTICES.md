# Third-party components

- Sim-QVFR's DXBC shader conversion is included under PolyForm Noncommercial 1.0.0.
  Required Notice: Copyright (c) 2026 Williem3 (https://github.com/Williem3/Sim-QVFR)
  See `mod/vendor/sim_qvfr/LICENSE.md` (portable package: `licenses/Sim-QVFR-LICENSE.md`).
  OpenGrid adapts both NVIDIA and instanced focus rendering from the [LMU implementation](https://github.com/Williem3/Sim-QVFR/tree/c7846d385c9fc5b7802299fd29a9a4f48c0df6bd/src/games/lmu).
- [srounce's LMU foveation work in DXVK](https://github.com/srounce/dxvk/blob/548008900fff1882239a66f30b7dab9b78e641db/docs/lmu-foveation.md) inspired avoiding unnecessary
  color resampling and retaining target metadata for the lifetime of an image.
  These optimizations are implemented in the native D3D11 layer.

- MinHook 1.3.4: BSD 2-Clause, with bundled HDE notices. See `mod/vendor/minhook-1.3.4/LICENSE.txt`.
- Khronos OpenXR headers: Apache-2.0. See `third_party/openxr/LICENSE`; provenance is retained beside the headers.
- NVIDIA NVAPI headers: MIT, with copyright and permission notices retained at the top of the headers in `mod/vendor/nvapi/`. Pinned source URLs and hashes are in `provenance.json`.

- OpenGrid XR 0.6.0 by Dan Suzuki and contributors (`opengrid/`): PolyForm
  Noncommercial 1.0.0, see `opengrid/LICENSE`; its own third-party components and
  notices are listed in `opengrid/THIRD-PARTY-NOTICES.md`. The 0.9.0 renderer
  hardening in `mod/xr_layer.cpp`, `mod/engine_bridge.cpp`, `mod/view_crop.hpp`
  and `mod/data_abi*.hpp` is adopted from OpenGrid XR's adapted copy. The
  launcher's helmet compositor, telemetry reader, lap benchmark, bindings, overlay
  and layer editors are .NET ports of OpenGrid XR code, and the helmet artwork in
  `helmets/` (`src/Assets/helmets`) is OpenGrid XR's original artwork; both are
  under the same license.

  Required Notice: Copyright (c) 2026 Dan Suzuki (https://github.com/DSuzuki35/OpenGridXR)
- Built-in rendering providers (1.1.0):
  - Quad-Views-Foveated 1.1.4 by Matthieu Bucchianeri (`third_party/quad-views-foveated/`,
    shipped as `providers/QVFR`): MIT, see `third_party/quad-views-foveated/LICENSE`.
    Its vendored dependency subsets (fmt, OpenXR SDK headers and registry, OpenXR-MixedReality
    utilities, FidelityFX CAS, WIL) keep their own licenses; pinned commits are listed in
    `third_party/quad-views-foveated/NOTICE-LMU.md`.
  - VectorXR 0.20.0 by DienerTech LLC: the quad-view compositor inside the OpenGrid XR
    engine `OpenGridXR-Enhancements.dll` (`opengrid/native/third_party/vectorxr/`):
    Mozilla Public License 2.0, see `opengrid/native/third_party/vectorxr/LICENSE`.
- Build-time only, never shipped: microsoft/DirectX-Headers (MIT) and
  KhronosGroup/Vulkan-Headers (Apache-2.0/MIT) for the OpenGrid XR Enhancements
  engine; Microsoft's d3dcompiler_47 from the Microsoft.WindowsDesktop.App.Runtime
  NuGet package compiles its shaders. See `tools/opengrid-runtime/fetch-deps.sh`.
- Compiled into `LMU-QuadView-Launcher.exe` and `LMU-QuadView-Autostart.exe` by
  NativeAOT, or shipped beside them: the .NET 10 runtime (MIT, .NET Foundation and
  Contributors; its bundled third-party notices), Avalonia 12 (MIT, AvaloniaUI OÜ),
  SkiaSharp (MIT, Xamarin/Microsoft) with Skia (BSD-3-Clause, Google) in
  `libSkiaSharp.dll`, HarfBuzzSharp (MIT) with HarfBuzz ("Old MIT") in
  `libHarfBuzzSharp.dll`, and the Inter font (SIL Open Font License 1.1, The Inter
  Project Authors). License texts: `third_party/launcher-licenses`, copied to
  `licenses/` in packages. Build-time only: the Windows SDK import libraries
  (Microsoft.Windows.SDK.CPP.x64 NuGet package) used by `tools/nativeaot` on Linux.

The repository contains no LMU executable, assets, memory dumps, or NVIDIA driver binaries. Original project code is licensed under PolyForm Noncommercial 1.0.0; see LICENSE.md. The third-party licenses above remain applicable to their respective files and are not replaced by the project's license.
