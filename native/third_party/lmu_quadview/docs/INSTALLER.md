# Native Windows installer

The setup EXE uses NSIS Unicode and the same complete portable payload as the ZIP. There is no Electron runtime. A new installation defaults to `%LOCALAPPDATA%\Programs\LMU Quad View`, creates Start menu shortcuts and an Add/Remove Programs entry, and offers a desktop shortcut. Installation needs no administrator elevation and does not register or modify OpenXR layers.

Build from a fresh, complete directory emitted by `package.ps1`. The repository `VERSION`, staged `VERSION`, core binary metadata and SHA256 manifest must agree. Every staged file must be accounted for; settings, profiles, logs, runtime data and directory links are rejected as installer inputs.

```powershell
# Release payloads must already have valid timestamped Authenticode signatures
# from the same signer. This is also the default if no signature switch is given.
./tools/build-installer.ps1 -PackageDirectory 'C:\release\package-stage' -OutputDirectory 'C:\release\setup' -RequireSigned

# Explicit local testing only; do not distribute this output.
./tools/build-installer.ps1 -PackageDirectory 'C:\test\package-stage' -OutputDirectory 'C:\test\setup' -AllowUnsigned
```

The output is `LMU-Quad-View-Mod-<VERSION>-Setup-x64.exe`. Sign and timestamp that final EXE using the existing release signing workflow, then verify its signature before publication. NSIS is discovered on `PATH` or in the existing electron-builder cache; alternatively pass `-MakeNsisPath 'C:\tools\NSIS\Bin\makensis.exe'`. The build installs no system tools and does not download dependencies. Generated `installer-build-*` directories retain the exact install/uninstall commands and `installer-files.txt` ownership list for review.

Upgrades overwrite only the shipped files. They preserve `launcher-settings.xml`, `profiles`, `logs`, `runtime` and unknown files. The current uninstaller contains explicit package paths compiled from the checked portable manifest; it never reads deletion instructions from the writable installed manifest, never uses recursive deletion or wildcards, and leaves nonempty directories alone. Files from older versions that no longer ship are retained rather than guessing their ownership. Reparse points in package paths or their parents stop installation/removal before package mutations.

The installer and uninstaller refuse to proceed while Le Mans Ultimate or any LMU Quad View launcher is running. Updater handoff uses `/WAITPID=<launcherPid>` to wait up to 15 seconds for that launcher to exit before the ordinary process checks; no process is killed. Standard NSIS `/D=<absolute package root>` as the last argument preserves an existing portable package location, including paths with spaces. Existing installer registrations supply the upgrade default location. This remains a per-user installation; a protected folder must be writable by that user.

Automatic startup remains an explicit Settings action with Windows administrator consent. After an upgrade, reinstall automatic startup from Settings to approve the updated signed binaries. Uninstall blocks when the current user's `AutoStart\PackageRoot` registry value or `autoload.ini` points to this package; open the launcher, remove automatic startup in Settings, close it, and retry. The uninstaller does not call the global autoload removal command, so it cannot silently disable an activation owned by another portable root. It preserves the protected layer files and all OpenXR registry state. If another installed root owns the Add/Remove Programs entry, removing an older root also preserves the shared shortcuts and that entry.

Run the fixture checks with an existing NSIS compiler and a full 7-Zip executable that supports NSIS extraction:

```powershell
./tools/test-installer.ps1 -SevenZipPath 'C:\Program Files\7-Zip\7z.exe'
```

These tests compile to paths containing spaces, check version metadata, unpack and hash every payload file, reject unsigned release inputs and damaged/unsafe manifests, inspect explicit uninstall commands, and execute blocked installer/uninstaller paths with disposable simulated launcher/game processes. They never complete an installation. An interactive installation, upgrade, UAC autoload removal and uninstall smoke test should still be performed in a disposable Windows account before publishing a release.
