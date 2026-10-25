# Telemetry and spatial overlays

## Scope

Add read-only iRacing and Le Mans Ultimate telemetry, up to 15 independently positioned URL/window overlays, and explicit VR centering controls. All features remain free and open source. Publish the project on the owner's public GitHub with source, license notices, build instructions and honest validation limits.

The implementation proceeds in stages: telemetry; GPU overlay transport and anchoring; desktop controls; integrated validation and publication. Existing crop, streaming, sharpening, frame generation and performance-panel behavior remains available.

## Implementation decisions

- A small native telemetry helper reads the documented shared-memory interfaces. Bounded snapshots become normalized JSON lines, never private driver/session metadata. Poll at 20 Hz by default, at most 30 Hz; reconnect at 1 Hz. Unavailable values are null. Stale values are clearly identified. Desktop consumers do not need game SDK bindings.
- Electron renders URL and window sources into shared GPU textures. Remote pages have sandboxing, context isolation, no Node integration, no privileged preload, and no access to application IPC. Only explicit HTTP(S) URLs are accepted. Window selection is explicit and handles are never inferred from titles alone.
- The native layer consumes completed textures without waiting for a producer inside the VR render loop. Retain the most recent good image; drop surplus frames. Disabled sources release resources. Static pages produce no extra frames. Resolution, frequency and a total pixel-rate budget are configurable and bounded.
- Up to 15 saved overlays have stable IDs, source selection, head/local anchors, angular and meter positioning, size, opacity, and a priority order. Presets cover dashboard, left/right console and overhead information. Runtime layer capacity is checked after game layers and the performance panel; unavailable slots are reported, never submitted beyond the limit.
- Overlay centering realigns the local overlay anchor to the current head yaw and position. Whole-game centering must have a separate verified implementation or be explicitly labelled as a game-bound action; it must not be silently substituted by overlay centering.
- Capture and telemetry workers run only when demanded, with bounded queues and no full-resolution CPU bitmap transport in the frame path. Unsupported GPU/runtime paths report an error and leave the game running.
- Each source has an explicit `showInStream` choice, false by default. Selected sources use a separate keyed GPU ring only while Clean Output is active. The stream shows their actual VR placement, projected through the captured eye and the same crop/stabilization camera; position, perspective, roll, aspect and opacity must agree. There are no independent 2D stream placement controls. Private/disabled sources are withdrawn on changes and never silently retained in the stream. The independent ring avoids VR/output consumers competing for one keyed mutex.

## Validation

Use synthetic shared memory and a synthetic OpenXR runtime to verify malformed/stale data, geometry, layer limits, frame ownership and cleanup. Electron smoke tests verify real sandboxed source creation, source lifecycle and profile persistence. Hardware tests on the development machine do not establish Pimax/RTX 5090 or live-game compatibility. Public documentation identifies what remains untested.
