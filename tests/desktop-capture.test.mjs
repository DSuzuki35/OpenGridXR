import test from "node:test";
import assert from "node:assert/strict";
import { EventEmitter } from "node:events";
import { setImmediate as tick } from "node:timers/promises";
import { createRequire } from "node:module";
import { readFile } from "node:fs/promises";
import vm from "node:vm";
import { createDocument, validateDocument } from "../src/model.mjs";
import { createSpatialOverlay } from "../src/spatial-overlays.mjs";
const { createSpatialOverlays } = createRequire(import.meta.url)("../desktop/spatial-overlays.cjs");

async function fixture({ validate = validateDocument } = {}) {
  const windows = [], commands = [], captureSessions = [];
  const display = { id: -42, label: "Left monitor", bounds: { x: -1280, y: 0, width: 1280, height: 720 }, size: { width: 1280, height: 720 }, scaleFactor: 1.5, rotation: 0 };
  const screen = new EventEmitter();
  let displays = [display], sources = [{ id: "screen:7:0", name: "Left monitor", display_id: "-42" }], available = true, inputAvailable = true, inputPaused = false, nativeState = 1, frameId = 1, capacity = 15, sessionsActive = true, lookups = 0, captureLookup;
  screen.getAllDisplays = () => displays;
  // Mixed DPI physical origin is provided by Electron, never guessed by scaling the DIP origin.
  screen.dipToScreenRect = (_, bounds) => ({ x: -1920, y: 0, width: Math.round(bounds.width * 1.5), height: Math.round(bounds.height * 1.5) });
  class Window {
    static getAllWindows() { return []; }
    constructor(options) {
      this.options = options; this.webContents = new EventEmitter();
      this.webContents.mainFrame = {};
      this.webContents.getTitle = () => this.title || "";
      for (const method of ["setAudioMuted", "setFrameRate", "setWindowOpenHandler", "invalidate"]) this.webContents[method] = () => {};
      windows.push(this);
    }
    setBounds() {} loadFile(file, options) { this.loadOptions = options; return Promise.resolve(); } loadURL(url) { this.loadedUrl = url; return Promise.resolve(); }
    isDestroyed() { return !!this.destroyed; } destroy() { this.destroyed = true; }
  }
  const controller = await createSpatialOverlays({
    electron: { BrowserWindow: Window, screen, desktopCapturer: { getSources: async () => { lookups++; return captureLookup ? captureLookup() : sources; } }, session: { fromPartition() {
      const ses = { setPermissionRequestHandler(fn) { this.permission = fn; }, setPermissionCheckHandler() {}, removeAllListeners() {}, on() {}, setDisplayMediaRequestHandler(fn) { this.capture = fn; } };
      captureSessions.push(ses); return ses;
    } } }, model: { validateDocument: validate }, getSessions: async () => [{ game: "iracing", active: sessionsActive, engineLoaded: true }],
    setTimer: () => 1, clearTimer() {}, hostFactory: () => ({ start() {}, running: () => true, send(c) { commands.push(c); return true; }, submit: () => true, close() {},
      state: () => ({ available, capacity, slots: [{ slot: 0, state: nativeState, frameId, inputAvailable, inputPaused }] }) }),
  });
  const doc = createDocument(), item = { ...createSpatialOverlay(), kind: "desktop", enabled: true, displayId: "-42", interaction: "head" };
  doc.profiles[0].spatialOverlays = { ...doc.profiles[0].spatialOverlays, enabled: true, items: [item] };
  const settle = async () => { for (let i = 0; i < 6; i++) await tick(); };
  return { controller, windows, commands, captureSessions, doc, item, display, screen, settle,
    setDisplays(value) { displays = value; }, setSources(value) { sources = value; }, setAvailable(value) { available = value; }, setCaptureLookup(value) { captureLookup = value; }, setInput(available, paused = false) { inputAvailable = available; inputPaused = paused; },
    setNative(state, frame = 1) { nativeState = state; frameId = frame; }, setCapacity(value) { capacity = value; }, setSession(value) { sessionsActive = value; }, lookupCount: () => lookups };
}

