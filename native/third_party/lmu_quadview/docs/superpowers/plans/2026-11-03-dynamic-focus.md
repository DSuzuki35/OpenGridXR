# Dynamic focus FOV implementation plan

Goal: Implement the requested optional automatic center FOV adjustment with configurable minimum/maximum and fixed image dimensions; diagnose and fix the supplied QVFR log failure.

Design: Scale the freshly located focus tangent extents about their gaze center, before the existing offset/crop transform. Preserve aspect ratio, poses, outer FOV, and swapchain dimensions. Bounds are percentages of the configured baseline focus FOV (default 70..100%). GPU app-span timing versus predicted display period is an estimate, excluding compositor work. Reduce below 2% estimated reserve; increase above 7%; smooth timing and require sustained conditions. Never claim a guaranteed performance gain at fixed pixel count.

Alternatives: Dynamic texture recreation is substantially more invasive in LMU; variable viewport rendering needs coordinated engine render-target changes. Neither is included without a demonstrated safe engine path. Use existing asynchronous query infrastructure independently of benchmark UI. No valid scene samples means hold the last FOV. Disabled means original behavior.

Authorization: User explicitly requested implementation and then configurable min/max, with dynamic resolution only if feasible. Work proceeds in a separate fresh GitHub clone on codex/dynamic-focus-fov, based on 02c0625. Routine controller constants are implementation choices.

- [x] Reproduce valid handle 1 rejection in the real engine eye-identity function, remove numeric sentinel assumption, retain exact unique vector identity checks.
- [x] Test bounded controller, hysteresis, invalid/stale samples and disabled state; implement controller.
- [x] Integrate GPU queries, display-period tracking and per-frame FOV snapshot; test runtime FOV integration with unchanged dimensions.
- [x] Add persisted launcher opt-in, min/max and reserve settings; test validation and child environment. Show clear relative-FOV units.
- [x] Run native and launcher suites, inspect launcher rendering, package a local test build and report log diagnosis and limitations.

Review focus: never use stale/menu/failed-frame GPU samples; both eyes use the same frame scale; no mid-frame projection change; opaque handles may equal 1; VRS and disabled mode unaffected.

User addition: display rendered center FOV in the overlay. Both layouts show the
frame's scale and actual per-eye horizontal/vertical degrees after crop. Use a
seqlocked reserved header extension, preserve ABI-v1 sizes, and test unavailable,
torn, menu and live values plus C++/C# compatibility. Detailed includes source frame.

Validation: MSVC full build passed (80 XR scenarios plus native suites); launcher build and settings/layout suites passed. Two independent review findings corrected and rechecked. Existing XR-test compiler warnings remain; baseline has the same warning classes/count. Headset testing is outstanding, not represented as a passing check.
