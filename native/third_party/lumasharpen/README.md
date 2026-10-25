# LumaSharpen provenance

Original project: [CeeJayDK/SweetFX](https://github.com/CeeJayDK/SweetFX).
Pinned revision: `93ddf39b357f5da534ed6d34ba4ec8cc7dcfa361`.
Original file: `Shaders/SweetFX/LumaSharpen.fx`, retained here unmodified.
License: MIT, copyright 2014 CeeJayDK; see `LICENSE`.
Author: Christian Cann Schuldt Jensen / CeeJay.dk.

OpenGridXR's `src/image_processing_shader.hpp` adapts the four sampling patterns
and BT.709 luma unsharp mask into standalone HLSL. It does not require ReShade,
SweetFX, SteamVR compositor hooks, the sboys3 driver, or a separate application.
The original MIT algorithm is used instead of copying the GPL sboys3 adaptation.

Changes: samples clamp to the selected projection image rectangle; alpha is
preserved; the direct luma-delta clamp avoids division by zero; patterns are
numbered 1–4 in configuration. User strength 0–100 maps to original strength
0–3. For an sRGB swapchain, this algorithm reads and writes its display-encoded
UNORM values, as the original SweetFX pass does. MSAA is resolved using the
swapchain's typed format first. There is no resizing or optical prewarp.

The source file in this directory is provenance/reference only. The compiled
adaptation is embedded in `image_processing_shader.hpp`.
