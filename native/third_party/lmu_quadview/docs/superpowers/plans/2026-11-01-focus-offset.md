# Foveation offset release implementation plan

> **For agentic workers:** Use superpowers:subagent-driven-development.

**Goal:** Release tested horizontal/vertical foveation offsets as 0.1.1.
**Architecture:** Translate the provider focus FOV once in the OpenXR adapter;
launcher/CLI save and pass two scalar offsets. Existing render/outline/composition
consumers share that shifted FOV. No SPS experiments are merged into this branch.
**Tech Stack:** C++17/OpenXR/D3D11, C# WinForms, PowerShell.
**Spec:** docs/superpowers/specs/2026-11-01-focus-offset-design.md

## Global constraints

- Offsets -50..50 percent of each eye's full outer tangent-space span; default0.
- Positive X right, positive Y up; both fixed and tracked modes/all providers.
- Zero exact no-op; no gaze accumulation, pose change, outer clamp or buffer resize.
- One shifted focus FOV for rendering, outline and submission; no new GPU pass.
- Native runtime implementation stays on released-main rendering, SPS excluded.
- User authorized autonomous implementation and publication of version0.1.1.

## Review focus

- Runtime-settings path accidentally skipping offsets; double application.
- Gaze drift across repeated locates, asymmetric eyes or unchanged-axis rounding.
- Old settings/stale inherited environment and locale-dependent decimals.
- Reset and controls usable for all providers at supported layouts/DPI.
- Release package matching reviewed source/version without local diagnostics.

### Task 1: Native, launcher and CLI feature

Files: mod/focus_box.hpp, mod/xr_layer.cpp, mod/tests/focus_box_test.cpp,
mod/xr_layer_tests.cpp, mod/launcher/QvfrSettings.cs, LaunchService.cs, Launcher.cs,
SettingsTests.cs, LaunchServiceTests.cs, GuiLayoutTests.cs, mod/launch.ps1.
May update mod/build.ps1 and mod/build-msvc.ps1 to run added integration test modes.
Preserve CLI regression coverage in mod/tests/focus_offset_launch_test.ps1.
No unrelated edits.

Interfaces: settings FocusOffsetXPercent and FocusOffsetYPercent (decimal);
environment LMU_QV_FOCUS_OFFSET_X_PERCENT and LMU_QV_FOCUS_OFFSET_Y_PERCENT;
CLI same settings parameter names (double). Native helper names are implementer
choice; pure helper validates then commits atomically as specified in design.

- [ ] Write and run failing tests for native translation and settings plumbing.
- [ ] Implement helper and parsing; apply after legacy scaling in every quad
  LocateViews call, even UseRuntimeSettings; update inaccurate FOV log comments.
- [ ] Add saved controls/reset and launch/metadata/CLI wiring without provider writes.
- [ ] Cover exact zero/default migration, signed fractional extrema/invalid values,
  per-eye asymmetric outer spans, moving gaze/no accumulation, unchanged poses,
  render/submitted FOV agreement, debug outline's shared FOV, all provider controls,
  reset and settings persistence. Check existing GUI layouts plus new card.
- [ ] Focused tests during development; complete native and launcher build once
  after changes, then self-review and report. Root sets VERSION0.1.1 before build.

### Task 2: Review, documentation and release

- [ ] Root updates VERSION/README/CHANGELOG/portable docs, records SPS0.2.0 scope.
- [ ] Independent feature/whole-change review; resolve important findings.
- [ ] Incorporate the user's final brand alignment/numeric spacing refinements
  and close the tiny CLI-offset formatting mismatch; scoped independent review
  of the follow-up diff and affected tests, then rebuild the release archive.
- [ ] Verify built GUI and package archive/extraction/hash/version; record exact
  synthetic versus live validation scope. Commit and push reviewed release branch.
- [ ] Merge through GitHub and publish v0.1.1 with portable ZIP and release notes;
  verify public release tag, commit and downloadable asset.
