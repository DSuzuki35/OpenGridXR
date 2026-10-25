# OpenGrid XR enhancements

The user requested linked stream expansion, integrated headset frame generation, a configurable FPS/frame-time/toolkit overlay, and research-led supersampling/foveation/sharpening/distortion controls. Implementation is authorized; no additional VectorXR desktop installation should be necessary.

## Chosen implementation

- Add an optional per-profile symmetric expansion flag. Enabling it retains the larger existing margin on both sides; disabling it retains the values. Old profiles remain asymmetric.
- Keep the existing single OpenGrid registration. Privately chain a bundled, adapted VectorXR engine after OpenGrid crop/capture and before the original downstream runtime. This preserves the game's D3D11 capture path when frame generation bridges to D3D12. A second independently ordered implicit layer would be unreliable; rewriting the frame generation engine would discard an existing tested implementation.
- Keep the engine's source and attribution under MPL-2.0. Preserve source notices, rename its user-visible identity, isolate every config/log/relay path under OpenGrid, and do not bundle the upstream desktop or its branding. Disable unrelated Depth, Pivot and Turbo modules.
- Frame generation is off by default, experimental, and supports interpolation/extrapolation, dynamic/fixed multipliers, quality, motion estimator, artifact limits, diagnostics and a keyboard A/B toggle. Enabling needs a new game session. Stream output continues to capture actual game frames.
- Overlay is off by default and head-following. Configure layout, position, distance, size, opacity, refresh rate, contents, color and shortcuts in OpenGrid. It shows application timing, actual FG state, optional performance metrics, and configured toolkit settings. It is a status panel; it does not pretend to be an interactive VR settings menu. Headset hotkeys toggle visibility/layout and FG A/B.
- Save independent enhancement JSON per game beside the runtime DLL. Profiles without these groups use neutral defaults. Validate ranges, enums, colors and distinct keyboard shortcuts. Installation updates owned files only; never register upstream VectorXR or change the user's OpenXR runtime.

## Additional rendering work

Existing render scale is per-axis supersampling (50–200%, 200% means four times the pixels). Label this clearly. Research game-native foveation first: iRacing's native NVIDIA MVP rendering must not be confused with the OpenXR quad-view extension. Any game-INI editing must offer a concrete preview, exact backup, stale-write protection, and require the sim to be closed. Actual headset distortion belongs to the runtime/driver; do not apply an existing lens calibration again as an application prewarp. Sharpening can be implemented independently. The final supported rendering controls depend on the inspected source and device capabilities; do not ship pretend controls.

## Verification and boundaries

Use model/config tests, a fake runtime loading the actual private chain, native GPU tests for frame generation and OSD rasterization, and packaged desktop workflows. Assert legacy profiles, disabled behavior, wrong/missing engine, native capture before release, source/path isolation, and truthful status. No test registers a layer or alters a real sim configuration. Real headset/runtime behavior remains hardware validation, not a synthetic-test claim.
