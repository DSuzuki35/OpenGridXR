"use strict";
const path = require("node:path");
const { pathToFileURL } = require("node:url");
const { createOverlayHost } = require("./overlay-host.cjs");
const { selectedForGame } = require("./runtime-config.cjs");
const { randomUUID } = require('node:crypto');
const { createQuickMenuGateway, menuActions } = require('./quick-menu.cjs');

async function createSpatialOverlays({ electron, executable, getSessions, model, hostFactory = createOverlayHost, setTimer = setInterval, clearTimer = clearInterval, getMenuState, onMenuAction }) {
  const { BrowserWindow, desktopCapturer, session, screen } = electron;
  const { effectiveOverlayRates, spatialDefaults } = await import(pathToFileURL(path.join(__dirname, "../src/spatial-overlays.mjs")).href);
  const games = new Map(), selections = new Map(), titles = new Map();
  let document = null, closed = false, syncing = false, syncAgain = false, generation = 0, timer = null;
  const menuGateway = createQuickMenuGateway({setTimer,clearTimer});
  const gameFor = (game) => {
    if (!["iracing", "lmu"].includes(game)) throw new Error("Unsupported overlay game.");
    if (!games.has(game)) games.set(game, { game, host: null, sources: new Map(), message: "Save and enable overlays, then start an OpenXR session.", retryAt: 0 });
    return games.get(game);
  };
  async function listWindows() {
    const ownIds = new Set(BrowserWindow.getAllWindows().map((window) => window.getMediaSourceId()));
    return (await desktopCapturer.getSources({ types: ["window"], thumbnailSize: { width: 0, height: 0 }, fetchWindowIcons: false })).filter((source) => !ownIds.has(source.id));
  }
  const publicDisplay = (display) => {
    const bounds = screen.dipToScreenRect(null, display.bounds);
    return { id: String(display.id), name: display.label || `Display ${display.id}`, x: bounds.x, y: bounds.y, width: bounds.width, height: bounds.height, scaleFactor: display.scaleFactor };
  };
  const displayIdentity = (display) => JSON.stringify([publicDisplay(display), display.rotation, display.internal]);
  function currentDisplay(selection) {
    if (!selection || selection.kind !== "desktop") return null;
    const display = screen?.getAllDisplays().find(d => String(d.id) === selection.id);
    return display && displayIdentity(display) === selection.identity ? publicDisplay(display) : null;
  }
  const sourceText = (value, limit = 500) => String(value || "").replace(/[\u0000-\u001f\u007f-\u009f]/g, "").trim().slice(0, limit);
  function selectedSource(game, item, revokeMismatch = false) {
    const key = `${game}:${item.id}`, selection = selections.get(key);
    if (!selection) return null;
    if (selection.kind !== item.kind || selection.id !== (item.kind === "desktop" ? item.displayId : item.windowId)) {
      if (revokeMismatch) selections.delete(key);
      return null;
    }
    if (item.kind === "desktop" && !currentDisplay(selection)) { selections.delete(key); return null; }
    return selection;
  }
  const nativeSlot = (native, slot) => native?.slots?.find(entry => entry.slot === slot);
  const isDisplaying = (settings, item, slot, rates, source, native, admitted) => {
    const input = nativeSlot(native, slot);
    return !!(settings.enabled && item.enabled && rates[slot] > 0 && admitted.has(item.id) && native.available && source && !source.stopped && source.status === "capturing" && input?.state === 1 && input.frameId > 0);
  };
  function sourceSummary(game, item, source, displaying) {
    if (item.kind === 'menu') return {kind:'menu',reference:'opengrid-menu',name:'OpenGrid menu',detail:'Local headset controls',displaying};
    if (item.kind === "url") {
      if (!item.url) return null;
      let hostname; try { hostname = new URL(item.url).hostname; } catch { return null; }
      const saved = titles.get(`${game}:${item.id}`);
      const title = !source?.window?.isDestroyed() ? source?.window?.webContents.getTitle?.() : "";
      return { kind: "url", reference: sourceText(item.url, 2048), name: sourceText(title || source?.title || (saved?.url === item.url ? saved.title : "")) || hostname, detail: sourceText(item.url, 2048), displaying };
    }
    const selection = selectedSource(game, item);
    if (!selection) return null;
    const display = item.kind === "desktop" ? currentDisplay(selection) : null;
    return { kind: item.kind, reference: sourceText(selection.id, 2048), name: sourceText(display?.name || selection.name), detail: display ? `${display.width} × ${display.height}` : "", displaying };
  }
  async function availableDisplays() {
    if (!screen) return [];
    const sources = await desktopCapturer.getSources({ types: ["screen"], thumbnailSize: { width: 0, height: 0 }, fetchWindowIcons: false });
    return screen.getAllDisplays().flatMap(display => {
      const source = sources.find(s => s.display_id === String(display.id));
      return source ? [{ ...publicDisplay(display), source, identity: displayIdentity(display) }] : [];
    });
  }
  const sourceSignature = (item, fps) => JSON.stringify([item.kind, item.url, item.windowId, item.displayId, item.width, item.height, fps,
    ...(["desktop","menu"].includes(item.kind) ? [item.anchor, item.x, item.y, item.z, item.yaw, item.pitch, item.roll, item.widthMeters, item.opacity, item.interaction, item.precisionScale, item.precisionShortcut] : [])]);
  function invalidateDisplay(_, display) {
    for (const [key, selection] of selections) {
      if (selection.kind !== "desktop" || (display && selection.id !== String(display.id))) continue;
      selections.delete(key);
      for (const g of games.values()) for (const [id, source] of g.sources) if (source.desktopSelection === selection) stopSource(g, id);
    }
    generation++; void sync();
  }
  screen?.on("display-removed", invalidateDisplay);
  screen?.on("display-metrics-changed", invalidateDisplay);
  function stopSource(g, id) {
    const source = g.sources.get(id); if (!source) return;
    source.stopped = true;
    source.menu?.close();
    clearTimeout(source.navigationTimer);
    clearTimeout(source.refreshTimer);
    g.host?.send({ op: "clear", slot: source.slot });
    if (source.window && !source.window.isDestroyed()) source.window.destroy();
    g.sources.delete(id);
  }
  function stopGame(g) {
    for (const id of [...g.sources.keys()]) stopSource(g, id);
    g.host?.close(); g.host = null; g.retryAt = 0;
  }
  function failSource(g, source, reason) {
    if (source.stopped) return;
    source.status = "error"; source.reason = reason.slice(0, 500); source.stopped = true;
    source.menu?.close();
    clearTimeout(source.navigationTimer); clearTimeout(source.refreshTimer);
    g.host?.send({ op: "clear", slot: source.slot });
    if (source.window && !source.window.isDestroyed()) source.window.destroy();
  }
  async function startSource(g, item, slot, fps, expectedGeneration, desktop, uiToken) {
    const record = { slot, uiToken, stopped: false, window: null, status: "starting", reason: "", fps, signature: sourceSignature(item, fps), desktopSelection: desktop?.selection };
    g.sources.set(item.id, record);
    const windowSelection = selectedSource(g.game, item);
    let selectedWindow;
    if (item.kind === "window") {
      if (!windowSelection || windowSelection.id !== item.windowId) { record.status = "select-window"; record.reason = "Select the window again for this app session."; return; }
      selectedWindow = (await listWindows()).find((source) => source.id === windowSelection.id && source.name === windowSelection.name);
      if (closed || record.stopped) return;
      if (expectedGeneration !== generation) { stopSource(g, item.id); return; }
      if (!selectedWindow) { selections.delete(`${g.game}:${item.id}`); record.status = "unavailable"; record.reason = "The selected window is no longer available. Select it again."; return; }
    } else if (item.kind === "desktop") {
      if (!desktop) { record.status = "select-display"; record.reason = "Select a connected monitor again for this app session."; return; }
      selectedWindow = desktop.source;
    } else if (item.kind !== 'menu' && !item.url) { record.status = "missing-url"; record.reason = "Enter an HTTP or HTTPS URL and save."; return; }
    if (closed || record.stopped) return;
    if (expectedGeneration !== generation) { stopSource(g, item.id); return; }
    const partition = `opengrid-overlay-${g.game}-${item.id}`;
    const ses = session.fromPartition(partition, { cache: true });
    const captureAllowed = () => !record.stopped && (item.kind === "window" || (item.kind === "desktop" && selections.get(`${g.game}:${item.id}`) === desktop?.selection && !!currentDisplay(desktop?.selection)));
    ses.setPermissionRequestHandler((contents, permission, callback) => callback(captureAllowed() && contents === record.window?.webContents && permission === "display-capture"));
    ses.setPermissionCheckHandler((contents, permission) => captureAllowed() && contents === record.window?.webContents && permission === "display-capture");
    ses.removeAllListeners("will-download");
    ses.on("will-download", (event) => event.preventDefault());
    ses.setDisplayMediaRequestHandler(null);
    if (item.kind === 'window' || item.kind === 'desktop') ses.setDisplayMediaRequestHandler((request, callback) => {
      if (request.frame === record.window?.webContents.mainFrame && captureAllowed()) callback({ video: selectedWindow }); else callback({});
    });
    const window = new BrowserWindow({ x: 0, y: 0, width: item.width, height: item.height, useContentSize: true, show: false, frame: false, thickFrame: false, resizable: false, transparent: true,
      webPreferences: { session: ses, ...(item.kind==='menu'?{preload:path.join(__dirname,'quick-menu-preload.cjs')}:{}), offscreen: { useSharedTexture: true }, sandbox: true, contextIsolation: true, nodeIntegration: false, webSecurity: true, backgroundThrottling: false, spellcheck: false } });
    // Windows initial frameless bounds can gain 1-2 pixels at fractional desktop DPI.
    // A post-creation resize keeps coded texture size inside the configured budget.
    window.setBounds({ x: 0, y: 0, width: item.width, height: item.height });
    record.window = window;
    const contents = window.webContents;
    if (item.kind === 'menu') {
      const profile=selectedForGame(document,g.game);
      const context={game:g.game,profileId:profile.id,id:item.id};
      record.menu=menuGateway.register({contents,token:uiToken,...context,width:item.width,height:item.height,
        isCurrent:()=>!closed&&!record.stopped&&g.sources.get(item.id)===record&&g.host?.running()&&g.host.state().available&&
          document?.selectedProfileId===context.profileId&&selectedForGame(document,g.game)?.id===context.profileId&&
          selectedForGame(document,g.game)?.spatialOverlays?.enabled&&selectedForGame(document,g.game)?.spatialOverlays.items.some(o=>o.id===item.id&&o.enabled&&o.kind==='menu'),
        getState:()=>getMenuState?getMenuState(context):{profileName:profile.name,game:g.game,actions:menuActions(profile)},
        runAction:(actionId,{canApply})=>{if(!onMenuAction)throw Error('Menu actions are unavailable.');return onMenuAction({...context,actionId,canApply});},
      });
    }
    contents.setAudioMuted(true); contents.setFrameRate(fps);
    contents.setWindowOpenHandler(() => ({ action: "deny" }));
    const allowedNavigation = (url) => { try { return item.kind === "url" && ["http:", "https:"].includes(new URL(url).protocol); } catch { return false; } };
    for (const eventName of ["will-navigate", "will-redirect"]) contents.on(eventName, (event, url) => { if (!allowedNavigation(url)) event.preventDefault(); });
    contents.on("paint", (event) => { if (!event.texture) return; if (record.stopped) event.texture.release(); else { const submitted = g.host?.submit(slot, event.texture); if (!g.host) event.texture.release(); if (submitted) record.status = "capturing"; } });
    contents.on("render-process-gone", () => failSource(g, record, "The overlay renderer stopped. Disable and re-enable the source."));
    contents.on("did-fail-load", (_, code, description, url, mainFrame) => { if (mainFrame && code !== -3) failSource(g, record, `Source could not load: ${description}`); });
    contents.on("page-title-updated", (_, title) => {
      if (record.stopped) return;
      if (item.kind === "url") { record.title = sourceText(title); titles.set(`${g.game}:${item.id}`, { url: item.url, title: record.title }); }
      else if (title === "Capture unavailable") failSource(g, record, "Windows capture ended or could not start. Select an available source.");
    });
    record.navigationTimer = setTimeout(() => {
      if (record.stopped || window.isDestroyed()) return;
      failSource(g, record, "Source loading timed out. Check the URL, then disable and re-enable this overlay.");
    }, 15000);
    // External navigation must never hold the lifecycle loop: disable/session exit
    // must tear down sources even when a server never finishes its response.
    const navigation = item.kind === "url" ? window.loadURL(item.url) : item.kind==='menu' ? window.loadFile(path.join(__dirname,'quick-menu.html')) : window.loadFile(path.join(__dirname, "capture-window.html"), { query: { fps: String(fps), width: String(item.width), height: String(item.height), fit: item.kind === "desktop" ? "fill" : "contain" } });
    void navigation.then(() => clearTimeout(record.navigationTimer)).catch((error) => {
      clearTimeout(record.navigationTimer);
      if (record.stopped) return;
      failSource(g, record, error.message);
    });
  }
  async function sync() {
    if (closed || !document) return;
    if (syncing) { syncAgain = true; return; }
    syncing = true;
    const expectedGeneration = generation;
    try {
      const profiles = ["iracing", "lmu"].map((game) => selectedForGame(document, game));
      const needsSessions = profiles.some((p) => p?.spatialOverlays?.enabled && p.spatialOverlays.items.some((o) => o.enabled));
      const sessions = needsSessions ? await getSessions() : [];
      if (closed || expectedGeneration !== generation) return;
      for (const game of ["iracing", "lmu"]) {
        if (closed) return;
        const g = gameFor(game), p = selectedForGame(document, game), settings = p?.spatialOverlays || spatialDefaults();
        const active = settings.enabled && sessions.some((s) => s.game === game && s.active && s.engineLoaded);
        if (!active || !settings.items.some((o) => o.enabled)) { stopGame(g); g.message = settings.enabled ? "Waiting for an active OpenXR session with the updated overlay engine." : "Spatial overlays are off."; continue; }
        if (g.host && !g.host.running()) { stopGame(g); }
        if (!g.host && Date.now() >= g.retryAt) {
          g.host = hostFactory({ executable, game, onUiPointer: event => {
            const source=[...g.sources.values()].find(entry=>entry.slot===event.slot);
            if(source&&!source.stopped&&source.uiToken===event.uiToken)source.menu?.pointer(event);
          }, onSlotReady: (slot) => {
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
          const signature = sourceSignature(item, rates[slot]);
          const source = g.sources.get(item.id);
          if (source && (source.signature !== signature || source.slot !== slot)) stopSource(g, item.id);
          const existing = g.sources.get(item.id);
          if (existing && hostState.frameErrors?.[slot]) {
            failSource(g, existing, hostState.frameErrors[slot]);
            continue;
          }
          let desktop;
          if (item.kind === "desktop") {
            const selection = selectedSource(game, item), display = currentDisplay(selection);
            if (display) desktop = { ...display, source: selection.source, selection };
            if (!desktop && existing?.window) { selections.delete(`${game}:${item.id}`); stopSource(g, item.id); }
          }
          const uiToken=item.kind==='menu'?(g.sources.get(item.id)?.uiToken||randomUUID()):undefined;
          const configuration = { op: "configure", slot, ...(uiToken?{uiToken}:{}), enabled: item.kind !== "desktop" || !!desktop, anchor: item.anchor, x: item.x, y: item.y, z: item.z, yaw: item.yaw, pitch: item.pitch, roll: item.roll, widthMeters: item.widthMeters, opacity: item.opacity / 100,
            showInStream: item.kind!=='menu'&&item.showInStream === true, desktopMode: item.kind==='menu'?2:desktop && item.interaction === "head" ? 1 : 0,
            desktopX: desktop?.x || 0, desktopY: desktop?.y || 0, desktopWidth: item.kind==='menu'?item.width:desktop?.width || 0, desktopHeight: item.kind==='menu'?item.height:desktop?.height || 0,
            precisionScale: Number.isFinite(item.precisionScale) ? Math.min(3, Math.max(1, item.precisionScale)) : 1.75,
            precisionShortcut: typeof item.precisionShortcut === "string" && /^(?:Ctrl\+)?(?:Alt\+)?(?:Shift\+)?F(?:[1-9]|1[0-2])$/.test(item.precisionShortcut) ? item.precisionShortcut : "" };
          const configSignature = JSON.stringify(configuration);
          if (g.sources.get(item.id)?.configSignature !== configSignature) g.host.send(configuration);
          if (!g.sources.has(item.id)) await startSource(g, item, slot, rates[slot], expectedGeneration, desktop, uiToken);
          if (closed || expectedGeneration !== generation) return;
          const current = g.sources.get(item.id); if (current) current.configSignature = configSignature;
        }
      }
    } catch (error) { for (const g of games.values()) g.message = error.message.slice(0, 500); }
    finally {
      syncing = false;
      if (syncAgain && !closed) { syncAgain = false; void sync(); }
    }
  }
  return {
    update(doc) {
      if (closed) return;
      model.validateDocument(doc); document = structuredClone(doc);
      menuGateway.refresh();
      generation++;
      // Selection identity is transient consent for this exact source reference.
      // Hiding a source retains it; changing the source cannot restore it later.
      for (const game of ["iracing", "lmu"]) {
        const items = selectedForGame(document, game)?.spatialOverlays?.items || [];
        for (const key of selections.keys()) if (key.startsWith(`${game}:`)) {
          const item = items.find(entry => key === `${game}:${entry.id}`);
          if (item) selectedSource(game, item, true); else selections.delete(key);
        }
        for (const [key, title] of titles) if (key.startsWith(`${game}:`) && !items.some(item => key === `${game}:${item.id}` && item.kind === "url" && item.url === title.url)) titles.delete(key);
      }
      // A pending desktop-capture lookup must not delay disabling sources.
      // The generation check also prevents the old pass from starting later slots.
      for (const [game, g] of games) {
        const settings = selectedForGame(document, game)?.spatialOverlays;
        const rates = settings ? effectiveOverlayRates(settings) : [];
        const wanted = new Set(settings?.enabled ? settings.items.filter((item, slot) => item.enabled && rates[slot] > 0).map(item => item.id) : []);
        if (!wanted.size) stopGame(g);
        else for (const [id, source] of g.sources) {
          const slot = settings.items.findIndex(item => item.id === id), item = settings.items[slot];
          if (!wanted.has(id) || source.slot !== slot || source.signature !== sourceSignature(item, rates[slot])) stopSource(g, id);
        }
      }
      const active = ['iracing', 'lmu'].some(game => {
        const settings = selectedForGame(document, game)?.spatialOverlays;
        return settings?.enabled && settings.items.some(item => item.enabled);
      });
      if (active && timer === null) { timer = setTimer(sync, 1000); timer?.unref?.(); }
      if (!active && timer !== null) { clearTimer(timer); timer = null; }
      void sync();
    },
    async listWindows() { return (await listWindows()).map(({ id, name }) => ({ id, name })); },
    async listDisplays() { return (await availableDisplays()).map(({ source, identity, ...display }) => display); },
    async selectDisplay(game, id, displayId) {
      const g = gameFor(game);
      if (typeof id !== "string" || !/^[0-9a-f-]{36}$/i.test(id) || typeof displayId !== "string") throw new Error("Invalid overlay or display ID.");
      const display = (await availableDisplays()).find(d => d.id === displayId);
      if (!display) throw new Error("This monitor is no longer available.");
      selections.set(`${game}:${id}`, { kind: "desktop", id: display.id, identity: display.identity, sourceId: display.source.id, source: display.source });
      generation++;
      stopSource(g, id); void sync();
      const { source, identity, ...result } = display; return result;
    },
    async selectWindow(game, id, sourceId) {
      const g = gameFor(game);
      if (typeof id !== "string" || !/^[0-9a-f-]{36}$/i.test(id)) throw new Error("Invalid overlay ID.");
      const source = (await listWindows()).find((s) => s.id === sourceId);
      if (!source) throw new Error("This window is no longer available.");
      selections.set(`${game}:${id}`, { kind: "window", id: source.id, name: source.name }); generation++; stopSource(g, id); void sync();
      return { id: source.id, name: source.name };
    },
    status(game) {
      const g = gameFor(game), settings = selectedForGame(document || { profiles: [] }, game)?.spatialOverlays || spatialDefaults(), rates = effectiveOverlayRates(settings);
      const native = g.host?.state() || { available: false, capacity: 0, activeOverlayCount: 0 };
      const admitted = new Set(settings.items.filter((item, slot) => item.enabled && rates[slot] > 0).slice(0, native.capacity).map((item) => item.id));
      return { ...native, running: !!g.host, message: g.message,
        items: settings.items.map((item, slot) => {
          const source = g.sources.get(item.id), input = nativeSlot(native, slot);
          return { ...(item.kind === "desktop" ? { pointerMode: item.interaction === "head" ? "head" : "mouse", inputAvailable: input?.inputAvailable === true, inputPaused: input?.inputPaused === true, inputError: input?.inputError || "" } : {}),
            id: item.id, sourceKind: item.kind, sourceReference: item.kind === "desktop" ? item.displayId : item.kind === "window" ? item.windowId : item.url,
            source: sourceSummary(game, item, source, isDisplaying(settings, item, slot, rates, source, native, admitted)),
            status: source?.status || (!settings.enabled || !item.enabled ? "disabled" : rates[slot] === 0 ? "budget-paused" : native.available && !admitted.has(item.id) ? "layer-limit" : "waiting"), reason: source?.reason || "", effectiveFps: rates[slot] };
        }) };
    },
    desktopAction(game, id, action, options) {
      if (action === "precision" && (typeof options?.held !== "boolean" || !Number.isSafeInteger(options.pressId) || options.pressId <= 0)) throw new Error("Invalid precision hold or press ID.");
      const g = gameFor(game), settings = selectedForGame(document || { profiles: [] }, game)?.spatialOverlays;
      const item = settings?.items.find(item => item.id === id), source = g.sources.get(id), native = g.host?.state();
      // Release is best effort even after hide, removal, takeover or host exit.
      // A cleared slot and a stopped host already cancel native precision.
      if (action === "precision" && options.held === false) {
        if (source && !source.stopped) g.host?.send({ op: "precision", slot: source.slot, held: false, pressId: options.pressId });
        return { message: "Desktop precision released." };
      }
      const input = nativeSlot(native, source?.slot);
      if (!["leftClick", "rightClick", "resume", "precision"].includes(action) || !settings?.enabled || !item?.enabled || item.kind !== "desktop" || item.interaction !== "head" || !source || source.stopped || source.status !== "capturing" || !native?.available || selectedSource(game, item) !== source.desktopSelection || !currentDisplay(source.desktopSelection) || (action !== "resume" && (input?.inputPaused || (action !== "precision" && input?.inputAvailable !== true)))) throw new Error("Desktop interaction is unavailable. Show a selected monitor and enable head pointing first.");
      if (action === "precision") {
        const rates = effectiveOverlayRates(settings), admitted = new Set(settings.items.filter((item, slot) => item.enabled && rates[slot] > 0).slice(0, native.capacity).map(item => item.id));
        if (!isDisplaying(settings, item, source.slot, rates, source, native, admitted)) throw new Error("Desktop precision is unavailable. Wait for a visible desktop frame.");
        if (!g.host.send({ op: "precision", slot: source.slot, held: true, pressId: options.pressId })) throw new Error("The desktop input host is unavailable.");
        return { message: "Desktop precision held." };
      }
      if (action === "resume") { source.configSignature = ""; void sync(); return { message: "Desktop pointing resumed." }; }
      if (!g.host.send({ op: "click", slot: source.slot, button: action === "leftClick" ? "left" : "right" })) throw new Error("The desktop input host is unavailable.");
      return { message: "Desktop click requested." };
    },
    menuAction(event,actionId){return menuGateway.run(event,actionId);},
    refreshMenus(){menuGateway.refresh();},
    prepareMenuSelect(game,id){
      const g=gameFor(game),source=g.sources.get(id),target=source?.menu?.selectionContext();
      if(!target||source.stopped||!g.host?.state().available)throw Error('Point at the visible headset menu first.');
      return {game,id,token:source.uiToken,...target,requestedAt:Date.now()};
    },
    menuSelect(game,id,request){
      const g=gameFor(game),source=g.sources.get(id),item=selectedForGame(document||{profiles:[]},game)?.spatialOverlays?.items.find(o=>o.id===id&&o.kind==='menu'&&o.enabled);
      if(!item||!source?.menu||source.stopped||!g.host?.state().available)throw Error('Show the menu in a running XR session first.');
      const target=source.menu.selectionContext();
      if(!request||request.game!==game||request.id!==id||request.token!==source.uiToken||!target||['session','focusEpoch','configRevision','sourceRevision','recenter'].some(key=>target[key]!==request[key])||
        !Number.isFinite(request.requestedAt)||Date.now()<request.requestedAt||Date.now()-request.requestedAt>200)throw Error('The menu selection is stale or its source changed. Select again.');
      if(!g.host.send({op:'click',slot:source.slot,button:'left',uiToken:source.uiToken,session:request.session,focusEpoch:request.focusEpoch,configRevision:request.configRevision,sourceRevision:request.sourceRevision,recenter:request.recenter}))throw Error('Menu input host unavailable.');
      return {message:'Headset menu selection requested.'};
    },
    recenter(game) { const g = gameFor(game); if (!g.host?.state().available) throw new Error("Start the sim and enable spatial overlays before centering them."); g.host.send({ op: "recenter", slot: -1 }); return { message: "Overlay anchors recentered. The game camera is unchanged." }; },
    close() { closed = true; menuGateway.close(); if (timer !== null) clearTimer(timer); timer = null; screen?.removeListener("display-removed", invalidateDisplay); screen?.removeListener("display-metrics-changed", invalidateDisplay); selections.clear(); titles.clear(); for (const g of games.values()) stopGame(g); },
  };
}
module.exports = { createSpatialOverlays };
