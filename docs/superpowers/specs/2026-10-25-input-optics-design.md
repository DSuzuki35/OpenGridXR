# Input bindings and runtime optics

The user requested joystick support for every application toggle, and the Wide FOV, eye adjustment, FOV Zoom and Flat FOV Zoom controls familiar from Sboys3. The final clarification requests both native Pimax OpenXR and Sboys3/SteamVR paths. All application text uses short ASCII hyphens.

## Input

Use a native background, nonexclusive DirectInput reader for wheel, joystick and buttonbox buttons and POV hats. Browser Gamepad polling alone cannot reliably cover background driving or high button counts. Bindings are optional per profile and refer to stable action and device IDs, never device enumeration order. Fire only on a released-to-pressed transition; startup, reconnect and capture of a binding must not activate a setting. Stop the helper when no bindings or binding capture need it. Poll at a modest bounded rate, enumerate devices infrequently, and emit changes only. No force feedback or exclusive acquisition.

Every existing boolean Toggle gets a binding affordance. Bindings operate even on hidden tabs while the application is running. The same validated action implementation handles all controls, including symmetric crop side linking. Saved runtime settings are updated on an input action; unsaved editor changes must not be silently committed or overwritten. A dirty editor pauses runtime actions and explains why. Restarts required by a setting remain required and are disclosed. Disabled controls remain disabled. A dedicated binding view can show device/status and remove or relearn bindings.

## Optics

Sboys3 controls use the existing documented configuration, with narrow field changes, previews, stale-write rejection, exact backups and restore. Preserve all unrelated fields and never bundle the driver or proprietary components. No implicit changes on import or app startup. Dream Air and Dream Air SE are separate targets. Read defaults and limits from the researched pinned source, not community presets. Wide FOV is a set of explicit FOV/clamping/rotation/zoom adjustments, not a promise of extra physical display area.

Investigate Pimax's real runtime interface independently. Do not invent private setting keys, equate legacy 8K FOV levels to Dream Air Wide FOV, or claim optical distortion control from ordinary OpenXR projection changes. Expose only controls supported by verified interfaces; clearly report unsupported settings and missing runtime. If the native path lacks an accessible interface, record the exact gap and preserve it as pending work instead of adding nonfunctional sliders.

## Validation

Isolated configuration fixtures and synthetic input events cover malformed input, lost devices, held buttons, duplicate bindings, profile switches, dirty editing, child failure, queue bounds and stale writes. Native self-tests never send input or modify live driver settings. Package and smoke-test before delivery; physical wheel/headset validation remains explicit.
