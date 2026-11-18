# Mirror your SimHub wheel lights

Use this optional observer to copy your physical wheel’s RPM, flags and button colors to its virtual counterpart. A dashboard URL alone needs only SimHub’s web server.

1. Close SimHub. Open the `simhub` folder beside the installed OpenGrid XR app.
2. Copy `oVRlay.SimHubLeds.dll` into the SimHub installation folder beside `SimHub.Plugins.dll`. An existing installation of the same oVRlay observer also works.
3. Start SimHub and enable **oVRlay lights** in its plugin list. On its settings page, enable **Mirror my GSI wheel lights in oVRlay**.
4. In SimHub Devices, connect and enable the physical wheel. Select the matching virtual model in OpenGrid and enable its SimHub lights. Use a SimHub LED test effect to check the result.

Hyper P1, Hyper SL, GXL V2, GT-MAX 32 and FPE2 have separate LED layouts. The observer reads the final RGB values after SimHub applies effects, mapping and brightness. Each wheel receives only its own colors. OpenGrid discards stale colors after one second.

**If the lights stay off**, check the observer’s device status and close any other receiver using local UDP port 29543, including a running oVRlay lights receiver. The observer sends only to the same PC. To uninstall it, close SimHub and remove the observer DLL.

### Build the observer from source

Run `./scripts/build-simhub-lights.ps1` in Windows PowerShell. Use `-SimHubPath 'D:\SimHub'` for a custom installation, or `-TestOnly` for the packet tests without SimHub. The resulting DLL is in `.local/simhub-build`. SimHub’s proprietary assemblies are referenced from the local installation. After a SimHub SDK incompatibility, rebuild against the installed version.
