const { _electron } = require("playwright-core");
const fs = require("node:fs/promises");
const path = require("node:path");
const os = require("node:os");
const { pathToFileURL } = require("node:url");
const assert = require("node:assert/strict");
(async () => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), "opengrid-capture-aspect-")), entry = path.join(directory, "main.cjs");
  await fs.writeFile(entry, 'const{app,BrowserWindow}=require("electron");app.whenReady().then(()=>new BrowserWindow({show:false,useContentSize:true,width:1024,height:576,webPreferences:{contextIsolation:true,sandbox:true}}).loadURL("about:blank"));');
  const env = { ...process.env }; delete env.ELECTRON_RUN_AS_NODE; let app;
  try {
    app = await _electron.launch({ executablePath: require("electron"), args: [entry], env }); const page = await app.firstWindow();
    await page.addInitScript(() => {
      navigator.mediaDevices.getDisplayMedia = async constraints => {
        window.captureConstraints = constraints;
        const canvas = document.createElement("canvas");
        [canvas.width, canvas.height] = new URL(location.href).searchParams.get("fixture") === "portrait" ? [300, 500] : [400, 300];
        const context = canvas.getContext("2d"), paint = () => {
          context.fillStyle = "cyan"; context.fillRect(0, 0, canvas.width, canvas.height);
          context.fillStyle = "red"; context.fillRect(0, 0, 20, canvas.height);
          context.fillStyle = "lime"; context.fillRect(canvas.width - 20, 0, 20, canvas.height);
          context.fillStyle = "blue"; context.fillRect(0, 0, canvas.width, 20);
          context.fillStyle = "yellow"; context.fillRect(0, canvas.height - 20, canvas.width, 20);
        };
        paint(); setInterval(paint, 30); return canvas.captureStream(10);
      };
    });
    const captureURL = pathToFileURL(path.resolve(__dirname, "../desktop/capture-window.html")).href;
    for (const fixture of ["4:3", "portrait"]) {
      await page.goto(`${captureURL}?fit=fill&fixture=${fixture}`);
      await page.waitForFunction(() => document.querySelector("video").readyState >= 2);
      const samples = await app.evaluate(async ({ BrowserWindow }) => {
        const image = await BrowserWindow.getAllWindows()[0].webContents.capturePage();
        const { width, height } = image.getSize(), pixels = image.toBitmap();
        const at = (u, v) => { const i = (Math.floor(v * height) * width + Math.floor(u * width)) * 4; return [pixels[i + 2], pixels[i + 1], pixels[i]]; };
        return { left: at(.01, .5), right: at(.99, .5), top: at(.5, .01), bottom: at(.5, .99) };
      });
      for (const [edge, expected] of Object.entries({ left: [255, 0, 0], right: [0, 255, 0], top: [0, 0, 255], bottom: [255, 255, 0] })) {
        assert.ok(samples[edge].every((channel, i) => Math.abs(channel - expected[i]) <= 3), `${fixture}: ${edge} monitor edge must fill overlay edge without actionable letterbox bars: ${samples[edge]}`);
      }
      const constraints = await page.evaluate(() => window.captureConstraints.video);
      assert.equal(constraints.resizeMode, "none"); assert.equal(constraints.width, undefined); assert.equal(constraints.height, undefined);
    }
    await page.goto(`${captureURL}?fit=contain&fixture=4:3`);
    assert.equal(await page.locator("video").evaluate(video => getComputedStyle(video).objectFit), "contain");
    console.log("4:3 and portrait capture visibly fill all overlay edges; window contain is preserved.");
  } finally { if (app) await app.close(); await fs.rm(directory, { recursive: true, force: true }); }
})().catch(error => { console.error(error); process.exitCode = 1; });
