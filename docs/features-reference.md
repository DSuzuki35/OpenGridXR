# Feature and compatibility reference

For first setup, use [Getting started](getting-started.md). This reference keeps the processing details and limits out of the quick start.

## Optional modules

Open **Setup → Modules** to choose available features globally. **Core only** keeps Session, View & crop, Profiles, OpenXR layers and Setup. **Driving essentials** adds HUD, input bindings and sharpening; **All modules** restores every feature page. Apply the selection after saving or discarding profile edits. Existing installs start with all modules available, preserving their current behavior.

The selection lives in `module-settings.json` beside the profiles. It does not rewrite profile preferences or exported profiles: disabled features and their controller assignments return with the same settings when re-enabled. Optional pages load on first visit. Disabling modules closes their telemetry, recording, capture and input services; previously loaded JavaScript remains cached until app restart. An enabled module makes its controls available; its per-profile enable switch still determines actual use.

Restart the sim after changing modules. Disabled Clean Output makes **Keep full render area for stream** effectively off while preserving its saved preference. Benchmark requires Telemetry and a native engine session, for example an enabled Performance HUD profile; availability of Benchmark alone does not load the engine.

OpenGrid still registers **one OpenXR layer**. The private enhancements DLL loads only for enabled native engine features. Disabled spatial renderers defer graphics resources and reference spaces until needed; inactive image processing skips projection-region collection. Without a Clean Output viewer, connection probes are bounded to four per second, so opening output can take up to 250 ms to attach. Quad stream copies pause while the viewer still holds its last frame. Full-image single-slice processing skips an unnecessary preservation copy; partial images, arrays, mip chains and MSAA keep it. These reduce unused work, not a promised in-game FPS increase.


## Performance HUD

Choose a compact or expanded display for application FPS, frame intervals and hardware metrics. Adjust the panel's position, size and opacity in **Performance HUD**. The default shortcuts are **Ctrl+Alt+F10** for visibility and **Ctrl+Alt+F11** for layout. You can also use the [OpenGrid menu](quick-menu.md).

Enable **Show render resolution & scale** in the HUD to show both the actual submitted left/right projection dimensions and OpenGrid's configured scaling percentage. Pixel dimensions come from the application's submitted eye rectangles, not an estimate from the scale slider. Before a valid frame, dimensions are unavailable. The configured percentage is separate because a game may ignore resolution recommendations.



## Supersampling and sharpening

**View & crop → Supersampling / render scale** sets OpenGrid's per-axis resolution recommendation, from 50 to 200%. At 200%, width and height double, so pixel count quadruples. Game and runtime supersampling multipliers combine with this setting; a game may ignore recommendations.

The **LMU rendering** page offers Native, bundled QVFR and embedded VectorXR quad providers, plus a separate stereo VRS mode. OpenGrid 1.0.0 integrates LMU Quad View Mod source version 1.1.0. Optional NVIDIA or D3D11 instanced multi-view rendering pairs the outer and focus scene for each eye after validating its images. Advanced settings include CPU upload reuse or batching, outer reuse and outer SSR. The AMD cache-core preference defaults off and stays on this PC. Save your profile and launch LMU in VR through Steam. The LMU page reports hook and provider failures even with verbose logs off. [Setup, requirements and earlier rig checks](lmu-rendering.md)

**Headset sharpening** provides Luma Sharpen (strength, clamp, radius and sample pattern) and AMD FidelityFX CAS. It processes supported D3D11 projection images; the status overlay remains separate and clean stream capture precedes headset sharpening. The original MIT-licensed LumaSharpen algorithm also underlies the Sboys3 implementation. Lens correction belongs to the headset driver/runtime and must not be applied twice as a generic application warp.


## OpenXR layer editor

**OpenXR layers** lists current-user and all-user implicit/explicit registrations, their manifests and libraries, and registration health. Close the sim before enabling or disabling an entry. Current-user changes require no administrator approval and apply to the shared 32/64-bit registration view. All-user changes require **Allow administrator changes** and a Windows approval prompt for each change.

Each change is backed up and verified. **Change history → Restore** restores the exact previous value only while the registration still matches the change made by OpenGrid. Registration state does not prove that a game loaded a layer, and list order is not loading order. Explicit layers still need an application request. Changes apply on the application's next launch; the browser preview is read-only.

## Telemetry

Enable Telemetry in a saved game profile to read the native iRacing SDK or LMU shared-memory interface. The page shows freshness, speed, RPM, pedals, fuel, laps and supported flags; stale or unknown values are not presented as live. Collection defaults to 20 Hz, is capped at 30 Hz and pauses without a consumer. Supported LMU builds use their built-in interface without an external shared-memory plugin. See the [read-only telemetry API](telemetry-api.md) and [source requirements](../native/telemetry/README.md).


## Wheel, joystick and buttonbox bindings

Controller bindings offer quick driving actions: performance HUD visibility/layout, spatial overlay visibility, menu selection, **Center VR in game** using the saved game shortcut. Setup and geometry settings are not bindable. The controller icon beside a supported action learns a button or POV direction. Save the profile after learning. The **Controller bindings** page lists assignments and supports removing or relearning them. Bindings are per profile and use device instance IDs rather than enumeration order; reconnecting through a different device instance can require relearning.

The native DirectInput reader supports up to 128 buttons and four POV hats per device, uses background nonexclusive acquisition, and sends press transitions only. Holding a button does not repeatedly toggle. Initial and reacquired device states establish a baseline without firing actions. The helper starts only for enabled saved bindings or learning, and stops when unused. No force-feedback or keyboard injection is used for toggle bindings.

Actions work on the saved selected profile while the app remains open, including when a sim is in the foreground. They pause while the editor is dirty, busy or learning a binding. HUD bindings send acknowledged commands to the matching live game session; the feature must be enabled when that session starts. They do not rewrite startup preferences. Obsolete setup bindings from older profiles remain import-compatible but cannot execute. Physical wheel/buttonbox validation on the game PC is still required.
