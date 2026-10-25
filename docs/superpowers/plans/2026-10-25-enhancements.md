# OpenGrid XR enhancement implementation

User has authorized implementation. Continue in the existing development checkout; preserve all earlier features and reference projects.

1. Desktop UI task: symmetric expansion with backward-compatible validation and a saved-profile workflow. Then implement separate Frame generation and VR overlay pages using `src/enhancements.mjs` defaults, existing controls and honest previews.
2. Root: validated optional `framegen` and `overlay` profile groups, per-game upstream-format serializer, native INI enable flag, atomic configuration writes, owned runtime install/update with the private DLL and accurate engine status. Tests cover malformed imports, shortcut collisions, profile selection, disabled modules and isolation.
3. Native task: pin/vendor necessary VectorXR native source, private loader chain, isolated paths/identity, toolkit-summary OSD adaptation, Release build and tests. Preserve existing crop/capture/output tests. Document dependencies, source modifications and actual limitations.
4. Research task: inspect Sboys3 sharpening/distortion source and licenses, iRacing/LMU native foveation, and OpenXR quad-view requirements. Implement only supported, testable controls after deriving exact contracts; do not confuse postprocessing with saved rendering work.
5. Root integration: independent code review, fix substantive findings, build the Windows package, run isolated workflows against it, verify packaged native artifacts, and provide matching source/notices. No silent runtime registration or sim-INI changes.

Review focus: accidental VectorXR writes, loader ordering and acquired-image lifetime, old profile recovery, duplicate shortcuts, configured-vs-active status, stale game-config writes, and unsupported rendering capabilities.
