# SimHub virtual wheel lights

Bridge version **1.3 or newer** can run SimHub LED effects without owning or connecting a physical wheel. Supported models are Hyper P1, Hyper SL, GXL V2, GT-MAX 32, FPE2 and Ascher Artura Ultimate. Update the OpenGrid XR Lights bridge before using these controls; version 1.2 only mirrors connected hardware.

## Set up a wheel

1. Install/update `oVRlay.SimHubLeds.dll` using OpenGrid's SimHub bridge setup and restart SimHub after updating the DLL. Enable the **OpenGrid XR Lights** plugin and SimHub **Devices**.
2. Open **OpenGrid XR Lights** in SimHub. Leave **Mirror my wheel lights in OpenGrid XR** checked.
3. Change the desired model from **Connected wheel** to **Virtual wheel**, then click **Add virtual device** in that row. You can also add **OpenGrid XR virtual [model]** from SimHub Devices under the **OpenGrid XR** brand. No USB detection or earlier hardware connection is needed.
4. In **Devices**, select and enable the new virtual device. Edit its **LEDs** effects, button colors, individual LEDs and brightness normally. It starts with the matching stock SimHub LED profiles when those defaults are installed. Keep the device enabled and unsuspended.
5. Select the same wheel model and SimHub light source in OpenGrid. Start a game, replay telemetry or use SimHub's effects preview/test tools. Some effects intentionally stay dark outside a running game.

Each model needs its own virtual device. The bridge does not borrow colors from another wheel. A one-second discovery delay after adding/enabling a device is normal. The status line marks a source as **virtual** or **connected** and reports live output.

## Physical and virtual settings

**Connected wheel** is the default for every model, including upgrades from older bridge settings. It observes the matching physical device exactly as before. **Virtual wheel** selects that model's separate virtual identity; plugging in physical hardware does not change the selected source.

Virtual profiles and brightness are saved in SimHub's regular device settings and remain independent of physical profiles. Switching back to Connected wheel, disabling mirroring or stopping the plugin disconnects virtual output. Removing/disabling a virtual device stops its output. Re-enabling it resumes the saved effects. Physical managers are never replaced, so there is no original hardware driver to restore or physical setting to undo.

Virtual mode does not disable a separately configured physical SimHub device: that physical device can continue driving its own LEDs through SimHub. The virtual device itself has no USB transport, detection descriptor or physical driver. It only receives computed effects and forwards bounded RGB frames over the existing localhost bridge.

## Compatibility and troubleshooting

- **No virtual devices in Add Device:** verify bridge version 1.3+, enable the plugin and restart SimHub. Keep the DLL filename unchanged.
- **Waiting for virtual LED output:** add the matching **OpenGrid XR virtual** device, enable it in Devices, select Virtual wheel in the plugin and enable mirroring.
- **Connected but dark:** inspect the active effect profile, game requirements and brightness. Missing stock defaults leave normal editable SimHub effects available; configure or import a profile in the virtual device.
- **Incompatible API:** rebuild against the actual installed SimHub runtime. The bridge checks the LED API and supports the six-channel display callback, including raw and external overrides. It does not alter licensing checks or unlock paid SimHub features.

The registry uses SimHub's public `IDeviceDescriptorsRegistry` and a `LedModuleSettings` subclass with its protected `DeviceDriver` setter. It avoids reflection-based takeover of physical managers. Physical maps use SimHub's mapper primitives and the verified model-specific LED layout; group brightness and physical overrides are applied by the same mapper used for connected-mode forwarding.

## Build and verification

From the repository root on Windows:

```powershell
./scripts/build-simhub-lights.ps1 -OutputDirectory .local/simhub-build
```

This compiles against the installed SimHub runtime, runs packet tests and an isolated SDK harness, and records reference hashes in the build manifest. It neither copies files into SimHub nor restarts the application. `-TestOnly` runs only the hardware-independent packet tests; use the full command for virtual-device validation.

The SDK harness exercises all six device factories and real stock settings loading, saved effect/brightness round trips, model-specific physical mapping, brightness, raw/external overrides, independent modes, close/reconnect/dispose, effect exceptions and overlapping callbacks. Its idle game-service double avoids starting a game or hardware transport. Test-generated settings stay in the build output directory. Live SimHub UI interaction and physical USB hotplug remain separate manual checks.

Development reference: [AZOM's SimHub API notes](https://github.com/giantorth/AZOM/blob/main/docs/simhub.md#ileddevicemanager-virtual-driver-injection) helped identify the LED-manager integration point. The implementation is original and uses separate device identities rather than the documented reflective driver replacement. Installed runtime signatures and mappings were checked locally; SimHub assemblies and decompiled sources are not redistributed.
