# Quad-Views-Foveated, built into LMU Quad View

Source: https://github.com/mbucchia/Quad-Views-Foveated
Upstream version: 1.1.4 (`version.info`)
Pinned commit: 79855a001302472a0f6c0703567dfe8ebfa6f988
License: MIT, complete text in `LICENSE` (Copyright (c) 2022-2023 Matthieu Bucchianeri).

The LMU import contains the `openxr-api-layer` sources, shaders, dispatch generator and default `settings.cfg`. Its CMake build replaces the upstream Visual Studio project and uses the static CRT on Windows x64. OpenGrid keeps the layer name `XR_APILAYER_MBUCCHIA_quad_views_foveated` and the per-user `%LOCALAPPDATA%\Quad-Views-Foveated\settings.cfg` convention.

OpenGrid builds this source in its Windows native build. The `qvfr-package` target stages the DLL, manifest and default settings under `<build>/<config>/providers/qvfr`. The runtime installer places a private provider beside the OpenGrid layer. It does not register QVFR as another implicit layer.

OpenGrid adapted `openxr-api-layer/framework/entry.cpp` to find the module directory and local application-data directory using wide Windows paths. The log opens using its filesystem path. Configuration diagnostics in `openxr-api-layer/layer.cpp` render paths as UTF-8. This supports user and install directories containing non-ASCII characters. The original provider and dependency licenses continue to apply.

Minimal dependency subsets (each with its license file):

| Folder | Upstream | Commit | License |
| --- | --- | --- | --- |
| external/fmt | https://github.com/fmtlib/fmt | f5e54359df4c26b6230fc61d38aa294581393084 | MIT |
| external/OpenXR-SDK (include, src/common) | https://github.com/KhronosGroup/OpenXR-SDK | 58a00cf85c39ad5ec4dc43a769624e420c06179a | Apache-2.0 / MIT |
| external/OpenXR-SDK-Source (registry, generator scripts) | https://github.com/KhronosGroup/OpenXR-SDK-Source | 71088743c0214e1c7bb753aae2578a0adbf0f30c | Apache-2.0 |
| external/OpenXR-MixedReality (shared/XrUtility, ScopeGuard.h) | https://github.com/microsoft/OpenXR-MixedReality | ef759465acd79e117c2650ea8eb32a4d31a6b675 | MIT |
| external/FidelityFX-CAS (ffx-cas) | https://github.com/GPUOpen-Effects/FidelityFX-CAS | 9fabcc9a2c45f958aff55ddfda337e74ef894b7f | MIT |
| external/wil (include) | https://github.com/microsoft/wil | abb1bd3509b6e5db53ba29738a4180d857adfd40 | MIT |

Upstream used the WIL NuGet package 1.0.220201.1; the header-only GitHub tree
above replaces it.

## Binary in packages built on Linux

Linux cross-builds can stage the Windows-built provider in
`mod/build/providers/QVFR` before running `tools/cross-package.sh`. Alternatively,
that directory can contain this repository's layer manifest and mbucchia's
official, Authenticode-signed x64 DLL and default `settings.cfg` from release 1.1.3
(`Quad-Views-Foveated-1.1.3.msi`, SHA-256
7b59593b83cd38da6f55070a06a1bbb26312d304cbee54a8d68ea493d3118326), unpacked without
running the installer. DLL SHA-256:
`20673164cfd789d70b3cd6f7e2b49a1a0618e7fbb606e5e6787e2f66518ad482`. Without
these inputs the Linux developer package omits QVFR. The Windows release build
runs `tools/build-providers.ps1` against the vendored source.
