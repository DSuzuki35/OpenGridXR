# OpenGrid XR private enhancements engine

Source: https://github.com/DienerTech/vectorxr
Pinned commit: 9e8b49b1721eb30ce27d7ba72f1488050160a29d
License: Mozilla Public License 2.0; complete license in LICENSE.

Only native source, headers, shaders, CMake helpers and tests are vendored. No upstream desktop app, branding assets, sounds, installer or manifest. Existing copyright notices remain. OpenGrid modifications: private layer identity/output; OpenGrid config/log/relay/cache paths and environment names; unrelated modules and extended head prediction disabled; OSD branding/toolkit summary; header-only OpenXR linkage; standalone static-CRT CMake wrapper; isolation/desktop JSON tests. Internal depthxr identifiers remain for provenance.

OpenGridXR-Enhancements.dll is privately chained below OpenGridXR.dll, never separately registered. Contract and honest validation limits: ../../README.md.

NVIDIA Optical Flow interface headers are unchanged from the pinned tree (SDK5.0.7); MIT notices are retained and LICENSE-MIT.txt accompanies them. Driver-provided nvofapi64.dll is not distributed. Vulkan-Headers fetch uses exact commit b379292b2ab6df5771ba9870d53cf8b2c9295daf and SHA256 2ad177c0e468ac2eb3db9b5be6b8a18a6730d339378ac670e5fbda3503ac7d64. License texts are in third_party/vulkan-headers. OpenXR headers use OpenGrid's existing sibling dependency (MIT elected). Original THIRD-PARTY-NOTICES.txt is retained as provenance; desktop dependencies listed there are not bundled here.
