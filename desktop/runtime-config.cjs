"use strict";
const defaults = require("../src/enhancement-defaults.json");
const colorDefaults = require("../src/color-correction-defaults.json");
// Native Mode remains a compatibility field; profiles use the explicit option.
function usesFullRenderArea(profile) {
  return profile.stream?.preserveFullFrame ?? profile.mode === 'streaming';
}
function colorFields(profile) {
  const color = {...colorDefaults,...profile?.colorCorrection};
  const fields = {ColorCorrectionEnabled:color.enabled?1:0,ColorCorrectionPreset:color.preset,ColorCorrectionShortcut:color.cycleShortcut};
  for(const id of ['sun','night','custom']) {
    const preset={...colorDefaults.presets[id],...color.presets?.[id]};
    for(const key of ['brightness','contrast','saturation','gamma'])
      fields[`Color${id[0].toUpperCase()+id.slice(1)}${key[0].toUpperCase()+key.slice(1)}`]=preset[key];
  }
  return fields;
}
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
  const q = p?.lmuRendering;
  const quadProvider = game === "lmu" && q?.mode === "quad" && q.provider === "vectorxr";
  const summary = p
    ? `Configured: ${usesFullRenderArea(p) ? 'Full render area for stream' : 'View crop applied'} | SS ${p.view.renderScale}% | ${p.view.disableCrop ? 'Crop disabled' : `Crop ${crop.left}/${crop.right}/${crop.top}/${crop.bottom}%`}`
    : "No configured game profile";
  return {
    version: 3,
    core: {
      enabled: Boolean(p && (quadProvider || f.enabled || o.enabled || p.spatialOverlays?.enabled || p.helmet?.enabled || p.colorCorrection?.enabled)),
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
      quadviews: {
        enabled: quadProvider,
        profiles: [],
        ...(quadProvider ? { defaults: {
          trackingMode: q.gaze === "fixed" ? "head" : "eye",
          peripheralScale: q.peripheralScale / 100,
          focusScale: q.focusScale / 100,
          focusHorizontalSizePercent: q.focusWidth,
          focusVerticalSizePercent: q.focusHeight,
          // The adapter owns offsets and crop for every provider.
          horizontalOffsetDegrees: 0,
          verticalOffsetDegrees: 0,
        } } : {}),
      },
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
          interpolateFlow: f.interpolateFlow,
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
    const c = p.view.disableCrop === true ? {left:0,right:0,top:0,bottom:0} : p.view.crop,
      s = p.stream;
    const fields = {
      Enabled: 1,
      ...(game === 'lmu' ? {
        LmuMode: p.lmuRendering?.mode ?? 'off',
        LmuProvider: p.lmuRendering?.provider ?? 'native',
        LmuGaze: p.lmuRendering?.gaze ?? 'auto',
        LmuFocusWidth: p.lmuRendering?.focusWidth ?? 50,
        LmuFocusHeight: p.lmuRendering?.focusHeight ?? 50,
        LmuOffsetX: p.lmuRendering?.focusOffsetX ?? 0,
        LmuOffsetY: p.lmuRendering?.focusOffsetY ?? 0,
        LmuSmoothing: p.lmuRendering?.outerSmoothing ?? 0,
        LmuShadingRate: p.lmuRendering?.peripheralShadingRate ?? 2,
        LmuNativeOuter: p.lmuRendering?.nativeOuterResolution === false ? 0 : 1,
        LmuDebugFocus: p.lmuRendering?.debugFocus ? 1 : 0,
      } : {}),
      EnhancementsEnabled: p.framegen?.enabled || p.overlay?.enabled || p.spatialOverlays?.enabled || p.helmet?.enabled || p.colorCorrection?.enabled || (game === "lmu" && p.lmuRendering?.mode === "quad" && p.lmuRendering.provider === "vectorxr") ? 1 : 0,
      ...colorFields(p),
      SharpeningMode: p.sharpening?.mode ?? defaults.sharpening.mode,
      SharpeningStrength:
        p.sharpening?.strength ?? defaults.sharpening.strength,
      LumaClamp: p.sharpening?.clamp ?? defaults.sharpening.clamp,
      LumaRadius: p.sharpening?.radius ?? defaults.sharpening.radius,
      LumaPattern: p.sharpening?.pattern ?? defaults.sharpening.pattern,
      Mode: usesFullRenderArea(p) ? 'streaming' : 'driving',
      CropLeft: c.left,
      CropRight: c.right,
      CropTop: c.top,
      CropBottom: c.bottom,
      RenderScale: p.view.renderScale,
      MaskEnabled: p.mask.enabled && !usesFullRenderArea(p) && p.view.disableCrop !== true ? 1 : 0,
      MaskPadding: p.mask.padding,
      MaskLeft: p.mask.left.map((q) => `${q.x},${q.y}`).join(";"),
      MaskRight: (p.mask.linked ? p.mask.left : p.mask.right)
        .map((q) => `${q.x},${q.y}`)
        .join(";"),
      StreamEye: s.eye,
      Smoothing: s.smoothing,
      HorizonLock: s.horizonLock,
      AdaptiveHorizon: s.adaptiveHorizon ? 1 : 0,
      PitchLock: s.pitchLock ?? 0,
      MirrorLimit: s.mirrorLimit ?? -1,
      MirrorSoftness: s.mirrorSoftness ?? 0,
      MirrorStretch: s.mirrorStretch ?? 0,
      ReserveLeft: s.reserve.left,
      ReserveRight: s.reserve.right,
      ReserveTop: s.reserve.top,
      ReserveBottom: s.reserve.bottom,
      ExtraLeft: usesFullRenderArea(p) ? s.extraLeft : 0,
      ExtraRight: usesFullRenderArea(p) ? s.extraRight : 0,
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
