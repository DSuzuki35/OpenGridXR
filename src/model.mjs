import {
  overlayDefaults,
  sharpeningDefaults,
  validateEnhancements,
} from "./enhancements.mjs";
const clone = (value) => structuredClone(value);
export const MAX_PROFILE_BYTES = 8 * 1024 * 1024;
import { spatialDefaults, spatialShortcutEntries, validateSpatialOverlays, validateSpatialShortcuts, validateCentering } from "./spatial-overlays.mjs";
import { bindingDefaults, validateBindings } from "./input-bindings.mjs";
import { visorDefaults, validateVisor } from "./helmet.mjs";
import { lmuRenderingDefaults, validateLmuRendering } from './lmu-rendering.mjs';
import {colorCorrectionDefaults, validateColorCorrection, colorPresetIsIdentity} from './color-correction.mjs';
const PRESETS = {
  formula: [
    [0.12, 0.22],
    [0.3, 0.12],
    [0.7, 0.12],
    [0.88, 0.22],
    [0.94, 0.46],
    [0.86, 0.7],
    [0.66, 0.8],
    [0.34, 0.8],
    [0.14, 0.7],
    [0.06, 0.46],
  ],
  gt: [
    [0.16, 0.18],
    [0.36, 0.13],
    [0.64, 0.13],
    [0.84, 0.18],
    [0.94, 0.4],
    [0.91, 0.66],
    [0.73, 0.82],
    [0.27, 0.82],
    [0.09, 0.66],
    [0.06, 0.4],
  ],
  open: [
    [0.08, 0.12],
    [0.3, 0.07],
    [0.7, 0.07],
    [0.92, 0.12],
    [0.97, 0.47],
    [0.9, 0.88],
    [0.65, 0.94],
    [0.35, 0.94],
    [0.1, 0.88],
    [0.03, 0.47],
  ],
};
export function contourPreset(style = "formula") {
  return (PRESETS[style] || PRESETS.formula).map(([x, y]) => ({ x, y }));
}
export function createProfile(game = "iracing", name) {
  if (!["iracing", "lmu"].includes(game)) throw new Error("Unsupported game.");
  return {
    id: globalThis.crypto.randomUUID(),
    name:
      name ||
      (game === "iracing"
        ? "iRacing · baseline"
        : "Le Mans Ultimate · baseline"),
    game,
    overlay: overlayDefaults(),
    sharpening: sharpeningDefaults(),
    lmuRendering: lmuRenderingDefaults(),
    colorCorrection: colorCorrectionDefaults(),
    telemetry: { enabled: false, updateHz: 20 },
    spatialOverlays: spatialDefaults(),
    inputBindings: bindingDefaults(),
    view: { crop: { left: 0, right: 0, top: 0, bottom: 0 }, renderScale: 100, disableCrop:false },
    helmet: {
      enabled: false,
      style: "formula",
      opacity: 100,
      scale: 100,
      openingWidth: 100,
      shellBrightness: 100,
      shellContrast: 100,
      shellSaturation: 100,
      shellBlur: 0,
      visorPosition: "closed",
      visorHotkey: false,
      detail: "performance",
      offsetX: 0,
      offsetY: 0,
      stereoOffset: 0,
      ambientLight: 0,
      visorTint: 0,
      visorTintColor: "#142035",
      visor: visorDefaults(),
    },
    mask: {
      enabled: false,
      linked: true,
      padding: 3,
      name: "Formula aperture",
      left: contourPreset(),
      right: contourPreset(),
    },
    stream: {
      preserveFullFrame: false,
      eye: 1,
      stabilize: true,
      smoothing: 40, separateSmoothing: false, horizontalSmoothing: 40, verticalSmoothing: 40,
      horizonLock: 0,
      adaptiveHorizon: false,
      pitchLock: 0,
      reserve: { left: 5, right: 5, top: 5, bottom: 5 },
      extraLeft: 10,
      extraRight: 0,
      extraSymmetric: false,
      offsetX: 0,
      offsetY: 0,
      width: 1920,
      height: 1080,
      fps: 60,
      mirror: false,
      mirrorLimit: 50,
      mirrorSoftness: 0,
      mirrorStretch: 0,
      helmet: { enabled: false, scale: 100, offsetX: 0, offsetY: 0, opacity: 100, ambientLight: 0 },
      displayId: "primary",
      idleOutput: "wait",
      cockpitOnly: false,
      outsideCarOutput: "clone",
      cloneDisplayId: "primary",
      fullscreen: true,
    },
  };
}
export function createDocument() {
  const profiles = [createProfile("iracing"), createProfile("lmu")];
  return { version: 1, selectedProfileId: profiles[0].id, profiles };
}
export function polygonArea(points) {
  if (!Array.isArray(points) || points.length < 3) return 0;
  return (
    Math.abs(
      points.reduce((a, p, i) => {
        const q = points[(i + 1) % points.length];
        return a + p.x * q.y - q.x * p.y;
      }, 0),
    ) / 2
  );
}
function cross(a, b, c) {
  return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}
