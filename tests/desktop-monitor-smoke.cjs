const { _electron } = require("playwright-core");
const fs = require("node:fs/promises");
const path = require("node:path");
const os = require("node:os");
const assert = require("node:assert/strict");
(async () => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), "opengrid-monitor-capture-"));
  const entry = path.join(directory, "main.cjs");
  await fs.writeFile(entry, 'const{app,BrowserWindow}=require("electron");app.whenReady().then(()=>new BrowserWindow({show:false,webPreferences:{contextIsolation:true}}).loadURL("about:blank"));');
  const env = { ...process.env }; delete env.ELECTRON_RUN_AS_NODE;
  let app;
  try {
    app = await _electron.launch({ executablePath: require("electron"), args: [entry], env }); await app.firstWindow();
    const result = await app.evaluate(async (electron, root) => {
      const { createRequire } = process.getBuiltinModule("module");
      const load = createRequire(root + "/desktop/spatial-overlays.cjs");
      const { createSpatialOverlays } = load(root + "/desktop/spatial-overlays.cjs");
      const model = load(root + "/src/model.mjs");
      const { createSpatialOverlay } = load(root + "/src/spatial-overlays.mjs");
      const frames = [], commands = [];
      const controller = await createSpatialOverlays({ electron, model, getSessions: async () => [{ game: "iracing", active: true, engineLoaded: true }],
        hostFactory: () => ({ start() {}, running: () => true, close() {}, state: () => ({ available: true, capacity: 15, slots: [] }), send(command) { commands.push(command); return true; },
          submit(slot, texture) { const info = texture.textureInfo; frames.push({ width: info.codedSize.width, height: info.codedSize.height, format: info.pixelFormat, handleBytes: info.handle.ntHandle.length }); texture.release(); return true; } }) });
      try {
        const displays = await controller.listDisplays(); if (!displays.length) throw new Error("No capturable monitor is connected.");
        const doc = model.createDocument(), item = { ...createSpatialOverlay(), kind: "desktop", enabled: true, displayId: displays[0].id };
        doc.profiles[0].spatialOverlays = { ...doc.profiles[0].spatialOverlays, enabled: true, items: [item] };
        await controller.selectDisplay("iracing", item.id, item.displayId); controller.update(doc);
        let source, capture;
        const deadline = Date.now() + 12000;
        while (Date.now() < deadline) {
          source = electron.BrowserWindow.getAllWindows().find(window => window.webContents.getURL().includes("capture-window.html"));
          if (source) capture = await source.webContents.executeJavaScript('(()=>{const v=document.querySelector("video");return {ready:v.readyState,settings:v.srcObject?.getVideoTracks()[0]?.getSettings(),node:typeof process,bridge:typeof openGrid,require:typeof require}})()');
          if (capture?.ready >= 2 && frames.length >= 3) break;
          await new Promise(resolve => setTimeout(resolve, 50));
        }
        const status = controller.status("iracing");
        doc.profiles[0].spatialOverlays.items[0].enabled = false; controller.update(doc);
        return { display: displays[0], frames: frames.slice(-3), capture, status: status.items[0]?.status, destroyedAfterHide: !!source?.isDestroyed(), cleared: commands.some(command => command.op === "clear" && command.slot === 0) };
      } finally { controller.close(); }
    }, path.resolve(__dirname, ".."));
    assert.ok(result.capture?.ready >= 2, JSON.stringify(result));
    assert.equal(result.capture.settings.cursor, "always"); assert.equal(result.capture.settings.displaySurface, "monitor");
    assert.equal(result.capture.node, "undefined"); assert.equal(result.capture.bridge, "undefined"); assert.equal(result.capture.require, "undefined");
    assert.equal(result.frames.length, 3); assert.ok(result.frames.every(frame => frame.width === 1024 && frame.height === 576 && frame.handleBytes === 8));
    assert.equal(result.status, "capturing"); assert.equal(result.destroyedAfterHide, true); assert.equal(result.cleared, true);
    console.log(JSON.stringify(result));
  } finally { if (app) await app.close(); await fs.rm(directory, { recursive: true, force: true }); }
})().catch(error => { console.error(error); process.exitCode = 1; });
