# OpenXR layers

Open **OpenXR layers** to inspect Windows OpenXR layer registrations for the current user (HKCU) and all users (HKLM). Machine-wide registrations have separate 64-bit and 32-bit registry views. Current-user registrations share one key across both views and are shown once as shared. Refresh reads existing registrations; opening the page does not change them.

**Enabled** means the registration's DWORD value is zero. A nonzero DWORD disables that registration. This does not prove the layer was loaded by a running application: the loader, application architecture, manifest conditions, and other activation settings still apply. Explicit layers must also be requested by the application. The editor does not set implicit-layer ordering or select an OpenXR runtime.

Close iRacing, Le Mans Ultimate, and Clean Output before editing. Enable or disable one existing DWORD registration at a time. Missing or unreadable manifests and missing libraries are reported separately from registration status. Other registry value types and unsupported manifest paths are shown read-only; entries are never created or deleted.

Current-user changes normally need no administrator rights. To edit all-user registrations, enable **Allow administrator changes** on this page. Each change or restore opens its own Windows administrator prompt. OpenGrid XR itself stays unelevated. Cancelling the prompt does not confirm a change.

Before each change, OpenGrid XR saves the exact original DWORD and target to `layer-backups` in its data folder. Restore works only while the registration still has the value written by that change; if another tool changes or removes it, refresh and review it instead. Backups survive restarts. If a process was interrupted, a pending backup may be offered for restore only when its recorded post-change value matches the current DWORD. A restore cannot detect an external change that later returns to that same value.

The fixed-purpose `OpenGridXR-Layers.exe` helper accepts only the current-user/all-user scope, 32/64-bit view, implicit/explicit kind, an absolute local JSON manifest path, and two unsigned DWORD values. It opens only the corresponding Khronos OpenXR 1 ApiLayers key, requires an existing DWORD, compares the expected value, writes transactionally, and reads it back. The desktop service separately reads the inventory again before reporting success. It never executes a manifest's library.

Layer metadata and backup files stay on this computer. No registry change, elevation, game launch, or library load is performed by the automated editor tests.
