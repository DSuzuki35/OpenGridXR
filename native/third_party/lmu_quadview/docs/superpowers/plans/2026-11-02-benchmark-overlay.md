# Benchmark implementation plan

Spec: ../specs/2026-11-02-benchmark-overlay.md

- [x] Native bounded shared-memory ABI, scene timing, sparse nonblocking GPU spans and separate quad overlay; lifecycle/error tests.
- [x] Managed ring reader, recording/checkpoints/partial capture, self-contained report, Minimal/Detailed rendering and statistical/escaping tests.
- [x] Launcher controls/settings, DirectInput edge bindings/disconnect handling, layout tests.
- [x] Latest-run diagnosis with recovered-error handling, missing-log distinction and bounded reads; regression tests.
- [x] Cross-language ABI test, integrate builds, run native/managed regression suite and inspect visual artifacts.
- [x] Package tester candidate, verify hashes/privacy, document untested headset/hardware behavior.

Validation 2026-11-02: MSVC native regression suite and .NET Framework launcher suite passed, including the raw native/C# ABI fixture, WARP overlay lifecycle, optional-overlay failure isolation, recording/report recovery and input edge handling. Minimal/Detailed previews and launcher layout artifacts inspected; 18 simulated layout cases passed at an actual 144-DPI HWND. Packaged binary hashes and all 30 manifest entries verified; path/token privacy scan passed. No real headset, wheel/joystick, Qubic or physical DPI-transition test was performed. Test candidate only; not published to GitHub.
