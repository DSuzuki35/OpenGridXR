"use strict";
const defaults = require("../src/enhancement-defaults.json");
function selectedForGame(doc, game) {
  return (
    doc.profiles.find(
      (p) => p.game === game && p.id === doc.selectedProfileId,
    ) ||
    doc.profiles.find(
      (p) => p.game === game && p.id === doc.activeProfiles?.[game],
    ) ||
    doc.profiles.find((p) => p.game === game)
  );
}
const binding = (value) =>
  value ? { type: "keyboard", chord: value.split("+") } : { type: "none" };
function toEnhancementsConfig(doc, game) {
  const p = selectedForGame(doc, game),
    f = { ...defaults.framegen, ...p?.framegen },
    o = { ...defaults.overlay, ...p?.overlay };
  const crop = p?.view.crop;
  const summary = p
    ? `Configured: ${p.mode} | SS ${p.view.renderScale}% | Crop ${crop.left}/${crop.right}/${crop.top}/${crop.bottom}%`
    : "No configured game profile";
  return {
    version: 3,
    core: {
      enabled: Boolean(p && (f.enabled || o.enabled || p.spatialOverlays?.enabled || p.helmet?.enabled)),
      overlays: { enabled: Boolean(p?.spatialOverlays?.enabled) },
      logLevel: "info",
      logRetentionFiles: 3,
      trackSeenApps: false,
      sound: { volume: 0 },
      osd: {
        enabled: o.enabled,
        visibleOnStart: true,
        compact: o.layout === "compact",
        horizontalDegrees: o.horizontalDegrees,
        verticalDegrees: o.verticalDegrees,
        distanceMeters: o.distanceMeters,
        scale: o.scale,
        opacity: o.opacity,
        updateHz: o.updateHz,
        showGraph: o.showGraph,
        showRuntime: o.showRuntime,
        showBrand: true,
        showApp: o.showApp,
        showClock: o.showClock,
        showPerformance: o.showPerformance,
        showResolution: o.showResolution,
        configuredRenderScale: p?.view.renderScale ?? 100,
        showFramegen: o.showFramegen,
        showFramegenFps: o.showFramegen,
        showModules: o.showToolkit,
        showTurbo: false,
        showPivot: false,
        compactShowRuntime: o.showRuntime,
        compactShowBrand: true,
        compactShowApp: o.showApp,
        compactShowClock: o.showClock,
        compactShowPerformance: o.showPerformance,
        compactShowFramegen: o.showFramegen,
        compactShowTurbo: false,
        compactShowPivot: false,
        compactMetrics: o.compactMetrics,
        clockFormat: "24",
        accent: o.accent,
        customColor: o.customColor,
        rowOrder: [
          "graph",
          "performance",
          "framegen",
          "modules",
          "turbo",
          "pivot",
        ],
        toolkitSummary: summary,
        toggleBinding: binding(o.toggleShortcut),
        cycleBinding: binding(o.cycleShortcut),
      },
    },
    applications: [],
    modules: {
      depthxr: {
        enabled: false,
        defaults: { stereoBoost: 1, convergence: 0, depthAnchor: false },
        bindings: {
          toggleEnabled: { type: "none" },
          toggleAnchor: { type: "none" },
        },
        profiles: [],
      },
      pivotxr: { enabled: false, profiles: [] },
      quadviews: { enabled: false, profiles: [] },
      turbo: { enabled: false, profiles: [] },
      framegen: {
        enabled: Boolean(p && f.enabled),
        diagnosticView: f.diagnosticView,
        toggleBinding: binding(f.toggleShortcut),
        profiles: [],
        defaults: {
          synthesisMode: f.synthesisMode,
          flowQuality: f.flowQuality,
          motionEstimation: f.motionEstimation,
          frameMultiplier: f.frameMultiplier,
          artifactSuppression: f.artifactSuppression,
          motionLimitPercent: f.motionLimitPercent,
          extendHeadPrediction: false,
        },
      },
    },
  };
}
function toRuntimeIni(doc) {
  const sections = [
    "; OpenGrid XR. Generated from saved profiles. Restart the game after changes.",
  ];
  for (const game of ["iracing", "lmu"]) {
    const p = selectedForGame(doc, game);
    if (!p) {
      sections.push(`[${game}]\nEnabled=0`);
      continue;
    }
    const c = p.view.crop,
      s = p.stream;
    const fields = {
      Enabled: 1,
      EnhancementsEnabled: p.framegen?.enabled || p.overlay?.enabled || p.spatialOverlays?.enabled || p.helmet?.enabled ? 1 : 0,
      HelmetVisorEnabled: p.helmet?.enabled ? 1 : 0,
      HelmetVisorContrast: p.helmet?.visorContrast ?? 100,
      HelmetVisorBrightness: p.helmet?.visorBrightness ?? 100,
      HelmetVisorSaturation: p.helmet?.visorSaturation ?? 100,
      SharpeningMode: p.sharpening?.mode ?? defaults.sharpening.mode,
      SharpeningStrength:
        p.sharpening?.strength ?? defaults.sharpening.strength,
      LumaClamp: p.sharpening?.clamp ?? defaults.sharpening.clamp,
      LumaRadius: p.sharpening?.radius ?? defaults.sharpening.radius,
      LumaPattern: p.sharpening?.pattern ?? defaults.sharpening.pattern,
      Mode: p.mode,
      CropLeft: c.left,
      CropRight: c.right,
      CropTop: c.top,
      CropBottom: c.bottom,
      RenderScale: p.view.renderScale,
      MaskEnabled: p.mask.enabled && p.mode === "driving" ? 1 : 0,
      MaskPadding: p.mask.padding,
      MaskLeft: p.mask.left.map((q) => `${q.x},${q.y}`).join(";"),
      MaskRight: (p.mask.linked ? p.mask.left : p.mask.right)
        .map((q) => `${q.x},${q.y}`)
        .join(";"),
      StreamEye: s.eye,
      Smoothing: s.smoothing,
      HorizonLock: s.horizonLock,
      ReserveLeft: s.reserve.left,
      ReserveRight: s.reserve.right,
      ReserveTop: s.reserve.top,
      ReserveBottom: s.reserve.bottom,
      ExtraLeft: p.mode === "streaming" ? s.extraLeft : 0,
      ExtraRight: p.mode === "streaming" ? s.extraRight : 0,
      OffsetX: s.offsetX,
      OffsetY: s.offsetY,
      OutputWidth: s.width,
      OutputHeight: s.height,
      OutputFps: s.fps,
      Mirror: s.mirror ? 1 : 0,
    };
    sections.push(
      `[${game}]\n` +
        Object.entries(fields)
          .map(([k, v]) => `${k}=${v}`)
          .join("\n"),
    );
  }
  return sections.join("\n\n") + "\n";
}
module.exports = { toRuntimeIni, toEnhancementsConfig, selectedForGame };
