# Sim-QVFR 1.1.1-beta and OpenGrid's LMU renderer

Reviewed Williem3's November 13 release at commit `2de90b08c857e838ccb9e0135402a3bc89ef8039`. The patch release removes a development tool that wrote a copy of LMU's executable to disk. OpenGrid's LMU integration did not import that tool. Its own launcher has no corresponding dump to remove. [Release notes](https://github.com/Williem3/Sim-QVFR/releases/tag/v1.1.1-beta)

## Changes in OpenGrid

- Each eye now retries a failed pairing after 3, 6, 12 and then 20 seconds, measured on a monotonic clock. The old limit of 1,800 eye passes could keep a low-frame-rate session waiting much longer. An eye also backs off after 45 qualification attempts without a reference resolve. Its ordinary focus pass continues to render. A new session resets both eyes, and 600 successful reuses return the affected eye to the initial retry interval. OpenGrid still requires its own reference-image checks before reuse. [Upstream recovery](https://github.com/Williem3/Sim-QVFR/commit/2385ee4f8f2c9a47a461cbf045d789158ab55691)
- The D3D11 instancing path reads the current input layout each draw and retains its validated metadata for consecutive draws with that layout. The per-pass cache owns COM references to both the game layout and its doubled-step-rate replacement. It avoids repeated private-data lookups while preserving checks on current shader-resource bindings. Upstream applies the same principle to pixel-shader roles in a different pass tracker. No frame-time gain has been measured here. [Upstream per-draw work](https://github.com/Williem3/Sim-QVFR/commit/54807d13919eca48f6c3eec3dca93601bb70d157)

## Architectural differences

OpenGrid retains its anchored four-pass route as a fallback. Upstream's D3D11-only pass detection, two-pass-only operation, FSR support and DLSS/NR pipeline depend on its own scene-copy and post-processing paths. Copying their settings would advertise capabilities absent from OpenGrid. Its command-list shadow fix suspends pairing during replay. OpenGrid's binding path already leaves pairing before replay and refuses that frame's focus reuse when recorded draws cannot be qualified. [Upstream renderer](https://github.com/Williem3/Sim-QVFR/tree/2de90b08c857e838ccb9e0135402a3bc89ef8039/src/games/lmu) · [Command-list fix](https://github.com/Williem3/Sim-QVFR/commit/cc90866f0dd25e9832d5d7253994f9ecb6cd5bfd)

The native policy fixture checks the retry clock and cap. The D3D11 WARP pipeline fixture checks missing-resolve backoff and eye isolation, focus qualification, paired pixel output and restoration of the game's input layout. In-game headset timing and visual quality require a separate rig run.

Required Notice: Copyright (c) 2026 Williem3 (https://github.com/Williem3/Sim-QVFR)

[PolyForm Noncommercial 1.0.0 and upstream notice](../../native/third_party/lmu_quadview/mod/vendor/sim_qvfr/LICENSE.md)
