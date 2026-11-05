# Feature and compatibility reference

For first setup, use [Getting started](getting-started.md). This reference keeps the processing details and limits out of the quick start.

## Optional modules

Open **Setup → Modules** to choose available features globally. **Core only** keeps Session, View & crop, Profiles, OpenXR layers and Setup. **Driving essentials** adds helmet, HUD, input bindings and sharpening; **All modules** restores every feature page. Apply the selection after saving or discarding profile edits. Existing installs start with all modules available, preserving their current behavior.

The selection lives in `module-settings.json` beside the profiles. It does not rewrite profile preferences or exported profiles: disabled features and their controller assignments return with the same settings when re-enabled. Optional pages load on first visit. Disabling modules closes their telemetry, recording, capture and input services; previously loaded JavaScript remains cached until app restart. An enabled module makes its controls available; its per-profile enable switch still determines actual use.

Restart the sim after changing modules. Disabled Clean Output makes **Keep full render area for stream** effectively off while preserving its saved preference. Benchmark requires Telemetry and a native engine session, for example an enabled Performance HUD profile; availability of Benchmark alone does not load the engine.

OpenGrid still registers **one OpenXR layer**. The private enhancements DLL loads only for enabled native engine features. Disabled helmet and spatial renderers defer graphics resources and reference spaces until needed; inactive image processing skips projection-region collection. Without a Clean Output viewer, connection probes are bounded to four per second, so opening output can take up to 250 ms to attach. Quad stream copies pause while the viewer still holds its last frame. Full-image single-slice processing skips an unnecessary preservation copy; partial images, arrays, mip chains and MSAA keep it. These reduce unused work, not a promised in-game FPS increase.


## Headset frame generation and VR overlay

