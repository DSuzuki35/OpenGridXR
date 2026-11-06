# Upload reuse runtime toggle implementation plan

> **For agentic workers:** Use superpowers:executing-plans inline. Keep the existing isolated research worktree.

**Goal:** Compare focus-upload reuse on/off in one cockpit session with Ctrl+Alt+F10.

**Architecture:** Arm a render-thread hotkey only when render profiling and the verified focus-only upload hook are already enabled at startup. Apply changes at the left-eye boundary before rendering; keep hook installation fixed. Reject presses during a capture and consume the key edge so holding it cannot toggle later. End cached upload assumptions on each transition. Log state transitions and each capture's configuration.

**Tech stack:** Existing C++17 D3D11 native layer and MSVC tests.

**Spec:** User requested runtime toggling for a controlled performance comparison; the diagnostic hotkey and capture lockout are implementation choices.

## Constraints and review focus

- Leave the other chat's running game untouched. No injection, signing or publication.
- One initial restart loads the new DLL. Changes apply to both eyes from the next left-eye boundary.
- Ordinary runs retain their existing settings and no extra key polling.
- Scope: focus-only reuse; outer reuse, batching, VRS and draw audits do not arm this toggle.
- A held key toggles once; a press during measurement is rejected, never deferred.
- No cached GPU-content assumptions survive an on/off transition; native draws/bindings remain intact.
- OFF retains the same installed hooks as ON, which makes comparisons fair but differs slightly from a build without these hooks.

## Task

- [x] Write policy tests for edges, capture lockout, disabled capability and reconfiguration; watch RED.
- [x] Implement the small hotkey policy and integrate at the left-eye frame boundary under the existing upload mutex.
- [x] Run byte-exact native upload regressions using WARP, plus hook integration tests proving off/on invalidates cached assumptions.
- [x] Wire into native test/build commands; run the full native/XR suite with hardware MVR disabled.
- [x] Prepare an isolated unsigned diagnostic package, preserving user graphics and signed source payloads; do not start a game.
- [x] Record verification and hotkey instructions, then review the diff.

## Verification and rulings

- Full `mod/build-msvc.cmd` completed with exit 0; log `mod/build/runtime-toggle-final-msvc.log`: 80 XR scenario results, WARP upload content/binding regressions, hotkey policy and runtime hook integration passed. Six existing XR test compiler warnings remain.
- Policy assertions failed before implementation. Review regressions separately reproduced foreign-thread toggling and missing OFF-state summaries, then passed after the owner/capability fixes.
- Fresh-context review found those two issues; the follow-up review reports no remaining findings.
- Test package: `../../dist/cpu-runtime-toggle-20261105` relative to this worktree root's parent experimental directory, or the workspace `dist/cpu-runtime-toggle-20261105`. All 45 manifest entries verified. Native DLL is intentionally unsigned; existing launcher/autostart/provider payloads copied from the verified signed source package.
- Native DLL SHA256: `37EB187216ECAE662640FC55560E2F56C83E52566BE8AEBCBA2DA1BC38C3466D`.
- Ruling: restrict hotkey interpretation to the profiler owner, because collecting() is intentionally false on foreign threads and cannot by itself enforce capture lockout.
- Ruling: preserve session summary when the final toggle state is OFF; current optimization state is no longer a measure of whether the session used reuse.
- Live LMU validation is pending; the unrelated running game was left untouched. No signing or GitHub publication performed.
