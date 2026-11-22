# Credits and license

Starting with version 0.3.12, original OpenGrid XR code and assets are offered under [PolyForm Noncommercial 1.0.0](../LICENSE), except where a file or dependency carries separate terms. This is a source-available license with noncommercial restrictions. Third-party code, fonts and modifications to MPL-covered VectorXR files retain their respective licenses; see [THIRD-PARTY-NOTICES.md](../THIRD-PARTY-NOTICES.md) and [native details](../native/README.md). Earlier releases remain available under the licenses granted with those releases, including GPL-2.0; this change does not revoke those rights. No expiry, account or paid feature gates.


## Credits

**Markus Stahl** contributed the OpenGrid XR review-fixes patch integrated and adapted for **0.6.0**: close confirmation, bounded backups, stale-lock recovery, version-aware startup, scan-code recentering, reduced redundant work, unlocked image waits, helper hardening and CI permissions/pinning. The wait change includes added lifetime protection. See the [changelog](../CHANGELOG.md).

Optional support links are available at the bottom of the sidebar, above **Quick start & about**: [Buy me a coffee](https://buymeacoffee.com/dansuzuki) and [affiliate discounts](https://dansuzuki.com/discounts/). Affiliate purchases may earn Dan a commission. Support is optional and does not unlock features.

OpenGrid XR is developed by **Dan Suzuki and contributors**, with OpenAI Codex assistance in development and documentation. Claude Code contributed accessibility and renderer reviews and keyboard dialog improvements for 1.1.0-beta. Claude Fable 5.2 reviewed rendering performance and update and wheel setup for the stable 1.1.0 release. The toolkit builds on the following projects and references:

| Project / author | Contribution and license context |
| --- | --- |
| [VectorXR by DienerTech LLC](https://github.com/DienerTech/vectorxr) | Adapted native engine from **v0.20.0**, including the performance HUD and embedded quad provider. Covered source and modifications remain **MPL-2.0**. |
| [LMU Quad View Mod by Dan Suzuki](https://github.com/DSuzuki35/LMU-Quad-View-Mod) | Integrated LMU **1.1.0** renderer for quad views and stereo VRS, with provider and performance contributions from **Markus Stahl**. Original code uses **PolyForm Noncommercial 1.0.0**. |
| [Horizonlock VR / SteadyXR by Dan Suzuki](https://github.com/DSuzuki35/horizonlock-vr) | Stabilization math, with the explicit additional PolyForm grant for the included copies; prior GPL-2.0 grants remain effective. |
| [Sim-QVFR by Williem3](https://github.com/Williem3/Sim-QVFR) | DXBC shader conversion and the LMU rendering work adapted for NVIDIA and D3D11 instanced focus rendering, under **PolyForm Noncommercial 1.0.0**. The instanced renderer and exposure investigation build on [1.0.4-beta](https://github.com/Williem3/Sim-QVFR/releases/tag/v1.0.4-beta). Williem3 also supplied the reference for scene-reactive lighting and documented the native Pimax/Varjo focus-region fix and MVR diagnostics in [1.0.5-beta](https://github.com/Williem3/Sim-QVFR/releases/tag/v1.0.5-beta). |
| [Quad-Views-Foveated by Matthieu Bucchianeri](https://github.com/mbucchia/Quad-Views-Foveated) | Bundled QVFR 1.1.4 quad-view provider, **MIT**. |
| GSI and oVRlay | Steering wheel models from the GSI configurator, native stereo mesh rendering and the SimHub LED observer. [Asset and material credits](../THIRD-PARTY-NOTICES.md#gsi-steering-wheels-and-simhub-observer) |
| [SweetFX / LumaSharpen by CeeJay.dk](https://github.com/CeeJayDK/SweetFX) | Original LumaSharpen algorithm by Christian Cann Schuldt Jensen, **MIT**. |
| [FidelityFX CAS by AMD GPUOpen](https://github.com/GPUOpen-Effects/FidelityFX-CAS) | Contrast Adaptive Sharpening, **MIT**. |
| [iRacing SDK](https://forums.iracing.com/discussion/62/iracing-sdk) and [TinyPedal/pyLMUSharedMemory](https://github.com/TinyPedal/pyLMUSharedMemory) | Telemetry ABI and field-layout references, with the retained **BSD** and **MIT** notices respectively. |

The QVFR source includes dependency subsets from fmt, the OpenXR SDK, OpenXR-MixedReality, FidelityFX CAS and WIL. Component notices give the license and pinned revision of each subset. [QVFR provenance](../native/third_party/quad-views-foveated/NOTICE-LMU.md) · [Licenses and corresponding source](../THIRD-PARTY-NOTICES.md)

OpenGrid’s LMU integration adds host crop, snapshot and image-ownership callbacks, bounded waits and startup reporting. The cache-core scheduling algorithm remains upstream code and defaults off in OpenGrid. [Modification provenance](../native/third_party/lmu_quadview/OPENGRID-PROVENANCE.md)

LMU MVR recovery draws on [Williem3’s Sim-QVFR](https://github.com/Williem3/Sim-QVFR). See the [implementation comparison](research/sim-qvfr-1.1.1.md). Stream camera presets and independent horizontal and vertical smoothing follow [Dan Suzuki’s steadyXR 1.0.2](https://github.com/DSuzuki35/steadyXR/releases/tag/v1.0.2), which credits Coltangent for requesting the axis controls.

**Martin Ascher** provided the Ascher Artura model included in the wheel library. **Rak3tenmann** contributed the stereo projection fix for duplicated Pimax OpenXR wheels in [PR #1](https://github.com/DSuzuki35/OpenGridXR/pull/1).