The optional native engine adapts frame generation from VectorXR **0.20.0** by [DienerTech's VectorXR](https://github.com/DienerTech/vectorxr), credited and linked directly in the Frame generation page. No VectorXR desktop app or second layer installation is needed. OpenGrid privately loads `OpenGridXR-Enhancements.dll` downstream of its crop/capture layer. Source and modifications are included with MPL-2.0 notices and corresponding source links in [Third-party notices](../THIRD-PARTY-NOTICES.md). Existing VectorXR settings are not used or changed. The explanatory preview changes with interpolation or extrapolation; it is not a live benchmark.

In **Frame generation**, enable the feature, select interpolation or extrapolation, choose Dynamic/Auto/2×/3×/4× and adjust quality/artifact controls. Shader motion estimation is the portable default; NVIDIA hardware estimation uses the optical-flow component in the installed graphics driver and can fall back where unavailable. Enabling frame generation requires a game restart. Disable the runtime's own motion smoothing while using it. Frame generation is experimental: generated frames can exhibit artifacts and interpolation adds latency. The streaming window continues to show captured real game frames.

In **Performance HUD**, select compact or expanded layout, position, distance, scale, opacity, colors, update rate and displayed information. The real head-following panel can show application FPS/frame intervals, a timing graph, performance metrics and actual frame-generation state. The toolkit summary is labelled **Configured**: a saved setting is not proof that a running session has applied a restart-required change. For interactive live controls, add the separate [OpenGrid menu](quick-menu.md). Desktop previews use placeholders, not invented live metrics.

Default shortcuts are **Ctrl+Alt+F9** for the frame-generation A/B toggle, **Ctrl+Alt+F10** for overlay visibility and **Ctrl+Alt+F11** for overlay layout. Keys are configurable or can be unbound; duplicate assignments are rejected. Both features default to off. After updating, close the sims/output and use **Setup → Install/update runtime layer** to deploy the current engine.

Enable **Show render resolution & scale** in the HUD to show both the actual submitted left/right projection dimensions and OpenGrid's configured scaling percentage. Pixel dimensions come from the application's submitted eye rectangles, not an estimate from the scale slider. Before a valid frame, dimensions are unavailable. The configured percentage is separate because a game may ignore resolution recommendations.

The frame-generation HUD also shows cumulative generated frames, generated frames per second and their share of successful submissions. Counts reflect actual successful synthetic submissions, not the configured multiplier. The lap recorder retains separate application, generated, repeated and submitted totals.


## Supersampling and sharpening

**View & crop → Supersampling / render scale** sets OpenGrid's per-axis resolution recommendation, from 50 to 200%. At 200%, width and height double, so pixel count quadruples. Game and runtime supersampling multipliers combine with this setting; a game may ignore recommendations.

LMU quad views and stereo VRS are configured on the dedicated **LMU rendering** page. The integrated adapter comes from LMU Quad View Mod v0.2.1 and offers Native, external QVFR and embedded VectorXR quad providers; stereo VRS is a separate mode. Dynamic focus FOV is excluded. The normal Steam VR path loads saved rendering settings directly in the registered OpenGrid layer. No OpenGrid Steam launch option or running editor is needed. Remove the old `--lmu-steam-launch` option if previously configured. EAC remains enabled; signatures and actual protected-launch compatibility require separate verification. See [LMU rendering](lmu-rendering.md) for setup, providers, executable checks and validation limits. iRacing graphics settings remain managed in iRacing itself; OpenGrid does not edit its renderer INI.

**Headset sharpening** provides Luma Sharpen (strength, clamp, radius and sample pattern) and AMD FidelityFX CAS. It processes supported D3D11 projection images before frame generation; the status overlay remains separate and clean stream capture precedes headset sharpening. The original MIT-licensed LumaSharpen algorithm also underlies the Sboys3 implementation. Lens correction belongs to the headset driver/runtime and must not be applied twice as a generic application warp.


## OpenXR layer editor

**OpenXR layers** lists current-user and all-user implicit/explicit registrations, their manifests and libraries, and registration health. Close the sim before enabling or disabling an entry. Current-user changes require no administrator approval and apply to the shared 32/64-bit registration view. All-user changes require **Allow administrator changes** and a Windows approval prompt for each change.

Each change is backed up and verified. **Change history → Restore** restores the exact previous value only while the registration still matches the change made by OpenGrid. Registration state does not prove that a game loaded a layer, and list order is not loading order. Explicit layers still need an application request. Changes apply on the application's next launch; the browser preview is read-only.

## Telemetry

Enable Telemetry in a saved game profile to read the native iRacing SDK or LMU shared-memory interface. The page shows freshness, speed, RPM, pedals, fuel, laps and supported flags; stale or unknown values are not presented as live. Collection defaults to 20 Hz, is capped at 30 Hz and pauses without a consumer. Supported LMU builds use their built-in interface without an external shared-memory plugin. See the [read-only telemetry API](telemetry-api.md) and [source requirements](../native/telemetry/README.md).


## Wheel, joystick and buttonbox bindings

Controller bindings offer quick driving actions: performance HUD visibility/layout, frame generation on/off, spatial overlay visibility, menu selection, and **Center VR in game** using the saved game shortcut. Setup and geometry settings are not bindable. The controller icon beside a supported action learns a button or POV direction. Save the profile after learning. The **Controller bindings** page lists assignments and supports removing or relearning them. Bindings are per profile and use device instance IDs rather than enumeration order; reconnecting through a different device instance can require relearning.

The native DirectInput reader supports up to 128 buttons and four POV hats per device, uses background nonexclusive acquisition, and sends press transitions only. Holding a button does not repeatedly toggle. Initial and reacquired device states establish a baseline without firing actions. The helper starts only for enabled saved bindings or learning, and stops when unused. No force-feedback or keyboard injection is used for toggle bindings.

Actions work on the saved selected profile while the app remains open, including when a sim is in the foreground. They pause while the editor is dirty, busy or learning a binding. HUD and frame-generation bindings send acknowledged commands to the matching live game session; the feature must be enabled when that session starts. They do not rewrite startup preferences. Obsolete setup bindings from older profiles remain import-compatible but cannot execute. Physical wheel/buttonbox validation on the game PC is still required.
