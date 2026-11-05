# Contributing

Useful contributions include reproducible bug reports, headset/provider test
results, clearer documentation and focused fixes. Read the [project overview](README.md)
and [architecture guide](docs/ARCHITECTURE.md) before changing renderer behavior.
Original contributions use the repository's [PolyForm Noncommercial license](LICENSE.md);
this is source-available software, not OSI open source.

## Build and test on Windows x64

Use Windows PowerShell with either:

- Visual Studio Build Tools with the **Desktop development with C++** workload,
  including the Windows SDK and x64 MSVC tools. `build-msvc.cmd` finds Visual
  Studio through `vswhere` and initializes the x64 compiler environment.
- A Windows x64 MinGW-w64 toolchain with `gcc.exe`, `g++.exe` and `windres.exe`
  on `PATH`. Release 0.2.1 uses the MSVC native build and test path.

The launcher also needs the .NET Framework x64 C# compiler at
`%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe` and .NET Framework 4.8.
D3D11/WARP must be available for graphics tests. No game assets are needed for
the normal automated suite. Run these commands from the repository root.

Recommended MSVC native build and tests:

```powershell
.\mod\build-msvc.cmd
if ($LASTEXITCODE -ne 0) { throw 'Native build/tests failed' }
```

Alternative MinGW-w64 native build and tests:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\mod\build.ps1
if ($LASTEXITCODE -ne 0) { throw 'Native build/tests failed' }
```

Then build/test the launcher and package the release:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\mod\build-launcher.ps1
if ($LASTEXITCODE -ne 0) { throw 'Launcher build/tests failed' }
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\package.ps1
if ($LASTEXITCODE -ne 0) { throw 'Packaging failed' }
```

Both native scripts run their test suite before producing
`mod/build/lmu_quadview.dll`. The launcher script runs settings, geometry, launch
service and GUI layout tests, writes layout artifacts under `mod/build/gui-qa`,
and produces `mod/build/LMU-QuadView-Launcher.exe` plus its `.exe.config`.
Packaging requires both builds and the native stereo-check executable; it does
not compile missing outputs. The default ZIP name is derived from `VERSION`
(`dist/LMU-Quad-View-Mod-0.2.1.zip` for this release). Packaging refuses to overwrite
an existing archive; use a new destination for a repeat verification:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\package.ps1 -OutputDirectory .\dist\verification
if ($LASTEXITCODE -ne 0) { throw 'Packaging failed' }
```

The script execution-policy flag applies to these developer commands only.
End users launch the EXE directly.

The standalone PowerShell compatibility tests should also pass before release:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\mod\tests\portable_launch_test.ps1
if ($LASTEXITCODE -ne 0) { throw 'Portable launch tests failed' }
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\mod\tests\qvfr-profile-test.ps1
if ($LASTEXITCODE -ne 0) { throw 'QVFR profile tests failed' }
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\mod\tests\focus_offset_launch_test.ps1
if ($LASTEXITCODE -ne 0) { throw 'Focus-offset CLI tests failed' }
```

## Find the relevant code

For the opt-in performance branch controls, also run
`powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\mod\tests\performance_launch_test.ps1`.
This checks launch configuration without starting the game. See
[PERFORMANCE-EXPERIMENT.md](PERFORMANCE-EXPERIMENT.md) for scope, usage and pending
headset validation.

| Area | Start here |
| --- | --- |
| Launcher UI and launch environment | `mod/launcher/Launcher.cs`, `LaunchService.cs` |
| QVFR profile and FOV calibration | `mod/launcher/QvfrSettings.cs`, `FocusGeometry.cs` |
| OpenXR interception, swapchains, frame submission | `mod/xr_layer.cpp` |
| Game hooks, focus scene traversal, state restoration | `mod/engine_bridge.cpp` |
| XR/engine boundary and borrowed resources | `mod/bridge.hpp` |
| Outer render bank and copies | `mod/native_outer_*.hpp`, `mod/outer_resample.hpp` |
| Diagnostics and captures | `mod/render_profile.hpp`, `mod/gpu_timing.hpp`, `mod/xr_capture.hpp` |
| Opt-in renderer research | `mod/native_upload_*.hpp`, `mod/stereo_*.cpp` |
| Native and fake-runtime tests | `mod/tests/`, `mod/xr_layer_tests.cpp` |
| Launcher tests | `mod/launcher/*Tests.cs` |

## Keep changes readable and safe

Keep each change focused, use descriptive names, and explain non-obvious resource
lifetimes or game-build assumptions beside the code. Prefer small helpers with
clear inputs and ownership over adding more work to an already large hook.
Follow the local style without reformatting unrelated code. Keep diagnostics out
of normal hot paths when disabled, and avoid per-frame allocation or synchronous
GPU waits without evidence that they are necessary.

Preserve the executable hash/build guard, native eye-handle identity checks,
format/size validation, and conservative fallbacks. Do not infer eye identity from
texture dimensions or creation order. Borrowed COM pointers must not be released
or retained beyond their documented lifetime. Acquired swapchain images must be
waited, used and released exactly once; restore every modified game binding,
camera/history field and D3D state on success and failure paths. Keep environment
changes local to the child process and QVFR profile edits scoped to LMU.

Add meaningful regression coverage when behavior changes: exercise the failure
that motivated the fix and relevant fallback/cleanup paths. Reuse the existing
native, fake-runtime, WARP and launcher suites. A startup success or WARP pass
cannot prove correct headset rendering. State which toolchain and tests ran, and
which hardware checks remain unperformed.

For performance changes, compare warmed captures of the same replay segment,
track, car/grid, pose, runtime/provider, refresh rate, graphics settings and actual
image sizes. Use the same diagnostic configuration and capture point in both
runs; repeat if results vary. Follow [RENDER-PROFILING.txt](RENDER-PROFILING.txt).
Report capture conditions, sample counts and frame-time distributions. Nested CPU
scopes overlap and include waits; sparse GPU scopes omit copies/composition.
Do not present their sum as total frame time or a private stereo proof as a
production draw reduction.

Keep personal paths, settings, logs, shader captures, credentials and game files
out of commits. Never include the LMU executable, proprietary assets or personal
replays. Share redacted diagnostics only when needed to reproduce a problem.

## Submit a change

Describe the triggering problem, resulting behavior, affected providers, test
commands/results and remaining hardware limitations. Include screenshots for UI
changes and matched capture evidence for performance claims. Keep experimental
features opt-in and clearly label their scope. Documentation improvements should
also update the portable README when they change installation or user behavior.

## Prepare a release

1. Set the root `VERSION` to the intended `X.Y.Z`. It is authoritative; launcher
   and DLL version metadata are generated from it. Update `CHANGELOG.md` with behavior,
   compatibility limits and known issues.
2. Run the native and launcher builds/tests above from clean source. Confirm the
   supported game hash and perform available headset/provider checks; document
   gaps rather than implying a broader validation matrix.
3. Run `package.ps1`. Inspect the extracted archive, included notices, `SHA256.txt`,
   launcher config, DLL and portable instructions. Test launching the extracted
   copy and review diagnostics for private data before publishing anything.
4. Tag the reviewed release commit `vX.Y.Z` and publish the matching versioned ZIP
   and release notes through GitHub Releases. Keep the tag, `VERSION`, changelog
   and archive name aligned. Do not commit build products or local captures.