function on(a, b, p) {
  return (
    Math.abs(cross(a, b, p)) < 1e-9 &&
    p.x >= Math.min(a.x, b.x) - 1e-9 &&
    p.x <= Math.max(a.x, b.x) + 1e-9 &&
    p.y >= Math.min(a.y, b.y) - 1e-9 &&
    p.y <= Math.max(a.y, b.y) + 1e-9
  );
}
function intersects(a, b, c, d) {
  const ab1 = cross(a, b, c),
    ab2 = cross(a, b, d),
    cd1 = cross(c, d, a),
    cd2 = cross(c, d, b);
  return (
    (ab1 * ab2 < 0 && cd1 * cd2 < 0) ||
    on(a, b, c) ||
    on(a, b, d) ||
    on(c, d, a) ||
    on(c, d, b)
  );
}
export function isSimplePolygon(points) {
  if (!Array.isArray(points) || points.length < 3) return false;
  for (let i = 0; i < points.length; i++) {
    const a = points[i],
      b = points[(i + 1) % points.length];
    if (
      !a ||
      !b ||
      ![a.x, a.y, b.x, b.y].every(Number.isFinite) ||
      Math.hypot(a.x - b.x, a.y - b.y) < 1e-6
    )
      return false;
    for (let j = i + 1; j < points.length; j++) {
      if (j === (i + 1) % points.length || i === (j + 1) % points.length)
        continue;
      if (intersects(a, b, points[j], points[(j + 1) % points.length]))
        return false;
    }
  }
  return polygonArea(points) > 0.000001;
}
export function usesFullRenderArea(profile) {
  return profile.stream?.preserveFullFrame ?? profile.mode === "streaming";
}
export function effectiveMask(profile) {
  return false; // Custom visibility masks are retired; retain legacy profile data only.
}
export function effectiveCrop(profile) {
  return profile.view.disableCrop === true ? {left:0,right:0,top:0,bottom:0} : profile.view.crop;
}
export function pixelBudget(profile) {
  const c = effectiveCrop(profile),
    retainedPercent =
      ((100 - c.left - c.right) * (100 - c.top - c.bottom)) / 100;
  const sourcePercent =
    usesFullRenderArea(profile)
      ? 100 + profile.stream.extraLeft + profile.stream.extraRight
      : retainedPercent;
  const renderedPercent = sourcePercent * (profile.view.renderScale / 100) ** 2;
  return {
    retainedPercent,
    renderedPercent,
    renderPercent: renderedPercent,
    maskPercent:
      (polygonArea(profile.mask.left) + polygonArea(profile.mask.right)) * 50,
    maskActive: effectiveMask(profile),
    streaming: usesFullRenderArea(profile),
  };
}
function fail(message) {
  throw new Error(message);
}
function obj(v, label) {
  if (!v || typeof v !== "object" || Array.isArray(v))
    fail(`${label} must be an object.`);
}
function text(v, label, max = 100) {
  if (
    typeof v !== "string" ||
    !v.trim() ||
    v.length > max ||
    /[\u0000-\u001f]/.test(v)
  )
    fail(`${label} must be 1-${max} characters.`);
}
function num(v, label, min, max, integer = false) {
  if (
    typeof v !== "number" ||
    !Number.isFinite(v) ||
    v < min ||
    v > max ||
    (integer && !Number.isInteger(v))
  )
    fail(
      `${label} must be ${integer ? "an integer " : ""}between ${min} and ${max}.`,
    );
}
function bool(v, label) {
  if (typeof v !== "boolean") fail(`${label} must be true or false.`);
}
function choice(v, allowed, label) {
  if (!allowed.includes(v)) fail(`Unsupported ${label}.`);
}
function safeTree(v, depth = 0) {
  if (depth > 20) fail("Profile nesting is too deep.");
  if (v && typeof v === "object")
    for (const [key, value] of Object.entries(v)) {
      if (["__proto__", "constructor", "prototype"].includes(key))
        fail("Unsafe profile key.");
      safeTree(value, depth + 1);
    }
  else if (typeof v === "number" && !Number.isFinite(v))
    fail("Non-finite profile value.");
}
export function validateProfile(p) {
  obj(p, "Profile");
  safeTree(p);
  validateEnhancements(p);
  validateLmuRendering(p.lmuRendering, p.game);
  if (p.colorCorrection !== undefined) validateColorCorrection(p.colorCorrection);
  if (p.spatialOverlays !== undefined) validateSpatialOverlays(p.spatialOverlays);
  validateSpatialShortcuts(p);
  if (p.centering !== undefined) {
    validateCentering(p.centering);
    const centerKey = p.centering.gameShortcut;
    if (centerKey && [p.overlay?.toggleShortcut ?? "Ctrl+Alt+F10", p.overlay?.cycleShortcut ?? "Ctrl+Alt+F11"].includes(centerKey)) throw new Error("VR centering shortcut conflicts with a toolkit shortcut.");
  }
  if (p.telemetry !== undefined) {
    if (!p.telemetry || typeof p.telemetry.enabled !== "boolean" ||
        !Number.isInteger(p.telemetry.updateHz) || p.telemetry.updateHz < 1 || p.telemetry.updateHz > 30)
      throw new Error("Invalid telemetry settings.");
  }
  text(p.id, "Profile ID", 80);
  if (!/^[a-zA-Z0-9_-]+$/.test(p.id)) fail("Invalid profile ID.");
  text(p.name, "Profile name");
  choice(p.game, ["iracing", "lmu"], "game");
  if (p.mode !== undefined) choice(p.mode, ["driving", "streaming"], "mode");
  obj(p.view, "View");
  obj(p.view.crop, "Crop");
  if (p.view.disableCrop !== undefined) bool(p.view.disableCrop, "Disable crop");
  num(p.view.renderScale, "Render scale", 50, 200);
  for (const k of ["left", "right", "top", "bottom"])
    num(p.view.crop[k], `Crop ${k}`, 0, 40);
  if (
    p.view.crop.left + p.view.crop.right > 70 ||
    p.view.crop.top + p.view.crop.bottom > 70
  )
    fail("Crop must retain at least 30% on each axis.");
  obj(p.helmet, "Helmet");
  bool(p.helmet.enabled, "Helmet enabled");
  if (p.helmet.visorPosition !== undefined) choice(p.helmet.visorPosition, ["closed", "half", "open"], "visor position");
  if (p.helmet.visorHotkey !== undefined) bool(p.helmet.visorHotkey, "Visor shortcut");
  if (p.helmet.detail !== undefined) choice(p.helmet.detail, ["performance", "3200", "detail"], "helmet detail");
  choice(p.helmet.style, ["formula", "gt", "open", "level3"], "helmet style");
  num(p.helmet.opacity, "Opacity", 0, 100);
  num(p.helmet.scale, "Helmet scale", 50, 150);
  if (p.helmet.openingAngle !== undefined) num(p.helmet.openingAngle, "Vertical visor opening", 25, 100);
  if (p.helmet.openingWidth !== undefined) num(p.helmet.openingWidth, "Visor opening width", 50, 150);
  if (p.helmet.shellBlur !== undefined) num(p.helmet.shellBlur, "Helmet interior blur", 0, 8);
  for (const [key, max] of [['shellBrightness', 150], ['shellContrast', 200], ['shellSaturation', 200]])
    if (p.helmet[key] !== undefined) num(p.helmet[key], key, 0, max);
  if (p.helmet.ambientLight !== undefined) num(p.helmet.ambientLight, "Helmet scene lighting", 0, 100);
  if (p.helmet.stereoOffset !== undefined) num(p.helmet.stereoOffset, "Helmet stereo alignment", -5, 5);
  num(p.helmet.offsetX, "Helmet X", -100, 100);
  num(p.helmet.offsetY, "Helmet Y", -100, 100);
  num(p.helmet.visorTint, "Visor tint", 0, 100);
  if (p.helmet.visorTintColor !== undefined && !/^#[a-fA-F0-9]{6}$/.test(p.helmet.visorTintColor)) fail("Visor tint color must be a six-digit hex color.");
  for (const key of ["visorContrast", "visorBrightness"]) if (p.helmet[key] !== undefined) num(p.helmet[key], key, 50, 150);
  if (p.helmet.visorSaturation !== undefined) num(p.helmet.visorSaturation, "Visor saturation", 0, 150);
  validateVisor(p.helmet.visor);
  obj(p.mask, "Mask");
  bool(p.mask.enabled, "Mask enabled");
  bool(p.mask.linked, "Linked eyes");
  num(p.mask.padding, "Mask padding", 0, 10);
  text(p.mask.name, "Contour name");
  for (const eye of ["left", "right"]) {
    const pts = p.mask[eye];
    if (!Array.isArray(pts) || pts.length < 3 || pts.length > 64)
      fail("Contour needs 3-64 points.");
    for (const pt of pts) {
      obj(pt, "Contour point");
      num(pt.x, "Contour X", 0, 1);
      num(pt.y, "Contour Y", 0, 1);
    }
    if (!isSimplePolygon(pts) || polygonArea(pts) < 0.04)
      fail("Contour must be a simple polygon with at least 4% visible area.");
  }
  obj(p.stream, "Stream");
  if (p.stream.preserveFullFrame !== undefined) bool(p.stream.preserveFullFrame, "Keep full render area for stream");
  choice(p.stream.eye, [0, 1], "eye");
  if (p.stream.separateSmoothing !== undefined && typeof p.stream.separateSmoothing !== "boolean") fail("Invalid advanced smoothing setting.");
  for (const key of ["horizontalSmoothing", "verticalSmoothing"])
    if (p.stream[key] !== undefined) num(p.stream[key], key, 0, 100);
  num(p.stream.smoothing, "Smoothing", 0, 100);
  if (p.stream.stabilize !== undefined) bool(p.stream.stabilize, "Camera stabilization");
  num(p.stream.horizonLock, "Horizon lock", 0, 100);
  if (p.stream.adaptiveHorizon !== undefined) bool(p.stream.adaptiveHorizon, "Adaptive roll leveling");
  for (const k of ["pitchLock", "mirrorSoftness", "mirrorStretch"])
    if (p.stream[k] !== undefined) num(p.stream[k], k, 0, 100);
  if (p.stream.mirrorLimit !== undefined) num(p.stream.mirrorLimit, "Mirror limit", 0, 50);
  if (p.stream.helmet !== undefined) {
    obj(p.stream.helmet, "Stream helmet");
    bool(p.stream.helmet.enabled, "Stream helmet enabled");
    num(p.stream.helmet.scale, "Stream helmet scale", 50, 200);
    if (p.stream.helmet.shellBlur !== undefined) num(p.stream.helmet.shellBlur, "Stream helmet blur", 0, 8);
    num(p.stream.helmet.opacity, "Stream helmet opacity", 0, 100);
    if (p.stream.helmet.ambientLight !== undefined) num(p.stream.helmet.ambientLight, "Stream helmet scene lighting", 0, 100);
    for (const k of ["offsetX", "offsetY"]) num(p.stream.helmet[k], `Stream helmet ${k}`, -100, 100);
  }
  obj(p.stream.reserve, "Reserve");
  for (const k of ["left", "right", "top", "bottom"])
    num(p.stream.reserve[k], `Reserve ${k}`, 0, 30);
  for (const k of ["extraLeft", "extraRight"]) num(p.stream[k], k, 0, 30);
  if (p.stream.extraSymmetric !== undefined)
    bool(p.stream.extraSymmetric, "Symmetric extra area");
  if (p.stream.extraSymmetric && p.stream.extraLeft !== p.stream.extraRight)
    fail("Symmetric extra area requires equal left and right values.");
  for (const k of ["offsetX", "offsetY"]) num(p.stream[k], k, -100, 100);
  num(p.stream.width, "Output width", 640, 7680, true);
  num(p.stream.height, "Output height", 360, 4320, true);
  choice(p.stream.fps, [30, 60, 90, 120], "output frame rate");
  bool(p.stream.mirror, "Mirror edges");
  for (const key of ["idleOutput", "outsideCarOutput"])
    if (p.stream[key] !== undefined) choice(p.stream[key], ["wait","transparent","close","clone"], key);
  if (p.stream.cockpitOnly !== undefined) bool(p.stream.cockpitOnly, "In-car output only");
  if (p.stream.cloneDisplayId !== undefined && (typeof p.stream.cloneDisplayId !== "string" || !/^(primary|-?\d{1,20})$/.test(p.stream.cloneDisplayId)))
    fail("Invalid clone display.");
  if (p.stream.displayId !== undefined) {
    text(p.stream.displayId, "Output display", 80);
    if (!/^(primary|-?\d+)$/.test(p.stream.displayId))
      fail("Invalid output display.");
  }
  if (p.stream.fullscreen !== undefined)
    bool(p.stream.fullscreen, "Fullscreen output");
  validateBindings(p.inputBindings, p);
  return p;
}
export function validateDocument(doc) {
  obj(doc, "Document");
  safeTree(doc);
  if (doc.version !== 1) fail("Unsupported profile version.");
  if (
    !Array.isArray(doc.profiles) ||
    !doc.profiles.length ||
    doc.profiles.length > 100
  )
    fail("Document needs 1-100 profiles.");
  const ids = new Set();
  for (const p of doc.profiles) {
    validateProfile(p);
    if (ids.has(p.id)) fail("Duplicate profile ID.");
    ids.add(p.id);
  }
  if (!ids.has(doc.selectedProfileId)) fail("Selected profile does not exist.");
  if (doc.activeProfiles !== undefined) {
    obj(doc.activeProfiles, "Active profiles");
    for (const [game, id] of Object.entries(doc.activeProfiles)) {
      choice(game, ["iracing", "lmu"], "active profile game");
      text(id, "Active profile ID", 80);
    }
  }
  return doc;
}
export function parseProfile(textValue, options) {
  if (textValue.length > MAX_PROFILE_BYTES) fail("Profile file is too large.");
  const value = JSON.parse(textValue);
  obj(value, "Profile");
  safeTree(value);
  if (value.profile !== undefined && value.version !== 1)
    fail("Unsupported profile exchange version.");
  const p = value.profile ?? value;
  validateProfile(p);
  return normalizeProfile(p, options);
}
// Retired iRacing INI preferences have no runtime meaning. Normalize copies so
// loading old files remains read-only and the next save/export drops this data.
export function normalizeProfile(profile, {helmetModuleEnabled = true} = {}) {
  const result = clone(profile);
  if (result.mask) result.mask.enabled = false;
  result.stream.separateSmoothing ??= false;
  result.stream.horizontalSmoothing ??= result.stream.smoothing;
  result.stream.verticalSmoothing ??= result.stream.smoothing;
  delete result.framegen;
  delete result.overlay?.showFramegen;
  if (result.inputBindings) result.inputBindings.bindings = result.inputBindings.bindings.filter(
    binding => binding.actionId !== 'framegen.enabled' && binding.actionId !== 'overlay.showFramegen');
  delete result.lmuRendering?.outerSmoothing;
  result.stream.preserveFullFrame = usesFullRenderArea(result);
  delete result.mode;
  if (result.centering) {
    const spatial = result.spatialOverlays ||= spatialDefaults();
    spatial.recenterShortcut ??= "";
    const legacyKey = result.centering.gameShortcut;
    if (!spatial.recenterShortcut && legacyKey && !spatialShortcutEntries(result).some(([, , key]) => key === legacyKey))
      spatial.recenterShortcut = legacyKey;
    delete result.centering;
  }
  for (const binding of result.inputBindings?.bindings || [])
    if (binding.actionId === "centering.game") binding.actionId = "spatialOverlays.recenter";
  if (result.colorCorrection === undefined) {
    result.colorCorrection = colorCorrectionDefaults();
    const helmet = result.helmet || {};
    const custom = {brightness:helmet.visorBrightness ?? 100,contrast:helmet.visorContrast ?? 100,saturation:helmet.visorSaturation ?? 100,gamma:100};
    if (!colorPresetIsIdentity(custom)) {
      result.colorCorrection.presets.custom = custom;
      result.colorCorrection.preset = 'custom';
      result.colorCorrection.enabled = helmetModuleEnabled && helmet.enabled === true && !helmet.visorHotkey && (helmet.visorPosition ?? 'closed') === 'closed';
    }
  }
  for (const key of ['visorBrightness','visorContrast','visorSaturation','headMotion','vehicleMotion']) delete result.helmet?.[key];
  delete result.nativeFoveation;
  for (const item of result.spatialOverlays?.items || []) {
    item.cockpitOcclusion = item.kind === "url" && item.cockpitOcclusion === true;
    if (item.kind === 'menu') {
      Object.assign(item, {anchor:'local', interaction:'head', showInStream:false,
        url:'', windowId:'', displayId:'', precisionShortcut:'',
        width:item.width ?? 1024, height:item.height ?? 768,
        fps:Math.min(item.fps ?? 15, 15),
        shortcut:item.shortcut ?? 'Ctrl+Alt+F5', selectShortcut:item.selectShortcut ?? 'Ctrl+Alt+F4'});
    }
    if (item.interaction === "gaze") item.interaction = "head";
    delete item.winkClicks;
  }
  return result;
}
export function normalizeDocument(document, options) {
  return {...clone(document), profiles:document.profiles.map(profile=>normalizeProfile(profile,options))};
}