test("monitor capture requires live explicit selection and uses physical negative mixed-DPI bounds", async () => {
  const f = await fixture();
  try {
    f.controller.update(f.doc); await f.settle();
    assert.equal(f.windows.length, 0, "a saved display ID grants no capture");
    assert.equal(f.controller.status("iracing").items[0].status, "select-display");
    assert.deepEqual(await f.controller.listDisplays(), [{ id: "-42", name: "Left monitor", x: -1920, y: 0, width: 1920, height: 1080, scaleFactor: 1.5 }]);
    await assert.rejects(() => f.controller.selectDisplay("iracing", f.item.id, "7"), /available/i);
    await f.controller.selectDisplay("iracing", f.item.id, "-42"); await f.settle();
    assert.equal(f.windows.length, 1);
    const config = f.commands.filter(c => c.op === "configure").at(-1);
    assert.deepEqual([config.desktopMode, config.desktopX, config.desktopY, config.desktopWidth, config.desktopHeight], [1, -1920, 0, 1920, 1080]);
    const ses = f.captureSessions[0], window = f.windows[0];
    let grant; ses.capture({ frame: window.webContents.mainFrame }, value => { grant = value; });
    assert.equal(grant.video.id, "screen:7:0", "screen source is matched by display_id, never array order or screen source ID");
    let denied; ses.capture({ frame: {} }, value => { denied = value; }); assert.deepEqual(denied, {});
    window.webContents.emit("paint", { texture: {} });
    f.controller.desktopAction("iracing", f.item.id, "leftClick");
    assert.deepEqual(f.commands.at(-1), { op: "click", slot: 0, button: "left" });
    f.controller.desktopAction("iracing", f.item.id, "rightClick");
    assert.deepEqual(f.commands.at(-1), { op: "click", slot: 0, button: "right" });
    assert.throws(() => f.controller.desktopAction("iracing", f.item.id, "key"));
    f.item.enabled = false; f.controller.update(f.doc);
    assert.equal(window.destroyed, true);
    assert.throws(() => f.controller.desktopAction("iracing", f.item.id, "leftClick"));
    let revoked; ses.capture({ frame: window.webContents.mainFrame }, value => { revoked = value; }); assert.deepEqual(revoked, {});
  } finally { f.controller.close(); }
});

test("monitor removal and replacement never silently resume a saved display reference", async () => {
  const f = await fixture();
  try {
    await f.controller.selectDisplay("iracing", f.item.id, "-42"); f.controller.update(f.doc); await f.settle();
    const first = f.windows[0];
    f.setDisplays([]); f.screen.emit("display-removed", {}, f.display);
    assert.equal(first.destroyed, true, "hotplug immediately clears capture and input");
    assert.equal(f.controller.status("iracing").items[0].source, null, "hotplug immediately clears the selected identity");
    assert.equal(f.commands.at(-1).op, "clear");
    f.setDisplays([f.display]); f.screen.emit("display-added", {}, f.display); await f.settle();
    assert.equal(f.windows.length, 1);
    assert.equal(f.controller.status("iracing").items[0].status, "select-display");
    await f.controller.selectDisplay("iracing", f.item.id, "-42"); await f.settle();
    assert.equal(f.windows.length, 2);
    f.item.opacity = 0; f.controller.update(f.doc);
    assert.equal(f.windows[1].destroyed, true, "opacity zero stops capture immediately");
    assert.throws(() => f.controller.desktopAction("iracing", f.item.id, "leftClick"));
  } finally { f.controller.close(); }
  assert.equal(f.screen.listenerCount("display-removed"), 0);
});

test("changed monitor geometry invalidates selection before another click", async () => {
  const f = await fixture();
  try {
    await f.controller.selectDisplay("iracing", f.item.id, "-42"); f.controller.update(f.doc); await f.settle();
    f.windows[0].webContents.emit("paint", { texture: {} });
    f.screen.emit("display-metrics-changed", {}, f.display, ["bounds", "scaleFactor"]);
    assert.equal(f.windows[0].destroyed, true);
    assert.throws(() => f.controller.desktopAction("iracing", f.item.id, "leftClick"));
  } finally { f.controller.close(); }
});

test("physical mouse and unnormalized legacy eye input refuse synthetic interaction", async () => {
  for (const interaction of ["mouse", "gaze"]) {
    const f = await fixture({ validate() {} });
    try {
      f.item.interaction = interaction; f.item.winkClicks = true;
      await f.controller.selectDisplay("iracing", f.item.id, "-42"); f.controller.update(f.doc); await f.settle();
      f.windows[0].webContents.emit("paint", { texture: {} });
      assert.throws(() => f.controller.desktopAction("iracing", f.item.id, "leftClick"));
      assert.throws(() => f.controller.desktopAction("iracing", f.item.id, "resume"));
      assert.throws(() => f.controller.desktopAction("iracing", f.item.id, "precision", { held: true, pressId: 1 }));
      const status = f.controller.status("iracing").items[0];
      assert.equal(status.pointerMode, "mouse");
      assert.equal("winkAvailable" in status, false);
      assert.equal("winkError" in status, false);
      const config = f.commands.filter(c => c.op === "configure").at(-1);
      assert.equal(config.desktopMode, 0);
      assert.equal("winkClicks" in config, false);
    } finally { f.controller.close(); }
  }
});

