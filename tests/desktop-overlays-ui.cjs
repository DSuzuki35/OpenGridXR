const { _electron } = require("playwright-core");
const fs = require("node:fs/promises");
const path = require("node:path");
const os = require("node:os");
const http = require("node:http");
const assert = require("node:assert/strict");
(async () => {
  const { build } = await import("vite");
  const fixtureId = path.resolve("tests/__desktop_fixture__.jsx").replaceAll("\\", "/");
  const bundle = await build({ logLevel: "error", plugins: [{ name: "desktop-fixture", resolveId(id) { if (id === "virtual:desktop-fixture") return fixtureId; }, load(id) {
    if (id === fixtureId) return `import React,{useState} from "react";import{createRoot}from"react-dom/client";
      import Panel from"../src/components/SpatialOverlaysPanel.jsx";import{createProfile}from"../src/model.mjs";import{createSpatialOverlay}from"../src/spatial-overlays.mjs";
      function Fixture(){const[p,set]=useState(()=>{const p=createProfile();p.spatialOverlays.items=[{...createSpatialOverlay(),kind:"desktop"}];return p;});
      return <><Panel profile={p} gameName="iRacing" onChange={patch=>set(p=>({...p,spatialOverlays:{...p.spatialOverlays,...patch}}))} onCenterChange={()=>{}} run={async(_,fn)=>fn()} busy={false} dirty={false}/><output>{JSON.stringify(p)}</output></>;}
      createRoot(document.getElementById("root")).render(<Fixture/>);`;
  } }], build: { write: false, rollupOptions: { input: "virtual:desktop-fixture", output: { entryFileNames: "fixture.js" } } } });
  const assets = new Map(bundle.output.map(item => ["/" + item.fileName, item.type === "chunk" ? item.code : item.source]));
  const server = http.createServer((req, res) => { const body = req.url === "/" ? '<div id="root"></div><script type="module" src="/fixture.js"></script>' : assets.get(req.url); res.setHeader("Content-Type", req.url === "/" ? "text/html" : "text/javascript"); res.end(body || ""); });
  await new Promise(resolve => server.listen(0, "127.0.0.1", resolve));
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), "opengrid-desktop-ui-")), entry = path.join(dir, "main.cjs");
  await fs.writeFile(entry, 'const{app,BrowserWindow}=require("electron");app.whenReady().then(()=>new BrowserWindow({show:false,webPreferences:{contextIsolation:true}}).loadURL("about:blank"));');
  const env = { ...process.env }; delete env.ELECTRON_RUN_AS_NODE;
  let app;
  try {
    app = await _electron.launch({ executablePath: require("electron"), args: [entry], env });
    const page = await app.firstWindow(); page.setDefaultTimeout(3000);
    await page.addInitScript(() => { window.openGrid = {
      overlayStatus: async () => window.fixtureStatus || ({ items: [], shortcutErrors: [] }),
      listOverlayDisplays: async () => [{ id: "-42", name: "4:3 screen", x: -1600, y: 0, width: 1600, height: 1200 }, { id: "-43", name: "Portrait screen", x: 0, y: 0, width: 1080, height: 1920 }],
      selectOverlayDisplay: async (_, id, displayId) => ({ id: displayId, name: "Selected screen", ...(displayId === "-42" ? { width: 1600, height: 1200 } : { width: 1080, height: 1920 }) }),
      listOverlayWindows: async () => [{ id: "window:3:0", name: "Telemetry window" }],
      selectOverlayWindow: async () => ({ id: "window:3:0", name: "Telemetry window" }),
    }; });
    await page.goto(`http://127.0.0.1:${server.address().port}/`);
    await page.getByRole("button", { name: "Refresh monitors", exact: true }).click();
    await page.getByLabel("Current monitors").selectOption("-42");
    await page.getByRole("button", { name: "Use this monitor", exact: true }).click();
    const state = async () => JSON.parse(await page.locator("output").textContent());
    assert.equal((await state()).spatialOverlays.items[0].displayId, "-42");
    assert.match(await page.getByLabel("Selected source", { exact: true }).innerText(), /Selected screen/);
    assert.match(await page.getByLabel("Selected source", { exact: true }).innerText(), /1600 × 1200/);
    assert.equal(await page.getByLabel("Current monitors").inputValue(), "", "picker may clear without losing selected source identity");
    await page.evaluate(id => { window.fixtureStatus = { items: [{ id, sourceKind: "desktop", sourceReference: "-42", source: null }], shortcutErrors: [] }; }, (await state()).spatialOverlays.items[0].id);
    await page.waitForFunction(() => document.querySelector('[aria-label="Selected source"]').textContent.includes("Select a source for this app run"));
    assert.deepEqual([(await state()).spatialOverlays.items[0].width, (await state()).spatialOverlays.items[0].height], [1440, 1080], "4:3 monitor selection preserves its natural aspect within capture limits");
    await page.getByLabel("Current monitors").selectOption("-43");
    await page.getByRole("button", { name: "Use this monitor", exact: true }).click();
    assert.deepEqual([(await state()).spatialOverlays.items[0].width, (await state()).spatialOverlays.items[0].height], [608, 1080], "portrait monitor selection fits within capture limits");
    assert.equal(await page.getByRole("switch", { name: "Wink clicks" }).count(), 0);
    assert.equal(await page.getByLabel("Desktop pointing mode").locator('option[value="gaze"]').count(), 0);
    await page.getByLabel("Desktop pointing mode").selectOption("head");
    await page.getByRole("spinbutton", { name: "Precision zoom value", exact: true }).fill("225");
    assert.equal((await state()).spatialOverlays.items[0].precisionScale, 2.25);
    await page.getByLabel("Precision hold key", { exact: true }).selectOption("F7");
    await page.getByLabel("Precision key modifiers", { exact: true }).selectOption("Ctrl+Alt+");
    assert.equal((await state()).spatialOverlays.items[0].precisionShortcut, "Ctrl+Alt+F7");
    await page.getByLabel("Desktop visibility key", { exact: true }).selectOption("F9");
    await page.getByLabel("Desktop key modifiers", { exact: true }).selectOption("Ctrl+Alt+");
    assert.equal((await state()).spatialOverlays.items[0].shortcut, "Ctrl+Alt+F9");
    await page.getByLabel("Desktop pointing mode").selectOption("mouse");
    assert.equal(await page.getByLabel("Precision hold key", { exact: true }).isDisabled(), true);
    await page.getByRole("button", { name: "Window", exact: true }).click();
    await page.getByRole("button", { name: "Refresh windows", exact: true }).click();
    await page.getByLabel("Current desktop windows").selectOption("window:3:0");
    await page.getByRole("button", { name: "Use this window", exact: true }).click();
    assert.match(await page.getByLabel("Selected source", { exact: true }).innerText(), /Telemetry window/);
    await page.getByRole("button", { name: "Web URL", exact: true }).click();
    await page.getByLabel("Source URL", { exact: true }).fill("http://localhost:8888/dashboard");
    assert.match(await page.getByLabel("Selected source", { exact: true }).innerText(), /localhost:8888\/dashboard/);
    console.log("Source identity, head-only pointing and precision controls verified.");
  } finally { if (app) await app.close(); await new Promise(resolve => server.close(resolve)); await fs.rm(dir, { recursive: true, force: true }); }
})().catch(error => { console.error(error); process.exitCode = 1; });
