export const wheelModels = Object.freeze([
  {id:"hyper-p1", name:"GSI Hyper P1", width:.30, screenHeight:620},
  {id:"hyper-sl", name:"GSI Hyper SL", width:.30, screenHeight:0},
  {id:"gxl", name:"GSI GXL V2", width:.30, screenHeight:0},
  {id:"gt-max", name:"GSI GT-MAX 32", width:.32, screenHeight:576},
  {id:"fpe2", name:"GSI FPE2", width:.30, screenHeight:604},
]);
export function hasWheelScreen(modelId) {
  return !wheelModels.some(model => model.id === modelId && model.screenHeight === 0);
}
export const recenterKeys = [
  ...Array.from({length:12}, (_,i) => 'F' + (i+1)),
  ...'ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789',
  'Space','Home','End','Insert','Delete','PageUp','PageDown','Up','Down','Left','Right','Backspace','Tab','Enter','Pause',
];
const recenterChord = new RegExp("^(?:Ctrl\\+)?(?:Alt\\+)?(?:Shift\\+)?(" + recenterKeys.join("|") + ")$");
export const spatialDefaults = () => ({ enabled: false, recenterShortcut: "", pixelBudgetMP: 60, items: [] });
export const spatialPositionLimits = Object.freeze({ x: [-5, 5], y: [-5, 5], z: [-10, -0.1] });
export const steeringDefaults = Object.freeze({ steeringFollow: false, pivotX: 0, pivotY: 0, pivotZ: 0, steeringTilt: 0 });
export const poseRanges = Object.freeze({ ...spatialPositionLimits, yaw: [-180,180], pitch: [-180,180], roll: [-180,180], widthMeters: [0.1,3] });
export const steeringRanges = Object.freeze({ pivotX: [-5,5], pivotY: [-5,5], pivotZ: [-5,5], steeringTilt: [-90,90] });
const placementKeys = ["anchor", ...Object.keys(poseRanges), ...Object.keys(steeringDefaults)];
export function overlayPlacement(item, carId) {
  return (item.kind === "url" && carId && item.carPlacements?.find(pose => pose.carId === carId)) || item;
}
export function updateCarPlacement(item, carId, patch) {
  if (item.kind !== "url" || !carId) throw new Error("Choose a dashboard and enter a car to save its placement.");
  const placements = item.carPlacements || [];
  const current = { ...steeringDefaults, ...overlayPlacement(item, carId), ...patch };
  const pose = { carId, ...Object.fromEntries(placementKeys.map(key => [key, current[key]])) };
  if (placements.some(entry => entry.carId === carId))
    return { ...item, carPlacements: placements.map(entry => entry.carId === carId ? pose : entry) };
  if (placements.length >= 256) throw new Error("This dashboard already has 256 saved car placements.");
  return { ...item, carPlacements: [...placements, pose] };
}
export function createSpatialOverlay() {
  return { id: globalThis.crypto.randomUUID(), name: "New overlay", enabled: false, showInStream: false, cockpitOcclusion: false, kind: "url", url: "", windowId: "", displayId: "", shortcut: "", interaction: "mouse", precisionScale: 1.75, precisionShortcut: "", width: 1024, height: 576, fps: 10,
    ...steeringDefaults, anchor: "local", x: 0, y: -0.35, z: -1, yaw: 0, pitch: 0, roll: 0, widthMeters: 0.6, opacity: 100 };
}
export function createQuickMenuOverlay() {
  return { ...createSpatialOverlay(), name: "OpenGrid menu", kind: "menu", interaction: "head", anchor: "local", showInStream: false,
    width: 1024, height: 768, fps: 15, x: 0, y: 0, z: -1, widthMeters: 0.9, shortcut: "Ctrl+Alt+F5", selectShortcut: "Ctrl+Alt+F4" };
}
// Menu keys share the toolkit shortcut namespace, including saved inactive sources.
export function spatialShortcutEntries(profile) {
  return [
    ["overlay", "toggleShortcut", profile.overlay?.toggleShortcut ?? "Ctrl+Alt+F10"],
    ["overlay", "cycleShortcut", profile.overlay?.cycleShortcut ?? "Ctrl+Alt+F11"],
    ["colorCorrection", "cycleShortcut", profile.colorCorrection?.cycleShortcut ?? "Ctrl+Alt+F6"],
    ["spatialOverlays", "recenterShortcut", profile.spatialOverlays?.recenterShortcut || ""],
    ...(profile.spatialOverlays?.items || []).flatMap(item => [
      [item.id, "shortcut", item.shortcut || ""], [item.id, "precisionShortcut", item.precisionShortcut || ""],
      ...(item.kind === "url" ? [[item.id, "resetShortcut", item.resetShortcut || ""]] : []),
      ...(item.kind === "menu" ? [[item.id, "selectShortcut", item.selectShortcut || ""]] : []),
    ]),
  ];
}
export function occupiedShortcuts(profile, ownerId, field) {
  const menuIds = new Set(profile.spatialOverlays?.items?.filter(item => item.kind === "menu").map(item => item.id));
  return spatialShortcutEntries(profile)
    .filter(([id, name]) => id !== ownerId || name !== field)
    .filter(([id, name]) => {
      // Preserve the existing HUD and color shortcut combinations.
      if (!["overlay", "colorCorrection"].includes(ownerId)) return true;
      return name === "resetShortcut" || name === "recenterShortcut" || menuIds.has(id) ||
        (ownerId === "overlay" && id === "overlay");
    })
    .map(([, , chord]) => chord)
    .filter(Boolean);
}
export function validateSpatialShortcuts(profile) {
  const entries = spatialShortcutEntries(profile);
  for (const [id, field, value] of entries) {
    const menu = profile.spatialOverlays?.items?.some(item => item.id === id && item.kind === "menu");
    if (value && (menu || field === "recenterShortcut" || field === "resetShortcut") &&
        entries.some(([owner, key, chord]) => chord === value && (owner !== id || key !== field)))
      throw new Error("Spatial overlay shortcut conflicts with another toolkit shortcut.");
  }
}
function fail() { throw new Error("Invalid spatial overlay settings."); }
const range = (v, low, high, integer = false) => typeof v === "number" && Number.isFinite(v) && v >= low && v <= high && (!integer || Number.isInteger(v));
function validatePlacement(p) {
  if (!p || !["head", "local"].includes(p.anchor)) fail();
  if (p.steeringFollow !== undefined && typeof p.steeringFollow !== "boolean") fail();
  for (const [key, bounds] of Object.entries(poseRanges)) if (!range(p[key], ...bounds)) fail();
  for (const [key, bounds] of Object.entries(steeringRanges)) if (p[key] !== undefined && !range(p[key], ...bounds)) fail();
}
export function validateSpatialOverlays(s) {
  if (!s || typeof s.enabled !== "boolean" || !range(s.pixelBudgetMP, 15, 180, true) || !Array.isArray(s.items) || s.items.length > 15) fail();
  const validShortcut = value => value === undefined || (typeof value === "string" && (value === "" || /^(?:Ctrl\+)?(?:Alt\+)?(?:Shift\+)?F(?:[1-9]|1[0-2])$/.test(value)));
  if (s.recenterShortcut !== undefined && s.recenterShortcut !== "" &&
      (typeof s.recenterShortcut !== "string" || !recenterChord.test(s.recenterShortcut))) fail();
  const ids = new Set();
  let menuCount = 0;
  for (const o of s.items) {
    if (!o || typeof o.id !== "string" || !/^[0-9a-f-]{36}$/i.test(o.id) || ids.has(o.id) ||
        typeof o.name !== "string" || !o.name.trim() || o.name.length > 80 || /[\x00-\x1f]/.test(o.name) || typeof o.enabled !== "boolean" ||
        !["url", "window", "desktop", "menu"].includes(o.kind) || !["head", "local"].includes(o.anchor) ||
        typeof o.url !== "string" || o.url.length > 2048 || typeof o.windowId !== "string" || o.windowId.length > 100 ||
        (o.windowId !== "" && !/^window:\d+:\d+$/.test(o.windowId))) fail();
    if (!validShortcut(o.resetShortcut) || (o.resetShortcut && o.kind !== "url")) fail();
    if (o.wheelModel !== undefined && (typeof o.wheelModel !== "string" || (o.wheelModel &&
        (o.kind !== "url" || (!wheelModels.some(model=>model.id===o.wheelModel) && !/^[a-f0-9]{64}$/.test(o.wheelModel)))))) fail();
    if (o.wheelLights !== undefined && typeof o.wheelLights !== "boolean") fail();
    if (o.wheelAmbientLight !== undefined && !range(o.wheelAmbientLight, 0, 100)) fail();
    if (o.wheelDevice !== undefined && !range(o.wheelDevice,0,5,true)) fail();
    if (o.wheelName !== undefined && (typeof o.wheelName !== "string" || o.wheelName.length>80 || /[\x00-\x1f]/.test(o.wheelName))) fail();
    if (o.showInStream !== undefined && typeof o.showInStream !== "boolean") fail();
    if (o.cockpitOcclusion !== undefined && typeof o.cockpitOcclusion !== "boolean") fail();
    validatePlacement(o);
    if (o.carPlacements !== undefined) {
      if (!Array.isArray(o.carPlacements) || o.carPlacements.length > 256 || (o.kind !== "url" && o.carPlacements.length)) fail();
      const carIds = new Set();
      for (const pose of o.carPlacements) {
        validatePlacement(pose);
        if (typeof pose.carId !== "string" || pose.carId.length > 64 || !/^(?:iracing-[1-9]\d*|lmu-[0-9a-f]+)$/.test(pose.carId) || carIds.has(pose.carId) || Object.keys(pose).some(key => key !== "carId" && !placementKeys.includes(key))) fail();
        carIds.add(pose.carId);
      }
    }
    if (o.displayId !== undefined && (typeof o.displayId !== "string" || (o.displayId !== "" && !/^-?\d{1,20}$/.test(o.displayId)))) fail();
    if (o.shortcut !== undefined && (typeof o.shortcut !== "string" || (o.shortcut !== "" && !/^(?:Ctrl\+)?(?:Alt\+)?(?:Shift\+)?F(?:[1-9]|1[0-2])$/.test(o.shortcut)))) fail();
    // Accept the former experiment only to migrate existing profiles on load.
    if (o.interaction !== undefined && !["mouse", "head", "gaze"].includes(o.interaction)) fail();
    if (o.winkClicks !== undefined && typeof o.winkClicks !== "boolean") fail();
    if (o.precisionScale !== undefined && !range(o.precisionScale, 1, 3)) fail();
    if (o.precisionShortcut !== undefined && (typeof o.precisionShortcut !== "string" || (o.precisionShortcut !== "" && !/^(?:Ctrl\+)?(?:Alt\+)?(?:Shift\+)?F(?:[1-9]|1[0-2])$/.test(o.precisionShortcut)))) fail();
    if (o.precisionShortcut && o.precisionShortcut === o.shortcut) fail();
    if (o.selectShortcut !== undefined && (typeof o.selectShortcut !== "string" || (o.selectShortcut !== "" && !/^(?:Ctrl\+)?(?:Alt\+)?(?:Shift\+)?F(?:[1-9]|1[0-2])$/.test(o.selectShortcut)))) fail();
    if (o.kind === "menu" && (++menuCount > 1 || o.anchor !== "local" || o.interaction !== "head" || o.showInStream !== false || o.fps > 15 || o.url || o.windowId || o.displayId || o.precisionShortcut)) fail();
    if (o.kind === "menu" && o.shortcut && o.shortcut === o.selectShortcut) throw new Error("Menu shortcut conflicts with selection shortcut.");
    ids.add(o.id);
    if (o.url) {
      let url; try { url = new URL(o.url); } catch { fail(); }
      if (!["http:", "https:"].includes(url.protocol) || url.username || url.password || /[\x00-\x20]/.test(o.url)) fail();
    }
    for (const [key, low, high, integer] of [["width",256,1920,true],["height",128,1080,true],["fps",1,30,true],["opacity",0,100]])
      if (!range(o[key], low, high, integer)) fail();
  }
}
export function validateCentering(c) {
  if (!c || typeof c.gameShortcut !== "string" || (c.gameShortcut !== "" && !/^(?:Ctrl\+)?(?:Alt\+)?(?:Shift\+)?F(?:[1-9]|1[0-2])$/.test(c.gameShortcut)) || !range(c.delaySeconds, 1, 5, true)) throw new Error("Invalid VR centering settings.");
}
export function effectiveOverlayRates(settings) {
  const budget = settings.pixelBudgetMP * 1e6;
  const requested = settings.items.map((o) => settings.enabled && o.enabled && o.opacity > 0 ? o.fps : 0);
  const total = settings.items.reduce((n, o, i) => n + o.width * o.height * requested[i], 0);
  const factor = total ? Math.min(1, budget / total) : 0;
  let remaining = budget;
  // Order is priority when the budget cannot fund even 1 Hz for every source.
  return settings.items.map((o, i) => {
    if (!requested[i]) return 0;
    const pixels = o.width * o.height;
    const fps = Math.max(0, Math.min(Math.max(1, Math.floor(requested[i] * factor)), Math.floor(remaining / pixels)));
    remaining -= fps * pixels;
    return fps;
  });
}
