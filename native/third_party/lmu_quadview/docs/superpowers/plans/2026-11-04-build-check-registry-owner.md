# Executable hash and registry owner fixes

> Execute inline using superpowers:executing-plans; user explicitly requested removal after compatibility caveat and repair of the reported backup failure.

**Goal:** Permit other LMU executable hashes and preserve SYSTEM ownership during automatic-start installation.

**Design:** Remove launcher and DLL full-file hash gates; retain executable identity and native code/resource checks; record the actual executable hash. Enable SeRestorePrivilege on a duplicated thread token only around registry registration and restore the previous impersonation context afterwards. Keep verified backup/recreation/rollback and the prohibition on registry rename. Do not change ApiLayers ownership or grant Users additional access. Genuine write-denying ACLs remain a separate error.

**Alternatives:** Broad ownership/DACL changes would persistently alter runtime permissions. In-place reinsertion does not reliably reorder registry enumeration slots. Scoped restore privilege addresses the failing owner-copy operation directly.

**Tech:** .NET Framework C#, Windows token APIs, native C++/MSVC.

- [x] Launcher regression: different executable bytes accepted, missing file rejected, actual SHA256 recorded. Observe failure then remove fixed-hash gate.
- [x] Native regression: differently hashed process named Le Mans Ultimate.exe passes identity check; foreign name rejected. Retain hook-anchor tests.
- [x] Registry regression: temporary SYSTEM-owned parent/Implicit with admin write permissions; preserve values/order/owner/group/DACL, rollback and thread token. Run elevated fixture separately; never edit production OpenXR keys.
- [x] Scope restore privilege around RegisterFirst; preserve actionable backup errors including underlying Windows error.
- [x] Run launcher/native suites, independent review, signed test package and current documentation updates. No new GitHub release requested.

Limits: no alpha executable supplied; no claim of alpha rendering compatibility. Remote tester's exact ACL unknown.

## Verification

- `mod/build/hash-check-red.log`: launcher regression rejected the pinned diagnostic hash instead of the actual file hash. Final full launcher suite passed in `mod/build/owner-final-launcher.log`.
- `mod/build/registry-owner-red.log`: temporary SYSTEM-owned tree reproduced the exact backup refusal with Windows error "This security ID may not be assigned as the owner of this object." Before applying the wrapper, the fixture granted Administrators write access, proving ownership assignment was sufficient to trigger the reported error.
- `mod/build/registry-owner-final.log`: elevated fixture passed installation and injected rollback; owner/group/DACL, parent permissions, layer order/data and thread identity preserved. Only a GUID-named HKCU test subtree was touched and removed.
- `mod/build/native-hash-red.log`: differently hashed test process named Le Mans Ultimate.exe rejected. `mod/build/native-hash-final.log`: full MSVC/native/WARP/fake-runtime suite and DLL build passed after removing the hash gate.
- Portable launch, focus-offset CLI and performance CLI tests passed without launching a game. Review identified and resolved the remaining diagnostic `mod/launch.ps1` gate; it now records the actual hash.
- Independent source review found no further blockers in the token scope or native changes. `git diff --check` passed.
- After SimplySign authentication, all four packaged binaries passed publisher/timestamp verification (`mod/build/hash-owner-signing-retry.log`). Signed test ZIP: `../../dist/test-0.3.1-hash-owner/LMU-Quad-View-Mod-0.3.1-hash-owner-fix.zip`. All 39 manifest entries verified inside the ZIP. SHA256: `8BC5E9D67A48FB80D7FFF91BB063FABC99F3B59525B86C7ED419E911A811018E`. Existing installation and published 0.3.1 assets were not replaced.

## Release 0.3.2

User requested a new release build after cancelling CPU optimization experiments. Reverted the trial cached-bank change and its tests/build switch; existing CPU upload options retain their disabled defaults. Version 0.3.2 was unused on GitHub at build time. Full native and launcher suites, CLI checks and the elevated isolated SYSTEM-owner/rollback fixture passed again (`mod/build/release-032-*.log`). All four distribution binaries signed with valid timestamps; launcher and both DLLs report product 0.3.2/file 0.3.2.0. All 39 archive manifest entries verified with no local settings/logs/runtime data. Local release archive: `../../dist/release-0.3.2/LMU-Quad-View-Mod-0.3.2.zip`, SHA256 `D53B75D388D3B6ABB3DD1CCA1E01417D8A1C9F0869FBC27B1E4C53425902D142`. No GitHub publication performed in this build task. User preference: only sign release builds; use unsigned direct offline launches for testing.
