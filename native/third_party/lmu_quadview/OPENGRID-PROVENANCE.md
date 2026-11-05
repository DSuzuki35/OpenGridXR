# LMU QuadView integration

Upstream: https://github.com/DSuzuki35/LMU-Quad-View-Mod

Pinned v0.2.1 commit: `bf1189a6d841866b90eab2673b538eb06b471965`.

Vendored tracked mod sources/tests/standalone launcher, required prototype headers,
architecture documentation, capture/version tools and license notices. Game
binaries, personal files, TODO files and build artifacts are excluded.

OpenGrid changes: private static factory, coordinated four-edge crop, immutable
render-frame image/submission callbacks. The integrated build disables adaptive
focus FOV irrespective of inherited environment. Standalone launcher isolation
(`DISABLE_OPENGRIDXR`) remains unchanged.

The native outer-texture regression compares the active D3D11 view descriptor
fields instead of unused union storage, with poisoned-storage regressions for
Windows/WARP portability. Runtime texture cloning is unchanged by this test fix.

See LICENSE.md and THIRD_PARTY_NOTICES.md. Original code is licensed under
PolyForm Noncommercial 1.0.0; the upstream license and separate component notices
remain applicable. This integration does not relicense upstream sources.
