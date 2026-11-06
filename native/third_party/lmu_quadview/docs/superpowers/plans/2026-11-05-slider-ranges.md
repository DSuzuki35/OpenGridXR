# Visible valid slider ranges

Implement the requested live range indication in the existing native launcher.
Keep the full scale visible, dim unavailable sections, show an `Allowed` caption,
and apply the same bounds to mouse, keyboard and numeric input. Derive coupled
QVFR bounds from its existing buffer-fit validation, in provider units before
headset display calibration. Other providers retain their current behavior.

For ordinary editing, constrain the edited setting and leave other values
unchanged. Reset all bounds before loading complete settings/calibration, then
recalculate together. On switching an incompatible retained tuple to QVFR,
reduce outer first; only raise inner if necessary to permit the minimum outer.
Do not save this normalization until the user saves or launches.

Alternatives considered: shrinking the whole track hides the forbidden region;
warnings after editing allow invalid combinations. A fixed full track with a
live allowed interval directly implements the user's requested behavior.

1. Add failing tests for coupled bounds, calibration, provider switching and
   slider/numeric/keyboard agreement.
2. Add shared range calculations and constrained input/paint support.
3. Refresh all ranges on edits, complete settings loads and provider/calibration
   changes; retain exact valid imported values.
4. Run launcher tests plus hidden-window layout checks at simulated 100/150/200%
   scales. Inspect screenshots. Build a local UI candidate without publishing or
   changing native rendering binaries.

Verification: the original UI failed the new regression with outer 400 rather
than the valid maximum 50. The implementation passes the complete launcher
suite, the new live-bound/calibration/provider-switch/keyboard tests and all
hidden-window layout cases at simulated 100/150/200% and default/minimum sizes.
Allowed captions are explicitly checked for reachability and text clipping.
Screenshots include active QVFR range examples. Native rendering code is unchanged.
The independent final code review found no actionable issues.
