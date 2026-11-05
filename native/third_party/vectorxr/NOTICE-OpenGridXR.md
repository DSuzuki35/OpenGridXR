# OpenGrid XR private enhancements engine

Source: https://github.com/DienerTech/vectorxr
Upstream release: v0.20.0
Pinned commit: 87e575fb21cde8ad9ef87763c67471b7d0b7ee0a
License: Mozilla Public License 2.0; complete license in LICENSE.

Only native source, headers, shaders, CMake helpers and tests are vendored. No upstream desktop app, branding assets, sounds, installer or manifest. Existing copyright notices remain. OpenGrid modifications: private layer identity/output; OpenGrid config/log/relay/cache paths and environment names; unrelated modules and extended head prediction disabled; OSD branding/toolkit summary; header-only OpenXR linkage; standalone static-CRT CMake wrapper; isolation/desktop JSON tests. Internal depthxr identifiers remain for provenance.

OpenGridXR-Enhancements.dll is privately chained below OpenGridXR.dll, never separately registered. Contract and honest validation limits: ../../README.md.

NVIDIA Optical Flow interface headers are unchanged from the pinned tree (SDK5.0.7); MIT notices are retained and LICENSE-MIT.txt accompanies them. Driver-provided nvofapi64.dll is not distributed. Vulkan-Headers fetch uses exact commit b379292b2ab6df5771ba9870d53cf8b2c9295daf and SHA256 2ad177c0e468ac2eb3db9b5be6b8a18a6730d339378ac670e5fbda3503ac7d64. License texts are in third_party/vulkan-headers. OpenXR headers use OpenGrid's existing sibling dependency (MIT elected). Original THIRD-PARTY-NOTICES.txt is retained as provenance; desktop dependencies listed there are not bundled here.

The 0.20.0 integration also retains OpenGrid helmet/spatial overlays, render-resolution and generated-frame counters. New research switches and external status use private OpenGrid names and paths; no upstream configuration or status file is read or written.

OpenGrid XR 0.3.12 passes the runtime swapchain dimensions to the independent helmet renderer for safe texture fitting. The adapted VectorXR file remains MPL-2.0; OpenGrid's separate helmet implementation is governed by the repository license.

OpenGrid XR 0.4.0 adds an LMU-only, process-scoped policy gate for the embedded quad-view provider and preserves submitted rendered FOVs over later locate-cache entries in its shared composition plan. Other providers and processes do not enable quad emulation through this gate. These changes remain MPL-2.0; the separately linked LMU adapter remains in the sibling lmu_quadview directory under its own license.

For integrated LMU launches selecting external QVFR or native quad views, the private engine forwards the adapter's requested quad-view extensions even when a pre-instance probe cannot enumerate the downstream provider. Internal quad emulation remains disabled for those providers.

OpenGrid XR 0.5.2 extends the existing session-scoped live-control relay with a focused, momentary visor-cycle action and capability reporting. Rejected visor presses are consumed to prevent replay after focus returns. The adapted relay/engine files remain MPL-2.0; the separate helmet renderer retains the repository license.
