# Markus Stahl patch integration

**Goal:** Integrate Markus Stahl's 0.3.2-to-0.3.3 patch with attribution and Windows regression coverage.

**Architecture:** Retain existing launch modes, signatures and registry rollback. Add mode-specific hook selection, PE function-boundary checks, protected helper hashes and conservative generated-file cleanup. No CPU experiments or new executable hash allowlist.

**Tech stack:** C++17/MSVC, Win32, C#/.NET Framework 4.8, PowerShell.

- [x] Review patch against 0.3.2; preserve pre-existing SteadyXR changes.
- [x] Integrate hook guards with explicit known leaf exceptions and regression tests.
- [x] Pin and lock launcher plus .NET config during helper lifetime; test tampering, read failures and locks.
- [x] Restrict cleanup to generated files, preserve Unicode encoding/BOM, reject invalid input unchanged, test backup retention and registry cleanup.
- [x] Wire Windows builds/tests, update 0.3.3 documentation and credit Markus Stahl.
- [x] Run native and launcher suites, independent review and final diff checks. Do not publish or sign a test build.

Source patch also includes Linux cross-build scripts and unofficial release wording. These are outside the requested runtime fixes and are not used for the official Windows build.

## Verification

- Full MSVC native build and suite: exit 0 (mod/build/markus-native.log), including real child-process helper verification, WARP and fake-runtime cases.
- Full launcher build and suite: exit 0 (mod/build/markus-launcher.log), including registry preservation, junction cleanup, backup retention, Unicode and GUI checks.
- Independent review: no remaining must-fix issues. Known leaf bytes match existing decrypted reconstruction; raw game EXE code is encrypted.
- Source formatting check: git diff --check passed.
- All three core binary version resources report 0.3.3. No signing, installation or publication performed; live headset QA remains pending.
- The pre-existing SteadyXR policy changes remain untouched.