test("precision config and transient head hold renewals never mutate the saved profile", async () => {
  const f = await fixture();
  try {
    f.item.precisionScale = 2.5; f.item.precisionShortcut = "Ctrl+F8";
    await f.controller.selectDisplay("iracing", f.item.id, "-42"); f.controller.update(f.doc); await f.settle();
    const config = f.commands.filter(c => c.op === "configure").at(-1);
    assert.equal(config.precisionScale, 2.5); assert.equal(config.precisionShortcut, "Ctrl+F8");
    assert.throws(() => f.controller.desktopAction("iracing", f.item.id, "precision", { held: true, pressId: 7 }), /unavailable/i);
    f.windows[0].webContents.emit("paint", { texture: {} });
    const saved = structuredClone(f.doc), windowCount = f.windows.length, configurations = f.commands.filter(c => c.op === "configure").length;
    for (let i = 0; i < 3; i++) f.controller.desktopAction("iracing", f.item.id, "precision", { held: true, pressId: 7 });
    assert.deepEqual(f.commands.at(-1), { op: "precision", slot: 0, held: true, pressId: 7 });
    assert.deepEqual(f.doc, saved);
    assert.equal(f.commands.filter(c => c.op === "configure").length, configurations);
    assert.equal(f.windows.length, windowCount);
    f.setInput(false);
    assert.doesNotThrow(() => f.controller.desktopAction("iracing", f.item.id, "precision", { held: true, pressId: 7 }),
      "native geometry revision handoff must not be cancelled by a slower cached inputAvailable snapshot");
    f.setInput(true, true);
    assert.throws(() => f.controller.desktopAction("iracing", f.item.id, "precision", { held: true, pressId: 7 }), /unavailable/i);
    f.controller.desktopAction("iracing", f.item.id, "precision", { held: false, pressId: 7 });
    assert.deepEqual(f.commands.at(-1), { op: "precision", slot: 0, held: false, pressId: 7 });
    f.item.precisionScale = 3; f.controller.update(f.doc);
    assert.equal(f.windows[0].destroyed, true, "saving precision settings invalidates the old input target");
  } finally { f.controller.close(); }
});

test("precision release bypasses hidden, disabled, changed and disconnected target gates", async () => {
  const f = await fixture();
  try {
    await f.controller.selectDisplay("iracing", f.item.id, "-42"); f.controller.update(f.doc); await f.settle();
    f.windows[0].webContents.emit("paint", { texture: {} });
    f.controller.desktopAction("iracing", f.item.id, "precision", { held: true, pressId: 11 });
    f.setAvailable(false); f.setInput(false, true);
    f.controller.desktopAction("iracing", f.item.id, "precision", { held: false, pressId: 11 });
    assert.deepEqual(f.commands.at(-1), { op: "precision", slot: 0, held: false, pressId: 11 });
    f.item.enabled = false; f.controller.update(f.doc); await f.settle();
    const before = f.commands.length, windows = f.windows.length;
    assert.doesNotThrow(() => f.controller.desktopAction("iracing", f.item.id, "precision", { held: false, pressId: 11 }));
    f.item.kind = "url"; f.item.url = "https://example.com"; f.controller.update(f.doc); await f.settle();
    assert.doesNotThrow(() => f.controller.desktopAction("iracing", f.item.id, "precision", { held: false, pressId: 11 }));
    assert.equal(f.commands.length, before); assert.equal(f.windows.length, windows);
  } finally { f.controller.close(); }
});

