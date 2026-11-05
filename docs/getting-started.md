# Getting started

## Connect your first profile

1. Install the Windows x64 setup package from [GitHub Releases](https://github.com/DSuzuki35/OpenGridXR/releases/latest).
2. Select **iRacing** or **Le Mans Ultimate**, then a profile. Use neutral crop/render scale for the first connection and **Save profile**.
3. Close both sims and Clean Output. Open **Setup → Install/update runtime layer** and accept the Windows administrator prompt.
4. Launch the game through OpenXR. Refresh Setup status and look for an actual connected session.
5. Add one feature at a time using the [guides](../README.md#guides). Save and restart the sim for startup/render changes; close and reopen Clean Output for its framing/output changes.

Registration alone does not prove the game loaded the layer. Native OpenVR does not load it. Start with one crop mechanism when checking a setup: other layers' FOV/visibility changes can interact.

**Setup → Install/update runtime layer** registers OpenGrid's own system-wide 64-bit OpenXR entry and keeps executable runtime files in protected Program Files. Installing the app alone does not complete this step. Runtime setup does not switch the active runtime or remove other tools. **Repair runtime connection** verifies the system-wide entry before removing an older owned user-only entry. If you change the installed QVFR provider, update the runtime connection when Setup requests it.

## Choose features and profiles

In **Setup → Modules**, choose **Core only**, **Driving essentials** or **All modules**. Save/discard profile edits before applying. Module availability and each profile's feature enable switch both matter. Disabling a module keeps saved preferences and bindings; re-enabling restores them. Restart the sim after changing modules. [Module behavior →](features-reference.md#optional-modules)

0.5.0 replaces the global Driving/Streaming mode with **Stream → Keep full render area for stream**. Old Streaming profiles migrate with it on; old Driving profiles migrate with it off. New profiles start with it off. Explicit new settings take precedence on import. Name separate profiles for your preferred configurations. [Render-area behavior →](streaming.md#choose-how-much-the-game-renders)

## Update safely

Installed copies check stable GitHub releases at startup and every six hours. Choose **Download update**, **Later** or **Skip this version**. Downloading does not install automatically. Save profiles and close both sims and Clean Output before **Install and restart**. Then use **Setup → Update runtime layer** for the separately registered native files and restart the sim.

**Setup → Check for updates** also checks skipped versions. Source/unpacked builds do not self-update. Build instructions are in [Contributing](../CONTRIBUTING.md); keep the entire unpacked application directory together. CI/development builds are unsigned unless signing is configured externally.

## Recover a profile or remove the connection

Use **Setup → Open data folder** for profiles and exact-file backups. The last saved profile for each game is used for its next session. Imported profiles stay drafts until saved. Exports include contour and local settings, without importing executable code or external asset paths.

Stale saves are rejected. A damaged profile is preserved and a valid backup can be restored; restore also preserves the displaced file. Runtime status/support data lives under `%LOCALAPPDATA%/OpenGridXR/runtime`; installed executable layer files live in Program Files.

**Setup → Remove layer** removes only OpenGrid's owned registrations, including disabled entries, and preserves profiles/files for recovery. System-wide removal requests Windows administrator approval.

## Test LMU offline

After saving and updating the runtime, choose **Setup → Test offline in LMU** and select `Le Mans Ultimate.exe` or `LeMansUltimate.exe`. OpenGrid starts it directly in its game folder with `+XR`; keep Steam and the VR runtime running. Canceling the picker starts nothing, and an already-running sim blocks the test.

This follows the [official no-EAC procedure](https://guide.lemansultimate.com/hc/en-gb/articles/14590657733903-How-do-I-run-the-game-without-Easy-Anti-Cheat-EAC) and [OpenXR launch notes](https://guide.lemansultimate.com/hc/en-gb/articles/17030344145935-V1-4-0-2-Update-V1-4-Hotfix-2). Use it for offline Race Weekend tests. It does not change game files or uninstall EAC. Close LMU and start normally through Steam with EAC for online racing. Loading still depends on the runtime; offline success does not establish protected/online acceptance. [LMU providers and validation →](lmu-rendering.md)
