# Motion compensation and rendered crop implementation plan

**Goal:** restore quad submissions when an upper layer changes poses/reference space, and expose independent rendered crop in the launcher.

**Design:** incoming stereo projection poses are authoritative in the submitted space. Transfer each outer-eye pose correction to its focus view while retaining captured FOVs, images, and frame ownership checks. Do not re-locate views after rendering. Crop the outer tangent-space width/height around its center, reduce outer target dimensions by the same fractions, and fit the focus FOV within the cropped region. Focus texture resolution remains unchanged. Default 100/100 preserves prior behavior bit-for-bit. Cropping is launch-time; never claim live resizing or measured savings without hardware validation.

User chose reduced rendered FOV/GPU work, not a post-render mask. Existing autonomy authorization applies. Work is in the existing portable experiment worktree on current main. C++17/D3D11/OpenXR and C#/.NET 4.8 remain unchanged.

- [x] Pose regression: space changes, same-space correction, rotation/translation, invalid inputs and unchanged fast path. Implement and verify in frame submission tests.
- [x] Crop geometry: 25..100 independent percentages, parse/range rejection, asymmetric FOV, default identity, focus containment, rounded target sizing. Write failing tests then implement helper.
- [x] XR integration: apply crop to outer sizes and locate FOV before engine render; hold stable across session. Add fake-runtime crop/default/invalid tests.
- [x] Launcher: settings migration, validation, environment/run metadata, independent controls, restart copy and layout regression tests.
- [x] Review code, run native and launcher suites, package candidate and verify hashes/privacy. Document that actual Qubic and crop compositor behavior still require tester validation.

Live settings assessment: resolution changes require coordinating game-owned focus textures, native outer banks, provider recommendations and swapchain lifetimes. FOV-only changes can theoretically update at frame boundaries in existing textures, but smaller FOV alone does not reduce pixel work; provider focus configuration is not a general live API. Defer live control channel pending a separate verified implementation, explain distinction to user.

Verification: final MSVC native build exited 0 (75 fake-runtime scenarios plus native/unit/WARP suites). Launcher build and 12 layout cases passed. Pose regressions were observed failing before the fix; crop helper initially failed to compile before implementation. Integration crop tests were added concurrently. ZIP hashes/build identity/privacy checks passed. No live Qubic or headset validation performed.

