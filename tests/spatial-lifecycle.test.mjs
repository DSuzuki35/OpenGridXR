import test from "node:test";
import assert from "node:assert/strict";
import { EventEmitter } from "node:events";
import { setImmediate as tick } from "node:timers/promises";
import { setTimeout as delay } from "node:timers/promises";
import * as fs from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import { createRequire } from "node:module";
import { createDocument, validateDocument } from "../src/model.mjs";
import { createSpatialOverlay } from "../src/spatial-overlays.mjs";
const { createSpatialOverlays } = createRequire(import.meta.url)("../desktop/spatial-overlays.cjs");
test("a never-ending URL cannot block the next source or cleanup after disabling overlays", async () => {
  const windows = [], hosts = [], commands = [];
  let streamActive = false;
  class Window {
    static getAllWindows() { return []; }
    constructor(options) {
      this.options = options; this.destroyed = false;
      this.webContents = new EventEmitter();
      for (const key of ["setAudioMuted", "setFrameRate", "setWindowOpenHandler", "stop"]) this.webContents[key] = () => {};
      windows.push(this);
    }
    setBounds() {}
    loadURL() { return new Promise(() => {}); }
    isDestroyed() { return this.destroyed; }
    destroy() { this.destroyed = true; }
  }
  const makeSession = () => ({ setPermissionRequestHandler() {}, setPermissionCheckHandler() {}, removeAllListeners() {}, on() {}, setDisplayMediaRequestHandler() {} });
  const controller = await createSpatialOverlays({
    electron: { BrowserWindow: Window, desktopCapturer: { getSources: async () => [] }, session: { fromPartition: makeSession } },
    executable: "fixture", getSessions: async () => [{ game: "iracing", active: true, engineLoaded: true }], model: { validateDocument },
    hostFactory: () => { const host = { stopped: false, start() {}, running() { return !this.stopped; }, send(command) { commands.push(command); }, close() { this.stopped = true; }, state: () => ({ available: true, capacity: 1, streamActive, activeOverlayCount: 0, error: "", frameErrors: {} }) }; hosts.push(host); return host; },
  });
  try {
    const document = createDocument(); const settings = document.profiles[0].spatialOverlays;
    settings.enabled = true; settings.items = [1, 2].map(() => ({ ...createSpatialOverlay(), enabled: true, url: "http://localhost:8888/never-finishes" }));
    settings.items[1].showInStream = true;
    controller.update(document);
    await tick(); await tick();
    assert.equal(windows.length, 1, "without Output, VR capacity avoids starting an unusable source");
    streamActive = true; controller.update(document);
    for (let i = 0; i < 20 && windows.length < 2; i++) await tick();
    assert.equal(windows.length, 2, "both sources start even when navigation never completes");
    assert.equal(windows[0].options.webPreferences.sandbox, true);
    assert.equal(windows[0].options.webPreferences.nodeIntegration, false);
    windows[0].webContents.emit("render-process-gone");
    assert.ok(windows[0].destroyed);
    assert.ok(commands.some((command) => command.op === "clear" && command.slot === 0), "failed source withdraws the stale headset image");
    assert.equal(controller.status("iracing").items[0].status, "error");
    await tick(); settings.enabled = false; controller.update(document);
    for (let i = 0; i < 20 && !windows.every((window) => window.destroyed); i++) await tick();
    assert.ok(windows.every((window) => window.destroyed), "disabled sources are destroyed immediately");
    assert.ok(hosts.every((host) => host.stopped));
  } finally { controller.close(); }
});

test("enabling the experimental driver on a live wheel stages once and keeps its capture", async () => {
  const windows = [], commands = [];
  const root = await fs.mkdtemp(path.join(os.tmpdir(), "driver-lifecycle-"));
  const oldAppData = process.env.LOCALAPPDATA;
  process.env.LOCALAPPDATA = root;
  let stageCalls = 0;
  let driverState = 0;
  class Window {
    static getAllWindows() { return []; }
    constructor() {
      this.webContents = new EventEmitter();
      for (const key of ["setAudioMuted", "setFrameRate", "setWindowOpenHandler", "stop"]) this.webContents[key] = () => {};
      windows.push(this);
    }
    loadURL() { return Promise.resolve(); }
    isDestroyed() { return this.destroyed === true; }
    destroy() { this.destroyed = true; }
  }
  const makeSession = () => ({ setPermissionRequestHandler() {}, setPermissionCheckHandler() {}, removeAllListeners() {}, on() {}, setDisplayMediaRequestHandler() {} });
  const controller = await createSpatialOverlays({
    electron: { BrowserWindow: Window, session: { fromPartition: makeSession } }, executable: "fixture",
    getSessions: async () => [{ game: "iracing", active: true, engineLoaded: true }], model: { validateDocument },
    hostFactory: () => ({ start() {}, running: () => true, close() {}, send: command => commands.push(command),
      state: () => ({ available: true, capacity: 1, frameErrors: {}, slots: [{ slot: 0, driverState }] }) }),
    async stageDriverAsset() { if (++stageCalls === 1) throw new Error("Driver asset missing"); },
  });
  const document = createDocument(), settings = document.profiles[0].spatialOverlays;
  const wheel = { ...createSpatialOverlay(), enabled: true, wheelModel: "fpe2" };
  settings.enabled = true;
  settings.items = [wheel];
  const latest = () => commands.findLast(command => command.op === "configure");
  const error = () => controller.status("iracing").items[0].driverError;
  const waitFor = async predicate => {
    const deadline = Date.now() + 3000;
    while (!predicate() && Date.now() < deadline) await delay(10);
    assert.ok(predicate());
  };
  try {
    controller.update(document);
    await waitFor(() => windows.length > 0);
    assert.equal(stageCalls, 0);
    assert.equal(latest().driverEnabled, false);

    wheel.pivotX = 0.04;
    wheel.pivotY = 0.04;
    wheel.driverEnabled = true;
    controller.update(document);
    await waitFor(() => latest().pivotY === 0.04);
    assert.equal(stageCalls, 0, "combined pivot offset exceeds the supported radius");
    assert.equal(latest().driverEnabled, false);

    wheel.pivotY = 0.03;
    wheel.steeringTilt = -10;
    controller.update(document);
    await waitFor(error);
    assert.match(error(), /missing/);
    assert.equal(latest().wheelModel, "fpe2");
    assert.equal(latest().driverEnabled, false);

    wheel.driverEnabled = false;
    controller.update(document);
    await waitFor(() => !error());
    // Let the disabled configuration withdraw the previous staging attempt.
    await tick();
    wheel.driverEnabled = true;
    controller.update(document);
    await waitFor(() => latest().driverEnabled);
    driverState = 3;
    assert.match(error(), /renderer could not load/i);
    driverState = 2;
    assert.equal(error(), undefined);
    wheel.driverSuitTheme = 4;
    controller.update(document);
    await waitFor(() => latest().driverSuitTheme === 4);
    assert.equal(stageCalls, 2);
    assert.equal(windows.length, 1);
    assert.equal(windows[0].isDestroyed(), false);
    assert.ok(!commands.some(command => command.op === "clear"));
  } finally {
    controller.close();
    if (oldAppData === undefined) delete process.env.LOCALAPPDATA;
    else process.env.LOCALAPPDATA = oldAppData;
    await fs.rm(root, { recursive: true, force: true });
  }
});
