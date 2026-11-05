# Foveation offsets for 0.1.1

The user requests horizontal and vertical focus-box offsets, explicitly for both
fixed and eye-tracked foveation, followed by a GitHub 0.1.1 release. SPS research
moves to a future 0.2.0 target. This release starts from released main and excludes
the separate SPS branch's diagnostic/planning work.

Add saved horizontal/vertical offsets from -50 to +50 percent, default zero.
Each axis is a fraction of that eye's full outer-view tangent-space span. Positive
means right/up. Apply the translation to the provider's freshly located focus FOV
each frame, after any legacy size scaling, with unchanged view pose and focus
tangent-space width/height. Never accumulate previous offsets or replace gaze.
Use the shifted focus FOV consistently for native rendering, debug outline and
submitted composition. Apply in both runtime-settings and legacy quad modes,
independent of provider; stereo/trace paths remain untouched.

Use a native helper taking focus FOV, that eye's outer FOV and two percentages.
deltaX=(tan(outerRight)-tan(outerLeft))*horizontal/100;
deltaY=(tan(outerUp)-tan(outerDown))*vertical/100.
Translate the respective focus tangents and convert back using atan. Do not clamp
to the outer view: existing runtime gaze centers may legitimately extend beyond
it. Zero offsets are an exact no-op without a tangent round trip; an unchanged
axis preserves its original angle bits. Nonzero transforms validate finite,
ordered FOVs strictly inside +/-pi/2 and validate the result. Failure leaves the
focus argument unchanged and causes the existing located-view refusal path.
Invalid/missing environment values use zero safely, with existing logging style.

Persist FocusOffsetXPercent/FocusOffsetYPercent as decimal launcher settings,
default zero for old XML files; validate -50..50. Export invariant-culture
LMU_QV_FOCUS_OFFSET_X_PERCENT / LMU_QV_FOCUS_OFFSET_Y_PERCENT after clearing stale
inherited LMU_QV variables. Include values in diagnostic run metadata. CLI adds
matching parameters, accepts offsets with UseRuntimeSettings, and rejects
nonzero offsets outside quad mode and nonfinite numbers. Do not write provider
offset settings; the mod applies the shift exactly once for all three providers.

UI: add a separate Focus position card with two signed percent controls and
Reset offsets (both to zero), enabled for Native, QVFR and VectorXR. Explain
positive directions, percentage of full view, and offset from gaze in Auto.
Keep existing layout/scroll/DPI patterns. Existing width/height controls retain
their provider-dependent behavior. Settings take effect on the next game launch,
consistent with existing controls. No new per-frame allocation, GPU pass, buffer
size or rendering resolution change is required.

Verification: analytic shift/no-op/invalid-input tests, moving/asymmetric gaze
integration through located/render/submitted FOVs, settings migration/roundtrip/
validation, launch environment and metadata, reset/provider/layout coverage;
complete native and launcher suites; inspect rendered GUI and verify extracted
release package checksums and version metadata. Live headset testing, if
unavailable, must be reported accurately rather than implied by synthetic tests.

Version is 0.1.1 in root VERSION, generated binaries and package. Update release
notes and usage docs with signs, units, both modes, zero reset and no SPS claim.
Publish reviewed changes and portable ZIP through the authorized GitHub release.

## Final UI refinements requested before publication

Left-align the sidebar's LMU, QUAD VIEW and OPENXR RENDERING labels to one
shared visual edge, using consistent margins and text padding across font sizes. Add approximately six logical pixels between numeric values and
spinner arrows, including percentage controls, smoothing and capture duration.
Preserve native editing/spinner behavior and sufficient width for signed and
maximum values across supported DPI layouts. Inspect the final rendered result.

The CLI's invariant double formatting may use scientific notation for tiny
offsets. Accept general decimal/exponent notation in the offset parser while
retaining finite, range and full-string validation. Legacy size parsing is
unchanged. This closes the independent review's minor formatting mismatch.
