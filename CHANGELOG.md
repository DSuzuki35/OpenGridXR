# Changelog

## Unreleased (rig-edit-mode)

- Added a RaceLab-style edit mode for spatial overlays, used from the headset. With overlays showing in the game:
  **Tab** starts editing (the selected overlay gets a blue frame and stays bright, the others dim), **Space** selects the next overlay,
  the **mouse** moves it left, right, up and down, and the **wheel** or a **right-button drag** moves it nearer or farther.
  **M** switches to rotating (orange frame): the mouse turns and tilts, a right-button drag leans, and the wheel resizes. Hold **Shift**
  for fine steps. **Esc** (or Tab) finishes: then **Enter** saves and **Esc** cancels and puts everything back. Beeps confirm
  each step. While editing, those keys, the mouse and clicks don't reach the game; Alt+Tab always works. A car that already
  has its own saved placement keeps it per car. The new OpenGridXR-EditInput helper only runs while overlays are showing.

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