test("precision rejects malformed holds and press IDs before attempting a release", async () => {
  const f = await fixture();
  try {
    for (const options of [undefined, {}, { held: 1, pressId: 1 }, { held: false, pressId: 0 }, { held: true, pressId: -1 }, { held: false, pressId: 1.5 }, { held: false, pressId: Number.MAX_SAFE_INTEGER + 1 }]) {
      assert.throws(() => f.controller.desktopAction("iracing", f.item.id, "precision", options), /precision|press/i);
    }
    assert.equal(f.commands.length, 0);
  } finally { f.controller.close(); }
});
test("saving a placement or interaction change withdraws the old desktop target immediately", async () => {
  const f = await fixture();
  try {
    await f.controller.selectDisplay("iracing", f.item.id, "-42"); f.controller.update(f.doc); await f.settle();
    f.windows[0].webContents.emit("paint", { texture: {} });
    f.item.x = 1; f.controller.update(f.doc);
    assert.equal(f.windows[0].destroyed, true, "the old posed target is invalid before asynchronous session reconciliation");
    assert.throws(() => f.controller.desktopAction("iracing", f.item.id, "leftClick"));
  } finally { f.controller.close(); }
});
test("missing input capability fails closed and paused pointing requires explicit resume", async () => {
  const f = await fixture();
  try {
    await f.controller.selectDisplay("iracing", f.item.id, "-42"); f.controller.update(f.doc); await f.settle();
    f.windows[0].webContents.emit("paint", { texture: {} });
    f.setInput(undefined);
    assert.throws(() => f.controller.desktopAction("iracing", f.item.id, "leftClick"));
    f.setInput(true, true);
    assert.throws(() => f.controller.desktopAction("iracing", f.item.id, "rightClick"));
    const before = f.commands.filter(c => c.op === "configure").length;
    f.controller.desktopAction("iracing", f.item.id, "resume"); await f.settle();
    assert.equal(f.commands.filter(c => c.op === "configure").length, before + 1, "explicit resume reconfigures the native slot");
    assert.equal(f.commands.some(c => c.op === "click"), false);
  } finally { f.controller.close(); }
});

test("capture renderer requests the ordinary OS cursor and stops every track on unload", async () => {
  const code = await readFile(new URL("../desktop/capture-window.js", import.meta.url), "utf8");
  let constraints, stopped = 0; const listeners = {}, video = { style: {}, play: async () => {} };
  const track = { stop() { stopped++; }, addEventListener() {} };
  const stream = { getVideoTracks: () => [track], getTracks: () => [track] };
  vm.runInNewContext(code, { URL, location: { href: "file:///capture-window.html?fps=12" }, document: { querySelector: () => video },
    navigator: { mediaDevices: { getDisplayMedia: async value => { constraints = value; return stream; } } }, addEventListener: (name, fn) => { listeners[name] = fn; }, console });
  await tick();
  assert.equal(constraints.video.cursor, "always");
  assert.equal(constraints.audio, false);
  listeners.beforeunload(); assert.equal(stopped, 1); assert.equal(video.srcObject, null);
});
test("desktop capture fills the texture without source cropping while window capture retains contain", async () => {
  const f = await fixture();
  try {
    await f.controller.selectDisplay("iracing", f.item.id, "-42"); f.controller.update(f.doc); await f.settle();
    assert.equal(f.windows[0].loadOptions.query.fit, "fill");
    f.item.kind = "window"; f.item.windowId = "window:3:0";
    f.setSources([{ id: "window:3:0", name: "Window fixture" }]);
    await f.controller.selectWindow("iracing", f.item.id, "window:3:0"); f.controller.update(f.doc); await f.settle();
    assert.equal(f.windows.at(-1).loadOptions.query.fit, "contain");
  } finally { f.controller.close(); }
  const code = await readFile(new URL("../desktop/capture-window.js", import.meta.url), "utf8");
  for (const [fit, expectedResize] of [["fill", "none"], ["contain", undefined]]) {
    let constraints; const video = { style: {}, play: async () => {} }, track = { addEventListener() {}, stop() {} };
    vm.runInNewContext(code, { URL, location: { href: `file:///capture-window.html?fit=${fit}` }, document: { querySelector: () => video },
      navigator: { mediaDevices: { getDisplayMedia: async value => { constraints = value; return { getVideoTracks: () => [track], getTracks: () => [track] }; } } }, addEventListener() {}, console });
    await tick();
    assert.equal(video.style.objectFit, fit);
    assert.equal(constraints.video.resizeMode, expectedResize);
    if (fit === "fill") { assert.equal(constraints.video.width, undefined); assert.equal(constraints.video.height, undefined); }
  }
});
test("selected monitor identity persists through hide and session stop without repeated capture enumeration", async () => {
  const f = await fixture();
  try {
    f.controller.update(f.doc); await f.settle();
    assert.equal(f.controller.status("iracing").items[0].source, null, "a saved ID is not a selection");
    await f.controller.selectDisplay("iracing", f.item.id, "-42"); f.controller.update(f.doc); await f.settle();
    const selected = { kind: "desktop", reference: "-42", name: "Left monitor", detail: "1920 × 1080", displaying: false };
    assert.deepEqual(f.controller.status("iracing").items[0].source, selected);
    f.windows[0].webContents.emit("paint", { texture: {} });
    assert.equal(f.controller.status("iracing").items[0].source.displaying, true);
    const enumerations = f.lookupCount();
    f.controller.desktopAction("iracing", f.item.id, "resume"); await f.settle();
    for (let i = 0; i < 3; i++) { f.controller.update(f.doc); await f.settle(); }
    assert.equal(f.lookupCount(), enumerations, "status and reconciliation use selected source identity, not capture enumeration");
    f.item.enabled = false; f.controller.update(f.doc); await f.settle();
    assert.deepEqual(f.controller.status("iracing").items[0].source, selected);
    f.item.enabled = true; f.setSession(false); f.controller.update(f.doc); await f.settle();
    assert.deepEqual(f.controller.status("iracing").items[0].source, selected);
    f.setSession(true); f.controller.update(f.doc); await f.settle();
    assert.equal(f.windows.length, 2);
    f.setDisplays([{ ...f.display, label: "Replacement" }]);
    assert.equal(f.controller.status("iracing").items[0].source, null, "an identity mismatch immediately revokes the label");
    f.setDisplays([f.display]); f.item.displayId = "123"; f.controller.update(f.doc); await f.settle();
    f.item.displayId = "-42"; f.controller.update(f.doc); await f.settle();
    assert.equal(f.controller.status("iracing").items[0].source, null, "restoring an old reference never restores consent");
  } finally { f.controller.close(); }
});

