export const spatialDefaults = () => ({ enabled: false, pixelBudgetMP: 60, items: [] });
export const centeringDefaults = () => ({ gameShortcut: "", delaySeconds: 3 });
export function createSpatialOverlay() {
  return { id: globalThis.crypto.randomUUID(), name: "New overlay", enabled: false, showInStream: false, kind: "url", url: "", windowId: "", width: 1024, height: 576, fps: 10,
    anchor: "local", x: 0, y: -0.35, z: -1, yaw: 0, pitch: 0, roll: 0, widthMeters: 0.6, opacity: 100 };
}
function fail() { throw new Error("Invalid spatial overlay settings."); }
const range = (v, low, high, integer = false) => typeof v === "number" && Number.isFinite(v) && v >= low && v <= high && (!integer || Number.isInteger(v));
export function validateSpatialOverlays(s) {
  if (!s || typeof s.enabled !== "boolean" || !range(s.pixelBudgetMP, 15, 180, true) || !Array.isArray(s.items) || s.items.length > 15) fail();
  const ids = new Set();
  for (const o of s.items) {
    if (!o || typeof o.id !== "string" || !/^[0-9a-f-]{36}$/i.test(o.id) || ids.has(o.id) ||
        typeof o.name !== "string" || !o.name.trim() || o.name.length > 80 || /[\x00-\x1f]/.test(o.name) || typeof o.enabled !== "boolean" ||
        !["url", "window"].includes(o.kind) || !["head", "local"].includes(o.anchor) ||
        typeof o.url !== "string" || o.url.length > 2048 || typeof o.windowId !== "string" || o.windowId.length > 100 ||
        (o.windowId !== "" && !/^window:\d+:\d+$/.test(o.windowId))) fail();
    if (o.showInStream !== undefined && typeof o.showInStream !== "boolean") fail();
    ids.add(o.id);
    if (o.url) {
      let url; try { url = new URL(o.url); } catch { fail(); }
      if (!["http:", "https:"].includes(url.protocol) || url.username || url.password || /[\x00-\x20]/.test(o.url)) fail();
    }
    for (const [key, low, high, integer] of [["width",256,1920,true],["height",128,1080,true],["fps",1,30,true],["x",-5,5],["y",-5,5],["z",-10,-0.1],["yaw",-180,180],["pitch",-180,180],["roll",-180,180],["widthMeters",0.1,3],["opacity",0,100]])
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
