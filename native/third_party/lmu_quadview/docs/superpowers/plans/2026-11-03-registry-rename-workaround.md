# Registry rename crash workaround

User authorization: replace the diagnosed crash path and fix the tool, continuing
the existing `codex/implicit-autostart` working branch and pending startup fix.

Design: remove registry-key rename entirely. Snapshot the childless implicit-layer
key (values, types, order, owner/group/DACL), write and verify a persistent sibling
backup, then delete/recreate the key with its original security and desired values.
Verify readback; restore the snapshot on failure. Keep the backup if restoration
fails. Refuse unexpected subkeys. Never touch third-party layer enablement.
The existing independent HKLM backup remains available for interrupted installs.

Alternatives: rewriting values in place already failed to establish enumeration
order; keeping rename is prohibited by three matching kernel crash dumps; removing
automatic startup would not satisfy the requested Steam integration.

Constraints: no execution of old autostart test binaries, no rename-based RED
reproduction, no game/VR launch during automated tests, no public release/push.
Continue limited-rights process validation to fix the original Access denied.

- [x] Add a reflection-based native-import guard; demonstrate a safe RED against
  the existing launcher without invoking registration.
- [x] Replace RegisterFirst and extend temporary-HKCU tests for order, every value
  type, security preservation, repeated registration, rollback and subkey refusal.
- [x] Run the guard before tests, compile/run launcher suite and process regression.
- [x] Review changes, sign the updated launcher, package and replace the local
  runnable 0.3.1 folder while retaining settings and logs; verify hashes/signatures.
- [x] Document verification and limitations. Do not claim a headset live test.

Progress: root cause established from all three local dumps. Plan recorded before
implementation. Existing unrelated working changes are preserved.

Verification: the guard rejected the prior EXE's AutoStart.RegRenameKey import
without executing it. The complete launcher build/tests now exit 0; see
`mod/build/registry-workaround-tests.log`. This includes the original restricted
process-path regression and simulated GUI layout tests. The first new registry
test correctly detected an SDDL difference: Windows clears the DACL auto-inherited
history bit on creation even for a protected ACL. Verification now ignores only
that history bit while checking all ACEs, protection, owner and group. Independent
test assertions check those permission fields rather than mirroring SDDL text.

Independent read-only review identified foreign-owner assignment and concurrent
registration risks. Ruling: refuse unsupported ownership with actionable wrapper
fallback, before deleting the original; do not enable additional token privileges.
The local HKLM owner is Administrators. Install/remove are serialized with a named
mutex; conflicting values/security are persisted separately before rollback, and
unexpected subkeys prevent destructive rollback. A new conflict-preservation test
failed against the first workaround, then passed after the fix. Additional passing
cases cover inherited ACLs, REG_NONE, failed restoration, retained original backup,
and preservation of an unexpected child. Full launcher suite passed again.

Delivery: launcher signed with the existing Certum publisher certificate and
RFC3161 timestamp, verified by SignTool and Authenticode. Updated outer-workspace
`dist/LMU-Quad-View-Mod-0.3.1` and `dist/local-0.3.1/LMU-Quad-View-Mod-0.3.1.zip`;
the same corrected archive is under `dist/local-0.3.1-registry-fix`.
All 39 deployed manifest hashes verified. Settings and logs are byte-identical to
their pre-update state. Installed native DLL hashes match the package, and the
bootstrap remains first, so this update required no live registry reinstall.
The compiled safety guard also passed against the deployed signed EXE.
Archive SHA256: `1AB81D080F6B64886388078DC69B3725ABA9677F0158252C3E67CF6FC2D0EF7D`.
Old archive retained only as `diagnostics/20261103-bsod/unsafe-pre-fix-0.3.1.zip`.
No Steam/game/headset launch, public release, push or commit was performed.
