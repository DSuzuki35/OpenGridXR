export const spatialDefaults = () => ({ enabled: false, pixelBudgetMP: 60, items: [] });
export const centeringDefaults = () => ({ gameShortcut: "", delaySeconds: 3 });
export const spatialPositionLimits = Object.freeze({ x: [-5, 5], y: [-5, 5], z: [-10, -0.1] });
export function createSpatialOverlay() {
  return { id: globalThis.crypto.randomUUID(), name: "New overlay", enabled: false, showInStream: false, kind: "url", url: "", windowId: "", displayId: "", shortcut: "", interaction: "mouse", precisionScale: 1.75, precisionShortcut: "", width: 1024, height: 576, fps: 10,
    anchor: "local", x: 0, y: -0.35, z: -1, yaw: 0, pitch: 0, roll: 0, widthMeters: 0.6, opacity: 100 };
}
export function createQuickMenuOverlay() {
  return { ...createSpatialOverlay(), name: "OpenGrid menu", kind: "menu", interaction: "head", anchor: "local", showInStream: false,
    width: 1024, height: 768, fps: 15, x: 0, y: 0, z: -1, widthMeters: 0.9, shortcut: "Ctrl+Alt+F5", selectShortcut: "Ctrl+Alt+F4" };
}
// Menu keys share the toolkit shortcut namespace, including saved inactive sources.
export function spatialShortcutEntries(profile) {
  return [
    ["framegen", "toggleShortcut", profile.framegen?.toggleShortcut ?? "Ctrl+Alt+F9"],
    ["overlay", "toggleShortcut", profile.overlay?.toggleShortcut ?? "Ctrl+Alt+F10"],
    ["overlay", "cycleShortcut", profile.overlay?.cycleShortcut ?? "Ctrl+Alt+F11"],
    ["colorCorrection", "cycleShortcut", profile.colorCorrection?.cycleShortcut ?? "Ctrl+Alt+F6"],
    ["centering", "gameShortcut", profile.centering?.gameShortcut || ""],
    ...(profile.spatialOverlays?.items || []).flatMap(item => [
      [item.id, "shortcut", item.shortcut || ""], [item.id, "precisionShortcut", item.precisionShortcut || ""],
      ...(item.kind === "menu" ? [[item.id, "selectShortcut", item.selectShortcut || ""]] : []),
    ]),
  ];
}
export function validateSpatialShortcuts(profile) {
  const entries = spatialShortcutEntries(profile);
  for (const menu of profile.spatialOverlays?.items || []) {
    if (menu.kind !== "menu") continue;
    for (const field of ["shortcut", "selectShortcut"]) {
      const value = menu[field];
      if (value && entries.some(([id, key, chord]) => chord === value && (id !== menu.id || key !== field)))
        throw new Error("Menu shortcut conflicts with another toolkit shortcut.");
    }
  }
}
function fail() { throw new Error("Invalid spatial overlay settings."); }
const range = (v, low, high, integer = false) => typeof v === "number" && Number.isFinite(v) && v >= low && v <= high && (!integer || Number.isInteger(v));
export function validateSpatialOverlays(s) {
  if (!s || typeof s.enabled !== "boolean" || !range(s.pixelBudgetMP, 15, 180, true) || !Array.isArray(s.items) || s.items.length > 15) fail();
  const ids = new Set();
  let menuCount = 0;
  for (const o of s.items) {
    if (!o || typeof o.id !== "string" || !/^[0-9a-f-]{36}$/i.test(o.id) || ids.has(o.id) ||
        typeof o.name !== "string" || !o.name.trim() || o.name.length > 80 || /[\x00-\x1f]/.test(o.name) || typeof o.enabled !== "boolean" ||
        !["url", "window", "desktop", "menu"].includes(o.kind) || !["head", "local"].includes(o.anchor) ||
        typeof o.url !== "string" || o.url.length > 2048 || typeof o.windowId !== "string" || o.windowId.length > 100 ||
        (o.windowId !== "" && !/^window:\d+:\d+$/.test(o.windowId))) fail();
    if (o.showInStream !== undefined && typeof o.showInStream !== "boolean") fail();
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
    for (const [key, low, high, integer] of [["width",256,1920,true],["height",128,1080,true],["fps",1,30,true],...Object.entries(spatialPositionLimits).map(([key, limits]) => [key, ...limits]),["yaw",-180,180],["pitch",-180,180],["roll",-180,180],["widthMeters",0.1,3],["opacity",0,100]])
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
