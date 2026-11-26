# Ascher Artura dial animation

The built-in Artura model animates its two large12-position rotaries, including their set screws. Input is observed through the existing nonexclusive DirectInput helper. No SimHub/Lovely property, Python observer or hardware configuration change is required. Lovely remains responsible for modifier functions and display labels.

## Input and lifecycle

- Exact DirectInput device name: `Ascher Racing Artura ULTIMATE`.
- Default/measured zero-based button indices30–41: left positions1–12; indices42–53: right positions1–12. These are absolute position pulses, not accumulated clockwise/counterclockwise steps.
- Compatible with the measured pulsed output and permanent one-button-per-position output. Encoder-only and modifier-only firmware output, or remapped button indices, are not supported by this mapping. No mode or mapping is changed automatically.
- Before the first observed position, a dial retains its authored orientation. A single high position button establishes the absolute position; between pulses the last position remains. Multiple simultaneous position buttons are ignored.
- Device disappearance or polling loss clears both known positions. Reconnection starts unknown until a new position is observed. Multiple connected Arturas disable this source instead of selecting an arbitrary wheel.
- The existing steering/input service is also leased when an active game session shows an enabled built-in Artura wheel. No steering-axis assignment is required. The helper stops with the service/application and has no separate autostart.
- If bindings and steering/discovery use separate helpers, a session-local named mutex permits only one writer. Other helpers continue observing and can take over when the owner exits.

## Rendering and calibration

Shared memory `Local\OpenGridXR.ArturaDials.v1` contains two bounded angles, known-position flags and a monotonic heartbeat protected by a seqlock. The renderer never waits on the input helper; it rejects stale (>1 second), future, malformed and torn samples. One snapshot is used for both eyes of each rendered wheel.

Calibrated on Christian's physical wheel: clockwise positions advance by -30 degrees, with left offset -90 degrees and right offset +30 degrees relative to the authored CAD orientation. These offsets were confirmed in VR. A broader hardware validation is still needed before general release.

The reviewed integrated AGM has564,102 vertices/indices. Original CAD bodies Körper47/48 (large knobs) and Körper45/46 (set screws) are transformed around pivots(-23,-37) and(23,-37) mm in source coordinates. Normals rotate with positions. Dashboard/LED UVs, light groups, geometry data and all other controls are unchanged. A geometry-count/version guard disables these source-specific spans for an incompatible mesh. Future model regeneration must review the span and pivot mapping.

This is separate from the diffuse color change. No metallic material, reflection effect or increased render resolution is introduced. Functional tests cover calibration, positions, unknown/restart states, stale IPC, writer ownership and session activation. GPU renderer and layer-load checks complement physical headset testing; no claim of zero frame-time cost is made.