test("a selected source is only displaying after its native layer consumes a rendered frame", async () => {
  const f = await fixture();
  try {
    await f.controller.selectDisplay("iracing", f.item.id, "-42"); f.controller.update(f.doc); await f.settle();
    const displaying = () => f.controller.status("iracing").items[0].source.displaying;
    assert.equal(displaying(), false);
    f.windows[0].webContents.emit("paint", { texture: {} });
    assert.equal(displaying(), true);
    for (const [state, frame] of [[0, 1], [2, 1], [1, 0]]) {
      f.setNative(state, frame); assert.equal(displaying(), false);
      assert.throws(() => f.controller.desktopAction("iracing", f.item.id, "precision", { held: true, pressId: 1 }), /unavailable/i);
    }
    f.setNative(1, 1); f.setCapacity(0); assert.equal(displaying(), false);
    f.setCapacity(15); f.setAvailable(false); assert.equal(displaying(), false);
    f.setAvailable(true); f.windows[0].webContents.emit("page-title-updated", {}, "Capture unavailable");
    assert.equal(displaying(), false, "the capture failure listener still clears a submitted source");
    assert.equal(f.controller.status("iracing").items[0].status, "error");
  } finally { f.controller.close(); }
});

test("window and dashboard source summaries identify selected content and bound untrusted text", async () => {
  const f = await fixture();
  try {
    f.item.kind = "window"; f.item.windowId = "window:3:0";
    f.setSources([{ id: "window:3:0", name: "Timing\u0000 window\n" }]);
    await f.controller.selectWindow("iracing", f.item.id, "window:3:0"); f.controller.update(f.doc); await f.settle();
    assert.deepEqual(f.controller.status("iracing").items[0].source, { kind: "window", reference: "window:3:0", name: "Timing window", detail: "", displaying: false });
    f.item.enabled = false; f.controller.update(f.doc); await f.settle();
    assert.equal(f.controller.status("iracing").items[0].source.name, "Timing window");
    f.item.kind = "url"; f.item.url = "https://timing.example/dashboard"; f.item.enabled = true;
    f.controller.update(f.doc); await f.settle();
    assert.deepEqual(f.controller.status("iracing").items[0].source, { kind: "url", reference: "https://timing.example/dashboard", name: "timing.example", detail: "https://timing.example/dashboard", displaying: false });
    const window = f.windows.at(-1);
    window.title = "Live\u0000 timing\n"; window.webContents.emit("page-title-updated", {}, window.title);
    assert.equal(f.controller.status("iracing").items[0].source.name, "Live timing");
    f.item.enabled = false; f.controller.update(f.doc); await f.settle();
    assert.equal(f.controller.status("iracing").items[0].source.name, "Live timing", "hide retains the current dashboard title");
    f.item.enabled = true; f.controller.update(f.doc); await f.settle();
    const restored = f.windows.at(-1);
    restored.title = "x".repeat(2000); restored.webContents.emit("page-title-updated", {}, restored.title);
    assert.ok(f.controller.status("iracing").items[0].source.name.length <= 500);
    f.item.kind = "window"; f.controller.update(f.doc); await f.settle();
    assert.equal(f.controller.status("iracing").items[0].source, null, "changing kind revokes the old window selection");
  } finally { f.controller.close(); }
});
