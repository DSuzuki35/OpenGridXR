"use strict";
const path = require("node:path");
const { pathToFileURL } = require("node:url");
const { createOverlayHost } = require("./overlay-host.cjs");
const { selectedForGame } = require("./runtime-config.cjs");

async function createSpatialOverlays({ electron, executable, getSessions, model, hostFactory = createOverlayHost }) {
  const { BrowserWindow, desktopCapturer, session } = electron;
  const { effectiveOverlayRates, spatialDefaults } = await import(pathToFileURL(path.join(__dirname, "../src/spatial-overlays.mjs")).href);
  const games = new Map(), selections = new Map();
  let document = null, closed = false, syncing = false;
  const gameFor = (game) => {
    if (!["iracing", "lmu"].includes(game)) throw new Error("Unsupported overlay game.");
    if (!games.has(game)) games.set(game, { game, host: null, sources: new Map(), message: "Save and enable overlays, then start an OpenXR session.", retryAt: 0 });
    return games.get(game);
  };
  async function listWindows() {
    const ownIds = new Set(BrowserWindow.getAllWindows().map((window) => window.getMediaSourceId()));
    return (await desktopCapturer.getSources({ types: ["window"], thumbnailSize: { width: 0, height: 0 }, fetchWindowIcons: false })).filter((source) => !ownIds.has(source.id));
  }
  function stopSource(g, id) {
    const source = g.sources.get(id); if (!source) return;
    source.stopped = true;
    clearTimeout(source.navigationTimer);
    clearTimeout(source.refreshTimer);
    g.host?.send({ op: "clear", slot: source.slot });
    if (source.window && !source.window.isDestroyed()) source.window.destroy();
    g.sources.delete(id);
  }
  function stopGame(g) {
    for (const id of [...g.sources.keys()]) stopSource(g, id);
    g.host?.close(); g.host = null;
  }
  function failSource(g, source, reason) {
    if (source.stopped) return;
    source.status = "error"; source.reason = reason.slice(0, 500); source.stopped = true;
    clearTimeout(source.navigationTimer); clearTimeout(source.refreshTimer);
    g.host?.send({ op: "clear", slot: source.slot });
    if (source.window && !source.window.isDestroyed()) source.window.destroy();
  }
  async function startSource(g, item, slot, fps) {
    const record = { slot, stopped: false, window: null, status: "starting", reason: "", fps, signature: JSON.stringify([item.kind, item.url, item.windowId, item.width, item.height, fps]) };
    g.sources.set(item.id, record);
    const windowSelection = selections.get(`${g.game}:${item.id}`);
    let selectedWindow;
    if (item.kind === "window") {
      if (!windowSelection || windowSelection.id !== item.windowId) { record.status = "select-window"; record.reason = "Select the window again for this app session."; return; }
      selectedWindow = (await listWindows()).find((source) => source.id === windowSelection.id && source.name === windowSelection.name);
      if (!selectedWindow) { record.status = "unavailable"; record.reason = "The selected window is no longer available. Select it again."; return; }
    } else if (!item.url) { record.status = "missing-url"; record.reason = "Enter an HTTP or HTTPS URL and save."; return; }
    if (closed || record.stopped) return;
    const partition = `opengrid-overlay-${g.game}-${item.id}`;
    const ses = session.fromPartition(partition, { cache: true });
    ses.setPermissionRequestHandler((contents, permission, callback) => callback(item.kind === "window" && contents === record.window?.webContents && permission === "display-capture"));
    ses.setPermissionCheckHandler((contents, permission) => item.kind === "window" && contents === record.window?.webContents && permission === "display-capture");
    ses.removeAllListeners("will-download");
    ses.on("will-download", (event) => event.preventDefault());
    ses.setDisplayMediaRequestHandler(null);
    if (item.kind === "window") ses.setDisplayMediaRequestHandler((request, callback) => {
      if (request.frame === record.window?.webContents.mainFrame && !record.stopped) callback({ video: selectedWindow }); else callback({});
    });
    const window = new BrowserWindow({ x: 0, y: 0, width: item.width, height: item.height, useContentSize: true, show: false, frame: false, thickFrame: false, resizable: false, transparent: true,
      webPreferences: { session: ses, offscreen: { useSharedTexture: true }, sandbox: true, contextIsolation: true, nodeIntegration: false, webSecurity: true, backgroundThrottling: false, spellcheck: false } });
    // Windows initial frameless bounds can gain 1-2 pixels at fractional desktop DPI.
    // A post-creation resize keeps coded texture size inside the configured budget.
    window.setBounds({ x: 0, y: 0, width: item.width, height: item.height });
    record.window = window;
    const contents = window.webContents;
    contents.setAudioMuted(true); contents.setFrameRate(fps);
    contents.setWindowOpenHandler(() => ({ action: "deny" }));
    const allowedNavigation = (url) => { try { return item.kind === "url" && ["http:", "https:"].includes(new URL(url).protocol); } catch { return false; } };
    for (const eventName of ["will-navigate", "will-redirect"]) contents.on(eventName, (event, url) => { if (!allowedNavigation(url)) event.preventDefault(); });
    contents.on("paint", (event) => { if (!event.texture) return; if (record.stopped) event.texture.release(); else { const submitted = g.host?.submit(slot, event.texture); if (!g.host) event.texture.release(); if (submitted) record.status = "capturing"; } });
    contents.on("render-process-gone", () => failSource(g, record, "The overlay renderer stopped. Disable and re-enable the source."));
    contents.on("did-fail-load", (_, code, description, url, mainFrame) => { if (mainFrame && code !== -3) failSource(g, record, `Source could not load: ${description}`); });
    contents.on("page-title-updated", (_, title) => { if (item.kind === "window" && title === "Capture unavailable") failSource(g, record, "Windows capture ended or could not start. Select an available window."); });
    record.navigationTimer = setTimeout(() => {
      if (record.stopped || window.isDestroyed()) return;
      failSource(g, record, "Source loading timed out. Check the URL, then disable and re-enable this overlay.");
    }, 15000);
    // External navigation must never hold the lifecycle loop: disable/session exit
    // must tear down sources even when a server never finishes its response.
    const navigation = item.kind === "url" ? window.loadURL(item.url) : window.loadFile(path.join(__dirname, "capture-window.html"), { query: { fps: String(fps), width: String(item.width), height: String(item.height) } });
    void navigation.then(() => clearTimeout(record.navigationTimer)).catch((error) => {
      clearTimeout(record.navigationTimer);
      if (record.stopped) return;
      failSource(g, record, error.message);
    });
  }
  async function sync() {
    if (closed || syncing || !document) return;
    syncing = true;
    try {
      const profiles = ["iracing", "lmu"].map((game) => selectedForGame(document, game));
      const needsSessions = profiles.some((p) => p?.spatialOverlays?.enabled && p.spatialOverlays.items.some((o) => o.enabled));
      const sessions = needsSessions ? await getSessions() : [];
      for (const game of ["iracing", "lmu"]) {
        const g = gameFor(game), p = selectedForGame(document, game), settings = p?.spatialOverlays || spatialDefaults();
        const active = settings.enabled && sessions.some((s) => s.game === game && s.active && s.engineLoaded);
        if (!active || !settings.items.some((o) => o.enabled)) { stopGame(g); g.message = settings.enabled ? "Waiting for an active OpenXR session with the updated overlay engine." : "Spatial overlays are off."; continue; }
        if (g.host && !g.host.running()) { stopGame(g); }
        if (!g.host && Date.now() >= g.retryAt) {
          g.host = hostFactory({ executable, game, onSlotReady: (slot) => {
            const source = [...g.sources.values()].find((entry) => entry.slot === slot);
            if (!source || source.stopped || source.refreshTimer) return;
            // Repaint a dropped final/static frame without retaining another GPU texture.
            source.refreshTimer = setTimeout(() => {
              source.refreshTimer = null;
              if (!source.stopped && source.window && !source.window.isDestroyed()) source.window.webContents.invalidate();
            }, Math.max(34, Math.ceil(1000 / source.fps)));
          } }); g.host.start(); g.retryAt = Date.now() + 5000;
        }
        if (!g.host) continue;
        const hostState = g.host.state();
        g.message = hostState.error || "GPU overlay host connected.";
        if (!hostState.available) { for (const id of [...g.sources.keys()]) stopSource(g, id); continue; }
        const rates = effectiveOverlayRates(settings);
        const wanted = new Set(settings.items.filter((o, slot) => o.enabled && rates[slot] > 0).slice(0, hostState.capacity).map((o) => o.id));
        if (hostState.streamActive) settings.items.forEach((item, slot) => {
          if (item.enabled && item.showInStream === true && rates[slot] > 0) wanted.add(item.id);
        });
        for (const id of [...g.sources.keys()]) if (!wanted.has(id)) stopSource(g, id);
        for (let slot = 0; slot < settings.items.length; slot++) {
          const item = settings.items[slot];
          if (!wanted.has(item.id)) continue;
          const signature = JSON.stringify([item.kind, item.url, item.windowId, item.width, item.height, rates[slot]]);
          const source = g.sources.get(item.id);
          if (source && (source.signature !== signature || source.slot !== slot)) stopSource(g, item.id);
          const existing = g.sources.get(item.id);
          if (existing && hostState.frameErrors?.[slot]) {
            failSource(g, existing, hostState.frameErrors[slot]);
            continue;
          }
          const configuration = { op: "configure", slot, enabled: true, anchor: item.anchor, x: item.x, y: item.y, z: item.z, yaw: item.yaw, pitch: item.pitch, roll: item.roll, widthMeters: item.widthMeters, opacity: item.opacity / 100,
            showInStream: item.showInStream === true };
          const configSignature = JSON.stringify(configuration);
          if (g.sources.get(item.id)?.configSignature !== configSignature) g.host.send(configuration);
          if (!g.sources.has(item.id)) await startSource(g, item, slot, rates[slot]);
          const current = g.sources.get(item.id); if (current) current.configSignature = configSignature;
        }
      }
    } catch (error) { for (const g of games.values()) g.message = error.message.slice(0, 500); }
    finally { syncing = false; }
  }
  const timer = setInterval(sync, 1000); timer.unref?.();
  return {
    update(doc) { model.validateDocument(doc); document = structuredClone(doc); void sync(); },
    async listWindows() { return (await listWindows()).map(({ id, name }) => ({ id, name })); },
    async selectWindow(game, id, sourceId) {
      const g = gameFor(game);
      if (typeof id !== "string" || !/^[0-9a-f-]{36}$/i.test(id)) throw new Error("Invalid overlay ID.");
      const source = (await listWindows()).find((s) => s.id === sourceId);
      if (!source) throw new Error("This window is no longer available.");
      selections.set(`${game}:${id}`, { id: source.id, name: source.name }); stopSource(g, id); void sync();
      return { id: source.id, name: source.name };
    },
    status(game) {
      const g = gameFor(game), settings = selectedForGame(document || { profiles: [] }, game)?.spatialOverlays || spatialDefaults(), rates = effectiveOverlayRates(settings);
      const native = g.host?.state() || { available: false, capacity: 0, activeOverlayCount: 0 };
      const admitted = new Set(settings.items.filter((item, slot) => item.enabled && rates[slot] > 0).slice(0, native.capacity).map((item) => item.id));
      return { ...native, running: !!g.host, message: g.message,
        items: settings.items.map((item, slot) => { const source = g.sources.get(item.id); return { id: item.id, status: source?.status || (!settings.enabled || !item.enabled ? "disabled" : rates[slot] === 0 ? "budget-paused" : native.available && !admitted.has(item.id) ? "layer-limit" : "waiting"), reason: source?.reason || "", effectiveFps: rates[slot] }; }) };
    },
    recenter(game) { const g = gameFor(game); if (!g.host?.state().available) throw new Error("Start the sim and enable spatial overlays before centering them."); g.host.send({ op: "recenter", slot: -1 }); return { message: "Overlay anchors recentered. The game camera is unchanged." }; },
    close() { closed = true; clearInterval(timer); for (const g of games.values()) stopGame(g); },
  };
}
module.exports = { createSpatialOverlays };
