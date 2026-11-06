# Credits and license

Starting with version 0.3.12, original OpenGrid XR code and assets are offered under [PolyForm Noncommercial 1.0.0](../LICENSE), except where a file or dependency carries separate terms. This is a source-available license with noncommercial restrictions. Third-party code, fonts and modifications to MPL-covered VectorXR files retain their respective licenses; see [THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md) and [native details](../native/README.md). Earlier releases remain available under the licenses granted with those releases, including GPL-2.0; this change does not revoke those rights. No expiry, account or paid feature gates.


## Credits

Optional support links are available at the bottom of the sidebar, above **Quick start & about**: [Buy me a coffee](https://buymeacoffee.com/dansuzuki) and [affiliate discounts](https://dansuzuki.com/discounts/). Affiliate purchases may earn Dan a commission. Support is optional and does not unlock features.

OpenGrid XR is developed by **Dan Suzuki and contributors**, with OpenAI Codex assistance in code, documentation, debugging and development iterations. AI assistance is not evidence of correctness or compatibility. The toolkit builds on the following projects and references:

| Project / author | Contribution and license context |
| --- | --- |
| [VectorXR by DienerTech LLC](https://github.com/DienerTech/vectorxr) | Adapted native engine from **v0.20.0**, including frame generation and the embedded quad provider. Covered source and modifications remain **MPL-2.0**. |
| [LMU Quad View Mod by Dan Suzuki](https://github.com/DSuzuki35/LMU-Quad-View-Mod) | Integrated LMU adapter from **v0.5.4** for quad rendering and stereo VRS, including performance contributions credited upstream to **Markus Stahl**. Original code is **PolyForm Noncommercial 1.0.0**; bundled dependencies retain their own terms. |
| [Horizonlock VR / SteadyXR by Dan Suzuki](https://github.com/DSuzuki35/horizonlock-vr) | Stabilization math, with the explicit additional PolyForm grant for the included copies; prior GPL-2.0 grants remain effective. |
| [Sim-QVFR by Williem3](https://github.com/Williem3/Sim-QVFR) | Inspiration for the adjustable visor. No Sim-QVFR code or assets are included. |
| [SweetFX / LumaSharpen by CeeJay.dk](https://github.com/CeeJayDK/SweetFX) | Original LumaSharpen algorithm by Christian Cann Schuldt Jensen, **MIT**. |
| [FidelityFX CAS by AMD GPUOpen](https://github.com/GPUOpen-Effects/FidelityFX-CAS) | Contrast Adaptive Sharpening, **MIT**. |
| [iRacing SDK](https://forums.iracing.com/discussion/62/iracing-sdk) and [TinyPedal/pyLMUSharedMemory](https://github.com/TinyPedal/pyLMUSharedMemory) | Telemetry ABI and field-layout references, with the retained **BSD** and **MIT** notices respectively. |

The optional external QVFR provider is **mbucchia's Quad-Views-Foveated**, installed separately; selecting it does not redistribute it or change its license. OpenXR headers, MinHook, NVIDIA interface headers, Electron/Chromium, React, Lucide and the Archivo, Inter and DM Mono fonts also retain their respective notices. See [THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md) for full attributions, pinned revisions, license texts and corresponding source. Credits do not imply endorsement by upstream authors.

OpenGrid 0.5.4 retains the LMU upstream v0.5.4 source pin. OpenGrid's integration adds coordinated crop and rendered-frame ownership, stronger data-layout guards, bounded waits and minimal startup/error reporting; these adaptations do not change the upstream attribution or licenses. The cache-core scheduling algorithm remains the upstream policy and is opt-in in OpenGrid. See [modification provenance](../native/third_party/lmu_quadview/OPENGRID-PROVENANCE.md).
