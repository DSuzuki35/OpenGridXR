# Runtime optics research

Verified on 2026-10-25. This document describes integration options; OpenGrid XR does not currently write these driver settings. No vendor DLL was loaded, runtime changed, driver installed, or live configuration modified during this research.

## Decision for this build

The requested Wide FOV, Eye Rotation, FOV Zoom and Flat FOV Zoom controls do not share a verified interface across Sboys SteamVR and Pimax native OpenXR. Keep the current build's render FOV, crop and stream framing distinct from headset lens calibration. A control that only expands an application's render frustum cannot promise the same visible result as changing the compositor's distortion profile.

Sboys has an inspectable configuration contract. Pimax Play has a real Dream Air Wide FOV feature, but its current writable interface and equivalents for the three numeric controls were not established from the public sources below. No incomplete desktop adapter or inactive UI controls were added for this release.

## Sboys SteamVR driver

Reference: [CustomHeadsetOpenVR at 4f2582674a4500814425e460c5e40f3a06843e48](https://github.com/sboys3/CustomHeadsetOpenVR/tree/4f2582674a4500814425e460c5e40f3a06843e48). The read-only checkout is `.cache/sboys3-reference`; it is not bundled. The repository's [LICENCE.md](https://github.com/sboys3/CustomHeadsetOpenVR/blob/4f2582674a4500814425e460c5e40f3a06843e48/LICENCE.md) contains GPL version 2. This research copies no driver implementation, optical calibration profile, binary, or protected component into OpenGrid XR.

`ConfigLoader::GetConfigFolder()` in [ConfigLoader.cpp](https://github.com/sboys3/CustomHeadsetOpenVR/blob/4f2582674a4500814425e460c5e40f3a06843e48/CustomHeadsetOpenVR/src/Config/ConfigLoader.cpp#L20) selects two different Windows locations:

- Neutral build: `%APPDATA%/CustomHeadset/settings.json`.
- `VENDOR_PIMAX` build: `%APPDATA%/Pimax/CustomHeadset/settings.json`.

The top-level objects are `dreamAir` and `dreamAirSE`. Do not infer which installation is active from one file's presence. If both files exist, require an explicit target, show the full path and build variant, and keep inactive settings distinct from the active runtime.

Defaults come from `BaseHeadsetConfig`, `DreamAirConfig` and `DreamAirSEConfig` in [Config.h](https://github.com/sboys3/CustomHeadsetOpenVR/blob/4f2582674a4500814425e460c5e40f3a06843e48/CustomHeadsetOpenVR/src/Config/Config.h#L194). Slider limits come from [headset-settings.component.html](https://github.com/sboys3/CustomHeadsetOpenVR/blob/4f2582674a4500814425e460c5e40f3a06843e48/CustomHeadsetGUI/src/app/pages/devices/headset-settings/headset-settings.component.html).

| JSON field | Dream Air default | Dream Air SE default | Upstream UI range / meaning |
| --- | --- | --- | --- |
| `maxFovX` | 98 | 90 | Per-eye horizontal degrees; slider minimum 1, maximum is dynamic |
| `maxFovY` | 88 | 90 | Per-eye vertical degrees; slider minimum 1, maximum is dynamic |
| `fovClamping` | false | true | Clamp to the display-fitting distortion field |
| `eyeRotation` | 2 | 3 | Degrees per eye outward; slider -5 to 15, step 0.1 |
| `autoEyeRotation` | false | false | Read the platform's canting angle instead of a fixed value |
| `fovZoom` | 1 | 1 | Slider 0.8 to 1.1, step 0.005 |
| `flatFovZoom` | 1 | 1 | Slider 0.7 to 1.15, step 0.005 |
| `distortionZoom` | 1 | 1 | Separate calibration adjustment; slider 0.9 to 1.1, step 0.002 |

These are slider ranges, not verified safety limits or backend clamps. The numeric text fields lack equivalent min/max restrictions, and `parseBaseHeadsetConfig()` only checks numeric/boolean types. A future adapter must validate finite numbers itself. Do not present zero zoom, arbitrary field values or generic presets as supported just because JSON parsing accepts them.

`sliderMaxFovX/Y()` in [headset-settings.component.ts](https://github.com/sboys3/CustomHeadsetOpenVR/blob/4f2582674a4500814425e460c5e40f3a06843e48/CustomHeadsetGUI/src/app/pages/devices/headset-settings/headset-settings.component.ts#L175) uses the matching headset's diagnostic `resolution.fovMaxX/Y`, rounded up, with device defaults as fallback. `BaseHeadsetShim::UpdateSettings()` in [BaseHeadset.cpp](https://github.com/sboys3/CustomHeadsetOpenVR/blob/4f2582674a4500814425e460c5e40f3a06843e48/CustomHeadsetOpenVR/src/Headsets/BaseHeadset.cpp#L630) computes those maxima from a temporary display-clamped profile; with clamping disabled it multiplies the result by 1.1 as an approximation. They are not universal physical headset limits.

The actual operations are different:

- [RadialBezierDistortionProfile.cpp](https://github.com/sboys3/CustomHeadsetOpenVR/blob/4f2582674a4500814425e460c5e40f3a06843e48/CustomHeadsetOpenVR/src/Distortion/RadialBezierDistortionProfile.cpp#L349) first applies tangent-based angular scaling for `flatFovZoom`, then divides the radial profile's angles by `fovZoom`. Both modify compositor distortion calibration. Lower values expand the angular mapping; this is not merely supersampling.
- The same implementation uses the requested per-eye maxima directly when `fovClamping` is false. With clamping true it limits them to the field fitting all color channels. There is no separate `wideFov` boolean in this inspected contract.
- [BaseHeadset.cpp](https://github.com/sboys3/CustomHeadsetOpenVR/blob/4f2582674a4500814425e460c5e40f3a06843e48/CustomHeadsetOpenVR/src/Headsets/BaseHeadset.cpp#L680) combines `eyeRotation` with the selected distortion profile's `eyeRotationOffset` when updating eye transforms. Changing profile can therefore change effective eye rotation without changing the settings value. `autoEyeRotation` is resolved by [PimaxCommon.cpp](https://github.com/sboys3/CustomHeadsetOpenVR/blob/4f2582674a4500814425e460c5e40f3a06843e48/CustomHeadsetOpenVR/src/Helpers/PimaxCommon.cpp#L635); parallel projection with the builtin Pimax profile is a special zero-rotation case.
- `distortionZoom` scales distortion UV coordinates in `BaseHeadset.cpp`; it is separate from both FOV zoom controls and should not be silently changed with them.

The driver watches settings-file changes, updates projection and recommended render size, and sends lens-distortion change events. Its own FOV UI notes that a game restart can still be necessary. A future OpenGrid XR adapter should use explicit offline apply while the game, SteamVR and driver GUI are closed, followed by restart and observed-runtime verification. Successful persistence alone is not an active-runtime confirmation.

## Pimax native OpenXR

Pimax's [August 7, 2026 release notes](https://www.pimax.com/explore/blogs/progress-update-august-7th) explicitly add Dream Air and Crystal Light Wide FOV in Pimax Play 2.0.3. This establishes that the feature exists, not its external API. Pimax's [Dream Air FOV explanation](https://eu.pimax.com/blogs/blogs/about-the-fov-on-the-dream-air) describes a tradeoff with binocular overlap and lens positioning; it does not publish a settings key or SDK command. Keep this vendor feature separate from the Sboys controls.

Public code inspected:

- [mbucchia/PVR at 95c3f84a6ab6d1d7a3858b1dca6f5223c06e91d1](https://github.com/mbucchia/PVR/tree/95c3f84a6ab6d1d7a3858b1dca6f5223c06e91d1), local `.cache/pvr-optics-reference`. The repository describes itself as the official unmodified Pimax SDK. `PVR_API.h` exposes `pvr_getEyeRenderInfo()` and generic `pvr_get/setFloatConfig()`, `pvr_get/setIntConfig()`, vector/quaternion/string accessors. `PVR_Interface.h` contains their function table. The inspected headers do not establish current Dream Air Wide FOV, FOV Zoom or Flat FOV Zoom keys, units, supported ranges, transactions or restart semantics.
- [Pimax-OpenXR at fa6b97000b580959180346d10ae6dad25472bc2d](https://github.com/mbucchia/Pimax-OpenXR/tree/fa6b97000b580959180346d10ae6dad25472bc2d), local `.cache/pimaxxr-optics-reference`. This is the separate community PimaxXR runtime, not the current Pimax Play native runtime. `pimax-openxr/system.cpp` and `companion.cpp` read `fov_level`; `companion/MainForm.cs` maps it to the legacy Large/Normal/Small/Potato labels. That is not evidence for a Dream Air Wide FOV mapping. `view_rotation_fix` and `steamvr_use_native_fov` manage parallel projection, not the requested freely adjustable eye rotation.
- Do not reuse the community runtime's `getRuntimeStatus()` as a read-only probe: its implementation calls `pvr_setIntConfig(..., "view_rotation_fix", ...)` while querying status.

Pimax-OpenXR has an MIT license. The inspected PVR snapshot has no repository license file and several headers state all rights reserved. It also includes vendor libraries. Public visibility does not establish permission to redistribute those files. Nothing from that SDK checkout is bundled or linked here.

There is no verified mapping from Sboys `fovZoom`, `flatFovZoom` or `eyeRotation` to current Pimax native runtime controls in the inspected sources. Generic OpenXR view poses and FOV describe application rendering; they do not constitute a vendor lens-profile write API. An application-side image warp would be a different feature, requiring its own projection/submission, stereo, visibility-mask, overlay and optical validation. It must not be labelled as the same native calibration control.

## Concrete next implementation

1. Obtain a documented current Pimax API/IPC or a vendor-supported configuration schema for Dream Air Wide FOV and any exposed numeric calibration controls. Establish device/build eligibility, readback, restart rules, units and preservation/restore semantics. Until then, use Pimax Play for that vendor mode and show the integration as unsupported rather than inventing keys.
2. Implement the Sboys adapter independently without copying the driver: discover both known paths, select only `dreamAir` or `dreamAirSE`, bound file size and JSON structure, reject duplicate/ambiguous object members, preserve all unrelated settings, and permit only the eight fields above. Use finite numeric validation based on documented UI ranges, with dynamic FOV maxima clearly treated as diagnostic estimates. Display any pre-existing out-of-range values without silently normalizing them.
3. Prepare an exact field diff and hash of original bytes. Offline Apply must re-read and reject a changed hash, create a byte-exact backup, write a sibling temporary file and atomically replace the target. Restore must also check the current review hash and preserve the displaced current file. Do not write on slider movement, switch runtimes, install components, or change physical IPD implicitly.
4. Wire detection, review, apply and restore into one complete UI/IPC path. Keep selected configuration, saved configuration and observed active runtime separate. Test both path variants, invalid JSON, oversized input, wrong target object, non-finite/range errors, unrelated-field preservation, stale review, write failure, backup and restore. The currently shipped build has none of this new adapter wired.
5. Validate on the game PC with each actual driver/runtime and headset variant: record reported per-eye FOV and eye poses before/after a deliberate setting change, test startup/restart, inspect stereo alignment and distortion, and check crop, sharpening, raw stream capture and overlay placement. Never infer visible FOV or comfort from numeric configuration alone.

This research used source inspection and vendor documentation. Hardware optics, physical binocular overlap, game compatibility and native Pimax write behavior remain unvalidated.
