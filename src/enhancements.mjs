import defaults from "./enhancement-defaults.json" with { type: "json" };
export const framegenDefaults = () => structuredClone(defaults.framegen);
export const overlayDefaults = () => structuredClone(defaults.overlay);
export const sharpeningDefaults = () => structuredClone(defaults.sharpening);
export const nativeFoveationDefaults = () =>
  structuredClone(defaults.nativeFoveation);
function fail(label) {
  throw new Error(`Invalid ${label}.`);
}
function number(value, min, max, label, integer = false) {
  if (
    typeof value !== "number" ||
    !Number.isFinite(value) ||
    value < min ||
    value > max ||
    (integer && !Number.isInteger(value))
  )
    fail(label);
}
function choice(value, allowed, label) {
  if (!allowed.includes(value)) fail(label);
}
function bool(value, label) {
  if (typeof value !== "boolean") fail(label);
}
function object(value, label) {
  if (!value || typeof value !== "object" || Array.isArray(value)) fail(label);
}
function shortcut(value) {
  if (
    typeof value !== "string" ||
    (value !== "" && !/^Ctrl\+Alt\+F(?:[1-9]|1[0-2])$/.test(value))
  )
    fail("shortcut");
}
export function validateEnhancements(profile) {
  if (profile.nativeFoveation !== undefined) {
    object(profile.nativeFoveation, "native foveation");
    for (const [key, min, max] of [
      ["outerResolution", 25, 50],
      ["focusWidth", 25, 50],
      ["resolutionScale", 50, 200],
    ])
      number(profile.nativeFoveation[key], min, max, key, true);
  }
  const f = profile.framegen,
    o = profile.overlay,
    s = profile.sharpening;
  if (f !== undefined) {
    object(f, "frame generation");
    bool(f.enabled, "frame generation enabled");
    choice(f.synthesisMode, ["interpolate", "extrapolate"], "synthesis mode");
    choice(
      f.flowQuality,
      ["performance", "balanced", "quality"],
      "flow quality",
    );
    choice(f.motionEstimation, ["shader", "hardware"], "motion estimator");
    choice(
      f.frameMultiplier,
      ["dynamic", "auto", "2x", "3x", "4x"],
      "frame multiplier",
    );
    choice(
      f.diagnosticView,
      ["off", "flow", "confidence", "tint"],
      "diagnostic view",
    );
    number(f.artifactSuppression, 0, 100, "artifact suppression", true);
    number(f.motionLimitPercent, 2, 25, "motion limit", true);
    shortcut(f.toggleShortcut);
  }
  if (o !== undefined) {
    object(o, "VR overlay");
    if (o.showResolution !== undefined) bool(o.showResolution, "overlay showResolution");
    for (const name of [
      "enabled",
      "showGraph",
      "showRuntime",
      "showApp",
      "showClock",
      "showPerformance",
      "showFramegen",
      "showToolkit",
    ])
      bool(o[name], `overlay ${name}`);
    choice(o.layout, ["expanded", "compact"], "overlay layout");
    choice(
      o.accent,
      ["teal", "copper", "blue", "violet", "rose", "custom"],
      "overlay accent",
    );
    choice(
      o.compactMetrics,
      ["all", "fps", "frameTime", "none"],
      "compact metrics",
    );
    if (
      typeof o.customColor !== "string" ||
      !/^#[0-9a-fA-F]{6}$/.test(o.customColor)
    )
      fail("overlay color");
    for (const [name, min, max, integer] of [
      ["horizontalDegrees", -40, 40],
      ["verticalDegrees", -35, 35],
      ["distanceMeters", 0.5, 3],
      ["scale", 25, 150],
      ["opacity", 30, 100, true],
      ["updateHz", 1, 20, true],
    ])
      number(o[name], min, max, `overlay ${name}`, integer);
    shortcut(o.toggleShortcut);
    shortcut(o.cycleShortcut);
  }
  if (s !== undefined) {
    object(s, "sharpening");
    choice(s.mode, ["off", "luma", "cas"], "sharpening mode");
    number(s.strength, 0, 100, "sharpening strength");
    number(s.clamp, 0, 0.1, "luma clamp");
    number(s.radius, 0.1, 6, "luma radius");
    number(s.pattern, 1, 4, "luma pattern", true);
  }
  const keys = [
    (f ?? defaults.framegen).toggleShortcut,
    (o ?? defaults.overlay).toggleShortcut,
    (o ?? defaults.overlay).cycleShortcut,
  ].filter(Boolean);
  if (new Set(keys).size !== keys.length) fail("duplicate shortcut");
}
