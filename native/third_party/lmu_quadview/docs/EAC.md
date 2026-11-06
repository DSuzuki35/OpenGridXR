# Easy Anti-Cheat and file integrity

Release binaries are publisher-signed and timestamped. Easy Anti-Cheat can still
reject a signed DLL. A valid signature identifies the publisher and checks file
integrity; it does not grant anti-cheat approval. No anti-cheat compatibility or
ban-safety guarantee is claimed.

If an error names `lmu_autostart.dll`, run the read-only diagnostic from the
affected package in PowerShell, using the same Windows account that installed
automatic startup:

```powershell
powershell.exe -NoProfile -File .\tools\check-autostart-integrity.ps1
```

It reports package and installed DLL SHA256 hashes, signature status, publisher
and timestamp thumbprints, and launcher/configuration approval hashes. The
launcher and stereo check exist in the package; automatic startup installs only
the two DLLs into Program Files. It reads current-user activation and the 64-bit
OpenXR registration. It requires no administrator rights, changes no registry
settings and does not launch LMU or sign files.

Use `-OutputPath 'C:\chosen\integrity-report.json'` to save a report, or redirect
standard output. The report includes local paths; review them before sharing.
`-PackageRoot` selects another extracted package. `-InstalledDirectory` selects
an explicit folder for manual inspection and skips current-user/registry lookup.

Exit codes: **0** signatures and hashes match; **1** missing, invalid or mismatched
files/settings, or a read error; **2** no enabled automatic-start installation
found. Exit 0 does not establish EAC acceptance.

If validation fails, restore the complete official release package and use
**Settings > Install / update automatic startup** after closing LMU. Unsigned
development builds are intended for supported offline testing. If validation
passes and EAC still rejects the DLL, send the error and integrity report to the
game's support team for compatibility review. Use supported offline launch modes
where available; this diagnostic provides no anti-cheat bypass.
