# Changelog

## 1.2.1

- Added a **Changelog** button to update notifications and Application updates, opening the offered release's notes or the installed version's notes when no update is offered.
- Added an optional virtual driver for all six built-in wheels: GSI Hyper P1, Hyper SL, GXL V2, GT-MAX 32, FPE2 and Ascher Artura Ultimate.
- Added independently selectable glove and suit colors, textured sleeves, steering and regripping poses, and optional torso and legs. Enable **Driver (experimental)** in Spatial overlays after selecting a 3D wheel.
- Driver assets load in the background and share cached resources across the GSI models. Higher-resolution driver targets are allocated only when needed and released when no driver uses them.

**Highly experimental:** anatomy is still rather questionable! Hand contact, elbows and regripping need more work, and clipping can occur. This is an animated seated driver, not body or hand tracking. Live paddle-shift animation is not included. The feature starts off; compare your frametimes before keeping it enabled.

Thanks to **Valve**, **Elvaerwyn** and the **MakeHuman community** for the source assets. See [driver credits and licenses](assets/drivers/NOTICE.md) and the [setup guide](docs/driver-test.md).

## 1.2.0

- Added the Ascher Artura 3D steering wheel, with thanks to Martin Ascher for providing the model.
- Added a fitted SimHub display and mapped shift lights and button illumination for the Artura Ultimate.
- Added **Install SimHub bridge** in Setup, with automatic detection, folder selection, updates and a backup of the previous plugin.
- Fixed duplicated 3D wheels in stereo Pimax OpenXR sessions using the stereo projection approach contributed by [Rak3tenmann](https://github.com/DSuzuki35/OpenGridXR/pull/1). Four-view sessions retain the existing submission path.
- Added an explicit **3D model without occlusion** option, allowing each wheel to skip cockpit-depth processing while other overlays keep their own occlusion settings.

## 1.1.5

Initial release in the restarted public repository.

OpenXR tools for iRacing and Le Mans Ultimate: headset crop and sharpening, VR color presets, spatial dashboards and wheels, LMU QVFR / VRS, and stabilized Clean Output.

### Included improvements

- Removed the custom contour editor and disabled legacy visibility masks. Rectangular crop remains available.
- Added a link to Williem3’s Sim-QVFR in LMU QVFR & VRS, as an alternative that may work better on some systems.

- Removed VectorXR frame generation and its shortcuts. Existing profiles still load.
- Spatial overlay occlusion now explains the CPU frame-time cost and how to compare it in LMU.
- Renamed the rendering page to **LMU QVFR & VRS**. LMU MVR backs off when a focus resolve is missing and retries per eye. Recovery is informed by [Williem3’s Sim-QVFR](https://github.com/Williem3/Sim-QVFR).
- Clean Output camera tuning adds independent horizontal and vertical smoothing, Motion and No Motion presets, and the camera tutorial from [steadyXR 1.0.2](https://github.com/DSuzuki35/steadyXR/releases/tag/v1.0.2).

- Clean Output can wait, become transparent, close, or show a selected monitor when XR frames stop. An optional cockpit gate uses player telemetry to switch to the monitor in menus. Desktop Duplication runs only while the monitor clone is visible.

### Credits

Thanks to Markus Stahl for review fixes and to the upstream authors whose work is adapted or bundled here. See [Credits](docs/credits.md) and [Third-party notices](THIRD-PARTY-NOTICES.md) for attribution and licenses.
