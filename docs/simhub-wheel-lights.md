# Mirror your SimHub wheel lights

Use this optional observer to copy your physical wheel’s RPM, flags and button colors to its virtual counterpart. A dashboard URL alone needs only SimHub’s web server.

1. Close SimHub. Open the `simhub` folder beside the installed OpenGrid XR app.
2. Copy `oVRlay.SimHubLeds.dll` into the SimHub installation folder beside `SimHub.Plugins.dll`. Artura support requires observer version 1.2 or newer, included with this release.
3. Start SimHub and enable **oVRlay lights** in its plugin list. On its settings page, enable **Mirror my wheel lights in oVRlay**.
4. In SimHub Devices, connect and enable the physical wheel. Select the matching virtual model in OpenGrid and enable its SimHub lights. Use a SimHub LED test effect to check the result.

Hyper P1, Hyper SL, GXL V2, GT-MAX 32, FPE2 and Ascher Artura Ultimate have separate LED layouts. The observer reads the final RGB values after SimHub applies effects, mapping and brightness. Each wheel receives only its own colors. OpenGrid discards stale colors after one second.

The Artura model mirrors its 21 shift/flag lights and 14 illuminated button symbols. Its screen uses the dashboard URL selected in OpenGrid; the LED observer does not capture the physical display. SimHub identifies this wheel as **Ascher Racing Artura ULTIMATE**. Its physical mapper contains 63 channels, including unused slots, so the observer forwards the complete array without changing the existing GSI channel order.

The Artura mapping was checked against the installed SimHub device definition, physical mapper and button editor, then matched to the supplied CAD bodies. See `assets/wheels/ascher-artura.mapping.json` for the independently derived channel assignments. The integration has automated packet and renderer coverage; a connected Artura hardware comparison remains to be performed.

**If the lights stay off**, check the observer’s device status and close any other receiver using local UDP port 29543, including a running oVRlay lights receiver. The observer sends only to the same PC. To uninstall it, close SimHub and remove the observer DLL.

### Build the observer from source

Run `./scripts/build-simhub-lights.ps1` in Windows PowerShell. Use `-SimHubPath 'D:\SimHub'` for a custom installation, or `-TestOnly` for the packet tests without SimHub. The resulting DLL is in `.local/simhub-build`. SimHub’s proprietary assemblies are referenced from the local installation. After a SimHub SDK incompatibility, rebuild against the installed version.
