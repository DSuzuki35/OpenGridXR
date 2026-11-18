# FidelityFX CAS provenance

Original project: [GPUOpen-Effects/FidelityFX-CAS](https://github.com/GPUOpen-Effects/FidelityFX-CAS).
Pinned revision: `9fabcc9a2c45f958aff55ddfda337e74ef894b7f`.
Original files: `ffx-cas/ffx_cas.h` and `ffx-cas/ffx_a.h`, retained unmodified.
License: MIT. See the complete AMD notices in these files and `LICENSE`.

OpenGridXR's `src/image_processing_shader.hpp` adapts the five-tap, sharpen-only
`CasFilter(..., noScaling=true)` algorithm, with the full-precision reciprocal
and square root (`CAS_GO_SLOWER`) and per-channel weights (`CAS_SLOW`). It does
not implement the optional scaling path. No AMD driver API, vendor SDK, or
additional application is required; the shader runs on D3D11 feature level 11.0.

Changes: projection-rectangle sample clamps prevent cross-eye sampling; a zero
maximum is guarded to define the all-black case; original alpha is preserved.
User strength 0–100 controls both upstream sharpness 0–1 and the interpolation
from original to sharpened color, so zero is a genuine bypass. sRGB images are
decoded by their typed SRV and encoded by their typed RTV; plain UNORM follows
the color interpretation supplied by the application. No extra gamma transform
is guessed for UNORM images.

The original files are provenance/reference only. The compiled HLSL adaptation
is embedded in `image_processing_shader.hpp` and needs no shader files at runtime.
