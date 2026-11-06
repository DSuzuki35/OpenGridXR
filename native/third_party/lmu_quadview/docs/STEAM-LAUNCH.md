# Steam startup

## Automatic OpenXR startup (0.3.1)

Save settings in the extracted package, close LMU, then select **Settings >
Install / update automatic startup**. Approve the administrator prompt. Remove
the old launcher wrapper from Steam's custom Launch Options, retaining unrelated
arguments. Start LMU using Steam's **OpenXR** launch mode. No custom launch option,
service or permanently running launcher is needed. OpenVR and flat-screen starts
do not use this OpenXR layer.

Settings apply at the next game launch. The benchmark/joystick controller attaches
minimized when benchmarking is enabled; restore it from the taskbar to use its
controls. It closes after LMU exits. Keep the package in its current location,
because it holds settings, helper and logs. Reinstall after moving or updating it.
Do not run LMU as administrator; elevated game processes deliberately pass through.

The installer verifies timestamped publisher signatures, copies `lmu_autostart.dll`
and `lmu_quadview.dll` into a versioned folder under Program Files, then registers
the bootstrap first in the 64-bit HKLM OpenXR implicit-layer list. Other entries
retain their data, enabled/disabled state and relative order. Installation checks
the result and restores the original values on failure. The prior values are also
backed up under `HKLM\SOFTWARE\LMUQuadView\LayerOrderBackup`. Only the installing
user is activated, via `%LOCALAPPDATA%\LMUQuadView\autoload.ini`. Settings updates
this Unicode file atomically and removes it when automatic startup is disabled.
`HKCU\Software\LMUQuadView\AutoStart\PackageRoot` remains a compatibility fallback;
some protected game launches cannot see it even though the launcher can.

Registration does not rename registry keys. It verifies a persistent backup,
recreates the childless layer-list key with its original owner, group and access
rules, then verifies the new values and order. Unexpected subkeys are refused.
If rollback itself fails, the error identifies the retained sibling backup;
do not delete it. This avoids the registry-rename path associated with the local
`0x3B` crashes. Close other OpenXR installers while updating registration.
Conflicting value changes are copied to a separate `Implicit-LMU-Conflict-*`
key before rollback; the error reports its location. If Windows will not allow
preserving the original key owner/permissions, installation stops before replacing
the original list. The legacy Steam wrapper remains available in that case.

The installer temporarily enables Windows' restore privilege on its registration
thread to preserve an original SYSTEM owner when creating the backup and replacement
key. It restores the previous thread token afterwards. Taking ownership in Registry
Editor or granting Users full control is unnecessary for this owner-copy failure.
An ACL or system policy that denies registry writes can still prevent installation;
the error includes the underlying Windows message. Parent-key permissions are unchanged.

The bootstrap forwards non-LMU instances unchanged. For the configured LMU it
validates the configured executable path, package/installed DLL match, saved settings
and layer order before creating the adapter/provider chain. Quad sessions omit
OpenGridXR to avoid the known crop mismatch; VRS preserves it. QVFR retains the
VectorXR OSD layer. Registry entries are not disabled for other games. Existing
explicit launcher sessions disable the bootstrap to prevent a duplicate adapter.

Missing/moved packages or preparation errors stop OpenXR initialization with an
error instead of silently claiming that the mod is active. Helper details are in
`logs/autoload-error.txt`. If another installer changes layer order, reinstall
automatic startup here. A game update still needs validation of the supported
build. The adapter also installs LMU-specific native render hooks; this is not
merely an image postprocessing layer.

If no normal mod log appears, inspect
`%LOCALAPPDATA%\LMUQuadView\autoload-<game-pid>.log`. It records loader entry,
activation source, intentional skip reasons and helper/instance failures before
the normal logger is configured.

**Settings > Remove automatic startup** removes only our registered manifest and the
current user's activation. Protected DLL files are retained so already-running
OpenXR processes can unload them safely. Direct launcher startup remains available.
Steam settings and game files are never rewritten by the installer.

## Optional legacy Steam wrapper

The 0.3.0 wrapper remains supported without automatic installation:

```text
"C:\Your mod folder\LMU-QuadView-Launcher.exe" --steam %command%
```

Use the actual launcher path in Steam's Launch Options. Additional arguments go
after `%command%`. The wrapper checks the forwarded executable, preserves Windows
argument quoting, adds `+XR` when absent, and applies the same settings. Remove
this wrapper when switching to automatic installation. Direct launcher startup
does not require administrator rights.

References: [OpenXR loader specification](https://registry.khronos.org/OpenXR/specs/1.1/loader.html),
[Steam Launch Options](https://help.steampowered.com/en/faqs/view/7D01-D2DD-D75E-2955).
Automated chain/passthrough/rollback tests do not substitute for a live headset test.
