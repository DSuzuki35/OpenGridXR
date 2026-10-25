import {
  framegenDefaults,
  overlayDefaults,
  sharpeningDefaults,
  nativeFoveationDefaults,
  validateEnhancements,
} from "./enhancements.mjs";
const clone = (value) => structuredClone(value);
import { spatialDefaults, centeringDefaults, validateSpatialOverlays, validateCentering } from "./spatial-overlays.mjs";
import { bindingDefaults, validateBindings } from "./input-bindings.mjs";
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
    mode: "driving",
    framegen: framegenDefaults(),
    overlay: overlayDefaults(),
    sharpening: sharpeningDefaults(),
    nativeFoveation: nativeFoveationDefaults(),
    telemetry: { enabled: false, updateHz: 20 },
    spatialOverlays: spatialDefaults(),
    centering: centeringDefaults(),
    inputBindings: bindingDefaults(),
    view: { crop: { left: 0, right: 0, top: 0, bottom: 0 }, renderScale: 100 },
    helmet: {
      enabled: false,
      style: "formula",
      opacity: 100,
      scale: 100,
      offsetX: 0,
      offsetY: 0,
      visorTint: 0,
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
      eye: 1,
      smoothing: 40,
      horizonLock: 0,
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
      displayId: "primary",
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
export function effectiveMask(profile) {
  return profile.mask.enabled && profile.mode === "driving";
}
export function pixelBudget(profile) {
  const c = profile.view.crop,
    retainedPercent =
      ((100 - c.left - c.right) * (100 - c.top - c.bottom)) / 100;
  const sourcePercent =
    profile.mode === "streaming"
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
    streaming: profile.mode === "streaming",
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
  if (p.spatialOverlays !== undefined) validateSpatialOverlays(p.spatialOverlays);
  if (p.centering !== undefined) {
    validateCentering(p.centering);
    const centerKey = p.centering.gameShortcut;
    if (centerKey && [p.framegen?.toggleShortcut ?? "Ctrl+Alt+F9", p.overlay?.toggleShortcut ?? "Ctrl+Alt+F10", p.overlay?.cycleShortcut ?? "Ctrl+Alt+F11"].includes(centerKey)) throw new Error("VR centering shortcut conflicts with a toolkit shortcut.");
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
  choice(p.mode, ["driving", "streaming"], "mode");
  obj(p.view, "View");
  obj(p.view.crop, "Crop");
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
  choice(p.helmet.style, ["formula", "gt", "open"], "helmet style");
  num(p.helmet.opacity, "Opacity", 0, 100);
  num(p.helmet.scale, "Helmet scale", 50, 150);
  num(p.helmet.offsetX, "Helmet X", -100, 100);
  num(p.helmet.offsetY, "Helmet Y", -100, 100);
  num(p.helmet.visorTint, "Visor tint", 0, 100);
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
  choice(p.stream.eye, [0, 1], "eye");
  num(p.stream.smoothing, "Smoothing", 0, 100);
  num(p.stream.horizonLock, "Horizon lock", 0, 100);
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
export function parseProfile(textValue) {
  if (textValue.length > 1024 * 1024) fail("Profile file is too large.");
  const value = JSON.parse(textValue);
  obj(value, "Profile");
  safeTree(value);
  if (value.profile !== undefined && value.version !== 1)
    fail("Unsupported profile exchange version.");
  const p = value.profile ?? value;
  validateProfile(p);
  return clone(p);
}
