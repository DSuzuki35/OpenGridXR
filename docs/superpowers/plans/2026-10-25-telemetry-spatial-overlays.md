# Telemetry and spatial overlays implementation plan

> For agentic workers: execute stages in order, with independent review and tests at each boundary.

**Goal:** Resource-conscious telemetry and up to 15 useful VR content overlays, with a documented public source release.

**Architecture:** Native read-only telemetry helper plus a desktop lifecycle controller; sandboxed Electron GPU sources feed a native overlay transport and OpenXR compositor. Configuration stays in the existing validated profile document.

**Spec:** `docs/superpowers/specs/2026-10-25-telemetry-spatial-overlays-design.md`

## Stage 1: Telemetry

- [x] Verify primary iRacing/LMU layouts and redistribution rights; record provenance.
- [x] Add native readers, bounded parsers, normalized JSONL helper and synthetic mapping tests in `native/telemetry`.
- [x] Add `desktop/telemetry.cjs`, narrow IPC, profile validation and telemetry screen. Test stale data, child failure, line bounds and idle shutdown.

## Stage 2: Spatial overlays

- [x] Define and test a bounded GPU texture ownership/acknowledgment protocol.
- [x] Implement nonblocking native texture consumption, OpenXR quad submission, capacity accounting, anchor geometry and centering.
- [x] Implement sandboxed URL/window sources, frame throttling, source teardown and GPU incompatibility reporting.
- [x] Add 15-slot profile model and placement/preset/performance controls, with explicit source selection and centering semantics.

## Stage 3: Release

- [x] Add private-by-default per-source stream selection, matching VR placement through the captured/stabilized camera, lazy independent GPU stream rings and actual Clean Output composition; verify geometry, selection, cleanup and static-frame behavior.

- [x] Review integration, malformed inputs, simultaneous frame generation, GPU ownership and process shutdown.
- [x] Run native suites, profile/backend tests, desktop smoke tests and packaged tests.
- [x] Update user/build/API documentation and third-party notices. Scan source/docs for unwanted references and private data.
- [ ] Push a clean public source history to `DSuzuki35/OpenGridXR`, preserving existing local development history separately.

## Review focus

No stale telemetry presented as live; no unbounded child output or queues; no remote page access to main-process commands; no GPU waits in the game frame loop; no compositor layer-limit overflow; no publication of credentials or local artifacts.
