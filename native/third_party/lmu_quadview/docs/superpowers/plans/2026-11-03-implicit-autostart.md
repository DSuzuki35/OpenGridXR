# Automatic LMU OpenXR startup

The user selected a one-time administrator installation of an implicit OpenXR
layer, rather than a background starter, and explicitly requested re-signing.
Version 0.3.1 will keep the published 0.3.0 separate.

Implementation decision: a small signed implicit bootstrap DLL and the signed
adapter live in a versioned Program Files folder. HKLM registers the bootstrap
first while preserving the relative order and enabled state of existing values.
Installation backs up and recreates the childless key with the original registry
permissions, verifies readback, and restores the original values on failure.
Registry-key rename is prohibited after three diagnosed 0x3B kernel crashes;
see `2026-11-03-registry-rename-workaround.md` for the replacement and evidence.
Real Windows tests showed delete/reinsert retaining prior enumeration slots.
Uninstall removes only our registration. Per-user activation points to the
writable original package, so only users who enable it are affected. No game
files, Steam launch options, service or scheduled task are changed.

The bootstrap passes non-LMU applications, elevated applications, externally
configured launcher sessions and users without activation straight through.
For ordinary LMU OpenXR startup it invokes the signed launcher preparation mode
before instance creation: verify the exact game build/path, resolve provider,
apply saved settings/profile and write a bounded UTF-16 configuration response.
It then builds a private downstream chain with the selected provider after the
adapter. Quad mode skips OpenGridXR; VRS preserves it. Installed providers and
registry values are not disabled globally. Existing explicit launch disables
the implicit bootstrap to prevent duplicate adapters. Benchmark mode attaches
the existing launcher UI without launching a second game.

- [x] Add failing native chain-policy and launcher preparation/registration tests.
- [x] Implement bootstrap, guarded automatic preparation, install/remove UI and
  lifecycle; keep real non-LMU passthrough and existing renderer guards.
- [x] Build and test both components, independently review integration.
- [x] Sign all distribution DLLs/EXEs; verify timestamps and package hashes.
- [x] Install with elevation, test native OpenXR startup without custom Steam
  options, verify selected layer and quad submissions; record headset limits.

Review focus: rollback preserves third-party registry data; unsupported/missing
settings cannot silently claim active quad mode; no double adapter/provider;
foreign apps do not invoke helper or write settings; no work under DllMain;
helper arguments are quoted without a shell; installed bytes are signed and
protected by Program Files; never load a per-user helper in an elevated game.

## Initial local verification, superseded by the final verification below

- Full native build/tests exited 0 (`mod/build/autoload-full-native.log`).
- Updated bootstrap policy, chain and foreign-host lifecycle tests exited 0
  (`mod/build/autoload-native.log`).
- Launcher tests, real temporary-HKCU order/type/value/rollback tests and GUI
  layout tests exited 0 (`mod/build/autoload-launcher.log`). Registry tests need
  execution outside the process sandbox; no real OpenXR entries are used.
- Independent review found and verified fixes for loader retry self-disabling
  and OpenGrid's newly integrated adapter activating again during VRS.
- Four distributed binaries have valid same-publisher RFC3161 signatures.
  All 39 package manifest hashes verified. Archive SHA256:
  `b87040fa5a70cbba0f5e193f351a191e38c806125b8218a5cda75dfdba730491`.
- Local ZIP: `dist/local-0.3.1/LMU-Quad-View-Mod-0.3.1.zip` under the outer
  workspace. Runnable folder: `dist/LMU-Quad-View-Mod-0.3.1`, with copied local
  0.3.0 settings (not included in the distribution archive).
- Installation attempted via signed launcher `--enable-autoload`; a startup
  error dialog is pending and HKLM OpenXR registration remains unchanged.
  Desktop automation is occupied by another active request. Asked user for
  the dialog text; do not claim installation or headset validation succeeded.
- Steam's current LMU LaunchOptions now point to OpenGrid XR's
  `--lmu-steam-launch %command%` wrapper. This is a separate recent integration,
  not our old wrapper; it has not been edited. Resolve the intended startup path
  before the end-to-end Steam test. No GitHub push/release for 0.3.1 performed.

## Final local verification, 2026-11-03

- The protected game could load the bootstrap but could not read its HKCU
  activation (`error=2`). Setup now writes a per-user UTF-16 activation file;
  the bootstrap reads it first, retaining registry fallback for older setups.
  Early per-PID logs distinguish loader entry, skips, helper and chain failures.
- A second reproduced failure was the adapter above retained VectorXR: QVFR
  received no quad extension request, bypassed itself and returned a failed
  recommendation query. Retained overlays now precede the adapter, immediately
  followed by the selected provider. A synthetic extension-forwarding regression
  failed with the old order and passes with the corrected chain.
- Native activation/chain/passthrough tests and the full launcher suite passed.
  Independent review found no blocking issue in either repair.
- Live Steam OpenXR startup with empty custom launch options succeeded at 17:23
  Europe/Berlin: file activation, helper completion, VectorXR -> LMU -> QVFR,
  session result 0, engine hook start 1, and successful quad-frame submissions.
  The user confirmed the headset menu image and tracking frame.
- The user clarified that testing was in the menu, not a cockpit session.
  Do not describe this as live validation of the smaller native outer bank or
  PSVR2 performance. Its separate WARP regression and hardware-test limit are
  documented in `2026-11-03-psvr2-view-backed-depth.md`.
