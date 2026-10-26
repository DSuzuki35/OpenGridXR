# Runtime optics

The **Runtime optics** page edits existing Sboys headset-driver settings offline. It supports four fields for Dream Air and Dream Air SE: Eye Rotation, Automatic Eye Rotation, FOV Zoom and Flat FOV Zoom. Saved configuration is not confirmation that the running driver or headset has applied it.

## Select and review

1. Select the existing installation explicitly: **Neutral** (`%APPDATA%/CustomHeadset/settings.json`) or **Pimax build** (`%APPDATA%/Pimax/CustomHeadset/settings.json`). File presence does not identify the active runtime.
2. Select **Dream Air** or **Dream Air SE**. These remain separate objects; no generic calibration profile is copied between them.
3. Edit supported values and review the exact before/after changes. Unrelated fields and their formatting are preserved. Existing out-of-range values are displayed rather than silently corrected.
4. Close both sims, Clean Output, SteamVR and the Sboys settings app. Confirm that they are closed, then apply the reviewed changes. The app also checks known processes before writing.
5. Restart the driver/runtime and game as needed, then verify the actual view and alignment in the headset. No runtime switch, driver installation or IPD change is performed.

The adapter rejects malformed, oversized or duplicate-key JSON, missing fields, unknown changes and stale reviews. Every write preserves the original bytes in the app data folder under `runtime-optics-backups`. Restore reviews the complete backed-up file, including unrelated settings that would change, and preserves the displaced current file. A background driver or utility can still write after the application's final revision check; close all configuration writers before applying.

## Supported fields

| Field | Accepted edit range | Meaning |
| --- | --- | --- |
| `eyeRotation` | -5 to 15 | Degrees per eye outward |
| `autoEyeRotation` | true / false | Driver reads the platform canting angle |
| `fovZoom` | 0.8 to 1.1 | Angular distortion factor |
| `flatFovZoom` | 0.7 to 1.15 | Tangent-based FOV factor |

Ranges follow the upstream UI contract, not measured headset comfort limits. Zoom fields change the driver's distortion mapping; they are different from application supersampling or crop. Maximum FOV, clamping, distortion zoom, calibration profiles and physical IPD are not written by this adapter.

## Native Pimax

Pimax native OpenXR optics writes are unsupported. Use Pimax Play for its Dream Air Wide FOV mode. The existence of that feature does not establish an external API, and legacy `fov_level` controls are not treated as Dream Air Wide FOV support. The Pimax build of the Sboys driver is a separate target from Pimax native OpenXR.

## Source contract

The adapter is based on [CustomHeadsetOpenVR at 4f258267](https://github.com/sboys3/CustomHeadsetOpenVR/tree/4f2582674a4500814425e460c5e40f3a06843e48): `ConfigLoader.cpp` defines the paths, `Config.h` the headset objects, the headset-settings Angular component the input ranges, and `RadialBezierDistortionProfile.cpp` the zoom semantics. [build.js](https://github.com/sboys3/CustomHeadsetOpenVR/blob/4f2582674a4500814425e460c5e40f3a06843e48/build.js) identifies `custom-headset-gui.exe` for the offline process check. No driver implementation or vendor optical profile is bundled.

Physical Dream Air/Dream Air SE alignment, visible FOV and current driver compatibility require hardware validation. Synthetic file tests validate preservation, review and recovery behavior only.
