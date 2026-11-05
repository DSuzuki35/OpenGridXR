"use strict";
const { _electron } = require("playwright-core");
const assert = require("node:assert/strict");
const fs = require("node:fs/promises");
const os = require("node:os");
const path = require("node:path");
const http = require("node:http");
const { spawn } = require("node:child_process");
(async () => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), "opengrid-gpu-source-"));
  const server = http.createServer((_, response) => { response.setHeader("content-type", "text/html"); response.end('<!doctype html><style>body{margin:0;background:rgb(22,44,88)}</style><script>let n=0;setInterval(()=>document.body.style.backgroundColor=`rgb(${++n%255},44,88)`,100)</script>'); });
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  let app, receiver, streamReceiver; const received = [], streamReceived = [];
  const streamTest = process.argv.includes("--stream");
  const transport = process.argv.includes("--transport") || streamTest, d3d12 = process.argv.includes("--d3d12"), captureWindow = process.argv.includes("--window");
  const slots = process.argv.includes("--15-slots") ? 15 : streamTest ? 2 : 1;
  try {
    if (transport) {
      console.log("Starting isolated native GPU receiver.");
      receiver = spawn(path.resolve(__dirname, "../native/build/Release/opengrid-overlay-renderer-tests.exe"), ["--listen", "--seconds", "30", ...(d3d12 ? ["--d3d12"] : [])], { windowsHide: true, stdio: ["ignore", "pipe", "pipe"] });
      let buffer = "";
      await new Promise((resolve, reject) => {
        const timeout = setTimeout(() => reject(new Error("GPU receiver fixture did not start.")), 10000);
        receiver.stdout.on("data", (chunk) => { buffer += chunk; let end; while ((end = buffer.indexOf("\n")) >= 0) { const line = buffer.slice(0, end); buffer = buffer.slice(end + 1); try { const record = JSON.parse(line); received.push(record); if (record.ready) { clearTimeout(timeout); resolve(); } } catch {} } });
        receiver.once("error", reject); receiver.stderr.on("data", (chunk) => process.stderr.write(chunk));
      });
    }
    if (streamTest) {
      streamReceiver = spawn(path.resolve(__dirname, "../native/build/Release/opengrid-stream-overlay-tests.exe"), ["--listen", "--seconds", "30"], { windowsHide: true, stdio: ["ignore", "pipe", "pipe"] });
      let buffer = "";
      await new Promise((resolve, reject) => {
        const timeout = setTimeout(() => reject(new Error("Stream receiver fixture did not start.")), 10000);
        streamReceiver.stdout.on("data", (chunk) => { buffer += chunk; let end; while ((end = buffer.indexOf("\n")) >= 0) { const line = buffer.slice(0, end); buffer = buffer.slice(end + 1); try { const record = JSON.parse(line); streamReceived.push(record); if (record.ready) { clearTimeout(timeout); resolve(); } } catch {} } });
        streamReceiver.once("error", reject); streamReceiver.stderr.on("data", (chunk) => process.stderr.write(chunk));
      });
    }
    console.log("Starting isolated Electron source.");
    const packaged = Boolean(process.env.OPENGRIDXR_TEST_EXE);
    app = await _electron.launch({ executablePath: process.env.OPENGRIDXR_TEST_EXE || require("electron"), args: packaged ? [] : [path.resolve(__dirname, "..")], env: { ...process.env, OPENGRIDXR_TEST_MODE: "1", OPENGRIDXR_DATA_DIR: directory } });
    await app.firstWindow();
    console.log("Electron started; connecting source transport.");
    const result = await app.evaluate(async ({ BrowserWindow, screen, session, desktopCapturer, app: electronApp }, config) => {
      if (config.packaged) {
        config.hostModule = `${electronApp.getAppPath()}/desktop/overlay-host.cjs`;
        config.captureFile = `${electronApp.getAppPath()}/desktop/capture-window.html`;
      }
      let host;
      if (config.transport) {
        const load = process.getBuiltinModule("module").createRequire(config.hostModule);
        const { createOverlayHost } = load(config.hostModule);
        host = createOverlayHost({ executable: config.executable, game: "iracing", testMode: true }); host.start();
        const until = Date.now() + 8000;
        while (!host.state().available && Date.now() < until) await new Promise((resolve) => setTimeout(resolve, 50));
        if (!host.state().available) throw new Error(JSON.stringify(host.state()));
        for (let slot = 0; slot < config.slots; slot++) host.send({ op: "configure", slot, enabled: true, anchor: slot % 2 ? "head" : "local", x: config.streamTest || config.slots === 1 ? 0 : (slot % 5 - 2) * 0.2, y: config.streamTest && slot === 1 ? -0.5 : 0, z: -1, yaw: 0, pitch: 0, roll: 0, widthMeters: 0.6, opacity: 1, showInStream: config.streamTest && slot === 0 });
      }
      const captureSession = session.fromPartition("opengrid-gpu-capture-test");
      const probe = new BrowserWindow({ x: 0, y: 0, width: 1024, height: 576, useContentSize: true, frame: false, thickFrame: false, resizable: false, transparent: true, show: false, webPreferences: { session: captureSession, offscreen: { useSharedTexture: true }, sandbox: true, contextIsolation: true, nodeIntegration: false, backgroundThrottling: false } });
      probe.webContents.setFrameRate(10);
      probe.setBounds({ x: 0, y: 0, width: 1024, height: 576 });
      const frames = [], permissions = []; let sourceWindow;
      try {
        if (config.captureWindow) {
          sourceWindow = new BrowserWindow({ title: "OpenGrid capture test fixture", x: 30, y: 30, width: 320, height: 180, show: true, webPreferences: { sandbox: true, contextIsolation: true, nodeIntegration: false } });
          await sourceWindow.loadURL(config.url);
          const source = (await desktopCapturer.getSources({ types: ["window"], thumbnailSize: { width: 0, height: 0 } })).find((source) => source.id === sourceWindow.getMediaSourceId());
          if (!source) throw new Error("The isolated capture fixture window was not found.");
          captureSession.setPermissionRequestHandler((contents, permission, callback) => { permissions.push(["request", permission]); callback(contents === probe.webContents && permission === "display-capture"); });
          captureSession.setPermissionCheckHandler((contents, permission) => { permissions.push(["check", permission]); return contents === probe.webContents && permission === "display-capture"; });
          captureSession.setDisplayMediaRequestHandler((request, callback) => callback(request.frame === probe.webContents.mainFrame ? { video: source } : {}));
        }
        const painted = new Promise((resolve, reject) => {
          const timeout = setTimeout(() => reject(new Error("No shared GPU texture arrived.")), 10000);
          probe.webContents.on("paint", (event) => {
            if (!event.texture) return;
            const info = event.texture.textureInfo;
            frames.push({ width: info.codedSize.width, height: info.codedSize.height, format: info.pixelFormat, bytes: info.handle.ntHandle?.length, widget: info.widgetType });
            if (host) {
              let references = config.slots;
              for (let slot = 0; slot < config.slots; slot++) host.submit(slot, { textureInfo: event.texture.textureInfo, release() { if (--references === 0) event.texture.release(); } });
            } else event.texture.release();
            if (frames.length >= (host || config.captureWindow ? 20 : 3)) { clearTimeout(timeout); resolve(); }
          });
        });
        if (config.captureWindow) await probe.loadFile(config.captureFile, { query: { fps: "10" } });
        else await probe.loadURL(config.url);
        await painted;
        const privileges = await probe.webContents.executeJavaScript('({node:typeof process,bridge:typeof openGrid,require:typeof require})');
        let captureEnded;
        if (sourceWindow) {
          sourceWindow.destroy(); sourceWindow = null;
          const until = Date.now() + 3000;
          while (Date.now() < until && await probe.webContents.getTitle() !== "Capture unavailable") await new Promise((resolve) => setTimeout(resolve, 50));
          captureEnded = probe.webContents.getTitle() === "Capture unavailable";
        }
        return { frames, privileges, permissions, captureEnded, bounds: probe.getBounds(), contentBounds: probe.getContentBounds(), displayScale: screen.getDisplayMatching(probe.getBounds()).scaleFactor, devicePixelRatio: await probe.webContents.executeJavaScript('devicePixelRatio'), ...(host ? { host: host.state() } : {}) };
      } finally { probe.destroy(); sourceWindow?.destroy(); host?.close(); }
    }, { url: `http://127.0.0.1:${server.address().port}/`, transport, captureWindow, slots, streamTest, packaged, captureFile: path.resolve(__dirname, "../desktop/capture-window.html"), hostModule: path.resolve(__dirname, "../desktop/overlay-host.cjs"), executable: packaged ? path.join(path.dirname(process.env.OPENGRIDXR_TEST_EXE), "resources/native/OpenGridXR-OverlayHost.exe") : path.resolve(__dirname, "../native/build/Release/OpenGridXR-OverlayHost.exe") }).catch((error) => { console.error("GPU source evaluation failed:", error.message); throw error; });
    assert.ok(result.frames.length >= 3);
    assert.ok(result.frames.every((f) => ["rgba", "bgra"].includes(f.format) && f.bytes === 8 && f.width === 1024 && f.height === 576));
    assert.deepEqual(result.privileges, { node: "undefined", bridge: "undefined", require: "undefined" });
    if (captureWindow) assert.equal(result.captureEnded, true, "closing the captured app withdraws its stale video");
    if (transport) {
      assert.deepEqual(result.host.frameErrors, {}, JSON.stringify(result.host));
      assert.ok(received.some((record) => record.active === slots && record.pixel > 0), JSON.stringify(received));
    }
    if (streamTest) {
      assert.ok(streamReceived.some((record) => record.pixelCenter > 0xff000000), JSON.stringify(streamReceived));
      assert.ok(streamReceived.filter((record) => record.op === "stream-frame").every((record) => record.pixelDefault === 0xff000000 && record.pixelCorner === 0xff000000), "private lower VR panel is absent from stream");
    }
    console.log(JSON.stringify({ ...result, frames: result.frames.slice(0, 3), frameCount: result.frames.length, received: received.filter((record) => record.ready || record.active > 0), streamReceived }));
  } finally {
    if (app) { app.process().kill(); await Promise.race([app.close().catch(() => {}), new Promise((resolve) => setTimeout(resolve, 2000))]); }
    server.close(); await fs.rm(directory, { recursive: true, force: true });
    receiver?.kill();
    streamReceiver?.kill();
  }
})().catch((error) => { console.error(error); process.exitCode = 1; });
