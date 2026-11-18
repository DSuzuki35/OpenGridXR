# Changelog

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
