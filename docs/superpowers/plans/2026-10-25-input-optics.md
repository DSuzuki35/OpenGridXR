# Input and optics implementation plan

> For agentic workers: execute independent components with subagent-driven-development; review integration and test before publication.

**Goal:** Bind every application toggle to racing peripherals and expose verified runtime-specific optics controls.

**Architecture:** Native bounded DirectInput JSONL helper, validated shared action model, desktop controller and React binding controls. Optics uses separate verified adapters, never generic arbitrary driver writes.

**Tech stack:** Existing C++/Win32, Node/Electron and React.

**Spec:** `docs/superpowers/specs/2026-10-25-input-optics-design.md`

- [x] Implement and test native input helper and desktop controller with background nonexclusive acquisition and released-edge semantics.
- [x] Add validated profile bindings/action model and binding affordances for every boolean toggle, including dynamic spatial overlays.
- [x] Connect input actions to saved-profile writes with revision conflict protection, preserve dirty editor changes, and expose status/capture IPC only to the trusted main window.
- [x] Research Pimax interfaces and document exact unavailable optics controls. No verified native write interface was found; implementation remains pending in NEXT-STEPS.md.
- [ ] Implement narrow Sboys3 configuration adapter and UI with review/backup/restore, without touching live settings in tests.
- [x] Independent review, targeted regressions, packaged smoke, docs and public source publication for the input work. Optics implementation is explicitly deferred to NEXT-STEPS.md.

Review focus: startup/reconnect held inputs, duplicate device identities, unsaved editing versus background actions, missing runtime capabilities, stale external config writes, and source/runtime versions diverging from the open preview.
